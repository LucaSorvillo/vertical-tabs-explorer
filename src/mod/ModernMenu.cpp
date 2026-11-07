#include "pch.h"
#include "ModernMenu.h"

#include "MenuArray.h"
#include "ModLog.h"

#include <shlwapi.h>

namespace xpx::mod
{
    namespace
    {
        // Interfacce di Windows.UI.FileExplorer.dll e ExplorerFrame.dll, NON documentate: ordine dei metodi e
        // parametri dai nomi decorati dei simboli pubblici (ContextMenuPresenter, CNscTree), GUID dalle chiamate
        // di ExplorerFrame (CNscTree::EnsureContextMenuPresenter, tabella QISearch di CNscTree).
        struct __declspec(uuid("37a472f7-63cf-4ccf-a88b-5231a3c7d8b6")) __declspec(novtable) IContextMenuPresenter : ::IUnknown
        {
            virtual HRESULT STDMETHODCALLTYPE Initialize(BOOL explorer, ::IUnknown* host, HWND owner, int hostKind) = 0;
            virtual HRESULT STDMETHODCALLTYPE PrepareForContextMenu(DWORD location, ::IUnknown* site, POINT point, IContextMenu* menu, UINT firstId,
                                                                    UINT lastId, BOOL flag, void const* verbMap, GUID testId, IShellItem* item) = 0;
            virtual void STDMETHODCALLTYPE DoContextMenu(HMENU menu, DWORD flags, GUID testId) = 0;
            virtual void STDMETHODCALLTYPE DismissContextMenu(LPCWSTR reason) = 0;
            virtual BOOL STDMETHODCALLTYPE IsContextMenuOpen() = 0;
            virtual void STDMETHODCALLTYPE Invoke(UINT id) = 0;
            virtual BOOL STDMETHODCALLTYPE IsReadyForNewContextMenu(GUID testId, POINT point) = 0;
        };

        // Implementata da chi ospita il menu (nel riquadro: CNscTree). Il presenter la tiene SENZA riferimento.
        struct __declspec(uuid("9a19ddcf-9ed4-4a18-89de-c3b4dd1d7ae3")) __declspec(novtable) IInvokeContextMenuCommand : ::IUnknown
        {
            virtual void STDMETHODCALLTYPE Invoke(IContextMenu* menu, IShellItemArray* items, HMENU hmenu, UINT id, POINT point) = 0;
            virtual void STDMETHODCALLTYPE OnContextMenuDismiss() = 0;
            virtual void STDMETHODCALLTYPE DoExpandedContextMenu(POINT point, BOOL flag, GUID* testId, POINT* hitPoint, BOOL keyboard) = 0;
            virtual void STDMETHODCALLTYPE SetFocus() = 0;
        };

        // "File Explorer Context Menu" e la versione nuova, che il riquadro usa se la funzione 60813048 è accesa.
        constexpr CLSID kPresenterClsid = { 0x86ca1aa0, 0x34aa, 0x4e8b, { 0xa5, 0x09, 0x50, 0xc9, 0x05, 0xba, 0xe2, 0xa2 } };
        constexpr CLSID kPresenterNextClsid = { 0x51668040, 0x5e31, 0x4023, { 0xbb, 0xe3, 0x14, 0x91, 0x63, 0xbd, 0x80, 0xf1 } };
        constexpr UINT32 kFeatureNextPresenter = 60813048;

        // Come il riquadro: comandi della shell da 11 a 0x7FFF; voce del riquadro (LOCATION_CONTEXT_FLAGS 0x10);
        // ospite "riquadro di spostamento" (CONTEXT_MENU_PRESENTER_HOST 2).
        constexpr UINT kFirstId = 11;
        constexpr UINT kLastId = 0x7FFF;
        constexpr DWORD kLocationNavPaneItem = 0x10;
        constexpr int kHostNavPane = 2;
        // QueryContextMenu: CMF_EXPLORE | CMF_ASYNCVERBSTATE | 0x8000 (menu nuovo). Il riquadro aggiunge
        // CMF_CANRENAME perché rinomina nell'albero: nella barra non si rinomina, quindi niente "Rinomina".
        constexpr UINT kQueryFlags = CMF_EXPLORE | CMF_ASYNCVERBSTATE | 0x8000;
        // SetMenuRestrictions: solo i gestori che supportano il menu nuovo (gli altri in "Mostra altre opzioni").
        constexpr int kRestrictions = DFMR_OPTIN_HANDLERS_ONLY | 0x2000;
        constexpr int kRestrictionsNext = 0x8000;

