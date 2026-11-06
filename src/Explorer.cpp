#include "Explorer.h"

#include "Common.h"

#include <shellapi.h>
#include <shlguid.h>
#include <shlobj.h>

#include <algorithm>
#include <cstdio>
#include <memory>
#include <mutex>
#include <unordered_set>

using Microsoft::WRL::ComPtr;

namespace xpx
{
    namespace
    {
        constexpr wchar_t kCabinetClass[] = L"CabinetWClass";
        constexpr wchar_t kTabClass[] = L"ShellTabWindowClass";
        constexpr wchar_t kIslandClass[] = L"Microsoft.UI.Content.DesktopChildSiteBridge";
        constexpr wchar_t kViewClass[] = L"SHELLDLL_DefView";

        // Isola XAML dell'intestazione (striscia schede, barra degli indirizzi): la più in alto tra le isole
        // VISIBILI della finestra. Con le schede verticali c'è anche l'isola del pannello: visibile sta sotto
        // l'intestazione (ed è prima nell'ordine Z, quindi la prima isola trovata non è per forza
        // l'intestazione); nascosta (funzione spenta, finestra riusata da Esplora file) può avere una
        // posizione qualunque e si ignora.
        HWND HeaderIsland(HWND window)
        {
            HWND best = nullptr;
            HWND first = nullptr;
            LONG bestTop = LONG_MAX;
            for (HWND island = FindWindowExW(window, nullptr, kIslandClass, nullptr); island;
                 island = FindWindowExW(window, island, kIslandClass, nullptr))
            {
                // Il pannello delle schede verticali porta un contrassegno (la mod lo mette sulla sua isola).
                if (GetPropW(island, L"VTabsExplorer.VerticalTabsPanel"))
                {
                    continue;
                }
                first = first ? first : island;
                RECT rect{};
                if (IsWindowVisible(island) && GetWindowRect(island, &rect) && rect.top < bestTop)
                {
                    bestTop = rect.top;
                    best = island;
                }
            }
            return best ? best : first;
        }
        // Comandi (non documentati) della finestra di Esplora file, gli stessi delle sue scorciatoie da
        // tastiera (tabella degli acceleratori di ExplorerFrame.dll). "Nuova scheda" (Ctrl+T) apre la
        // scheda nella cartella iniziale, dopo quella attiva; lo usano anche strumenti come
        // ExplorerTabUtility.
        constexpr WPARAM kNewTabCommand = 0xA21B;      // Ctrl+T
        constexpr WPARAM kNextTabCommand = 0xA033;     // Ctrl+Tab
        constexpr WPARAM kPreviousTabCommand = 0xA031; // Ctrl+Maiusc+Tab
        // Le schede appena costruite hanno questo tempo per mostrare la loro cartella.
        constexpr DWORD kLoadTimeoutMs = 3000;
        constexpr wchar_t kThisPc[] = L"::{20D04FE0-3AEA-1069-A2D8-08002B30309D}";
        constexpr wchar_t kHome[] = L"::{F874310E-B6B7-47DC-BC84-B9E6B38F5903}";
        // L'ordine delle schede si rilegge quando cambiano schede o titoli e comunque ogni 20 s (possono
        // essere riordinate col mouse senza altri cambiamenti).
        constexpr ULONGLONG kOrderCacheMs = 20000;

        std::wstring ClassOf(HWND hwnd)
        {
            wchar_t name[256]{};
            GetClassNameW(hwnd, name, ARRAYSIZE(name));
            return name;
        }

        std::wstring TextOf(HWND hwnd)
        {
            wchar_t text[1024]{};
            GetWindowTextW(hwnd, text, ARRAYSIZE(text));
            return text;
        }

        // Finestre di Esplora file visibili, dalla più in alto nell'ordine Z.
        std::vector<HWND> CabinetWindows()
        {
            std::vector<HWND> result;
            EnumWindows(
                [](HWND hwnd, LPARAM param) -> BOOL {
                    if (IsWindowVisible(hwnd) && ClassOf(hwnd) == kCabinetClass)
                    {
                        reinterpret_cast<std::vector<HWND>*>(param)->push_back(hwnd);
                    }
                    return TRUE;
                },
                reinterpret_cast<LPARAM>(&result));
            return result;
        }

        // Schede di una finestra (ShellTabWindowClass) nell'ordine Z dei figli: la prima è quella attiva.
        std::vector<HWND> TabWindowsByZ(HWND window)
        {
            std::vector<HWND> result;
            for (HWND child = GetWindow(window, GW_CHILD); child; child = GetWindow(child, GW_HWNDNEXT))
            {
                if (ClassOf(child) == kTabClass)
                {
                    result.push_back(child);
                }
            }
            return result;
        }

        // Dalla vista (SHELLDLL_DefView) si risale alla sua scheda.
        HWND TabWindowOf(HWND inner)
        {
            for (HWND current = inner; current; current = GetParent(current))
            {
                if (ClassOf(current) == kTabClass)
                {
                    return current;
                }
            }
            return nullptr;
        }

        // La scheda ha la sua vista: ha caricato una cartella (controllo locale, senza chiamare Esplora file).
        bool HasView(HWND tabWindow)
        {
            bool found = false;
            EnumChildWindows(
                tabWindow,
                [](HWND child, LPARAM param) -> BOOL {
                    if (ClassOf(child) == kViewClass)
                    {
                        *reinterpret_cast<bool*>(param) = true;
                        return FALSE;
                    }
                    return TRUE;
                },
                reinterpret_cast<LPARAM>(&found));
            return found;
        }

        struct PidlFree
        {
            using pointer = PIDLIST_ABSOLUTE; // i PIDL della shell sono puntatori __unaligned
            void operator()(PIDLIST_ABSOLUTE pidl) const { CoTaskMemFree(pidl); }
        };
        using UniquePidl = std::unique_ptr<ITEMIDLIST_ABSOLUTE, PidlFree>;

