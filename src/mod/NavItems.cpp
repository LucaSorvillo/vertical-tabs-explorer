#include "pch.h"
#include "NavItems.h"

#include "ModLog.h"

#include <shlobj.h>
#include <shobjidl.h>
#include <propsys.h>

extern HMODULE g_module;

namespace xpx::mod
{
    namespace
    {
        using winrt::com_ptr;

        constexpr UINT kNotifyMessage = WM_APP + 41;      // notifiche della shell (SHChangeNotifyRegister)
        constexpr DWORD kDebounceMs = 400;                 // le notifiche arrivano a raffiche
        constexpr DWORD kSafetyRefreshMs = 5 * 60'000;     // rilettura comunque ogni 5 minuti
        constexpr ULONGLONG kNamespaceMaxAgeMs = 10 * 60'000; // voci del Desktop (Home, Questo PC...): cambiano di rado
        constexpr DWORD kPinnedSortIndex = 0x48;           // posto delle cartelle aggiunte (vedi NavItems.h)
        // Accesso rapido (aggiunte, frequenti e file recenti: lento da elencare) e "Posizioni frequenti", che ha
        // le stesse cartelle aggiunte, nello stesso ordine, senza i file recenti: ~30 ms invece di ~300.
        constexpr wchar_t kQuickAccess[] = L"shell:::{679f85cb-0220-4080-b29b-5540cc05aab6}";
        constexpr wchar_t kFrequentPlaces[] = L"shell:::{3936E9E4-D92C-4EEE-A85A-BC16D5EA0819}";

        // Elenco delle destinazioni di un'applicazione (windows.storage.dll, non documentato). Le cartelle aggiunte
        // ad Accesso rapido sono le "fissate" dell'AppID di Esplora file (quelle del suo jump list). Metodi nell'ordine
        // della vtable di CAutomaticDestinationList (simboli pubblici di Microsoft, build 26300); servono solo i
        // primi. Un'interfaccia cambiata avrebbe un IID nuovo (com'è successo da Windows 7 a 10): l'IID fa da controllo.
        // PinItem su una cartella già fissata la sposta prima dell'elemento che ha quell'indice (contato prima di
        // toglierla); indice = numero di fissate: in fondo. Salva il file e avvisa la shell da sé.
        struct __declspec(uuid("E9C5EF8D-FD41-4F72-BA87-EB03BAD5817C")) __declspec(novtable) IAutomaticDestinationList : IUnknown
        {
            virtual HRESULT __stdcall Initialize(PCWSTR appId, PCWSTR lnkPath, PCWSTR unused) = 0;
            virtual HRESULT __stdcall HasList(BOOL* has) = 0;
            virtual HRESULT __stdcall GetList(int type, UINT maxCount, int flags, REFIID riid, void** result) = 0;
            virtual HRESULT __stdcall AddUsagePoint(IUnknown* item) = 0;
            virtual HRESULT __stdcall PinItem(IUnknown* item, int index) = 0;
            virtual HRESULT __stdcall IsPinned(IUnknown* item, int* index) = 0;
        };
        constexpr CLSID kAutomaticDestinationList = { 0xF0AE1542, 0xF497, 0x484B, { 0xA1, 0x75, 0xA2, 0x0D, 0xB0, 0x91, 0x44, 0xBA } };
        constexpr wchar_t kExplorerAppId[] = L"Microsoft.Windows.Explorer";

        com_ptr<IShellItem> ItemOf(std::vector<uint8_t> const& idList)
        {
            com_ptr<IShellItem> item;
            if (idList.size() >= 2)
            {
                SHCreateItemFromIDList(reinterpret_cast<PCIDLIST_ABSOLUTE>(idList.data()), IID_PPV_ARGS(item.put()));
            }
            return item;
        }

        struct Candidate
        {
            NavItem item;
            DWORD sort = 0;
            size_t sequence = 0; // ordine di lettura (a parità di indice)
            com_ptr<IShellItem> shellItem;
        };

        // Voci della shell fissate nell'albero (lette al più ogni 10 minuti), solo dal thread delle letture: le svuota lui
        // quando si ferma (gli oggetti della shell vanno rilasciati dal thread che li ha creati, prima di scaricare la DLL).
        std::vector<Candidate>& NamespaceCache()
        {
            static std::vector<Candidate> items;
            return items;
        }
        ULONGLONG& NamespaceCacheTime()
        {
            static ULONGLONG read = 0;
            return read;
        }