        constexpr wchar_t kMessageClass[] = L"VTabsExplorer.ModernMenu";

        // Stato di una funzione di Windows (come la legge wil): accesa solo se configurata accesa.
        bool FeatureEnabled(UINT32 id)
        {
            using Query = LONG(NTAPI*)(UINT32, int, PULONGLONG, void*);
            static const auto query = reinterpret_cast<Query>(GetProcAddress(GetModuleHandleW(L"ntdll.dll"), "RtlQueryFeatureConfiguration"));
            if (!query)
            {
                return false;
            }
            struct
            {
                UINT32 id;
                UINT32 bits; // Priority:4, EnabledState:2 (1 spenta, 2 accesa), ...
                UINT32 payload;
            } config{};
            ULONGLONG stamp = 0;
            return query(id, 1 /* runtime */, &stamp, &config) >= 0 && ((config.bits >> 4) & 3) == 2;
        }

        // Messaggio con cui il presenter chiede (SendMessage alla finestra dell'ospite) di eseguire la voce scelta.
        UINT InvokeMessage()
        {
            static const UINT message = RegisterWindowMessageW(L"FILE_EXPLORER_CONTEXTMENU_INVOKEMENUITEM");
            return message;
        }

        HINSTANCE ThisModule()
        {
            HMODULE module = nullptr;
            GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                               reinterpret_cast<LPCWSTR>(&InvokeMessage), &module);
            return module;
        }