        // PIDL salvato ben formato: elementi dentro i limiti, chiusi da un elemento vuoto.
        bool IsValidIdList(std::vector<BYTE> const& bytes)
        {
            size_t offset = 0;
            while (offset + sizeof(USHORT) <= bytes.size())
            {
                USHORT cb = 0;
                memcpy(&cb, bytes.data() + offset, sizeof(cb));
                if (cb == 0)
                {
                    return offset + sizeof(USHORT) == bytes.size();
                }
                if (cb < sizeof(USHORT) || offset + cb > bytes.size())
                {
                    return false;
                }
                offset += cb;
            }
            return false;
        }

        // Cartella della scheda: dal nome di analisi (fallisce se una cartella di file system non esiste
        // più) o, per le cartelle virtuali, dal PIDL salvato.
        UniquePidl ToIdList(Tab const& tab)
        {
            if (!tab.path.empty())
            {
                PIDLIST_ABSOLUTE pidl = nullptr;
                if (SUCCEEDED(SHParseDisplayName(tab.path.c_str(), nullptr, &pidl, 0, nullptr)) && pidl)
                {
                    return UniquePidl(pidl);
                }
            }
            if (!IsFileSystemPath(tab.path) && IsValidIdList(tab.idList))
            {
                if (auto* copy = static_cast<BYTE*>(CoTaskMemAlloc(tab.idList.size())))
                {
                    memcpy(copy, tab.idList.data(), tab.idList.size());
                    return UniquePidl(reinterpret_cast<PIDLIST_ABSOLUTE>(copy));
                }
            }
            return nullptr;
        }

        std::vector<BYTE> IdListBytes(PCIDLIST_ABSOLUTE pidl)
        {
            const auto* bytes = reinterpret_cast<const BYTE*>(pidl);
            return { bytes, bytes + ILGetSize(pidl) };
        }

        // Nome con cui la scheda mostrerà la cartella (il titolo della sua finestra).
        std::wstring DisplayName(PCIDLIST_ABSOLUTE pidl)
        {
            std::wstring result;
            PWSTR name = nullptr;
            if (SUCCEEDED(SHGetNameFromIDList(pidl, SIGDN_NORMALDISPLAY, &name)) && name)
            {
                result = name;
            }
            CoTaskMemFree(name);
            return result;
        }

        // Finestre appena aperte da un ripristino e già assegnate (aperture in parallelo da più thread).
        std::mutex g_claimLock;
        std::unordered_set<HWND> g_claimed;

        // Attende (fino a timeoutMs) che la condizione diventi vera.
        template <typename Condition>
        bool WaitFor(Condition condition, DWORD timeoutMs, DWORD stepMs = 60)
        {
            const ULONGLONG deadline = GetTickCount64() + timeoutMs;
            for (;;)
            {
                if (condition())
                {
                    return true;
                }
                if (GetTickCount64() >= deadline)
                {
                    return false;
                }
                Sleep(stepMs);
            }
        }

        // Attende (fino a timeoutMs) che il thread della finestra risponda ai messaggi: Esplora file appena
        // avviato può restare occupato per secondi, e intanto i comandi mandati alla finestra aspettano in
        // coda (o vanno persi).
        bool WaitResponsive(HWND hwnd, DWORD timeoutMs)
        {
            return WaitFor(
                [hwnd] {
                    DWORD_PTR result = 0;
                    return !IsWindow(hwnd) || SendMessageTimeoutW(hwnd, WM_NULL, 0, 0, SMTO_ABORTIFHUNG | SMTO_BLOCK, 500, &result) != 0;
                },
                timeoutMs, 50);
        }

        // Solo per le prove, in modalità prova (la tray lavora allora solo con le finestre di prova): con
        // dev-drop-newtab.txt = "<lasciati passare> <persi>" (es. "1 2") i comandi "nuova scheda" si perdono
        // come con Esplora file appena riavviato, per provare il nuovo tentativo e le schede messe da parte
        // per Ctrl+Maiusc+T. Il file si aggiorna a ogni comando e sparisce quando ha finito.
        bool DropNewTabForTest()
        {
            const std::wstring path = DataDirectory() + L"\\dev-drop-newtab.txt";
            if (GetFileAttributesW(path.c_str()) == INVALID_FILE_ATTRIBUTES || DevFilterRoot().empty())
            {
                return false;
            }
            int pass = 0;
            int drop = 0;
            if (FILE* file = nullptr; _wfopen_s(&file, path.c_str(), L"r") == 0 && file)
            {
                if (fwscanf_s(file, L"%d %d", &pass, &drop) != 2)
                {
                    pass = drop = 0;
                }
                fclose(file);
            }
            const bool dropped = pass <= 0 && drop > 0;
            pass > 0 ? --pass : --drop;
            if (pass <= 0 && drop <= 0)
            {
                DeleteFileW(path.c_str());
            }
            else if (FILE* file = nullptr; _wfopen_s(&file, path.c_str(), L"w") == 0 && file)
            {
                fwprintf(file, L"%d %d", pass, drop);
                fclose(file);
            }
            if (dropped)
            {
                Log(L"Prova: comando \"nuova scheda\" perso di proposito.");
            }
            return dropped;
        }

        // Comando "nuova scheda" alla scheda attiva della finestra.
        bool RequestNewTab(HWND tabWindow)
        {
            return DropNewTabForTest() || PostMessageW(tabWindow, WM_COMMAND, kNewTabCommand, 0) != FALSE;
        }
    }

