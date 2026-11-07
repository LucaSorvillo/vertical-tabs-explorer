#include "pch.h"

#include <shellapi.h> // SEE_MASK_UNICODE (CMIC_MASK_UNICODE)
#include <shlobj.h>   // BHID_SFObject

#include "MenuArray.h"

#include <shlwapi.h>

namespace xpx::mod
{
    namespace
    {
        constexpr wchar_t kPlaceholder[] = L"{VTabsExplorer.MenuArray.Placeholder}";
        constexpr HRESULT kCopyEngineCancelled = static_cast<HRESULT>(0x80270000L); // COPYENGINE_E_USER_CANCELLED

        bool IsOffset(UINT_PTR verb)
        {
            return verb < 0x10000;
        }

        class MenuArray final : public IContextMenu3, public IObjectWithSite, public IServiceProvider, public IDefaultFolderMenuInitialize
        {
            // Conta fra gli oggetti vivi della DLL, come quelli C++/WinRT: la mod non si scarica finché la shell ne tiene uno.
            winrt::impl::update_module_lock m_moduleLock;

        public:
            explicit MenuArray(std::vector<winrt::com_ptr<IContextMenu>> menus) : m_menus(std::move(menus))
            {
                for (auto const& menu : m_menus)
                {
                    m_menus2.push_back(menu.try_as<IContextMenu2>());
                    m_menus3.push_back(menu.try_as<IContextMenu3>());
                }
                m_last.assign(m_menus.size(), 0);
            }

            // IUnknown
            IFACEMETHODIMP QueryInterface(REFIID riid, void** ppv) override
            {
                if (!ppv)
                {
                    return E_POINTER;
                }
                if (riid == IID_IUnknown || riid == IID_IContextMenu || riid == IID_IContextMenu2 || riid == IID_IContextMenu3)
                {
                    *ppv = static_cast<IContextMenu3*>(this);
                }
                else if (riid == IID_IObjectWithSite)
                {
                    *ppv = static_cast<IObjectWithSite*>(this);
                }
                else if (riid == IID_IServiceProvider)
                {
                    *ppv = static_cast<IServiceProvider*>(this);
                }
                else if (riid == __uuidof(IDefaultFolderMenuInitialize))
                {
                    *ppv = static_cast<IDefaultFolderMenuInitialize*>(this);
                }
                else
                {
                    *ppv = nullptr;
                    return E_NOINTERFACE;
                }
                AddRef();
                return S_OK;
            }
            IFACEMETHODIMP_(ULONG) AddRef() override
            {
                return ++m_refs;
            }
            IFACEMETHODIMP_(ULONG) Release() override
            {
                const ULONG refs = --m_refs;
                if (refs == 0)
                {
                    delete this;
                }
                return refs;
            }

            // IContextMenu
            IFACEMETHODIMP QueryContextMenu(HMENU menu, UINT index, UINT first, UINT last, UINT flags) override
            {
                m_first = first;
                // Segnaposto: dopo ogni menu si continua da dove è finito (le voci aggiunte lo spingono in giù).
                const bool placeholder = InsertMenuW(menu, index, MF_BYPOSITION | MF_STRING, 0, kPlaceholder) != FALSE;
                UINT position = index;
                UINT next = first;
                for (size_t i = 0; i < m_menus.size(); ++i)
                {
                    if (next >= last)
                    {
                        m_last[i] = i ? m_last[i - 1] : 0;
                        continue;
                    }
                    const HRESULT result = m_menus[i]->QueryContextMenu(menu, position, next, last, flags);
                    if (SUCCEEDED(result))
                    {
                        const UINT used = static_cast<UINT>(static_cast<USHORT>(HRESULT_CODE(result)));
                        m_last[i] = next + used - first;
                        next += used + 1;
                        if (placeholder)
                        {
                            position = PlaceholderPosition(menu, position);
                        }
                    }
                    else
                    {
                        m_last[i] = i ? m_last[i - 1] : 0;
                        if (i == 0)
                        {
                            ++next;
                        }
                    }
                }
                if (placeholder)
                {
                    DeleteMenu(menu, PlaceholderPosition(menu, position), MF_BYPOSITION);
                }
                RemoveDuplicateVerbs(menu);
                return MAKE_HRESULT(SEVERITY_SUCCESS, 0, next - first);
            }

