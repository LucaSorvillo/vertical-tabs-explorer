#include "pch.h"
#include "Discovery.h"

#include "Interop.h"
#include "ModLog.h"

#include <UIAutomationClient.h>

extern HMODULE g_module;

namespace xpx::mod::discovery
{
    namespace
    {
        namespace mux = winrt::Microsoft::UI::Xaml;
        namespace muxc = winrt::Microsoft::UI::Xaml::Controls;
        namespace muxap = winrt::Microsoft::UI::Xaml::Automation::Peers;

        // Slot nelle vtable (0-5: IUnknown e IInspectable). Vedi abi<...> in winrt/impl/Microsoft.UI.Xaml*.0.h.
        constexpr size_t kOnApplyTemplateSlot = 8;              // IFrameworkElementOverrides
        constexpr size_t kGetAutomationControlTypeCoreSlot = 9; // IAutomationPeerOverrides

        using OnApplyTemplateFn = int32_t(__stdcall*)(void* self);
        using GetControlTypeFn = int32_t(__stdcall*)(void* self, int32_t* result);

        std::atomic<OnApplyTemplateFn> g_onApplyTemplate{ nullptr };
        std::atomic<GetControlTypeFn> g_getControlType{ nullptr };
        std::atomic<Callback> g_callback{ nullptr };
        std::atomic<bool> g_installed{ false };
        std::atomic<bool> g_blocked{ false }; // XFG attivo: agganci impossibili per sempre in questo processo
        std::atomic<int> g_probes{ 0 };       // ricerche UI Automation nostre in corso
        std::mutex g_installLock;

        void Notify(muxc::TabView const& tabView) noexcept
        {
            if (const Callback callback = g_callback.load())
            {
                callback(tabView);
            }
        }

        // Scheda o barra: la barra è il primo TabView fra gli antenati visuali.
        muxc::TabView TabViewAncestor(mux::DependencyObject node)
        {
            for (int depth = 0; node && depth < 24; ++depth)
            {
                if (auto tabView = node.try_as<muxc::TabView>())
                {
                    return tabView;
                }
                node = mux::Media::VisualTreeHelper::GetParent(node);
            }
            return nullptr;
        }

        int32_t __stdcall OnApplyTemplateHook(void* self) noexcept
        {
            // Prima l'originale: il modello della barra deve essere applicato come sempre.
            const int32_t result = g_onApplyTemplate.load()(self);
            try
            {
                mux::IFrameworkElementOverrides overrides{ nullptr };
                winrt::copy_from_abi(overrides, self);
                if (const auto tabView = overrides.try_as<muxc::TabView>())
                {
                    Notify(tabView);
                }
            }
            catch (...)
            {
            }
            return result;
        }

        int32_t __stdcall GetControlTypeHook(void* self, int32_t* type) noexcept
        {
            const int32_t result = g_getControlType.load()(self, type);
            if (g_probes.load() > 0)
            {
                try
                {
                    muxap::IAutomationPeerOverrides overrides{ nullptr };
                    winrt::copy_from_abi(overrides, self);
                    if (const auto peer = overrides.try_as<muxap::FrameworkElementAutomationPeer>())
                    {
                        if (const auto tabView = TabViewAncestor(peer.Owner()))
                        {
                            Notify(tabView);
                        }
                    }
                }
                catch (...)
                {
                }
            }
            return result;
        }