    bool Explorer::IsStartLocation(std::wstring const& path)
    {
        if (EqualsIgnoreCase(path, kThisPc) || EqualsIgnoreCase(path, kHome))
        {
            return true;
        }
        // "Apri Esplora file in: Download"
        DWORD launchTo = 0;
        DWORD size = sizeof(launchTo);
        if (RegGetValueW(HKEY_CURRENT_USER, L"Software\\Microsoft\\Windows\\CurrentVersion\\Explorer\\Advanced", L"LaunchTo", RRF_RT_REG_DWORD,
                         nullptr, &launchTo, &size) == ERROR_SUCCESS &&
            launchTo == 3)
        {
            PWSTR downloads = nullptr;
            const bool same = SUCCEEDED(SHGetKnownFolderPath(FOLDERID_Downloads, KF_FLAG_DEFAULT, nullptr, &downloads)) && EqualsIgnoreCase(path, downloads);
            CoTaskMemFree(downloads);
            return same;
        }
        return false;
    }

    bool Explorer::IsRestorable(Tab const& tab)
    {
        return ToIdList(tab) != nullptr;
    }

    void Explorer::Reset()
    {
        m_shellWindows.Reset();
        m_orderCache.clear();
        m_lastKnown.clear();
        m_lastPosition.clear();
    }

    bool Explorer::EnsureShellWindows()
    {
        if (!m_shellWindows)
        {
            CoCreateInstance(__uuidof(ShellWindows), nullptr, CLSCTX_ALL, IID_PPV_ARGS(&m_shellWindows));
        }
        return m_shellWindows != nullptr;
    }