        PROPERTYKEY KeyOf(const wchar_t* name)
        {
            PROPERTYKEY key{};
            if (FAILED(PSGetPropertyKeyFromName(name, &key)))
            {
                key = {};
            }
            return key;
        }

        bool GetBool(IShellItem* item, PROPERTYKEY const& key)
        {
            com_ptr<IShellItem2> item2;
            BOOL value = FALSE;
            return key.fmtid != GUID{} && SUCCEEDED(item->QueryInterface(IID_PPV_ARGS(item2.put()))) && SUCCEEDED(item2->GetBool(key, &value)) &&
                   value;
        }

        std::wstring NameOf(IShellItem* item, SIGDN form)
        {
            PWSTR text = nullptr;
            if (FAILED(item->GetDisplayName(form, &text)) || !text)
            {
                return {};
            }
            std::wstring result{ text };
            CoTaskMemFree(text);
            return result;
        }

        std::vector<uint8_t> IdListOf(IShellItem* item)
        {
            PIDLIST_ABSOLUTE pidl = nullptr;
            std::vector<uint8_t> bytes;
            if (SUCCEEDED(SHGetIDListFromObject(item, &pidl)) && pidl)
            {
                const UINT size = ILGetSize(pidl);
                bytes.assign(reinterpret_cast<const uint8_t*>(pidl), reinterpret_cast<const uint8_t*>(pidl) + size);
                CoTaskMemFree(pidl);
            }
            return bytes;
        }

        // SortOrderIndex del CLSID (prima HKCU, poi HKLM); Questo PC non lo ha: dopo Galleria e OneDrive.
        DWORD SortIndexOf(std::wstring const& parsing)
        {
            if (parsing.size() < 40 || parsing.compare(0, 3, L"::{") != 0)
            {
                return 0x60;
            }
            const std::wstring clsid = parsing.substr(2, 38);
            if (_wcsicmp(clsid.c_str(), L"{20D04FE0-3AEA-1069-A2D8-08002B30309D}") == 0)
            {
                return 0x50;
            }
            for (const auto& [root, base] : { std::pair{ HKEY_CURRENT_USER, L"Software\\Classes\\CLSID\\" }, std::pair{ HKEY_LOCAL_MACHINE, L"SOFTWARE\\Classes\\CLSID\\" } })
            {
                DWORD value = 0;
                DWORD size = sizeof(value);
                if (RegGetValueW(root, (std::wstring(base) + clsid).c_str(), L"SortOrderIndex", RRF_RT_REG_DWORD, nullptr, &value, &size) == ERROR_SUCCESS)
                {
                    return value;
                }
            }
            return 0x60;
        }

        // Icona della voce, size x size pixel, BGRA premoltiplicato dall'alto.
        std::vector<uint8_t> IconOf(IShellItem* item, int size)
        {
            com_ptr<IShellItemImageFactory> factory;
            HBITMAP bitmap = nullptr;
            if (FAILED(item->QueryInterface(IID_PPV_ARGS(factory.put()))) || FAILED(factory->GetImage(SIZE{ size, size }, SIIGBF_ICONONLY, &bitmap)) ||
                !bitmap)
            {
                return {};
            }
            BITMAP info{};
            GetObjectW(bitmap, sizeof(info), &info);
            std::vector<uint8_t> pixels;
            if (info.bmWidth == size && std::abs(info.bmHeight) == size)
            {
                BITMAPINFO header{};
                header.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
                header.bmiHeader.biWidth = size;
                header.bmiHeader.biHeight = -size; // dall'alto
                header.bmiHeader.biPlanes = 1;
                header.bmiHeader.biBitCount = 32;
                header.bmiHeader.biCompression = BI_RGB;
                pixels.resize(static_cast<size_t>(size) * size * 4);
                const HDC dc = GetDC(nullptr);
                if (GetDIBits(dc, bitmap, 0, static_cast<UINT>(size), pixels.data(), &header, DIB_RGB_COLORS) != size)
                {
                    pixels.clear();
                }
                ReleaseDC(nullptr, dc);
            }
            DeleteObject(bitmap);
            // Premoltiplicato? Se un canale supera l'alfa non lo è.
            bool premultiplied = true;
            for (size_t i = 0; i + 3 < pixels.size() && premultiplied; i += 4)
            {
                premultiplied = pixels[i] <= pixels[i + 3] && pixels[i + 1] <= pixels[i + 3] && pixels[i + 2] <= pixels[i + 3];
            }
            if (!premultiplied)
            {
                for (size_t i = 0; i + 3 < pixels.size(); i += 4)
                {
                    const unsigned alpha = pixels[i + 3];
                    for (size_t c = 0; c < 3; ++c)
                    {
                        pixels[i + c] = static_cast<uint8_t>(pixels[i + c] * alpha / 255);
                    }
                }
            }
            return pixels;
        }