            IFACEMETHODIMP InvokeCommand(CMINVOKECOMMANDINFO* info) override
            {
                if (!info)
                {
                    return E_INVALIDARG;
                }
                const UINT_PTR verb = reinterpret_cast<UINT_PTR>(info->lpVerb);
                if (IsOffset(verb))
                {
                    UINT local = 0;
                    const int owner = Owner(static_cast<UINT>(verb), local);
                    if (owner < 0)
                    {
                        return E_INVALIDARG;
                    }
                    // Offset del menu che ha il comando; la struttura di chi chiama torna com'era.
                    auto* extended = info->cbSize >= sizeof(CMINVOKECOMMANDINFOEX) ? reinterpret_cast<CMINVOKECOMMANDINFOEX*>(info) : nullptr;
                    const LPCWSTR verbW = extended ? extended->lpVerbW : nullptr;
                    info->lpVerb = MAKEINTRESOURCEA(local);
                    if (extended && (extended->fMask & CMIC_MASK_UNICODE) && IS_INTRESOURCE(verbW))
                    {
                        extended->lpVerbW = MAKEINTRESOURCEW(local);
                    }
                    const HRESULT result = m_menus[owner]->InvokeCommand(info);
                    info->lpVerb = reinterpret_cast<LPCSTR>(verb);
                    if (extended)
                    {
                        extended->lpVerbW = verbW;
                    }
                    return result;
                }
                // Verbo per nome: il primo menu che lo esegue.
                HRESULT result = E_FAIL;
                for (auto const& menu : m_menus)
                {
                    result = menu->InvokeCommand(info);
                    if (SUCCEEDED(result) || result == HRESULT_FROM_WIN32(ERROR_CANCELLED) || result == kCopyEngineCancelled)
                    {
                        break;
                    }
                }
                return result;
            }

            IFACEMETHODIMP GetCommandString(UINT_PTR command, UINT type, UINT* reserved, CHAR* name, UINT cch) override
            {
                if (IsOffset(command))
                {
                    UINT local = 0;
                    const int owner = Owner(static_cast<UINT>(command), local);
                    return owner < 0 ? E_INVALIDARG : m_menus[owner]->GetCommandString(local, type, reserved, name, cch);
                }
                HRESULT result = E_FAIL;
                for (auto const& menu : m_menus)
                {
                    result = menu->GetCommandString(command, type, reserved, name, cch);
                    if (result == S_OK)
                    {
                        break;
                    }
                }
                return result;
            }

            // IContextMenu2 / IContextMenu3: i messaggi delle voci disegnate dalla shell e dei sottomenu (Nuovo,
            // Apri con, Invia a...) vanno al menu che ha l'id della voce (per un sottomenu: della sua prima voce).
            IFACEMETHODIMP HandleMenuMsg(UINT message, WPARAM wParam, LPARAM lParam) override
            {
                return HandleMenuMsg2(message, wParam, lParam, nullptr);
            }

            IFACEMETHODIMP HandleMenuMsg2(UINT message, WPARAM wParam, LPARAM lParam, LRESULT* result) override
            {
                UINT id = 0;
                switch (message)
                {
                case WM_MENUCHAR:
                    if (!result)
                    {
                        return E_FAIL;
                    }
                    for (auto const& menu3 : m_menus3)
                    {
                        if (menu3 && menu3->HandleMenuMsg2(message, wParam, lParam, result) == S_OK)
                        {
                            return S_OK;
                        }
                    }
                    return E_FAIL;
                case WM_DRAWITEM:
                    id = reinterpret_cast<DRAWITEMSTRUCT*>(lParam)->itemID;
                    break;
                case WM_MEASUREITEM:
                    id = reinterpret_cast<MEASUREITEMSTRUCT*>(lParam)->itemID;
                    break;
                case WM_INITMENUPOPUP:
                    id = GetMenuItemID(reinterpret_cast<HMENU>(wParam), 0);
                    break;
                case WM_MENUSELECT:
                    id = LOWORD(wParam);
                    if (HIWORD(wParam) & MF_POPUP)
                    {
                        MENUITEMINFOW info{ sizeof(info) };
                        info.fMask = MIIM_ID;
                        if (!GetMenuItemInfoW(reinterpret_cast<HMENU>(lParam), id, TRUE, &info))
                        {
                            return E_FAIL;
                        }
                        id = info.wID;
                    }
                    break;
                default:
                    return E_FAIL;
                }
                if (id < m_first)
                {
                    return E_FAIL;
                }
                UINT local = 0;
                const int owner = Owner(id - m_first, local);
                if (owner < 0)
                {
                    return E_FAIL;
                }
                if (m_menus3[owner])
                {
                    return m_menus3[owner]->HandleMenuMsg2(message, wParam, lParam, result);
                }
                if (m_menus2[owner] && !result)
                {
                    return m_menus2[owner]->HandleMenuMsg(message, wParam, lParam);
                }
                return E_FAIL;
            }