    bool Explorer::EnsureAutomation()
    {
        if (!m_automation)
        {
            CoCreateInstance(__uuidof(CUIAutomation), nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&m_automation));
        }
        return m_automation != nullptr;
    }

    bool Explorer::ReadDetails(LiveTab& live)
    {
        ComPtr<IServiceProvider> provider;
        if (FAILED(live.dispatch.As(&provider)) || FAILED(provider->QueryService(SID_STopLevelBrowser, IID_PPV_ARGS(&live.browser))))
        {
            return false;
        }
        ComPtr<IShellView> view;
        if (SUCCEEDED(live.browser->QueryActiveShellView(&view)) && view)
        {
            HWND viewWindow = nullptr;
            if (SUCCEEDED(view->GetWindow(&viewWindow)))
            {
                live.tab.tabWindow = TabWindowOf(viewWindow);
            }
            ComPtr<IFolderView> folderView;
            ComPtr<IPersistFolder2> folder;
            PIDLIST_ABSOLUTE pidl = nullptr;
            if (SUCCEEDED(view.As(&folderView)) && SUCCEEDED(folderView->GetFolder(IID_PPV_ARGS(&folder))) && SUCCEEDED(folder->GetCurFolder(&pidl)) &&
                pidl)
            {
                PWSTR name = nullptr;
                if (SUCCEEDED(SHGetNameFromIDList(pidl, SIGDN_DESKTOPABSOLUTEPARSING, &name)) && name)
                {
                    live.tab.path = name;
                    CoTaskMemFree(name);
                }
                live.tab.idList = IdListBytes(pidl);
                CoTaskMemFree(pidl);
            }
        }
        if (!live.tab.tabWindow)
        {
            HWND browserWindow = nullptr;
            if (SUCCEEDED(live.browser->GetWindow(&browserWindow)) && browserWindow && ClassOf(browserWindow) == kTabClass)
            {
                live.tab.tabWindow = browserWindow;
            }
        }
        if (live.tab.tabWindow)
        {
            live.tab.title = TextOf(live.tab.tabWindow);
        }
        return true;
    }

    std::vector<Explorer::LiveTab> Explorer::EnumerateTabs(HWND only, bool details)
    {
        std::vector<LiveTab> result;
        long count = 0;
        if (!EnsureShellWindows() || FAILED(m_shellWindows->get_Count(&count)))
        {
            // Esplora file riavviato: il collegamento va rifatto.
            m_shellWindows.Reset();
            if (!EnsureShellWindows() || FAILED(m_shellWindows->get_Count(&count)))
            {
                return result;
            }
        }
        for (long i = 0; i < count; ++i)
        {
            VARIANT index{};
            index.vt = VT_I4;
            index.lVal = i;
            LiveTab live;
            if (FAILED(m_shellWindows->Item(index, &live.dispatch)) || !live.dispatch)
            {
                continue;
            }
            ComPtr<IWebBrowserApp> app;
            SHANDLE_PTR top = 0;
            if (FAILED(live.dispatch.As(&app)) || FAILED(app->get_HWND(&top)) || !top)
            {
                continue;
            }
            live.top = reinterpret_cast<HWND>(top);
            if (only && live.top != only)
            {
                continue;
            }
            live.dispatch.As(&live.identity); // IUnknown: risposto dal proxy, senza chiamare Esplora file
            if (details && !ReadDetails(live))
            {
                continue;
            }
            result.push_back(std::move(live));
        }
        return result;
    }

    std::vector<Explorer::LiveTab> Explorer::TabsOf(HWND window)
    {
        return EnumerateTabs(window, true);
    }

    std::vector<Explorer::TabItem> Explorer::TabItems(HWND window)
    {
        std::vector<TabItem> result;
        ComPtr<IUIAutomationCondition> condition;
        VARIANT type{};
        type.vt = VT_I4;
        type.lVal = UIA_TabItemControlTypeId;
        if (!EnsureAutomation() || FAILED(m_automation->CreatePropertyCondition(UIA_ControlTypePropertyId, type, &condition)))
        {
            return result;
        }
        // La barra delle schede sta nell'isola XAML della finestra: cercarla lì evita di attraversare
        // l'elenco dei file, che può avere migliaia di elementi. Solo se non c'è si cerca in tutta la finestra.
        for (HWND root : { HeaderIsland(window), window })
        {
            ComPtr<IUIAutomationElement> element;
            ComPtr<IUIAutomationElementArray> found;
            int length = 0;
            if (!root || FAILED(m_automation->ElementFromHandle(root, &element)) || !element ||
                FAILED(element->FindAll(TreeScope_Descendants, condition.Get(), &found)) || !found || FAILED(found->get_Length(&length)) ||
                length == 0)
            {
                continue;
            }
            struct Entry
            {
                TabItem item;
                RECT rect{};
            };
            std::vector<Entry> entries;
            for (int i = 0; i < length; ++i)
            {
                Entry entry;
                if (FAILED(found->GetElement(i, &entry.item.element)) || !entry.item.element)
                {
                    continue;
                }
                BSTR name = nullptr;
                if (SUCCEEDED(entry.item.element->get_CurrentName(&name)) && name)
                {
                    entry.item.name.assign(name, SysStringLen(name));
                }
                SysFreeString(name);
                VARIANT value{};
                if (SUCCEEDED(entry.item.element->GetCurrentPropertyValue(UIA_PositionInSetPropertyId, &value)) && value.vt == VT_I4)
                {
                    entry.item.position = value.lVal;
                }
                VariantClear(&value);
                entry.item.element->get_CurrentBoundingRectangle(&entry.rect);
                entries.push_back(std::move(entry));
            }

            // Ordine visivo: la posizione esatta nell'elenco (PositionInSet) se ce l'hanno tutte; altrimenti
            // la coordinata X se sono tutte visibili; altrimenti l'ordine dell'albero. L'albero da solo non
            // basta: la barra ricicla i suoi elementi e può risultarne "ruotato".
            std::unordered_set<int> positions;
            for (auto const& entry : entries)
            {
                positions.insert(entry.item.position);
            }
            const bool byPosition = !positions.contains(0) && positions.size() == entries.size();
            const bool byX = std::all_of(entries.begin(), entries.end(), [](Entry const& entry) { return !IsRectEmpty(&entry.rect); });
            if (IsVerbose())
            {
                std::wstring detail;
                for (auto const& entry : entries)
                {
                    detail += L" " + std::to_wstring(entry.item.position) + L"@" +
                              (IsRectEmpty(&entry.rect) ? std::wstring(L"-") : std::to_wstring(entry.rect.left));
                }
                Log(L"Barra delle schede (ordine dell'albero, posizione@x):%s -> %s.", detail.c_str(),
                    byPosition ? L"per posizione" : byX ? L"per coordinata" : L"ordine dell'albero");
            }
            if (byPosition)
            {
                std::sort(entries.begin(), entries.end(), [](Entry const& a, Entry const& b) { return a.item.position < b.item.position; });
            }
            else
            {
                for (auto& entry : entries)
                {
                    entry.item.position = 0; // posizioni assenti o incoerenti: non si usano
                }
                if (byX)
                {
                    std::stable_sort(entries.begin(), entries.end(), [](Entry const& a, Entry const& b) { return a.rect.left < b.rect.left; });
                }
            }
            for (auto& entry : entries)
            {
                result.push_back(std::move(entry.item));
            }
            break;
        }
        return result;
    }

    std::vector<Explorer::LiveTab const*> Explorer::OrderTabs(HWND window, std::vector<LiveTab const*> tabs)
    {
        if (tabs.size() <= 1)
        {
            return tabs;
        }
        std::vector<std::pair<HWND, std::wstring>> signature;
        for (auto const* live : tabs)
        {
            signature.emplace_back(live->tab.tabWindow, live->tab.title);
        }
        std::sort(signature.begin(), signature.end());

        const ULONGLONG now = GetTickCount64();
        std::vector<HWND> order;
        const auto cached = m_orderCache.find(window);
        if (cached != m_orderCache.end() && cached->second.signature == signature && now - cached->second.tick < kOrderCacheMs)
        {
            order = cached->second.order;
        }
        else
        {
            // Ogni scheda presente nella barra va al suo posto, abbinata per titolo alla sua finestra (a
            // parità di titolo, nell'ordine della shell): con le posizioni esatte al posto indicato, senza
            // nell'ordine della barra.
            std::vector<HWND> slots(tabs.size(), nullptr);
            std::vector<bool> used(tabs.size(), false);
            size_t next = 0;
            size_t placed = 0;
            for (auto const& item : TabItems(window))
            {
                const size_t slot = item.position > 0 ? static_cast<size_t>(item.position - 1) : next++;
                if (slot >= slots.size() || slots[slot])
                {
                    continue;
                }
                for (size_t i = 0; i < tabs.size(); ++i)
                {
                    if (!used[i] && tabs[i]->tab.tabWindow && tabs[i]->tab.title == item.name)
                    {
                        used[i] = true;
                        slots[slot] = tabs[i]->tab.tabWindow;
                        ++placed;
                        break;
                    }
                }
            }
            // Le schede fuori vista (la barra, quando scorre, non le espone tutte) riempiono i posti liberi
            // nell'ordine della loro ultima posizione nota, poi in quello della shell (di creazione).
            std::vector<size_t> remaining;
            for (size_t i = 0; i < tabs.size(); ++i)
            {
                if (!used[i] && tabs[i]->tab.tabWindow)
                {
                    remaining.push_back(i);
                }
            }
            std::stable_sort(remaining.begin(), remaining.end(), [&](size_t a, size_t b) {
                const auto pa = m_lastPosition.find(tabs[a]->tab.tabWindow);
                const auto pb = m_lastPosition.find(tabs[b]->tab.tabWindow);
                return (pa != m_lastPosition.end() ? pa->second : SIZE_MAX) < (pb != m_lastPosition.end() ? pb->second : SIZE_MAX);
            });
            size_t r = 0;
            for (auto& slot : slots)
            {
                if (!slot && r < remaining.size())
                {
                    slot = tabs[remaining[r++]]->tab.tabWindow;
                }
            }
            for (HWND slot : slots)
            {
                if (slot)
                {
                    order.push_back(slot);
                }
            }
            if (placed != tabs.size())
            {
                XPX_VLOG(L"Barra delle schede: %zu schede su %zu in vista, le altre al loro ultimo posto noto.", placed, tabs.size());
            }
            m_orderCache[window] = CachedOrder{ std::move(signature), order, now };
        }

        std::vector<LiveTab const*> result;
        std::vector<bool> taken(tabs.size(), false);
        for (HWND tabWindow : order)
        {
            for (size_t i = 0; i < tabs.size(); ++i)
            {
                if (!taken[i] && tabs[i]->tab.tabWindow == tabWindow)
                {
                    taken[i] = true;
                    result.push_back(tabs[i]);
                    break;
                }
            }
        }
        for (size_t i = 0; i < tabs.size(); ++i)
        {
            if (!taken[i])
            {
                result.push_back(tabs[i]);
            }
        }
        for (size_t i = 0; i < result.size(); ++i)
        {
            if (result[i]->tab.tabWindow)
            {
                m_lastPosition[result[i]->tab.tabWindow] = i;
            }
        }
        return result;
    }
    std::vector<Explorer::LiveTab> Explorer::OrderedTabsOf(HWND window)
    {
        const auto all = EnumerateTabs(window, true);
        std::vector<LiveTab const*> mine;
        for (auto const& live : all)
        {
            mine.push_back(&live);
        }
        m_orderCache.erase(window); // le schede sono appena cambiate: l'ordine si rilegge
        std::vector<LiveTab> result;
        for (auto const* live : OrderTabs(window, mine))
        {
            result.push_back(*live);
        }
        return result;
    }

    std::vector<Window> Explorer::Snapshot()
    {
        const auto tabs = EnumerateTabs();
        std::vector<Window> result;
        std::unordered_set<HWND> seenTabs;
        std::unordered_set<HWND> seenWindows;
        for (HWND hwnd : CabinetWindows())
        {
            seenWindows.insert(hwnd);
            std::vector<LiveTab const*> mine;
            for (auto const& live : tabs)
            {
                if (live.top == hwnd)
                {
                    mine.push_back(&live);
                }
            }
            const auto byZ = TabWindowsByZ(hwnd);
            const HWND activeTab = byZ.empty() ? nullptr : byZ.front();
            Window window;
            window.hwnd = hwnd;
            window.placement.length = sizeof(WINDOWPLACEMENT);
            if (!GetWindowPlacement(hwnd, &window.placement))
            {
                window.placement = {};
            }
            for (auto const* live : OrderTabs(hwnd, mine))
            {
                Tab tab = live->tab;
                if (tab.tabWindow)
                {
                    seenTabs.insert(tab.tabWindow);
                }
                if (tab.path.empty())
                {
                    // Vista non pronta (la scheda sta caricando una cartella): vale l'ultima cartella nota.
                    const auto known = tab.tabWindow ? m_lastKnown.find(tab.tabWindow) : m_lastKnown.end();
                    if (known == m_lastKnown.end())
                    {
                        continue;
                    }
                    tab.path = known->second.path;
                    tab.idList = known->second.idList;
                }
                else if (tab.tabWindow)
                {
                    m_lastKnown[tab.tabWindow] = tab;
                }
                if (tab.tabWindow && tab.tabWindow == activeTab)
                {
                    window.active = static_cast<int>(window.tabs.size());
                }
                window.tabs.push_back(std::move(tab));
            }
            result.push_back(std::move(window));
        }
        std::erase_if(m_lastKnown, [&](auto const& entry) { return !seenTabs.contains(entry.first); });
        std::erase_if(m_lastPosition, [&](auto const& entry) { return !seenTabs.contains(entry.first); });
        std::erase_if(m_orderCache, [&](auto const& entry) { return !seenWindows.contains(entry.first); });
        return result;
    }

    HWND Explorer::OpenWindow(Tab const& tab)
    {
        const UniquePidl pidl = ToIdList(tab);
        if (!pidl)
        {
            return nullptr;
        }
        const auto existing = CabinetWindows();
        const std::unordered_set<HWND> before(existing.begin(), existing.end());

        // "Apri in una nuova finestra": sempre una finestra a sé (aprire la cartella e basta potrebbe
        // aggiungerla come scheda a una finestra già aperta).
        const ULONGLONG t0 = GetTickCount64();
        SHELLEXECUTEINFOW info{ sizeof(info) };
        info.fMask = SEE_MASK_INVOKEIDLIST | SEE_MASK_FLAG_NO_UI | SEE_MASK_NOASYNC;
        info.lpVerb = L"opennewwindow";
        info.lpIDList = pidl.get();
        info.nShow = SW_SHOWNORMAL;
        if (!ShellExecuteExW(&info))
        {
            Log(L"Apertura di una finestra non riuscita (errore %lu).", GetLastError());
            return nullptr;
        }
        const ULONGLONG t1 = GetTickCount64();

        // La nuova finestra: comparsa dopo la richiesta, con la scheda pronta sulla cartella chiesta e
        // non già presa da un'altra apertura in parallelo. Se dopo qualche secondo nessuna ha quella
        // cartella (nome di analisi diverso per una cartella virtuale), basta che sia nuova.
        HWND created = nullptr;
        {
            std::scoped_lock lock(g_claimLock);
            std::erase_if(g_claimed, [](HWND hwnd) { return !IsWindow(hwnd); });
        }
        WaitFor(
            [&] {
                const bool anyFolder = GetTickCount64() - t1 > 3000;
                for (HWND hwnd : CabinetWindows())
                {
                    if (before.contains(hwnd))
                    {
                        continue;
                    }
                    {
                        std::scoped_lock lock(g_claimLock);
                        if (g_claimed.contains(hwnd))
                        {
                            continue;
                        }
                    }
                    const auto tabs = EnumerateTabs(hwnd, true);
                    if (tabs.empty() || tabs.front().tab.path.empty() || (!anyFolder && !EqualsIgnoreCase(tabs.front().tab.path, tab.path)))
                    {
                        continue;
                    }
                    std::scoped_lock lock(g_claimLock);
                    if (g_claimed.insert(hwnd).second)
                    {
                        created = hwnd;
                        return true;
                    }
                }
                return false;
            },
            10000, 40);
        if (!created)
        {
            Log(L"La nuova finestra non è comparsa.");
            return nullptr;
        }
        XPX_VLOG(L"Finestra aperta: verbo %llu ms, finestra pronta %llu ms.", t1 - t0, GetTickCount64() - t1);
        return created;
    }

    std::vector<Tab> Explorer::TabsIn(HWND window)
    {
        std::vector<Tab> result;
        for (auto& live : EnumerateTabs(window, true))
        {
            result.push_back(std::move(live.tab));
        }
        return result;
    }

    bool Explorer::HasReadableTab(HWND window)
    {
        const auto tabs = EnumerateTabs(window, true);
        return std::any_of(tabs.begin(), tabs.end(), [](LiveTab const& live) { return !live.tab.path.empty(); });
    }

    ComPtr<IShellBrowser> Explorer::BrowserOf(HWND window, HWND tabWindow, Identities& mapped)
    {
        long count = 0;
        if (!EnsureShellWindows() || FAILED(m_shellWindows->get_Count(&count)))
        {
            m_shellWindows.Reset(); // Esplora file riavviato: al prossimo tentativo si ricollega
            return nullptr;
        }
        // Dal fondo: le schede nuove si registrano in fondo all'elenco.
        for (long i = count - 1; i >= 0; --i)
        {
            VARIANT index{};
            index.vt = VT_I4;
            index.lVal = i;
            LiveTab live;
            if (FAILED(m_shellWindows->Item(index, &live.dispatch)) || !live.dispatch)
            {
                continue;
            }
            live.dispatch.As(&live.identity);
            if (std::any_of(mapped.begin(), mapped.end(), [&](auto const& id) { return id.Get() == live.identity.Get(); }))
            {
                continue;
            }
            ComPtr<IWebBrowserApp> app;
            SHANDLE_PTR top = 0;
            if (FAILED(live.dispatch.As(&app)) || FAILED(app->get_HWND(&top)) || reinterpret_cast<HWND>(top) != window)
            {
                continue;
            }
            if (ReadDetails(live) && live.tab.tabWindow == tabWindow)
            {
                mapped.push_back(live.identity);
                return live.browser;
            }
        }
        return nullptr;
    }

    bool Explorer::NavigateTab(HWND window, HWND tabWindow, Tab const& tab, Identities& mapped, std::vector<Pending>& pending)
    {
        const UniquePidl pidl = ToIdList(tab);
        if (!pidl)
        {
            return false;
        }
        // Una scheda appena creata si registra tra quelle della shell poco dopo aver caricato la sua prima
        // cartella.
        ComPtr<IShellBrowser> browser;
        if (!WaitFor([&] { return (browser = BrowserOf(window, tabWindow, mapped)) != nullptr; }, 3000, 10))
        {
            Log(L"Scheda non trovata tra quelle della shell.");
            return false;
        }
        Pending entry{ tabWindow, TextOf(tabWindow), DisplayName(pidl.get()) };
        const HRESULT hr = browser->BrowseObject(pidl.get(), SBSP_SAMEBROWSER | SBSP_ABSOLUTE);
        if (FAILED(hr))
        {
            Log(L"Navigazione della scheda non riuscita (0x%08X).", static_cast<unsigned>(hr));
            return false;
        }
        pending.push_back(std::move(entry));
        return true;
    }

    HWND Explorer::NewTab(HWND window, Tab const& tab, Identities& mapped, std::vector<Pending>& pending, bool& navigated)
    {
        navigated = false;
        const auto before = TabWindowsByZ(window);
        if (before.empty())
        {
            return nullptr;
        }
        // Il comando parte quando la finestra risponde (subito, se non è occupata).
        WaitResponsive(before.front(), 10000);
        if (!RequestNewTab(before.front()))
        {
            return nullptr;
        }
        // La scheda nuova è pronta quando è in cima (attiva) e ha la sua vista sulla cartella iniziale.
        // Prima, un altro comando "nuova scheda" la lascerebbe scollegata dalla sua intestazione nella
        // barra delle schede (che poi non la seleziona più, e la finestra non si chiude più con la X), e
        // la navigazione verrebbe sovrascritta da quella iniziale. Non serve invece aspettare che abbia
        // caricato la cartella voluta: finisce di caricarla anche non più attiva.
        HWND created = nullptr;
        const auto ready = [&] {
            const auto now = TabWindowsByZ(window);
            if (!now.empty() && std::find(before.begin(), before.end(), now.front()) == before.end() && HasView(now.front()))
            {
                created = now.front();
                return true;
            }
            return false;
        };
        const auto anyNew = [&] {
            const auto now = TabWindowsByZ(window);
            return std::any_of(now.begin(), now.end(), [&](HWND hwnd) { return std::find(before.begin(), before.end(), hwnd) == before.end(); });
        };
        bool appeared = WaitFor(ready, 5000, 5);
        if (!appeared && IsWindow(window))
        {
            if (anyNew())
            {
                // C'è, ma non ha ancora la sua vista (Esplora file lento): la si aspetta ancora.
                XPX_VLOG(L"La nuova scheda è lenta a comparire: si aspetta ancora.");
                appeared = WaitFor(ready, 15000, 10);
            }
            else
            {
                // Nessuna traccia: il comando può essere ancora in coda dietro altro lavoro (finestra
                // occupata) oppure perso, come capita con Esplora file appena riavviato. Quando la finestra
                // torna a rispondere i messaggi in coda passano: se nemmeno allora compare niente, il comando
                // si ripete, una volta sola (con la finestra ancora occupata no: le schede sarebbero due).
                if (WaitResponsive(window, 10000) && !WaitFor(anyNew, 1500, 20))
                {
                    const auto now = TabWindowsByZ(window);
                    if (!now.empty() && RequestNewTab(now.front()))
                    {
                        Log(L"La nuova scheda non è comparsa: richiesta ripetuta.");
                    }
                }
                appeared = WaitFor(ready, 10000, 10);
            }
        }
        if (!appeared)
        {
            Log(L"La nuova scheda non è comparsa.");
            return nullptr;
        }
        navigated = NavigateTab(window, created, tab, mapped, pending);
        return created;
    }

    bool Explorer::ActivateTab(HWND window, std::vector<HWND> const& order, int index)
    {
        if (index < 0 || index >= static_cast<int>(order.size()))
        {
            return false;
        }
        const auto isActive = [&] {
            const auto byZ = TabWindowsByZ(window);
            return !byZ.empty() && byZ.front() == order[static_cast<size_t>(index)];
        };
        if (isActive())
        {
            return true;
        }
        const auto byZ = TabWindowsByZ(window);
        const auto current = std::find(order.begin(), order.end(), byZ.empty() ? nullptr : byZ.front());
        if (current == order.end())
        {
            return false;
        }
        // Come da tastiera, nel verso più corto (Ctrl+Tab dall'ultima torna alla prima).
        const int count = static_cast<int>(order.size());
        const int from = static_cast<int>(current - order.begin());
        const int forward = (index - from + count) % count;
        const int backward = (from - index + count) % count;
        const WPARAM command = forward <= backward ? kNextTabCommand : kPreviousTabCommand;
        for (int i = 0; i < std::min(forward, backward); ++i)
        {
            PostMessageW(byZ.front(), WM_COMMAND, command, 0);
        }
        return WaitFor(isActive, 2000, 10);
    }

    bool Explorer::BuildTabs(HWND window, std::vector<Tab> const& tabs, bool navigateFirst, int active, std::vector<size_t>* missing)
    {
        const ULONGLONG t0 = GetTickCount64();
        std::vector<HWND> order = TabWindowsByZ(window);
        // Con una sola scheda di partenza l'ordine finale è noto: quella e poi le nuove, nell'ordine in cui
        // si creano (ognuna dopo quella attiva, cioè la precedente).
        const bool orderKnown = order.size() == 1;
        Identities mapped;
        std::vector<Pending> pending;
        bool complete = true;
        size_t added = 0;
        size_t next = 0;
        if (navigateFirst && !tabs.empty() && !order.empty())
        {
            complete = NavigateTab(window, order.front(), tabs.front(), mapped, pending);
            if (!complete && missing)
            {
                missing->push_back(0);
            }
            next = 1;
        }
        for (; next < tabs.size(); ++next)
        {
            bool navigated = false;
            const HWND created = NewTab(window, tabs[next], mapped, pending, navigated);
            if (!created)
            {
                complete = false;
                break;
            }
            order.push_back(created);
            ++added;
            if (!navigated)
            {
                // Scheda rimasta sulla cartella iniziale e forse non del tutto pronta: meglio non crearne altre.
                complete = false;
                break;
            }
        }
        // Mancano la scheda su cui ci si è fermati e quelle dopo.
        for (size_t i = next; missing && i < tabs.size(); ++i)
        {
            missing->push_back(i);
        }
        const ULONGLONG t1 = GetTickCount64();

        if (!(orderKnown && ActivateTab(window, order, active)) && !SelectTab(window, active))
        {
            XPX_VLOG(L"Scheda %d da rendere attiva non trovata.", active);
        }
        // La finestra deve comparire con i nomi giusti nella barra: si aspetta che ogni scheda mostri la
        // sua cartella (il titolo cambia; se il nome è lo stesso di prima non c'è niente da aspettare).
        WaitFor(
            [&] {
                return std::all_of(pending.begin(), pending.end(), [](Pending const& entry) {
                    if (!IsWindow(entry.tabWindow) || entry.expected == entry.before)
                    {
                        return true;
                    }
                    const std::wstring title = TextOf(entry.tabWindow);
                    return title == entry.expected || title != entry.before;
                });
            },
            kLoadTimeoutMs, 15);
        XPX_VLOG(L"Schede costruite: %zu nuove in %llu ms, scheda attiva e caricamento %llu ms%s.", added, t1 - t0, GetTickCount64() - t1,
                 complete ? L"" : L" (incomplete)");
        return complete;
    }

    bool Explorer::SelectTab(HWND window, int index)
    {
        if (index < 0)
        {
            return false;
        }
        const auto items = TabItems(window);
        ComPtr<IUIAutomationElement> target;
        for (auto const& item : items)
        {
            if (item.position == index + 1)
            {
                target = item.element;
                break;
            }
        }
        if (!target && !items.empty() && items.front().position == 0 && index < static_cast<int>(items.size()))
        {
            target = items[static_cast<size_t>(index)].element; // senza posizioni: ordine della barra
        }
        if (!target && !items.empty())
        {
            // Fuori vista (la barra scorre ed è virtualizzata): la si cerca per nome nell'elenco delle
            // schede e la si fa comparire.
            const auto tabs = OrderedTabsOf(window);
            ComPtr<IUIAutomationTreeWalker> walker;
            ComPtr<IUIAutomationElement> list;
            ComPtr<IUIAutomationItemContainerPattern> container;
            if (index < static_cast<int>(tabs.size()) && SUCCEEDED(m_automation->get_ControlViewWalker(&walker)) &&
                SUCCEEDED(walker->GetParentElement(items.front().element.Get(), &list)) && list &&
                SUCCEEDED(list->GetCurrentPatternAs(UIA_ItemContainerPatternId, IID_PPV_ARGS(&container))) && container)
            {
                VARIANT name{};
                name.vt = VT_BSTR;
                name.bstrVal = SysAllocString(tabs[static_cast<size_t>(index)].tab.title.c_str());
                ComPtr<IUIAutomationElement> found;
                if (SUCCEEDED(container->FindItemByProperty(nullptr, UIA_NamePropertyId, name, &found)) && found)
                {
                    ComPtr<IUIAutomationVirtualizedItemPattern> virtualized;
                    if (SUCCEEDED(found->GetCurrentPatternAs(UIA_VirtualizedItemPatternId, IID_PPV_ARGS(&virtualized))) && virtualized)
                    {
                        virtualized->Realize();
                    }
                    target = found;
                }
                VariantClear(&name);
            }
        }
        ComPtr<IUIAutomationSelectionItemPattern> pattern;
        if (!target || FAILED(target->GetCurrentPatternAs(UIA_SelectionItemPatternId, IID_PPV_ARGS(&pattern))) || !pattern)
        {
            XPX_VLOG(L"Scheda %d da rendere attiva non trovata nella barra.", index);
            return false;
        }
        return SUCCEEDED(pattern->Select());
    }

    void Explorer::Place(HWND window, WINDOWPLACEMENT placement)
    {
        if (placement.length != sizeof(WINDOWPLACEMENT))
        {
            return;
        }
        // Se lo schermo di allora non c'è più, la posizione la sceglie Esplora file.
        if (!MonitorFromRect(&placement.rcNormalPosition, MONITOR_DEFAULTTONULL))
        {
            return;
        }
        // Una finestra ridotta a icona torna visibile (normale o ingrandita come prima).
        if (placement.showCmd == SW_SHOWMINIMIZED || placement.showCmd == SW_MINIMIZE || placement.showCmd == SW_SHOWMINNOACTIVE ||
            placement.showCmd == SW_FORCEMINIMIZE)
        {
            placement.showCmd = (placement.flags & WPF_RESTORETOMAXIMIZED) ? SW_SHOWMAXIMIZED : SW_SHOWNORMAL;
        }
        placement.flags = 0;
        SetWindowPlacement(window, &placement);
    }

    std::vector<std::vector<BYTE>> Explorer::SelectedItems(HWND window, size_t maxItems)
    {
        std::vector<std::vector<BYTE>> result;
        const auto byZ = TabWindowsByZ(window);
        for (auto& live : EnumerateTabs(window, true))
        {
            // La scheda attiva (in una finestra appena aperta c'è solo lei).
            if (!live.browser || (!byZ.empty() && live.tab.tabWindow && live.tab.tabWindow != byZ.front()))
            {
                continue;
            }
            ComPtr<IShellView> view;
            ComPtr<IFolderView> folderView;
            ComPtr<IEnumIDList> items;
            if (FAILED(live.browser->QueryActiveShellView(&view)) || !view || FAILED(view.As(&folderView)) ||
                FAILED(folderView->Items(SVGIO_SELECTION, IID_PPV_ARGS(&items))) || !items)
            {
                break;
            }
            PITEMID_CHILD child = nullptr;
            while (result.size() < maxItems && items->Next(1, &child, nullptr) == S_OK && child)
            {
                const auto* bytes = reinterpret_cast<const BYTE*>(child);
                result.emplace_back(bytes, bytes + ILGetSize(child));
                CoTaskMemFree(child);
                child = nullptr;
            }
            break;
        }
        return result;
    }

    bool Explorer::SelectItems(HWND window, std::wstring const& folder, std::vector<std::vector<BYTE>> const& items, DWORD timeoutMs)
    {
        if (items.empty())
        {
            return true;
        }
        // La vista carica gli elementi poco dopo aver mostrato la cartella: si riprova finché non sono selezionati.
        return WaitFor(
            [&] {
                const auto byZ = TabWindowsByZ(window);
                for (auto& live : EnumerateTabs(window, true))
                {
                    if (!live.browser || byZ.empty() || live.tab.tabWindow != byZ.front() || !EqualsIgnoreCase(live.tab.path, folder))
                    {
                        continue;
                    }
                    ComPtr<IShellView> view;
                    ComPtr<IFolderView> folderView;
                    if (FAILED(live.browser->QueryActiveShellView(&view)) || !view || FAILED(view.As(&folderView)))
                    {
                        return false;
                    }
                    UINT flags = SVSI_SELECT | SVSI_DESELECTOTHERS | SVSI_ENSUREVISIBLE | SVSI_FOCUSED;
                    for (auto const& item : items)
                    {
                        view->SelectItem(reinterpret_cast<PCUITEMID_CHILD>(item.data()), flags);
                        flags = SVSI_SELECT;
                    }
                    int selected = 0;
                    return SUCCEEDED(folderView->ItemCount(SVGIO_SELECTION, &selected)) && selected >= static_cast<int>(items.size());
                }
                return false;
            },
            timeoutMs, 60);
    }

    bool Explorer::OnCurrentDesktop(HWND window)
    {
        ComPtr<IVirtualDesktopManager> manager;
        BOOL current = TRUE;
        if (SUCCEEDED(CoCreateInstance(CLSID_VirtualDesktopManager, nullptr, CLSCTX_ALL, IID_PPV_ARGS(&manager))) && manager)
        {
            if (FAILED(manager->IsWindowOnCurrentVirtualDesktop(window, &current)))
            {
                current = TRUE;
            }
        }
        return current != FALSE;
    }

    void Explorer::Activate(HWND window)
    {
        if (GetForegroundWindow() == window)
        {
            return;
        }
        // Da un altro processo SetForegroundWindow di norma viene bloccato: il focus lo dà UI Automation.
        if (EnsureAutomation())
        {
            ComPtr<IUIAutomationElement> element;
            if (SUCCEEDED(m_automation->ElementFromHandle(window, &element)) && element)
            {
                element->SetFocus();
            }
        }
        if (GetForegroundWindow() != window)
        {
            SetForegroundWindow(window);
        }
    }
}