        template <typename F>
        void Safely(const wchar_t* where, F&& action) noexcept
        {
            try
            {
                action();
            }
            catch (...)
            {
                XPX_MLOG(L"ModernMenu: errore in %s (0x%08X).", where, static_cast<unsigned>(winrt::to_hresult()));
            }
        }
    }

    // L'ospite che il presenter richiama: scelta, chiusura, "Mostra altre opzioni", fuoco; e la finestra a cui
    // manda l'ordine di eseguire la voce (IOleWindow). owner si azzera quando ModernMenu se ne va.
    struct ModernMenuHost : winrt::implements<ModernMenuHost, IInvokeContextMenuCommand, IOleWindow>
    {
        ModernMenu* owner = nullptr;

        void STDMETHODCALLTYPE Invoke(IContextMenu* menu, IShellItemArray*, HMENU, UINT id, POINT point) override
        {
            if (owner)
            {
                owner->OnInvoke(menu, id, point);
            }
        }
        void STDMETHODCALLTYPE OnContextMenuDismiss() override
        {
            if (owner)
            {
                owner->OnDismiss();
            }
        }
        void STDMETHODCALLTYPE DoExpandedContextMenu(POINT point, BOOL, GUID*, POINT*, BOOL) override
        {
            if (owner)
            {
                owner->OnExpand(point);
            }
        }
        void STDMETHODCALLTYPE SetFocus() override
        {
            if (owner)
            {
                owner->OnFocus();
            }
        }
        HRESULT STDMETHODCALLTYPE GetWindow(HWND* window) override
        {
            if (!window)
            {
                return E_POINTER;
            }
            *window = owner ? owner->m_messageWindow : nullptr;
            return *window ? S_OK : E_FAIL;
        }
        HRESULT STDMETHODCALLTYPE ContextSensitiveHelp(BOOL) override
        {
            return E_NOTIMPL;
        }
    };

    namespace
    {
        IContextMenuPresenter* AsPresenter(winrt::com_ptr<::IUnknown> const& presenter)
        {
            return static_cast<IContextMenuPresenter*>(presenter.get());
        }
    }

    bool ModernMenu::UnregisterWindowClass()
    {
        WNDCLASSEXW info{ sizeof(info) };
        if (!GetClassInfoExW(ThisModule(), kMessageClass, &info))
        {
            return true;
        }
        return UnregisterClassW(kMessageClass, ThisModule()) != FALSE; // (fallisce se c'è ancora una finestra della classe)
    }

    ModernMenu::ModernMenu(HWND owner, Callbacks callbacks) : m_owner(owner), m_callbacks(std::move(callbacks))
    {
    }

    ModernMenu::~ModernMenu()
    {
        if (m_presenter)
        {
            Safely(L"chiusura", [&] {
                AsPresenter(m_presenter)->DismissContextMenu(L"VTabsExplorer");
                // Close scollega l'ospite dal presenter (che non lo richiamerà più) e libera la sua isola XAML.
                if (const auto closable = m_presenter.try_as<winrt::Windows::Foundation::IClosable>())
                {
                    closable.Close();
                }
            });
            m_presenter = nullptr;
        }
        if (m_host)
        {
            m_host->owner = nullptr;
            m_host = nullptr;
        }
        if (m_messageWindow)
        {
            DestroyWindow(m_messageWindow);
            m_messageWindow = nullptr;
        }
    }

    bool ModernMenu::Ensure()
    {
        if (m_presenter)
        {
            return true;
        }
        if (m_failed || !m_owner)
        {
            return false;
        }
        m_failed = true; // finché non riesce tutto

        // Ogni volta (la classe si toglie allo scaricamento della mod, e una ripartenza la rimette).
        const bool registered = [] {
            WNDCLASSEXW info{ sizeof(info) };
            if (GetClassInfoExW(ThisModule(), kMessageClass, &info))
            {
                return true;
            }
            WNDCLASSEXW wc{ sizeof(wc) };
            wc.lpfnWndProc = MessageProc;
            wc.hInstance = ThisModule();
            wc.lpszClassName = kMessageClass;
            return RegisterClassExW(&wc) != 0 || GetLastError() == ERROR_CLASS_ALREADY_EXISTS;
        }();
        if (!registered)
        {
            XPX_MLOG(L"ModernMenu: classe della finestra dei messaggi non registrata (%lu).", GetLastError());
            return false;
        }
        m_messageWindow = CreateWindowExW(0, kMessageClass, L"", 0, 0, 0, 0, 0, HWND_MESSAGE, nullptr, ThisModule(), nullptr);
        if (!m_messageWindow)
        {
            XPX_MLOG(L"ModernMenu: finestra dei messaggi non creata (%lu).", GetLastError());
            return false;
        }
        SetWindowLongPtrW(m_messageWindow, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(this));

        const bool next = FeatureEnabled(kFeatureNextPresenter);
        winrt::com_ptr<::IUnknown> presenter;
        HRESULT result = CoCreateInstance(next ? kPresenterNextClsid : kPresenterClsid, nullptr, CLSCTX_INPROC_SERVER, __uuidof(IContextMenuPresenter),
                                          presenter.put_void());
        if (FAILED(result) || !presenter)
        {
            XPX_MLOG(L"ModernMenu: presenter del menu non disponibile (0x%08X): resta il menu classico.", static_cast<unsigned>(result));
            return false;
        }
        m_host = winrt::make_self<ModernMenuHost>();
        m_host->owner = this;
        result = AsPresenter(presenter)->Initialize(TRUE, static_cast<IInvokeContextMenuCommand*>(m_host.get()), m_owner, kHostNavPane);
        if (FAILED(result))
        {
            XPX_MLOG(L"ModernMenu: Initialize del presenter non riuscita (0x%08X): resta il menu classico.", static_cast<unsigned>(result));
            m_host->owner = nullptr;
            m_host = nullptr;
            return false;
        }
        m_presenter = presenter;
        m_failed = false;
        XPX_MLOG(L"ModernMenu: presenter del menu nuovo pronto (%s).", next ? L"versione nuova" : L"File Explorer Context Menu");
        return true;
    }

    ModernMenu::ShowResult ModernMenu::Show(IContextMenu* menu, IShellItem* item, ::IUnknown* site, POINT screen, DWORD inputFlags)
    {
        if (!menu || !Ensure())
        {
            return ShowResult::Unavailable;
        }
        IContextMenuPresenter* presenter = AsPresenter(m_presenter);
        if (!presenter->IsReadyForNewContextMenu(GUID{}, screen))
        {
            // Il menu di prima si sta ancora chiudendo: il presenter si segna il punto e, chiuso, manda
            // WM_CONTEXTMENU alla finestra proprietaria (come per il riquadro). Intanto niente menu.
            XPX_MLOG(L"ModernMenu: il menu di prima si sta chiudendo: si riapre dopo, da sé.");
            return ShowResult::Deferred;
        }
        const HMENU popup = CreatePopupMenu();
        if (!popup)
        {
            return ShowResult::Unavailable;
        }
        winrt::com_ptr<IContextMenu> held;
        held.copy_from(menu);
        if (const auto init = held.try_as<IDefaultFolderMenuInitialize>())
        {
            init->SetMenuRestrictions(static_cast<DEFAULT_FOLDER_MENU_RESTRICTIONS>(kRestrictions | (FeatureEnabled(kFeatureNextPresenter) ? kRestrictionsNext : 0)));
        }
        HRESULT result = menu->QueryContextMenu(popup, 0, kFirstId, kLastId, kQueryFlags);
        if (FAILED(result))
        {
            XPX_MLOG(L"ModernMenu: QueryContextMenu non riuscita (0x%08X).", static_cast<unsigned>(result));
            DestroyMenu(popup);
            return ShowResult::Unavailable;
        }
        // Come il riquadro: niente "Crea collegamento" né "Apri" (la voce si apre con il clic).
        CleanupSeparators(popup);
        DeleteVerb(menu, popup, kFirstId, L"link");
        DeleteVerb(menu, popup, kFirstId, L"open");
        CleanupSeparators(popup);
        // Voci nostre qui non servono: il presenter mostra solo i verbi di Windows ammessi per il punto
        // (GetAllowedVerbs + FilterClassicMenu) e le estensioni installate; le altre restano in "Mostra altre opzioni".
        result = presenter->PrepareForContextMenu(kLocationNavPaneItem, site, screen, menu, kFirstId, kLastId, TRUE, nullptr, GUID{}, item);
        if (FAILED(result))
        {
            XPX_MLOG(L"ModernMenu: PrepareForContextMenu non riuscita (0x%08X): menu classico.", static_cast<unsigned>(result));
            DestroyMenu(popup);
            return ShowResult::Unavailable;
        }
        presenter->DoContextMenu(popup, inputFlags, GUID{}); // il menu (HMENU) ora è del presenter
        return ShowResult::Shown;
    }

    void ModernMenu::Dismiss()
    {
        if (m_presenter)
        {
            Safely(L"chiusura del menu", [&] { AsPresenter(m_presenter)->DismissContextMenu(L"VTabsExplorer"); });
        }
    }

    bool ModernMenu::IsOpen() const
    {
        return m_presenter && AsPresenter(m_presenter)->IsContextMenuOpen();
    }

    void ModernMenu::OnInvoke(IContextMenu* menu, UINT id, POINT point)
    {
        if (id >= kFirstId && menu && m_callbacks.invoke)
        {
            Safely(L"esecuzione della voce", [&] { m_callbacks.invoke(menu, id - kFirstId, point); });
        }
    }

    void ModernMenu::OnDismiss()
    {
        if (m_callbacks.dismissed)
        {
            Safely(L"chiusura", [&] { m_callbacks.dismissed(); });
        }
    }

    void ModernMenu::OnExpand(POINT point)
    {
        if (m_callbacks.expanded)
        {
            Safely(L"menu classico", [&] { m_callbacks.expanded(point); });
        }
    }

    void ModernMenu::OnFocus()
    {
        if (m_callbacks.focus)
        {
            Safely(L"fuoco", [&] { m_callbacks.focus(); });
        }
    }

    LRESULT CALLBACK ModernMenu::MessageProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam)
    {
        if (message == InvokeMessage())
        {
            auto* self = reinterpret_cast<ModernMenu*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
            if (self && self->m_presenter)
            {
                // Il presenter chiama l'ospite (Invoke, poi OnContextMenuDismiss) con la voce scelta.
                const winrt::com_ptr<::IUnknown> keep = self->m_presenter;
                Safely(L"voce scelta", [&] { AsPresenter(keep)->Invoke(static_cast<UINT>(wParam)); });
            }
            return 0;
        }
        return DefWindowProcW(hwnd, message, wParam, lParam);
    }
}