            // IObjectWithSite
            IFACEMETHODIMP SetSite(IUnknown* site) override
            {
                for (auto const& menu : m_menus)
                {
                    IUnknown_SetSite(menu.get(), site);
                }
                m_site.copy_from(site);
                return S_OK;
            }
            IFACEMETHODIMP GetSite(REFIID riid, void** ppv) override
            {
                if (!ppv)
                {
                    return E_POINTER;
                }
                *ppv = nullptr;
                return m_site ? m_site->QueryInterface(riid, ppv) : E_FAIL;
            }

            // IServiceProvider: dal sito.
            IFACEMETHODIMP QueryService(REFGUID service, REFIID riid, void** ppv) override
            {
                if (!ppv)
                {
                    return E_POINTER;
                }
                *ppv = nullptr;
                return m_site ? IUnknown_QueryService(m_site.get(), service, riid, ppv) : E_NOINTERFACE;
            }

            // IDefaultFolderMenuInitialize: le restrizioni a tutti i menu.
            IFACEMETHODIMP Initialize(HWND, IContextMenuCB*, PCIDLIST_ABSOLUTE, IShellFolder*, UINT, PCUITEMID_CHILD_ARRAY, IUnknown*, UINT, const HKEY*) override
            {
                return E_NOTIMPL;
            }
            IFACEMETHODIMP SetMenuRestrictions(DEFAULT_FOLDER_MENU_RESTRICTIONS values) override
            {
                for (auto const& menu : m_menus)
                {
                    if (const auto init = menu.try_as<IDefaultFolderMenuInitialize>())
                    {
                        init->SetMenuRestrictions(values);
                    }
                }
                m_restrictions = values;
                return S_OK;
            }
            IFACEMETHODIMP GetMenuRestrictions(DEFAULT_FOLDER_MENU_RESTRICTIONS mask, DEFAULT_FOLDER_MENU_RESTRICTIONS* values) override
            {
                if (!values)
                {
                    return E_POINTER;
                }
                *values = static_cast<DEFAULT_FOLDER_MENU_RESTRICTIONS>(m_restrictions & mask);
                return S_OK;
            }
            IFACEMETHODIMP SetHandlerClsid(REFCLSID) override
            {
                return E_NOTIMPL;
            }

        private:
            ~MenuArray() = default;

            // Menu che ha l'offset (id meno il primo) e l'offset nel suo spazio; -1 se nessuno.
            int Owner(UINT offset, UINT& local) const
            {
                for (size_t i = 0; i < m_menus.size(); ++i)
                {
                    if (offset <= m_last[i])
                    {
                        local = i == 0 ? offset : offset - m_last[i - 1] - 1;
                        return static_cast<int>(i);
                    }
                }
                return -1;
            }

            static UINT PlaceholderPosition(HMENU menu, UINT from)
            {
                const int count = GetMenuItemCount(menu);
                for (int k = 0; k < count; ++k)
                {
                    const UINT position = (from + static_cast<UINT>(k)) % static_cast<UINT>(count);
                    wchar_t text[64]{};
                    if (GetMenuStringW(menu, position, text, ARRAYSIZE(text), MF_BYPOSITION) && wcscmp(text, kPlaceholder) == 0)
                    {
                        return position;
                    }
                }
                return from;
            }

            // Verbo (minuscolo) della voce in quella posizione e menu che la ha; owner -1 se non è nostra o non ha verbo.
            std::wstring VerbAt(HMENU menu, int position, int& owner) const
            {
                owner = -1;
                MENUITEMINFOW info{ sizeof(info) };
                info.fMask = MIIM_FTYPE | MIIM_ID;
                if (!GetMenuItemInfoW(menu, position, TRUE, &info) || (info.fType & MFT_SEPARATOR) || info.wID < m_first)
                {
                    return {};
                }
                UINT local = 0;
                const int menuIndex = Owner(info.wID - m_first, local);
                if (menuIndex < 0)
                {
                    return {};
                }
                wchar_t verb[80]{};
                if (FAILED(m_menus[menuIndex]->GetCommandString(local, GCS_VERBW, nullptr, reinterpret_cast<LPSTR>(verb), ARRAYSIZE(verb))) || !verb[0])
                {
                    char verbA[80]{};
                    if (FAILED(m_menus[menuIndex]->GetCommandString(local, GCS_VERBA, nullptr, verbA, ARRAYSIZE(verbA))) || !verbA[0] ||
                        !MultiByteToWideChar(CP_ACP, 0, verbA, -1, verb, ARRAYSIZE(verb)))
                    {
                        return {};
                    }
                }
                std::wstring key(verb);
                CharLowerBuffW(key.data(), static_cast<DWORD>(key.size()));
                owner = menuIndex;
                return key;
            }