        // XFG (eXtended Flow Guard) attivo nel modulo: chi chiama attraverso la vtable controllerebbe un'impronta del tipo
        // della funzione che la nostra non ha (chiusura immediata del processo). Oggi Microsoft.UI.Xaml.dll usa solo CFG;
        // se un aggiornamento accendesse XFG, niente agganci (niente schede verticali) invece di un crash.
        bool ModuleHasXfg(const void* address)
        {
            HMODULE module = nullptr;
            if (!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                                    static_cast<LPCWSTR>(address), &module) ||
                !module)
            {
                return true; // indirizzo fuori da un modulo: meglio non toccare
            }
            const auto* base = reinterpret_cast<const BYTE*>(module);
            const auto* dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(base);
            const auto* nt = reinterpret_cast<const IMAGE_NT_HEADERS*>(base + dos->e_lfanew);
            const IMAGE_DATA_DIRECTORY& directory = nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_LOAD_CONFIG];
            if (!directory.VirtualAddress)
            {
                return false;
            }
            const auto* config = reinterpret_cast<const IMAGE_LOAD_CONFIG_DIRECTORY*>(base + directory.VirtualAddress);
            if (config->Size < offsetof(IMAGE_LOAD_CONFIG_DIRECTORY, GuardFlags) + sizeof(DWORD))
            {
                return false; // struttura vecchia, senza GuardFlags: niente XFG
            }
            constexpr DWORD kXfgEnabled = 0x00800000; // IMAGE_GUARD_XFG_ENABLED
            return (config->GuardFlags & kXfgEnabled) != 0;
        }

        // Sostituisce la voce slot della vtable dell'interfaccia abi con hook; original riceve la funzione di prima PRIMA
        // dello scambio (una chiamata che arriva subito dopo trova già l'originale). Idempotente.
        template <typename Fn>
        bool PatchSlot(void* abi, size_t slot, void* hook, std::atomic<Fn>& original)
        {
            void** vtable = *static_cast<void***>(abi);
            void** entry = vtable + slot;
            void* const current = *entry;
            if (current == hook)
            {
                return true; // già nostro
            }
            if (ModuleHasXfg(current) || ModuleHasXfg(vtable))
            {
                g_blocked = true;
                XPX_MLOG(L"Agganci: XFG attivo nel modulo della classe, vtable non toccata (schede verticali non disponibili).");
                return false;
            }
            original.store(reinterpret_cast<Fn>(current));
            DWORD protection = 0;
            if (!VirtualProtect(entry, sizeof(void*), PAGE_READWRITE, &protection))
            {
                XPX_MLOG(L"Agganci: VirtualProtect non riuscita (%lu).", GetLastError());
                return false;
            }
            const bool swapped = InterlockedCompareExchangePointer(entry, hook, current) == current;
            DWORD ignored = 0;
            VirtualProtect(entry, sizeof(void*), protection, &ignored);
            return swapped;
        }

        bool InstallOnThisThread()
        {
            try
            {
                // Istanze nostre, mai in un albero: servono solo per arrivare alle vtable delle classi.
                muxc::TabView tabView;
                const auto overrides = tabView.as<mux::IFrameworkElementOverrides>();
                const bool template_ = PatchSlot(winrt::get_abi(overrides), kOnApplyTemplateSlot, reinterpret_cast<void*>(&OnApplyTemplateHook), g_onApplyTemplate);

                muxc::TabViewItem item;
                const auto peer = muxap::FrameworkElementAutomationPeer::CreatePeerForElement(item);
                const std::wstring peerType = peer ? std::wstring(winrt::get_class_name(peer)) : L"-";
                const auto peerOverrides = peer ? peer.try_as<muxap::IAutomationPeerOverrides>() : nullptr;
                const bool automation = peerOverrides &&
                                        PatchSlot(winrt::get_abi(peerOverrides), kGetAutomationControlTypeCoreSlot, reinterpret_cast<void*>(&GetControlTypeHook),
                                                  g_getControlType);
                XPX_MLOG(L"Agganci: OnApplyTemplate di TabView %s, GetAutomationControlTypeCore di %s %s.", template_ ? L"installato" : L"NON installato",
                         peerType.c_str(), automation ? L"installato" : L"NON installato");
                return template_ && automation;
            }
            catch (...)
            {
                XPX_MLOG(L"Agganci: errore 0x%08X.", static_cast<unsigned>(winrt::to_hresult()));
                return false;
            }
        }

        UINT InstallMessage()
        {
            static const UINT message = RegisterWindowMessageW(L"VTabsExplorer.Mod.InstallDiscovery");
            return message;
        }

        std::atomic<int> g_installResult{ -1 }; // -1 nessuna risposta, 0 fallito, 1 riuscito

        LRESULT CALLBACK InstallProc(int code, WPARAM wParam, LPARAM lParam)
        {
            const auto* message = reinterpret_cast<CWPSTRUCT const*>(lParam);
            if (code == HC_ACTION && message && message->message == InstallMessage() && g_installResult.load() == -1)
            {
                g_installResult = InstallOnThisThread() ? 1 : 0;
            }
            return CallNextHookEx(nullptr, code, wParam, lParam);
        }

        // Isola XAML dell'intestazione: figlia DesktopChildSiteBridge visibile più in alto, escluse le nostre.
        HWND HeaderIsland(HWND main)
        {
            HWND best = nullptr;
            LONG bestTop = LONG_MAX;
            for (HWND child : interop::DirectChildren(main))
            {
                if (!IsWindowVisible(child) || interop::ClassOf(child) != L"Microsoft.UI.Content.DesktopChildSiteBridge" ||
                    GetPropW(child, interop::kPanelProp) || GetPropW(child, interop::kNavBarProp))
                {
                    continue;
                }
                const RECT rect = interop::RectInClientOf(child, main);
                if (rect.top < bestTop)
                {
                    bestTop = rect.top;
                    best = child;
                }
            }
            return best;
        }
    }

    bool Installed()
    {
        return g_installed.load();
    }

    bool Blocked()
    {
        return g_blocked.load();
    }

    bool Install(HWND window, Callback callback)
    {
        g_callback = callback;
        std::scoped_lock lock(g_installLock);
        if (g_installed.load())
        {
            return true;
        }
        if (g_blocked.load())
        {
            return false;
        }
        const DWORD thread = GetWindowThreadProcessId(window, nullptr);
        if (!thread || thread == GetCurrentThreadId())
        {
            return false;
        }
        // Il lavoro va fatto sul thread della finestra (XAML è legato al suo thread): un gancio WH_CALLWNDPROC solo su quel
        // thread, poi un messaggio nostro mandato alla finestra; il gancio lo vede prima della finestra.
        g_installResult = -1;
        const HHOOK hook = SetWindowsHookExW(WH_CALLWNDPROC, InstallProc, g_module, thread);
        if (!hook)
        {
            XPX_MLOG(L"Agganci: SetWindowsHookEx non riuscita (%lu).", GetLastError());
            return false;
        }
        DWORD_PTR ignored = 0;
        SendMessageTimeoutW(window, InstallMessage(), 0, 0, SMTO_BLOCK | SMTO_ABORTIFHUNG, 5000, &ignored);
        UnhookWindowsHookEx(hook);
        const bool ok = g_installResult.load() == 1;
        g_installed = ok;
        return ok;
    }

    bool Probe(HWND window)
    {
        if (!g_installed.load() || !IsWindow(window))
        {
            return false;
        }
        const HWND island = HeaderIsland(window);
        if (!island)
        {
            return false;
        }
        try
        {
            winrt::com_ptr<IUIAutomation> automation;
            winrt::check_hresult(CoCreateInstance(__uuidof(CUIAutomation), nullptr, CLSCTX_INPROC_SERVER, __uuidof(IUIAutomation), automation.put_void()));
            winrt::com_ptr<IUIAutomationElement> root;
            winrt::check_hresult(automation->ElementFromHandle(island, root.put()));
            VARIANT type{};
            type.vt = VT_I4;
            type.lVal = UIA_TabItemControlTypeId;
            winrt::com_ptr<IUIAutomationCondition> condition;
            winrt::check_hresult(automation->CreatePropertyCondition(UIA_ControlTypePropertyId, type, condition.put()));
            winrt::com_ptr<IUIAutomationElement> found;
            ++g_probes;
            const HRESULT hr = root->FindFirst(TreeScope_Descendants, condition.get(), found.put());
            --g_probes;
            return SUCCEEDED(hr) && found;
        }
        catch (...)
        {
            XPX_MLOG(L"Ricerca della barra delle schede: errore 0x%08X.", static_cast<unsigned>(winrt::to_hresult()));
            return false;
        }
    }
}