        LRESULT CALLBACK NotifyProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam)
        {
            if (message == kNotifyMessage)
            {
                PIDLIST_ABSOLUTE* pidls = nullptr;
                LONG event = 0;
                if (const HANDLE lock = SHChangeNotification_Lock(reinterpret_cast<HANDLE>(wParam), static_cast<DWORD>(lParam), &pidls, &event))
                {
                    SHChangeNotification_Unlock(lock);
                }
                SetPropW(hwnd, L"VTabsExplorer.NavItems.Dirty", reinterpret_cast<HANDLE>(static_cast<uintptr_t>(GetTickCount64() | 1)));
                return 0;
            }
            return DefWindowProcW(hwnd, message, wParam, lParam);
        }
    }

    NavItems& NavItems::Instance()
    {
        static NavItems* instance = new NavItems(); // mai distrutta: la mod resta caricata fino alla fine del processo
        return *instance;
    }

    void NavItems::Start()
    {
        {
            std::scoped_lock lock(m_lock);
            if (m_started)
            {
                return;
            }
            m_started = true;
            m_stop = false;
            m_wake = CreateEventW(nullptr, FALSE, FALSE, nullptr);
        }
        std::thread worker([this] {
            try
            {
                Run();
            }
            catch (...)
            {
                XPX_MLOG(L"NavItems: errore inatteso, letture fermate (0x%08X).", static_cast<unsigned>(winrt::to_hresult()));
            }
        });
        // Un handle nostro al thread, per aspettarlo allo scaricamento (std::thread poi si stacca).
        HANDLE handle = nullptr;
        DuplicateHandle(GetCurrentProcess(), worker.native_handle(), GetCurrentProcess(), &handle, SYNCHRONIZE, FALSE, 0);
        worker.detach();
        std::scoped_lock lock(m_lock);
        m_thread = handle;
    }

    bool NavItems::Stop(DWORD timeoutMs)
    {
        HANDLE thread = nullptr;
        {
            std::scoped_lock lock(m_lock);
            if (!m_started)
            {
                return true;
            }
            thread = m_thread;
        }
        m_stop = true;
        if (m_wake)
        {
            SetEvent(m_wake);
        }
        if (thread && WaitForSingleObject(thread, timeoutMs) != WAIT_OBJECT_0)
        {
            XPX_MLOG(L"NavItems: il thread delle letture non si è fermato entro %lu ms.", timeoutMs);
            return false;
        }
        std::scoped_lock lock(m_lock);
        if (m_thread)
        {
            CloseHandle(m_thread);
            m_thread = nullptr;
        }
        if (m_wake)
        {
            CloseHandle(m_wake);
            m_wake = nullptr;
        }
        m_started = false;
        m_latest = nullptr;
        m_moves.clear();
        m_pins.clear();
        m_iconCache.clear();
        m_publishedSizes.clear();
        XPX_MLOG(L"NavItems: thread delle letture fermato.");
        return true;
    }

    void NavItems::UseIconSize(int pixels)
    {
        if (pixels <= 0 || pixels > 256)
        {
            return;
        }
        bool added = false;
        {
            std::scoped_lock lock(m_lock);
            if (std::find(m_sizes.begin(), m_sizes.end(), pixels) == m_sizes.end())
            {
                m_sizes.push_back(pixels);
                added = true;
            }
        }
        if (added)
        {
            Refresh();
        }
    }

    void NavItems::Refresh()
    {
        if (m_wake)
        {
            SetEvent(m_wake);
        }
    }

    std::shared_ptr<const NavSnapshot> NavItems::Latest() const
    {
        std::scoped_lock lock(m_lock);
        return m_latest;
    }

    void NavItems::MovePinned(NavMove move)
    {
        {
            std::scoped_lock lock(m_lock);
            m_moves.push_back(std::move(move));
        }
        Refresh();
    }

    void NavItems::PinFolders(NavPin pin)
    {
        if (pin.items.empty())
        {
            return;
        }
        {
            std::scoped_lock lock(m_lock);
            m_pins.push_back(std::move(pin));
        }
        Refresh();
    }

    void NavItems::DoPin(NavPin const& pin)
    {
        const ULONGLONG start = GetTickCount64();
        com_ptr<IAutomaticDestinationList> list;
        HRESULT result = CoCreateInstance(kAutomaticDestinationList, nullptr, CLSCTX_INPROC_SERVER, __uuidof(IAutomaticDestinationList), list.put_void());
        if (SUCCEEDED(result))
        {
            result = list->Initialize(kExplorerAppId, nullptr, nullptr);
        }
        if (FAILED(result))
        {
            XPX_MLOG(L"NavItems: elenco di Accesso rapido non disponibile, cartelle non aggiunte (0x%08X).", static_cast<unsigned>(result));
            return;
        }
        const auto anchor = ItemOf(pin.before);
        size_t added = 0;
        size_t already = 0;
        size_t failed = 0;
        HRESULT lastFailure = S_OK;
        for (auto const& idList : pin.items)
        {
            const auto item = ItemOf(idList);
            int index = -1;
            if (!item)
            {
                ++failed;
                continue;
            }
            if (SUCCEEDED(list->IsPinned(item.get(), &index)) && index >= 0)
            {
                ++already; // la sposta la barra, se serve
                continue;
            }
            // Prima della cartella indicata (il suo indice cresce a ogni aggiunta: le nuove restano nel loro ordine), altrimenti
            // in fondo: l'indice uguale al numero delle aggiunte (vedi IAutomaticDestinationList).
            int to = -1;
            if (!anchor || FAILED(list->IsPinned(anchor.get(), &to)) || to < 0)
            {
                to = 0;
                com_ptr<IObjectArray> pinned;
                UINT count = 0;
                if (SUCCEEDED(list->GetList(0, 1000, 0, IID_PPV_ARGS(pinned.put()))) && pinned && SUCCEEDED(pinned->GetCount(&count)))
                {
                    to = static_cast<int>(count);
                }
            }
            const HRESULT pinnedResult = list->PinItem(item.get(), to);
            if (SUCCEEDED(pinnedResult))
            {
                ++added;
            }
            else
            {
                ++failed;
                lastFailure = pinnedResult;
            }
        }
        XPX_MLOG(L"NavItems: %zu %s ad Accesso rapido (già aggiunte %zu, non riuscite %zu: 0x%08X), %llu ms.", added,
                 added == 1 ? L"cartella aggiunta" : L"cartelle aggiunte", already, failed, static_cast<unsigned>(lastFailure), GetTickCount64() - start);
    }

    void NavItems::DoMove(NavMove const& move)
    {
        const ULONGLONG start = GetTickCount64();
        com_ptr<IAutomaticDestinationList> list;
        HRESULT result = CoCreateInstance(kAutomaticDestinationList, nullptr, CLSCTX_INPROC_SERVER, __uuidof(IAutomaticDestinationList), list.put_void());
        if (SUCCEEDED(result))
        {
            result = list->Initialize(kExplorerAppId, nullptr, nullptr);
        }
        const auto item = ItemOf(move.item);
        const auto anchor = ItemOf(move.before.empty() ? move.after : move.before);
        int from = -1;
        int to = -1;
        if (SUCCEEDED(result))
        {
            if (!item || !anchor)
            {
                result = E_INVALIDARG;
            }
            else if (FAILED(list->IsPinned(item.get(), &from)) || FAILED(list->IsPinned(anchor.get(), &to)) || from < 0 || to < 0)
            {
                result = HRESULT_FROM_WIN32(ERROR_NOT_FOUND); // non (più) fissata: niente da spostare
            }
            else
            {
                if (move.before.empty())
                {
                    ++to;
                }
                result = from == to || from + 1 == to ? S_FALSE : list->PinItem(item.get(), to);
            }
        }
        XPX_MLOG(L"NavItems: cartella aggiunta spostata da %d a %d (prima della voce che era in quel posto): 0x%08X, %llu ms.", from, to,
                 static_cast<unsigned>(result), GetTickCount64() - start);
    }

    void NavItems::Run()
    {
        winrt::init_apartment(winrt::apartment_type::single_threaded);
        // Priorità bassa (anche per disco e memoria): le letture non devono mai rubare tempo a Esplora file.
        SetThreadPriority(GetCurrentThread(), THREAD_MODE_BACKGROUND_BEGIN);
        // Finestra solo-messaggi per le notifiche della shell su Accesso rapido e Posizioni frequenti
        // (aggiunte, rimozioni, nomi). Il Desktop no: i file salvati sul desktop farebbero rileggere di
        // continuo, e le sue voci fissate nell'albero (OneDrive, Linux...) cambiano di rado.
        WNDCLASSW windowClass{};
        windowClass.lpfnWndProc = NotifyProc;
        windowClass.hInstance = g_module;
        windowClass.lpszClassName = L"VTabsExplorer.NavItems";
        RegisterClassW(&windowClass);
        const HWND hwnd = CreateWindowExW(0, windowClass.lpszClassName, L"", 0, 0, 0, 0, 0, HWND_MESSAGE, nullptr, g_module, nullptr);
        std::vector<ULONG> registrations;
        std::vector<PIDLIST_ABSOLUTE> pidls;
        for (const wchar_t* folder : { kQuickAccess, kFrequentPlaces })
        {
            PIDLIST_ABSOLUTE pidl = nullptr;
            if (hwnd && SUCCEEDED(SHParseDisplayName(folder, nullptr, &pidl, 0, nullptr)) && pidl)
            {
                SHChangeNotifyEntry entry{ pidl, FALSE };
                if (const ULONG id = SHChangeNotifyRegister(hwnd, SHCNRF_ShellLevel | SHCNRF_InterruptLevel | SHCNRF_NewDelivery, SHCNE_ALLEVENTS,
                                                            kNotifyMessage, 1, &entry))
                {
                    registrations.push_back(id);
                }
                pidls.push_back(pidl); // in uso finché la registrazione resta (fino all'arresto del thread)
            }
        }
        XPX_MLOG(L"NavItems: in ascolto dei cambiamenti di Accesso rapido (%zu registrazioni).", registrations.size());

        Load();
        ULONGLONG lastLoad = GetTickCount64();
        while (!m_stop.load())
        {
            // A riposo il thread dorme fino alla prossima rilettura periodica; con una notifica in sospeso, fino
            // allo scadere della sua attesa (le notifiche arrivano a raffiche).
            ULONGLONG now = GetTickCount64();
            ULONGLONG dirty = hwnd ? reinterpret_cast<uintptr_t>(GetPropW(hwnd, L"VTabsExplorer.NavItems.Dirty")) : 0;
            ULONGLONG timeout = now - lastLoad >= kSafetyRefreshMs ? 0 : kSafetyRefreshMs - (now - lastLoad);
            if (dirty)
            {
                timeout = std::min<ULONGLONG>(timeout, now - dirty >= kDebounceMs ? 0 : kDebounceMs - (now - dirty));
            }
            const DWORD result = MsgWaitForMultipleObjectsEx(1, &m_wake, static_cast<DWORD>(timeout), QS_ALLINPUT, MWMO_INPUTAVAILABLE);
            MSG message;
            while (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE))
            {
                TranslateMessage(&message);
                DispatchMessageW(&message);
            }
            if (m_stop.load())
            {
                break;
            }
            now = GetTickCount64();
            dirty = hwnd ? reinterpret_cast<uintptr_t>(GetPropW(hwnd, L"VTabsExplorer.NavItems.Dirty")) : 0;
            const bool notified = dirty && now - dirty >= kDebounceMs;
            if (result == WAIT_OBJECT_0 || notified || now - lastLoad >= kSafetyRefreshMs)
            {
                if (hwnd)
                {
                    RemovePropW(hwnd, L"VTabsExplorer.NavItems.Dirty");
                }
                std::vector<NavMove> moves;
                std::vector<NavPin> pins;
                {
                    std::scoped_lock lock(m_lock);
                    moves.swap(m_moves);
                    pins.swap(m_pins);
                }
                // Prima le cartelle nuove (uno spostamento chiesto dopo può riguardarle), poi gli spostamenti.
                for (auto const& pin : pins)
                {
                    DoPin(pin);
                }
                for (auto const& move : moves)
                {
                    DoMove(move);
                }
                // Dopo uno spostamento si pubblica comunque: la barra lo ha già fatto a vista, e se non è
                // riuscito deve tornare all'ordine vero.
                m_forcePublish = m_forcePublish || !moves.empty() || !pins.empty();
                Load();
                lastLoad = GetTickCount64();
            }
        }

        // Arresto (scaricamento della mod): tutto quello che il thread ha messo in Esplora file va via da qui, sul suo thread.
        for (const ULONG id : registrations)
        {
            SHChangeNotifyDeregister(id);
        }
        for (PIDLIST_ABSOLUTE pidl : pidls)
        {
            CoTaskMemFree(pidl);
        }
        if (hwnd)
        {
            RemovePropW(hwnd, L"VTabsExplorer.NavItems.Dirty");
            DestroyWindow(hwnd);
        }
        UnregisterClassW(windowClass.lpszClassName, g_module);
        NamespaceCache().clear();
        NamespaceCacheTime() = 0;
        m_forcePublish = false;
        winrt::uninit_apartment();
    }

    void NavItems::Load()
    {
        const ULONGLONG start = GetTickCount64();
        std::vector<int> sizes;
        {
            std::scoped_lock lock(m_lock);
            sizes = m_sizes;
        }
        static const PROPERTYKEY pinnedToTree = KeyOf(L"System.IsPinnedToNameSpaceTree");
        static const PROPERTYKEY pinnedHome = KeyOf(L"System.Home.IsPinned");

        // 1) Elementi del Desktop fissati nell'albero (solo quelli della shell, ::{GUID}). Elencare il Desktop
        //    costa (passa anche da tutte le cartelle sul desktop dell'utente) e queste voci cambiano di rado:
        //    si rileggono al massimo ogni 10 minuti. Solo da questo thread.
        std::vector<Candidate>& namespaceItems = NamespaceCache();
        ULONGLONG& namespaceRead = NamespaceCacheTime();
        if (namespaceRead == 0 || GetTickCount64() - namespaceRead >= kNamespaceMaxAgeMs)
        {
            namespaceItems.clear();
            com_ptr<IShellFolder> desktop;
            com_ptr<IEnumIDList> children;
            if (SUCCEEDED(SHGetDesktopFolder(desktop.put())) && desktop->EnumObjects(nullptr, SHCONTF_FOLDERS, children.put()) == S_OK && children)
            {
                PITEMID_CHILD child = nullptr;
                while (children->Next(1, &child, nullptr) == S_OK)
                {
                    // Le cartelle vere (file system) si scartano subito, senza chiederne il nome.
                    SFGAOF attributes = SFGAO_FILESYSTEM;
                    PCUITEMID_CHILD one = child;
                    com_ptr<IShellItem> item;
                    if (SUCCEEDED(desktop->GetAttributesOf(1, &one, &attributes)) && !(attributes & SFGAO_FILESYSTEM) &&
                        SUCCEEDED(SHCreateItemWithParent(nullptr, desktop.get(), child, IID_PPV_ARGS(item.put()))))
                    {
                        const std::wstring parsing = NameOf(item.get(), SIGDN_DESKTOPABSOLUTEPARSING);
                        if (parsing.rfind(L"::", 0) == 0 && GetBool(item.get(), pinnedToTree))
                        {
                            Candidate candidate;
                            candidate.item.name = NameOf(item.get(), SIGDN_NORMALDISPLAY);
                            candidate.item.parsing = parsing;
                            candidate.item.idList = IdListOf(item.get());
                            candidate.sort = SortIndexOf(parsing);
                            candidate.sequence = namespaceItems.size();
                            candidate.shellItem = item;
                            namespaceItems.push_back(std::move(candidate));
                        }
                    }
                    CoTaskMemFree(child);
                }
            }
            namespaceRead = GetTickCount64();
        }
        std::vector<Candidate> candidates = namespaceItems;

        // 2) Cartelle aggiunte ad Accesso rapido, nel loro ordine (da "Posizioni frequenti", vedi sopra)
        com_ptr<IShellItem> quickAccess;
        com_ptr<IEnumShellItems> pinned;
        if (SUCCEEDED(SHCreateItemFromParsingName(kFrequentPlaces, nullptr, IID_PPV_ARGS(quickAccess.put()))) &&
            SUCCEEDED(quickAccess->BindToHandler(nullptr, BHID_EnumItems, IID_PPV_ARGS(pinned.put()))))
        {
            com_ptr<IShellItem> item;
            while (pinned->Next(1, item.put(), nullptr) == S_OK)
            {
                SFGAOF attributes = 0;
                if (GetBool(item.get(), pinnedHome) && SUCCEEDED(item->GetAttributes(SFGAO_FOLDER, &attributes)) && (attributes & SFGAO_FOLDER))
                {
                    Candidate candidate;
                    candidate.item.name = NameOf(item.get(), SIGDN_NORMALDISPLAY);
                    candidate.item.parsing = NameOf(item.get(), SIGDN_DESKTOPABSOLUTEPARSING);
                    candidate.item.idList = IdListOf(item.get());
                    candidate.item.pinned = true;
                    candidate.sort = kPinnedSortIndex;
                    candidate.sequence = candidates.size();
                    candidate.shellItem = item;
                    candidates.push_back(std::move(candidate));
                }
                item = nullptr;
            }
        }

        std::stable_sort(candidates.begin(), candidates.end(), [](Candidate const& a, Candidate const& b) { return a.sort < b.sort; });
        std::erase_if(candidates, [](Candidate const& candidate) { return candidate.item.idList.empty(); });

        // Stesse voci e stesse dimensioni delle icone dell'ultima volta: niente da fare (le icone non si
        // estraggono nemmeno).
        const auto latest = Latest();
        std::vector<int> sortedSizes = sizes;
        std::sort(sortedSizes.begin(), sortedSizes.end());
        if (!m_forcePublish && latest && sortedSizes == m_publishedSizes && latest->items.size() == candidates.size() &&
            std::equal(candidates.begin(), candidates.end(), latest->items.begin(), [](Candidate const& a, NavItem const& b) {
                return a.item.name == b.name && a.item.parsing == b.parsing && a.item.pinned == b.pinned;
            }))
        {
            static int quiet = 0;
            if (quiet < 3)
            {
                ++quiet;
                XPX_MLOG(L"NavItems: nessuna novità (lettura %llu ms).", GetTickCount64() - start);
            }
            return;
        }

        const ULONGLONG iconsStart = GetTickCount64();
        size_t extracted = 0;
        std::vector<NavItem> items;
        items.reserve(candidates.size());
        for (auto& candidate : candidates)
        {
            auto& cached = m_iconCache[candidate.item.parsing];
            for (const int size : sizes)
            {
                auto it = std::find_if(cached.begin(), cached.end(), [size](NavIcon const& icon) { return icon.size == size; });
                if (it == cached.end())
                {
                    cached.push_back(NavIcon{ size, IconOf(candidate.shellItem.get(), size) });
                    ++extracted;
                    it = cached.end() - 1;
                }
                candidate.item.icons.push_back(*it);
            }
            candidate.item.sortIndex = candidate.sort;
            items.push_back(std::move(candidate.item));
        }

        auto snapshot = std::make_shared<NavSnapshot>();
        snapshot->items = std::move(items);
        snapshot->readMs = GetTickCount64() - start;
        {
            std::scoped_lock lock(m_lock);
            snapshot->version = m_latest ? m_latest->version + 1 : 1;
            m_latest = snapshot;
        }
        m_publishedSizes = sortedSizes;
        m_forcePublish = false;
        XPX_MLOG(L"NavItems: %zu voci (lettura %llu ms, di cui icone %llu ms: %zu estratte; versione %llu).", snapshot->items.size(), snapshot->readMs,
                 GetTickCount64() - iconsStart, extracted, snapshot->version);
    }
}