            // Voci con un verbo dato anche da un menu precedente: via (resta quella del primo menu). Due passate:
            // la posizione non dice il menu (la shell mette per esempio "Proprietà" sempre in fondo).
            void RemoveDuplicateVerbs(HMENU menu)
            {
                std::unordered_map<std::wstring, int> first;
                for (int position = 0; position < GetMenuItemCount(menu); ++position)
                {
                    int owner = -1;
                    const auto verb = VerbAt(menu, position, owner);
                    if (owner >= 0)
                    {
                        const auto it = first.find(verb);
                        if (it == first.end() || owner < it->second)
                        {
                            first[verb] = owner;
                        }
                    }
                }
                for (int position = GetMenuItemCount(menu) - 1; position >= 0; --position)
                {
                    int owner = -1;
                    const auto verb = VerbAt(menu, position, owner);
                    if (owner >= 0 && first[verb] < owner)
                    {
                        DeleteMenu(menu, position, MF_BYPOSITION);
                    }
                }
            }

            std::atomic<ULONG> m_refs{ 1 };
            std::vector<winrt::com_ptr<IContextMenu>> m_menus;
            std::vector<winrt::com_ptr<IContextMenu2>> m_menus2;
            std::vector<winrt::com_ptr<IContextMenu3>> m_menus3;
            std::vector<UINT> m_last; // per menu: offset dopo la sua fascia (come il riquadro: "<= m_last" è suo)
            UINT m_first = 0;
            winrt::com_ptr<IUnknown> m_site;
            DEFAULT_FOLDER_MENU_RESTRICTIONS m_restrictions = DFMR_DEFAULT;
        };
    }

    winrt::com_ptr<IContextMenu> MakeMenuArray(std::vector<winrt::com_ptr<IContextMenu>> menus)
    {
        std::erase_if(menus, [](auto const& menu) { return !menu; });
        if (menus.size() == 1)
        {
            return menus.front();
        }
        winrt::com_ptr<IContextMenu> result;
        if (!menus.empty())
        {
            result.attach(static_cast<IContextMenu3*>(new MenuArray(std::move(menus)))); // il riferimento iniziale passa a result
        }
        return result;
    }

    winrt::com_ptr<IContextMenu> BackgroundMenuOf(IShellItem* item, HWND owner)
    {
        winrt::com_ptr<IContextMenu> menu;
        winrt::com_ptr<IShellFolder> folder;
        if (item && SUCCEEDED(item->BindToHandler(nullptr, BHID_SFObject, IID_PPV_ARGS(folder.put()))) && folder)
        {
            folder->CreateViewObject(owner, IID_PPV_ARGS(menu.put()));
        }
        return menu;
    }

    void DeleteVerb(IContextMenu* menu, HMENU popup, UINT firstId, const wchar_t* verb)
    {
        for (int i = GetMenuItemCount(popup) - 1; i >= 0; --i)
        {
            const UINT id = GetMenuItemID(popup, i);
            if (id == static_cast<UINT>(-1) || id < firstId)
            {
                continue;
            }
            wchar_t name[80]{};
            if (SUCCEEDED(menu->GetCommandString(id - firstId, GCS_VERBW, nullptr, reinterpret_cast<LPSTR>(name), ARRAYSIZE(name))) &&
                _wcsicmp(name, verb) == 0)
            {
                DeleteMenu(popup, i, MF_BYPOSITION);
            }
        }
    }

    void CleanupSeparators(HMENU menu)
    {
        bool previousSeparator = true; // in testa: niente separatori
        for (int position = 0; position < GetMenuItemCount(menu); ++position)
        {
            MENUITEMINFOW info{ sizeof(info) };
            info.fMask = MIIM_FTYPE;
            if (!GetMenuItemInfoW(menu, position, TRUE, &info))
            {
                continue;
            }
            const bool separator = (info.fType & MFT_SEPARATOR) != 0;
            if (separator && previousSeparator && DeleteMenu(menu, position, MF_BYPOSITION))
            {
                --position;
                continue;
            }
            previousSeparator = separator;
        }
        // In coda.
        for (int position = GetMenuItemCount(menu) - 1; position >= 0; --position)
        {
            MENUITEMINFOW info{ sizeof(info) };
            info.fMask = MIIM_FTYPE;
            if (!GetMenuItemInfoW(menu, position, TRUE, &info) || !(info.fType & MFT_SEPARATOR) || !DeleteMenu(menu, position, MF_BYPOSITION))
            {
                break;
            }
        }
    }
}
