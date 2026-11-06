#include "pch.h"
#include "WindowLayout.h"

#include "Glass.h"
#include "Interop.h"
#include "ModLog.h"

namespace xpx::mod
{
    namespace
    {
        constexpr UINT_PTR kSubclassId = 0x58505831; // "XPX1"
        constexpr int kMinContentPx = 240;            // il contenuto non diventa mai più stretto di così

        // Messaggio (registrato) che rimanda l'aggiornamento del pannello a quando Esplora file ha finito
        // di sistemare le sue finestre.
        UINT UpdateMessage()
        {
            static const UINT message = RegisterWindowMessageW(L"VTabsExplorer.VerticalTabs.Layout");
            return message;
        }

        bool SameRect(RECT const& a, RECT const& b)
        {
            return a.left == b.left && a.top == b.top && a.right == b.right && a.bottom == b.bottom;
        }

        RECT RectInParent(HWND child)
        {
            return interop::RectInClientOf(child, GetParent(child));
        }

        // Agganci degli eventi (WatchNavPane): la callback ritrova qui il suo layout. Ognuno si mette e si toglie
        // sul thread della sua finestra; eventi ancora in coda dopo averlo tolto non trovano più la voce.
        std::mutex g_navHooksLock;
        std::unordered_map<HWINEVENTHOOK, WindowLayout*> g_navHooks;

        // Impostazione del riquadro di spostamento salvata da Esplora file (Visualizza > Mostra), quella che dà
        // alle schede nuove: il secondo DWORD di PageSpaceControlSizer (1 = acceso). nullopt se non si legge.
        std::optional<bool> SavedNavPaneShown()
        {
            BYTE data[64]{};
            DWORD size = sizeof(data);
            if (RegGetValueW(HKEY_CURRENT_USER, L"Software\\Microsoft\\Windows\\CurrentVersion\\Explorer\\Modules\\GlobalSettings\\Sizer",
                             L"PageSpaceControlSizer", RRF_RT_REG_BINARY, nullptr, data, &size) != ERROR_SUCCESS ||
                size < 8)
            {
                return std::nullopt;
            }
            return data[4] != 0;
        }
    }

    WindowLayout::~WindowLayout()
    {
        Detach();
    }

    std::optional<WindowLayout::Role> WindowLayout::RoleOf(HWND hwnd)
    {
        const std::wstring name = interop::ClassOf(hwnd);
        if (name == L"ShellTabWindowClass")
        {
            return Role::Content;
        }
        if (name == L"TITLE_BAR_SCAFFOLDING_WINDOW_CLASS")
        {
            return Role::DragBar;
        }
        return std::nullopt;
    }

    bool WindowLayout::Attach(HWND main)
    {
        if (m_main)
        {
            return m_main == main;
        }
        if (!main || GetWindowThreadProcessId(main, nullptr) != GetCurrentThreadId())
        {
            XPX_MLOG(L"WindowLayout: la finestra %llx non appartiene a questo thread.", reinterpret_cast<uintptr_t>(main));
            return false;
        }
        if (!SetWindowSubclass(main, MainProc, kSubclassId, reinterpret_cast<DWORD_PTR>(this)))
        {
            XPX_MLOG(L"WindowLayout: subclass della finestra %llx non riuscito.", reinterpret_cast<uintptr_t>(main));
            return false;
        }
        m_main = main;
        m_dpi = GetDpiForWindow(main);
        glass::Install(); // (aggancia i moduli caricati dopo l'avvio della mod; gli altri sono già agganciati)
        TrackChildren();
        return true;
    }

    void WindowLayout::SetPanel(HWND panel)
    {
        m_panel = panel;
        PositionPanel();
    }

    void WindowLayout::Detach(bool restore)
    {
        WatchNavPane(false);
        m_navOff = false;
        if (!m_main)
        {
            return;
        }
        if (Engaged())
        {
            m_active = false;
            m_barDip = 0;
            if (restore)
            {
                ApplyAll(); // Esplora file ritrova le sue finestre dove le aveva messe
            }
        }
        // Sink del riquadro e della vista: di nuovo dove le vuole DirectUI, con la sua regione (solo se la finestra resta).
        ReleaseSinks(restore);
        m_buttons.Remove(restore && IsWindow(m_main)); // tornano i pulsanti di DWM
        if (restore && m_header && IsWindow(m_header) && m_headerRegionTop > 0)
        {
            SetWindowRgn(m_header, nullptr, TRUE);
        }
        m_headerRegionTop = 0;
        if (restore && IsWindow(m_main))
        {
            glass::SetTopLimit(m_main, std::nullopt, m_contentNatural.top); // torna la zona vetro di Esplora file
        }
        glass::Forget(m_main);
        for (auto const& [hwnd, tracked] : m_tracked)
        {
            RemoveWindowSubclass(hwnd, ChildProc, kSubclassId);
        }
        m_tracked.clear();
        RemoveWindowSubclass(m_main, MainProc, kSubclassId);
        m_main = nullptr;
        m_panel = nullptr;
        m_bar = nullptr;
        m_header = nullptr;
        m_headerNatural = {};
        m_dragNatural = {};
        m_geometry = { -1, -1 };
        m_haveContent = false;
        m_panelShown = false;
        m_barShown = false;
    }

    void WindowLayout::SetBar(int heightDip)
    {
        if (!m_main)
        {
            return;
        }
        heightDip = std::max(0, heightDip);
        if (heightDip != m_barDip)
        {
            m_barDip = heightDip;
            m_navOff = false;
            WatchNavPane(m_barDip > 0);
            RefreshNavPane(L"barra accesa"); // con il riquadro già spento nella scheda attiva la barra parte nascosta
            ApplyAll();
            if (m_barDip <= 0)
            {
                ReleaseSinks(true); // (ApplyAll le ha già rimesse dove le vuole DirectUI)
            }
        }
        PositionPanel();
    }

    void WindowLayout::SetBarWindow(HWND bar)
    {
        m_bar = bar;
        m_barShown = false;
        PositionPanel();
    }

    void WindowLayout::ForgetBar()
    {
        WatchNavPane(false);
        ReleaseSinks(false); // (la finestra si sta chiudendo: niente da rimettere a posto)
        m_bar = nullptr;
        m_barDip = 0;
        m_navOff = false;
        m_barShown = false;
    }

    int WindowLayout::BarHeightPx() const
    {
        return BarOn() ? MulDiv(m_barDip, static_cast<int>(m_dpi ? m_dpi : 96), 96) : 0;
    }

    std::optional<bool> WindowLayout::NavPaneShown(HWND content)
    {
        // Spento da Visualizza, Esplora file NASCONDE il CtrlNotifySink del riquadro (non lo distrugge) e la vista
        // dei file si allarga a sinistra; una scheda appena creata lo riceve dopo qualche decina di ms, già acceso
        // o spento. Lo stile e non IsWindowVisible: non dipende dalla finestra principale (Esplora file la nasconde
        // quando la tiene da parte per riusarla).
        const HWND view = FindWindowExW(content, nullptr, L"DUIViewWndClassName", nullptr);
        const HWND direct = view ? FindWindowExW(view, nullptr, L"DirectUIHWND", nullptr) : nullptr;
        if (!direct)
        {
            return std::nullopt;
        }
        std::optional<bool> shown;
        for (HWND sink = FindWindowExW(direct, nullptr, L"CtrlNotifySink", nullptr); sink; sink = FindWindowExW(direct, sink, L"CtrlNotifySink", nullptr))
        {
            if (FindWindowExW(sink, nullptr, L"NamespaceTreeControl", nullptr))
            {
                shown = shown.value_or(false) || (GetWindowLongW(sink, GWL_STYLE) & WS_VISIBLE) != 0;
            }
        }
        return shown;
    }

    bool WindowLayout::RefreshNavPane(const wchar_t* why)
    {
        if (!m_main || m_barDip <= 0)
        {
            return false;
        }
        std::optional<bool> shown;
        if (const HWND content = ActiveContent())
        {
            shown = NavPaneShown(content);
        }
        if (!shown)
        {
            shown = SavedNavPaneShown(); // scheda appena creata: Esplora file le darà l'impostazione salvata
        }
        if (!shown || *shown == !m_navOff)
        {
            return false;
        }
        m_navOff = !*shown;
        XPX_MLOG(L"WindowLayout: riquadro di spostamento %s nella scheda attiva: barra %s (%s).", m_navOff ? L"spento" : L"acceso",
                 m_navOff ? L"nascosta" : L"visibile", why);
        return true;
    }

    void WindowLayout::WatchNavPane(bool watch)
    {
        if (watch == (m_navHook != nullptr))
        {
            return;
        }
        if (watch)
        {
            // Solo comparsa e scomparsa (il riquadro spento viene nascosto, non distrutto), solo di questo thread,
            // fuori contesto: la callback arriva con i messaggi del thread, mai dentro il codice di Esplora file.
            m_navHook = SetWinEventHook(EVENT_OBJECT_SHOW, EVENT_OBJECT_HIDE, nullptr, NavEventProc, GetCurrentProcessId(),
                                        GetCurrentThreadId(), WINEVENT_OUTOFCONTEXT);
            if (!m_navHook)
            {
                XPX_MLOG(L"WindowLayout: eventi del riquadro di spostamento non disponibili (%lu): resta il controllo periodico.", GetLastError());
                return;
            }
            std::scoped_lock lock(g_navHooksLock);
            g_navHooks[m_navHook] = this;
        }
        else
        {
            {
                std::scoped_lock lock(g_navHooksLock);
                g_navHooks.erase(m_navHook);
            }
            UnhookWinEvent(m_navHook);
            m_navHook = nullptr;
        }
    }

    void CALLBACK WindowLayout::NavEventProc(HWINEVENTHOOK hook, DWORD, HWND hwnd, LONG idObject, LONG idChild, DWORD, DWORD)
    {
        if (!hwnd || idObject != OBJID_WINDOW || idChild != CHILDID_SELF)
        {
            return;
        }
        WindowLayout* self = nullptr;
        {
            std::scoped_lock lock(g_navHooksLock);
            const auto it = g_navHooks.find(hook);
            if (it == g_navHooks.end())
            {
                return;
            }
            self = it->second; // si distrugge solo su questo thread (dopo aver tolto l'aggancio)
        }
        if (!self->m_main || self->m_barDip <= 0)
        {
            return;
        }
        // Il riquadro (il suo CtrlNotifySink o l'albero dentro), una scheda di questa finestra o una vista dei file nuova
        // (cambiando cartella; con la barra va allargata come la sua sink): si ricontrolla al prossimo giro di messaggi,
        // prima che il contenuto venga ridisegnato.
        wchar_t name[32]{};
        GetClassNameW(hwnd, name, ARRAYSIZE(name));
        if (wcscmp(name, L"CtrlNotifySink") != 0 && wcscmp(name, L"NamespaceTreeControl") != 0 && wcscmp(name, L"ShellTabWindowClass") != 0 &&
            wcscmp(name, L"SHELLDLL_DefView") != 0)
        {
            return;
        }
        if (IsChild(self->m_main, hwnd))
        {
            self->RequestPanelUpdate();
        }
    }

    void WindowLayout::TrackSinks()
    {
        if (!m_main || m_barDip <= 0)
        {
            return;
        }
        // Contenuto di una scheda: DUIViewWndClassName > DirectUIHWND > una CtrlNotifySink per riquadro.
        for (auto const& [content, tracked] : m_tracked)
        {
            if (tracked.role != Role::Content || !IsWindow(content))
            {
                continue;
            }
            const HWND view = FindWindowExW(content, nullptr, L"DUIViewWndClassName", nullptr);
            const HWND direct = view ? FindWindowExW(view, nullptr, L"DirectUIHWND", nullptr) : nullptr;
            if (!direct)
            {
                continue;
            }
            for (HWND sink = FindWindowExW(direct, nullptr, L"CtrlNotifySink", nullptr); sink; sink = FindWindowExW(direct, sink, L"CtrlNotifySink", nullptr))
            {
                const bool nav = FindWindowExW(sink, nullptr, L"NamespaceTreeControl", nullptr) != nullptr;
                if (const auto it = m_sinks.find(sink); it != m_sinks.end())
                {
                    it->second.nav = it->second.nav || nav; // l'albero può arrivare dopo la sink
                    continue;
                }
                if (GetWindowThreadProcessId(sink, nullptr) != GetCurrentThreadId() ||
                    !SetWindowSubclass(sink, SinkProc, kSubclassId, reinterpret_cast<DWORD_PTR>(this)))
                {
                    continue;
                }
                Sink entry;
                entry.direct = direct;
                entry.nav = nav;
                entry.natural = RectInParent(sink);
                m_sinks.emplace(sink, entry);
            }
        }
        // Finestre interne delle sink (non del riquadro): la vista dei file (anche ricreata cambiando cartella).
        std::vector<std::pair<HWND, HWND>> outer; // sink, DirectUIHWND
        for (auto const& [hwnd, sink] : m_sinks)
        {
            if (!sink.parent && !sink.nav)
            {
                outer.emplace_back(hwnd, sink.direct);
            }
        }
        for (auto const& [sink, direct] : outer)
        {
            for (HWND inner = GetWindow(sink, GW_CHILD); inner; inner = GetWindow(inner, GW_HWNDNEXT))
            {
                if (m_sinks.contains(inner) || GetWindowThreadProcessId(inner, nullptr) != GetCurrentThreadId() ||
                    !SetWindowSubclass(inner, SinkProc, kSubclassId, reinterpret_cast<DWORD_PTR>(this)))
                {
                    continue;
                }
                Sink entry;
                entry.direct = direct;
                entry.parent = sink;
                entry.natural = RectInParent(inner);
                m_sinks.emplace(inner, entry);
            }
        }
    }

    int WindowLayout::AdjacentLeft(HWND direct) const
    {
        // Riquadro di spostamento visibile (spento da Visualizza, Esplora file ne nasconde la sink: lo stile, come in
        // NavPaneShown) e la sink visibile più vicina alla sua destra: la vista dei file.
        const Sink* nav = nullptr;
        for (auto const& [hwnd, sink] : m_sinks)
        {
            if (sink.direct == direct && !sink.parent && sink.nav && (GetWindowLongW(hwnd, GWL_STYLE) & WS_VISIBLE) &&
                sink.natural.right > sink.natural.left)
            {
                nav = &sink;
            }
        }
        if (!nav)
        {
            return -1;
        }
        int best = -1;
        for (auto const& [hwnd, sink] : m_sinks)
        {
            if (sink.direct != direct || sink.parent || sink.nav || !(GetWindowLongW(hwnd, GWL_STYLE) & WS_VISIBLE) ||
                sink.natural.right <= sink.natural.left)
            {
                continue;
            }
            if (sink.natural.left >= nav->natural.right - 1 && (best < 0 || sink.natural.left < best))
            {
                best = sink.natural.left;
            }
        }
        return best;
    }

    int WindowLayout::SinkShift(HWND direct) const
    {
        if (!BarOn())
        {
            return 0;
        }
        const int left = AdjacentLeft(direct);
        return left > 0 ? left : 0;
    }

    RECT WindowLayout::SinkWanted(Sink const& sink) const
    {
        RECT rect = sink.natural;
        if (sink.parent)
        {
            // Finestra interna: se occupa tutta la larghezza della sink, la segue quando si allarga (fino al suo bordo
            // destro nuovo, anche se Esplora file l'ha già fatta larga così).
            const auto it = m_sinks.find(sink.parent);
            if (it == m_sinks.end())
            {
                return rect;
            }
            const RECT outerNatural = it->second.natural;
            const RECT outerWanted = SinkWanted(it->second);
            const LONG naturalWidth = outerNatural.right - outerNatural.left;
            const LONG wantedWidth = outerWanted.right - outerWanted.left;
            if (wantedWidth > naturalWidth && rect.left <= 1 && rect.right >= naturalWidth - 1)
            {
                const LONG margin = std::max<LONG>(0, naturalWidth - rect.right); // 0 o 1 px dal bordo destro della sink
                rect.right = std::max(rect.right, wantedWidth - margin);
            }
            return rect;
        }
        const int shift = SinkShift(sink.direct);
        if (shift <= 0)
        {
            return rect;
        }
        if (sink.nav)
        {
            OffsetRect(&rect, -rect.right, 0); // tutta a sinistra del bordo: fuori vista (il contenuto la taglia)
        }
        else if (std::abs(static_cast<int>(rect.left) - AdjacentLeft(sink.direct)) <= 1)
        {
            rect.left -= shift; // la vista dei file fino al bordo sinistro, al posto del riquadro
        }
        return rect;
    }

    void WindowLayout::ApplySinks()
    {
        if (m_sinks.empty())
        {
            return;
        }
        // Copia delle chiavi: gli spostamenti mandano messaggi che possono toccare la mappa.
        std::vector<HWND> sinks;
        sinks.reserve(m_sinks.size());
        for (auto const& [hwnd, sink] : m_sinks)
        {
            sinks.push_back(hwnd);
        }
        // Prima le sink, poi le finestre interne (che seguono la loro sink).
        std::stable_partition(sinks.begin(), sinks.end(), [this](HWND hwnd) { return !m_sinks.at(hwnd).parent; });
        for (const HWND hwnd : sinks)
        {
            auto it = m_sinks.find(hwnd);
            if (it == m_sinks.end() || !IsWindow(hwnd))
            {
                continue;
            }
            // Spostata senza passare dal subclass: la posizione attuale è quella di Esplora file.
            const RECT current = RectInParent(hwnd);
            if (it->second.hasApplied && !SameRect(current, it->second.applied) && !SameRect(current, it->second.natural))
            {
                it->second.natural = current;
            }
            const RECT wanted = SinkWanted(it->second);
            it->second.applied = wanted;
            it->second.hasApplied = true;
            if (!SameRect(current, wanted))
            {
                Move(hwnd, wanted);
            }
            if (it = m_sinks.find(hwnd); it != m_sinks.end())
            {
                FixSinkRegion(hwnd, it->second);
            }
        }
    }

    void WindowLayout::OnSinkPosChanging(HWND hwnd, WINDOWPOS& pos)
    {
        if (hwnd == m_moving || ((pos.flags & SWP_NOMOVE) && (pos.flags & SWP_NOSIZE)))
        {
            return;
        }
        const auto it = m_sinks.find(hwnd);
        if (it == m_sinks.end())
        {
            return;
        }
        Sink& sink = it->second;
        // Nuovo rettangolo naturale: le parti indicate nella richiesta di DirectUI, il resto com'era.
        RECT natural = sink.natural;
        const LONG width = natural.right - natural.left;
        const LONG height = natural.bottom - natural.top;
        if (!(pos.flags & SWP_NOMOVE))
        {
            natural = { pos.x, pos.y, pos.x + width, pos.y + height };
        }
        if (!(pos.flags & SWP_NOSIZE))
        {
            natural.right = natural.left + pos.cx;
            natural.bottom = natural.top + pos.cy;
        }
        if (!SameRect(natural, sink.natural))
        {
            sink.natural = natural;
            RequestPanelUpdate(); // le altre sink dello stesso contenuto seguono (la vista segue il riquadro)
        }
        const RECT wanted = SinkWanted(sink);
        sink.applied = wanted;
        sink.hasApplied = true;
        if (SameRect(wanted, natural))
        {
            return;
        }
        pos.x = wanted.left;
        pos.y = wanted.top;
        pos.cx = wanted.right - wanted.left;
        pos.cy = wanted.bottom - wanted.top;
        pos.flags &= ~(SWP_NOMOVE | SWP_NOSIZE);
    }

    void WindowLayout::FixSinkRegion(HWND hwnd, Sink& sink)
    {
        if (sink.nav || sink.parent || hwnd == m_regionFixing)
        {
            return; // (solo le sink allargate hanno la regione di DirectUI da adattare)
        }
        RECT rect = RectInParent(hwnd);
        // Di quanto è allargata a sinistra rispetto al posto di DirectUI (con lo stesso bordo destro).
        const int widened = rect.right == sink.natural.right ? std::max(0, static_cast<int>(sink.natural.left - rect.left)) : 0;
        if (widened == 0 && sink.widened == 0)
        {
            return; // mai toccata
        }
        HRGN current = CreateRectRgn(0, 0, 0, 0);
        if (!current)
        {
            return;
        }
        const bool hasRegion = GetWindowRgn(hwnd, current) != ERROR;
        RECT box{};
        if (hasRegion)
        {
            GetRgnBox(current, &box);
        }
        const bool ours = hasRegion && sink.widened > 0 && EqualRect(&box, &sink.regionBox);
        const HWND previous = m_regionFixing;
        m_regionFixing = hwnd; // SetWindowRgn rimanda WM_WINDOWPOSCHANGED: quello è nostro
        if (widened > 0)
        {
            if (ours && sink.widened == widened)
            {
                DeleteObject(current);
                m_regionFixing = previous;
                return;
            }
            if (!ours)
            {
                // Regione nuova di DirectUI (o nessuna): se ne tiene una copia, per rimetterla.
                if (sink.duiRegion)
                {
                    DeleteObject(sink.duiRegion);
                    sink.duiRegion = nullptr;
                }
                sink.hadRegion = hasRegion;
                if (hasRegion)
                {
                    sink.duiRegion = current;
                    current = nullptr;
                }
            }
            if (current)
            {
                DeleteObject(current);
            }
            sink.widened = widened;
            sink.regionBox = {};
            if (sink.hadRegion && sink.duiRegion)
            {
                // La regione di DirectUI spostata con la sink, più la striscia allargata a sinistra.
                HRGN region = CreateRectRgn(0, 0, 0, 0);
                HRGN strip = CreateRectRgn(0, 0, widened + 1, rect.bottom - rect.top);
                if (region && strip && CombineRgn(region, sink.duiRegion, nullptr, RGN_COPY) != ERROR)
                {
                    OffsetRgn(region, widened, 0);
                    CombineRgn(region, region, strip, RGN_OR);
                    GetRgnBox(region, &sink.regionBox);
                    if (SetWindowRgn(hwnd, region, TRUE))
                    {
                        region = nullptr; // ora è della finestra
                    }
                    else
                    {
                        sink.regionBox = {};
                    }
                }
                if (region)
                {
                    DeleteObject(region);
                }
                if (strip)
                {
                    DeleteObject(strip);
                }
            }
        }
        else
        {
            // Di nuovo al posto di DirectUI: se la regione è ancora la nostra torna la sua.
            if (ours)
            {
                HRGN restore = nullptr;
                if (sink.duiRegion)
                {
                    restore = CreateRectRgn(0, 0, 0, 0);
                    if (restore)
                    {
                        CombineRgn(restore, sink.duiRegion, nullptr, RGN_COPY);
                    }
                }
                if (!SetWindowRgn(hwnd, restore, TRUE) && restore)
                {
                    DeleteObject(restore);
                }
            }
            DeleteObject(current);
            if (sink.duiRegion)
            {
                DeleteObject(sink.duiRegion);
                sink.duiRegion = nullptr;
            }
            sink.widened = 0;
            sink.hadRegion = false;
            sink.regionBox = {};
        }
        m_regionFixing = previous;
    }

    void WindowLayout::ReleaseSinks(bool restore)
    {
        std::vector<HWND> sinks;
        sinks.reserve(m_sinks.size());
        for (auto const& [hwnd, sink] : m_sinks)
        {
            sinks.push_back(hwnd);
        }
        for (const HWND hwnd : sinks)
        {
            auto it = m_sinks.find(hwnd);
            if (it == m_sinks.end())
            {
                continue;
            }
            if (IsWindow(hwnd))
            {
                if (restore)
                {
                    const RECT natural = it->second.natural;
                    if (!SameRect(RectInParent(hwnd), natural))
                    {
                        Move(hwnd, natural);
                    }
                    if (it = m_sinks.find(hwnd); it != m_sinks.end())
                    {
                        FixSinkRegion(hwnd, it->second); // regione di DirectUI, se era ancora la nostra
                    }
                }
                RemoveWindowSubclass(hwnd, SinkProc, kSubclassId);
            }
            if (it = m_sinks.find(hwnd); it != m_sinks.end())
            {
                if (it->second.duiRegion)
                {
                    DeleteObject(it->second.duiRegion);
                }
                m_sinks.erase(it);
            }
        }
    }

    void WindowLayout::UpdateHeaderRegion(HWND header, int topPx)
    {
        if (topPx == m_headerRegionTop || !header)
        {
            return;
        }
        if (topPx > 0)
        {
            // Coordinate dell'isola: si vede da topPx in giù (la cima della finestra più i 3 px del bordo sono sopra).
            const HRGN region = CreateRectRgn(0, topPx, 32767, 32767);
            if (region && SetWindowRgn(header, region, TRUE))
            {
                m_headerRegionTop = topPx;
            }
            else if (region)
            {
                DeleteObject(region);
            }
        }
        else
        {
            SetWindowRgn(header, nullptr, TRUE);
            m_headerRegionTop = 0;
        }
    }

    void WindowLayout::SetHeaderRows(double titleRowDip, double toolbarRowDip)
    {
        titleRowDip = std::max(0.0, titleRowDip);
        toolbarRowDip = std::max(0.0, toolbarRowDip);
        if (std::abs(titleRowDip - m_titleRowDip) < 0.05 && std::abs(toolbarRowDip - m_toolbarRowDip) < 0.05)
        {
            return;
        }
        m_titleRowDip = titleRowDip;
        m_toolbarRowDip = toolbarRowDip;
        XPX_MLOG(L"WindowLayout: righe dell'intestazione: titolo %.1f DIP, barra degli indirizzi %.1f DIP.", titleRowDip, toolbarRowDip);
        if (m_main)
        {
            ApplyAll(); // riga del titolo nascosta (o di nuovo visibile): si sposta tutto
        }
    }

    int WindowLayout::TitleRowPx() const
    {
        return TitleHidden() ? static_cast<int>(std::lround(m_titleRowDip * Dpi() / 96.0)) : 0;
    }

    int WindowLayout::ToolbarRowPx() const
    {
        return static_cast<int>(std::lround(m_toolbarRowDip * Dpi() / 96.0));
    }

    void WindowLayout::Enforce(HWND hwnd, Tracked const& tracked)
    {
        Move(hwnd, Wanted(hwnd, tracked));
        if (tracked.role == Role::Header)
        {
            UpdateHeaderRegion(hwnd, TitleRowPx());
        }
    }

    void WindowLayout::SetActive(bool active, int widthDip)
    {
        if (!m_main)
        {
            return;
        }
        const bool changed = active != m_active || widthDip != m_widthDip;
        m_active = active;
        m_widthDip = widthDip;
        if (changed)
        {
            ApplyAll();
        }
        PositionPanel();
    }

    void WindowLayout::Check()
    {
        if (!m_main || !IsWindow(m_main))
        {
            return;
        }
        TrackChildren();

        const UINT dpi = GetDpiForWindow(m_main);
        if (dpi && dpi != m_dpi)
        {
            m_dpi = dpi;
            ApplyAll();
        }
        // Riquadro di spostamento acceso o spento da Visualizza (rete di sicurezza degli eventi, vedi WatchNavPane).
        if (RefreshNavPane(L"controllo periodico"))
        {
            ApplyAll();
        }

        // Spostamenti sfuggiti al subclass (es. SWP_NOSENDCHANGING): si prende la posizione attuale come
        // quella voluta da Esplora file e si corregge di nuovo. Anche il riquadro di spostamento può comparire
        // dopo (scheda appena creata): cambia la larghezza da nascondere.
        if (Engaged())
        {
            for (auto& [hwnd, tracked] : m_tracked)
            {
                const RECT current = RectInParent(hwnd);
                if (SameRect(current, Wanted(hwnd, tracked)))
                {
                    if (tracked.role == Role::Header)
                    {
                        UpdateHeaderRegion(hwnd, TitleRowPx());
                    }
                    continue;
                }
                // Dove l'avevamo messa noi (e intanto è cambiata la larghezza del riquadro da nascondere) o
                // spostata da Esplora file?
                if (!(tracked.hasApplied && SameRect(current, tracked.applied)))
                {
                    if (!m_driftLogged)
                    {
                        m_driftLogged = true;
                        XPX_MLOG(L"WindowLayout: %s spostata senza preavviso, corretta.",
                                 tracked.role == Role::Content   ? L"contenuto"
                                 : tracked.role == Role::Header ? L"intestazione"
                                                                : L"barra di trascinamento");
                    }
                    tracked.natural = current;
                    if (tracked.role == Role::Content)
                    {
                        m_contentNatural = current;
                        m_haveContent = true;
                    }
                    else if (tracked.role == Role::Header)
                    {
                        m_headerNatural = current;
                    }
                    else
                    {
                        m_dragNatural = current;
                    }
                }
                Enforce(hwnd, tracked);
            }
        }
        // Riquadro di spostamento nascosto dalla barra (sink nuove, spostamenti sfuggiti al subclass).
        TrackSinks();
        ApplySinks();
        PositionPanel();
        m_buttons.Verify();
    }

    HWND WindowLayout::ActiveContent() const
    {
        if (!m_main)
        {
            return nullptr;
        }
        for (HWND child = GetWindow(m_main, GW_CHILD); child; child = GetWindow(child, GW_HWNDNEXT))
        {
            if (IsWindowVisible(child) && interop::ClassOf(child) == L"ShellTabWindowClass")
            {
                return child;
            }
        }
        return nullptr;
    }

    HWND WindowLayout::ContentFocusTarget() const
    {
        const HWND content = ActiveContent();
        if (!content)
        {
            return nullptr;
        }
        HWND view = nullptr;
        EnumChildWindows(
            content,
            [](HWND hwnd, LPARAM param) -> BOOL {
                if (IsWindowVisible(hwnd) && interop::ClassOf(hwnd) == L"DirectUIHWND" && interop::ClassOf(GetParent(hwnd)) == L"SHELLDLL_DefView")
                {
                    *reinterpret_cast<HWND*>(param) = hwnd;
                    return FALSE;
                }
                return TRUE;
            },
            reinterpret_cast<LPARAM>(&view));
        return view ? view : content;
    }

    void WindowLayout::TrackChildren()
    {
        for (auto it = m_tracked.begin(); it != m_tracked.end();)
        {
            if (!IsWindow(it->first) || GetParent(it->first) != m_main)
            {
                if (it->first == m_header)
                {
                    if (m_headerRegionTop > 0 && IsWindow(m_header))
                    {
                        SetWindowRgn(m_header, nullptr, TRUE);
                    }
                    m_header = nullptr;
                    m_headerNatural = {};
                    m_headerRegionTop = 0;
                }
                else if (it->second.role == Role::DragBar)
                {
                    m_dragNatural = {};
                }
                RemoveWindowSubclass(it->first, ChildProc, kSubclassId);
                it = m_tracked.erase(it);
            }
            else
            {
                ++it;
            }
        }
        // In ordine Z: i contenuti (davanti) si agganciano prima dell'intestazione, che si riconosce anche dalla
        // posizione (sopra il contenuto).
        for (HWND child : interop::DirectChildren(m_main))
        {
            if (!m_tracked.contains(child))
            {
                if (const auto role = RoleOf(child))
                {
                    Track(child, *role);
                }
                else if (!m_header && IsHeaderIsland(child))
                {
                    Track(child, Role::Header);
                }
            }
        }
    }

    bool WindowLayout::IsHeaderIsland(HWND child) const
    {
        // Solo dal controllo delle figlie (mai alla creazione, WM_PARENTNOTIFY): le nostre isole ricevono il loro
        // contrassegno subito dopo essere state create, prima del controllo successivo.
        if (child == m_panel || child == m_bar || interop::ClassOf(child) != L"Microsoft.UI.Content.DesktopChildSiteBridge" ||
            GetPropW(child, interop::kPanelProp) || GetPropW(child, interop::kNavBarProp))
        {
            return false;
        }
        const RECT rect = RectInParent(child);
        return rect.right > rect.left && rect.bottom > rect.top && (!m_haveContent || rect.bottom <= m_contentNatural.top + 2);
    }

    void WindowLayout::Track(HWND child, Role role)
    {
        if (m_tracked.contains(child) || GetWindowThreadProcessId(child, nullptr) != GetCurrentThreadId())
        {
            return;
        }
        if (!SetWindowSubclass(child, ChildProc, kSubclassId, reinterpret_cast<DWORD_PTR>(this)))
        {
            return;
        }
        Tracked tracked;
        tracked.role = role;
        tracked.natural = RectInParent(child);
        if (role == Role::Content)
        {
            m_contentNatural = tracked.natural;
            m_haveContent = true;
        }
        else if (role == Role::Header)
        {
            m_header = child;
            m_headerNatural = tracked.natural;
            XPX_MLOG(L"WindowLayout: intestazione %llx [%ld,%ld %ldx%ld]: la colonna può partire dalla cima.", reinterpret_cast<uintptr_t>(child),
                     tracked.natural.left, tracked.natural.top, tracked.natural.right - tracked.natural.left,
                     tracked.natural.bottom - tracked.natural.top);
        }
        else
        {
            m_dragNatural = tracked.natural;
        }
        m_tracked.emplace(child, tracked);
        if (Engaged())
        {
            if (role == Role::Header)
            {
                ApplyAll(); // con l'intestazione cambia il posto di tutto (colonna in cima)
            }
            else
            {
                Enforce(child, m_tracked.at(child));
            }
            RequestPanelUpdate();
        }
    }

    int WindowLayout::PanelWidthPx() const
    {
        const int wanted = MulDiv(m_widthDip, static_cast<int>(m_dpi ? m_dpi : 96), 96);
        if (!m_haveContent)
        {
            return wanted;
        }
        const int available = (m_contentNatural.right - m_contentNatural.left) - kMinContentPx;
        return std::clamp(wanted, 0, std::max(0, available));
    }

    int WindowLayout::PanelTopPx() const
    {
        const int below = m_contentNatural.top + BarHeightPx() - TitleRowPx();
        return Raised() ? std::min(static_cast<int>(m_headerNatural.top), below) : below;
    }

    int WindowLayout::CaptionHeightPx() const
    {
        if (!Raised())
        {
            return 0;
        }
        const int top = PanelTopPx();
        const int contentTop = static_cast<int>(m_contentNatural.top) + BarHeightPx() - TitleRowPx();
        int bottom = 0;
        if (TitleHidden())
        {
            // Riga del titolo nascosta: la prima riga è la barra degli indirizzi (i pulsanti della colonna con i suoi).
            bottom = top + ToolbarRowPx();
        }
        else
        {
            // La zona di trascinamento è alta quanto i pulsanti della finestra (dalla cima della finestra).
            const bool haveDrag = m_dragNatural.bottom > m_dragNatural.top;
            bottom = haveDrag ? static_cast<int>(m_dragNatural.bottom) : top + MulDiv(32, static_cast<int>(Dpi()), 96);
        }
        return std::clamp(bottom - top, 0, std::max(0, contentTop - top));
    }

    RECT WindowLayout::PanelCaptionRect() const
    {
        const int height = CaptionHeightPx();
        if (!m_panelShown || height <= 0)
        {
            return {};
        }
        const int top = PanelTopPx();
        return { 0, top, PanelWidthPx(), top + height };
    }

    RECT WindowLayout::Wanted(HWND, Tracked const& tracked) const
    {
        RECT rect = tracked.natural;
        if (!Engaged())
        {
            return rect;
        }
        switch (tracked.role)
        {
        case Role::Content:
            // A destra della colonna, sotto la barra (il riquadro di spostamento lo nascondono le sink, dentro).
            rect.left = std::max(rect.left, static_cast<LONG>(m_active ? PanelWidthPx() : 0));
            rect.top += BarHeightPx() - TitleRowPx(); // sale con l'intestazione se la riga del titolo è nascosta
            rect.right = std::max(rect.right, rect.left);
            rect.bottom = std::max(rect.bottom, rect.top);
            break;
        case Role::DragBar:
            if (TitleHidden())
            {
                // Riga del titolo nascosta: solo i pulsanti della finestra più il tratto libero alla loro sinistra, alto
                // quanto la barra degli indirizzi (che a destra lascia loro questo spazio). Dalla cima della finestra:
                // risponde anche per il bordo superiore e per l'angolo.
                RECT client{};
                GetClientRect(m_main, &client);
                rect.right = client.right;
                rect.left = std::clamp(client.right - CaptionReservePx(), 0L, client.right);
                rect.left = std::max(rect.left, static_cast<LONG>(PanelWidthPx()));
                rect.top = 0;
                rect.bottom = std::max(static_cast<LONG>(m_headerNatural.top + ToolbarRowPx()), 1L);
            }
            else if (Raised())
            {
                // Tutta la riga del titolo a destra della colonna, comprese le schede orizzontali nascoste; a sinistra
                // c'è la fascia del titolo del pannello (che si trascina con HTCAPTION della finestra principale).
                rect.left = PanelWidthPx();
                rect.right = std::max(rect.right, rect.left);
            }
            else if (m_active)
            {
                rect.left = std::min(rect.left, 0L);
            }
            break;
        case Role::Header:
            if (Raised())
            {
                rect.left = std::max(rect.left, static_cast<LONG>(PanelWidthPx()));
                rect.right = std::max(rect.right, rect.left);
                // Riga del titolo nascosta: l'isola sale della sua altezza (la parte sopra resta fuori dalla finestra e
                // la regione la taglia): la barra degli indirizzi comincia dove cominciava l'isola.
                const LONG hidden = TitleRowPx();
                rect.top -= hidden;
                rect.bottom -= hidden;
            }
            break;
        }
        return rect;
    }

    void WindowLayout::Move(HWND child, RECT const& rect)
    {
        if (const auto it = m_tracked.find(child); it != m_tracked.end())
        {
            it->second.applied = rect;
            it->second.hasApplied = true;
        }
        if (SameRect(RectInParent(child), rect))
        {
            return;
        }
        const HWND previous = m_moving;
        m_moving = child;
        SetWindowPos(child, nullptr, rect.left, rect.top, rect.right - rect.left, rect.bottom - rect.top,
                     SWP_NOZORDER | SWP_NOACTIVATE | SWP_NOOWNERZORDER);
        m_moving = previous;
    }

    void WindowLayout::ApplyAll()
    {
        // Copia: gli spostamenti possono far arrivare notifiche che toccano la mappa.
        const std::vector<std::pair<HWND, Tracked>> all(m_tracked.begin(), m_tracked.end());
        for (auto const& [hwnd, tracked] : all)
        {
            if (IsWindow(hwnd))
            {
                Enforce(hwnd, tracked);
            }
        }
        TrackSinks();
        ApplySinks();
        PositionPanel();
    }

    void WindowLayout::OnChildPosChanging(HWND child, WINDOWPOS& pos)
    {
        if (child == m_moving)
        {
            return;
        }
        const auto it = m_tracked.find(child);
        if (it == m_tracked.end())
        {
            return;
        }
        // Cambio di scheda (il contenuto della nuova passa in cima): ognuna ha il suo riquadro di spostamento,
        // acceso o spento, e la barra segue quello della scheda attiva.
        if (it->second.role == Role::Content && !(pos.flags & SWP_NOZORDER) && m_barDip > 0)
        {
            RequestPanelUpdate();
        }
        // Esplora file rimette in cima la zona di trascinamento a ogni layout (SetWindowPos HWND_TOP con SWP_SHOWWINDOW): i
        // nostri pulsanti della finestra, nello stesso posto, resterebbero sotto finché non tornano su, e un clic in quel
        // momento trascinerebbe la finestra. In cima va subito sotto di loro (lo stesso per l'intestazione); PositionPanel li
        // rimette comunque in cima.
        if (it->second.role != Role::Content && !(pos.flags & SWP_NOZORDER) && m_buttons.Window())
        {
            if (pos.hwndInsertAfter == HWND_TOP && child != m_buttons.Window())
            {
                pos.hwndInsertAfter = m_buttons.Window();
            }
            RequestPanelUpdate();
        }
        if ((pos.flags & SWP_NOMOVE) && (pos.flags & SWP_NOSIZE))
        {
            return;
        }
        Tracked& tracked = it->second;

        // Nuovo rettangolo naturale: le parti indicate nella richiesta, il resto com'era.
        RECT natural = tracked.natural;
        const LONG width = natural.right - natural.left;
        const LONG height = natural.bottom - natural.top;
        if (!(pos.flags & SWP_NOMOVE))
        {
            natural = { pos.x, pos.y, pos.x + width, pos.y + height };
        }
        if (!(pos.flags & SWP_NOSIZE))
        {
            natural.right = natural.left + pos.cx;
            natural.bottom = natural.top + pos.cy;
        }
        const bool changed = !SameRect(natural, tracked.natural);
        tracked.natural = natural;
        if (changed)
        {
            if (tracked.role == Role::Content)
            {
                m_contentNatural = natural;
                m_haveContent = true;
            }
            else if (tracked.role == Role::Header)
            {
                m_headerNatural = natural;
            }
            else
            {
                m_dragNatural = natural; // altezza della fascia del titolo del pannello
            }
            RequestPanelUpdate();
        }
        if (!Engaged())
        {
            return;
        }
        const RECT wanted = Wanted(child, tracked);
        pos.x = wanted.left;
        pos.y = wanted.top;
        pos.cx = wanted.right - wanted.left;
        pos.cy = wanted.bottom - wanted.top;
        pos.flags &= ~(SWP_NOMOVE | SWP_NOSIZE);
        tracked.applied = wanted;
        tracked.hasApplied = true;
        if (tracked.role == Role::Header)
        {
            UpdateHeaderRegion(child, TitleRowPx());
        }
    }

    void WindowLayout::RequestPanelUpdate()
    {
        if (!m_updatePosted && m_main)
        {
            m_updatePosted = PostMessageW(m_main, UpdateMessage(), 0, 0) != FALSE;
        }
    }

    void WindowLayout::PositionBar()
    {
        if (!m_main || !m_bar || !IsWindow(m_bar))
        {
            return;
        }
        // Sotto l'intestazione, dove cominciava il contenuto, per tutta la sua larghezza (con la colonna in cima, dal suo
        // bordo, come l'intestazione); non con il riquadro di spostamento spento nella scheda attiva.
        const bool shown = BarOn() && m_haveContent && !IsIconic(m_main);
        if (shown)
        {
            const LONG left = Raised() ? std::max(m_contentNatural.left, static_cast<LONG>(PanelWidthPx())) : m_contentNatural.left;
            const LONG top = m_contentNatural.top - TitleRowPx(); // con la riga del titolo nascosta sale con l'intestazione
            const RECT rect{ left, top, std::max(m_contentNatural.right, left), top + BarHeightPx() };
            if (!SameRect(RectInParent(m_bar), rect))
            {
                SetWindowPos(m_bar, HWND_TOP, rect.left, rect.top, rect.right - rect.left, rect.bottom - rect.top, SWP_NOACTIVATE);
            }
        }
        if (shown != m_barShown)
        {
            m_barShown = shown;
            if (onBarVisibility)
            {
                onBarVisibility(shown);
            }
        }
    }

    void WindowLayout::UpdateGlass()
    {
        if (!m_main || !m_haveContent)
        {
            return;
        }
        // Zona vetro (Mica) della finestra: Esplora file la estende fino al fondo dell'intestazione, cioè fino all'inizio
        // naturale del contenuto. Con la riga del titolo nascosta il contenuto sale di quella riga e ci entrerebbe, e lì DWM
        // lo schiarisce (vedi Glass.h): la zona finisce dove comincia il contenuto. La barra delle cartelle, sopra di lui,
        // resta nel vetro come l'intestazione.
        const int contentTop = static_cast<int>(m_contentNatural.top) + BarHeightPx() - TitleRowPx();
        glass::SetTopLimit(m_main, TitleHidden() ? std::optional<int>(contentTop) : std::nullopt, static_cast<int>(m_contentNatural.top));
    }

    void WindowLayout::UpdateCaptionButtons()
    {
        if (!m_main || IsIconic(m_main))
        {
            return; // ridotta a icona: resta tutto com'è
        }
        RECT client{};
        GetClientRect(m_main, &client);
        if (client.right <= client.left)
        {
            return; // appena ripristinata da icona l'area client è vuota per un attimo: resta tutto com'è
        }
        // A destra della barra degli indirizzi (la prima riga dell'intestazione salita, sotto i 3 px del bordo superiore): i
        // pulsanti vanno dalla cima della finestra al suo fondo, con i glifi centrati su di lei.
        RECT row{};
        if (TitleHidden() && m_header && IsWindowVisible(m_header) && m_toolbarRowDip > 0)
        {
            row = { client.left, m_headerNatural.top, client.right, m_headerNatural.top + ToolbarRowPx() };
        }
        m_buttons.Update(m_main, row);
    }

    void WindowLayout::PositionPanel()
    {
        PositionBar();
        UpdateGlass();
        UpdateCaptionButtons();
        if (!m_main || !m_panel || !IsWindow(m_panel))
        {
            return;
        }
        const bool shown = m_active && m_haveContent && !IsIconic(m_main) && PanelWidthPx() > 0;
        if (shown)
        {
            // Dalla cima dell'intestazione (o da sotto, senza) al fondo del contenuto.
            const RECT rect{ 0, PanelTopPx(), PanelWidthPx(), m_contentNatural.bottom };
            if (!SameRect(RectInParent(m_panel), rect))
            {
                SetWindowPos(m_panel, HWND_TOP, rect.left, rect.top, rect.right - rect.left, rect.bottom - rect.top, SWP_NOACTIVATE);
            }
        }
        // Prima della visibilità: il pannello che compare ha già la sua fascia del titolo.
        const std::pair<int, int> geometry =
            Raised() && m_haveContent
                ? std::pair<int, int>{ CaptionHeightPx(), static_cast<int>(m_contentNatural.top) + BarHeightPx() - TitleRowPx() - PanelTopPx() }
                : std::pair<int, int>{ 0, 0 };
        if (geometry != m_geometry)
        {
            m_geometry = geometry;
            XPX_MLOG(L"WindowLayout: colonna %s (fascia del titolo %d px, schede da %d px; riga del titolo %s).",
                     geometry.first > 0 ? L"in cima alla finestra" : L"sotto l'intestazione", geometry.first, geometry.second,
                     TitleHidden() ? L"nascosta" : L"visibile");
            if (onPanelGeometry)
            {
                onPanelGeometry(geometry.first, geometry.second);
            }
        }
        if (shown != m_panelShown)
        {
            m_panelShown = shown;
            if (onPanelVisibility)
            {
                onPanelVisibility(shown);
            }
        }
        UpdateCaptionButtons(); // di nuovo in cima all'ordine Z, sopra il pannello appena spostato
    }

    LRESULT CALLBACK WindowLayout::ChildProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam, UINT_PTR id, DWORD_PTR refData)
    {
        auto* self = reinterpret_cast<WindowLayout*>(refData);
        switch (message)
        {
        case WM_WINDOWPOSCHANGING:
            self->OnChildPosChanging(hwnd, *reinterpret_cast<WINDOWPOS*>(lParam));
            break;
        case WM_CONTEXTMENU:
            if (lParam != -1 && self->onContextMenu)
            {
                const POINT point{ static_cast<short>(LOWORD(lParam)), static_cast<short>(HIWORD(lParam)) };
                if (self->onContextMenu(point))
                {
                    return 0;
                }
            }
            break;
        case WM_DESTROY:
            if (self->onContentDestroying)
            {
                if (const auto it = self->m_tracked.find(hwnd); it != self->m_tracked.end() && it->second.role == Role::Content)
                {
                    const auto callback = self->onContentDestroying;
                    callback(hwnd);
                }
            }
            break;
        case WM_NCDESTROY:
            // Prima lo stato, poi il subclass; DefSubclassProc per ultima (dopo non si tocca più self).
            if (hwnd == self->m_header)
            {
                self->m_header = nullptr;
                self->m_headerNatural = {};
                self->m_headerRegionTop = 0; // la regione se ne va con la finestra
            }
            else if (const auto it = self->m_tracked.find(hwnd); it != self->m_tracked.end() && it->second.role == Role::DragBar)
            {
                self->m_dragNatural = {};
            }
            self->m_tracked.erase(hwnd);
            RemoveWindowSubclass(hwnd, ChildProc, id);
            return DefSubclassProc(hwnd, message, wParam, lParam);
        default:
            break;
        }
        return DefSubclassProc(hwnd, message, wParam, lParam);
    }

    LRESULT CALLBACK WindowLayout::SinkProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam, UINT_PTR id, DWORD_PTR refData)
    {
        auto* self = reinterpret_cast<WindowLayout*>(refData);
        switch (message)
        {
        case WM_WINDOWPOSCHANGING:
            self->OnSinkPosChanging(hwnd, *reinterpret_cast<WINDOWPOS*>(lParam));
            break;
        case WM_WINDOWPOSCHANGED:
        {
            // Spostata (da noi o da DirectUI, che subito dopo le dà la regione del suo posto): regione adattata.
            const LRESULT result = DefSubclassProc(hwnd, message, wParam, lParam);
            if (const auto it = self->m_sinks.find(hwnd); it != self->m_sinks.end())
            {
                self->FixSinkRegion(hwnd, it->second);
            }
            return result;
        }
        case WM_NCDESTROY:
            // Prima lo stato, poi il subclass; DefSubclassProc per ultima.
            if (const auto it = self->m_sinks.find(hwnd); it != self->m_sinks.end())
            {
                if (it->second.duiRegion)
                {
                    DeleteObject(it->second.duiRegion);
                }
                self->m_sinks.erase(it);
            }
            RemoveWindowSubclass(hwnd, SinkProc, id);
            return DefSubclassProc(hwnd, message, wParam, lParam);
        default:
            break;
        }
        return DefSubclassProc(hwnd, message, wParam, lParam);
    }

    LRESULT CALLBACK WindowLayout::MainProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam, UINT_PTR id, DWORD_PTR refData)
    {
        auto* self = reinterpret_cast<WindowLayout*>(refData);
        if (message == UpdateMessage())
        {
            self->m_updatePosted = false;
            if (self->RefreshNavPane(L"subito"))
            {
                self->ApplyAll(); // comprende sink e PositionPanel
            }
            else
            {
                self->TrackSinks(); // (scheda nuova, riquadro o vista appena creati o spostati da DirectUI)
                self->ApplySinks();
                self->PositionPanel();
            }
            return 0;
        }
        if (self->onMenuMessage && (message == WM_INITMENUPOPUP || message == WM_DRAWITEM || message == WM_MEASUREITEM || message == WM_MENUCHAR))
        {
            LRESULT result = 0;
            if (self->onMenuMessage(message, wParam, lParam, result))
            {
                return result;
            }
        }
        switch (message)
        {
        case WM_NCHITTEST:
        {
            // Fascia del titolo in cima alla colonna: dove il pannello non c'è (la sua regione esclude la fascia, salvo i
            // pulsanti) la finestra si comporta come sulla sua barra del titolo. Bordi e pulsanti restano di Esplora file.
            const LRESULT result = DefSubclassProc(hwnd, message, wParam, lParam);
            if (result == HTCLIENT)
            {
                POINT point{ static_cast<short>(LOWORD(lParam)), static_cast<short>(HIWORD(lParam)) };
                ScreenToClient(hwnd, &point);
                const RECT caption = self->PanelCaptionRect();
                if (PtInRect(&caption, point))
                {
                    return HTCAPTION;
                }
            }
            return result;
        }
        case WM_CONTEXTMENU:
            if (lParam != -1 && self->onContextMenu)
            {
                const POINT point{ static_cast<short>(LOWORD(lParam)), static_cast<short>(HIWORD(lParam)) };
                if (self->onContextMenu(point))
                {
                    return 0;
                }
            }
            break;
        case WM_PARENTNOTIFY:
            // Una scheda nuova: il suo contenuto si aggancia appena creato, prima che si veda.
            if (LOWORD(wParam) == WM_CREATE)
            {
                const HWND child = reinterpret_cast<HWND>(lParam);
                if (GetParent(child) == hwnd)
                {
                    if (const auto role = RoleOf(child))
                    {
                        self->Track(child, *role);
                    }
                }
            }
            break;
        case WM_SIZE:
        {
            const LRESULT result = DefSubclassProc(hwnd, message, wParam, lParam);
            self->PositionPanel();
            return result;
        }
        case WM_DPICHANGED:
        {
            const LRESULT result = DefSubclassProc(hwnd, message, wParam, lParam);
            self->m_dpi = HIWORD(wParam);
            self->ApplyAll();
            return result;
        }
        case WM_NCACTIVATE:
        {
            // Pulsanti della finestra più chiari con la finestra non attiva, come quelli di DWM.
            const LRESULT result = DefSubclassProc(hwnd, message, wParam, lParam);
            self->m_buttons.SetActive(wParam != FALSE);
            return result;
        }
        case WM_SETTINGCHANGE:
        case WM_THEMECHANGED:
        case WM_SYSCOLORCHANGE:
        case WM_DWMCOLORIZATIONCOLORCHANGED:
        {
            const LRESULT result = DefSubclassProc(hwnd, message, wParam, lParam);
            self->m_buttons.ThemeChanged();
            return result;
        }
        case WM_DESTROY:
        {
            // Il callback può staccare il layout (e togliere questo subclass): dopo non si tocca più self.
            const auto callback = self->onMainDestroying;
            if (callback)
            {
                callback();
            }
            return DefSubclassProc(hwnd, message, wParam, lParam);
        }
        case WM_NCDESTROY:
            self->WatchNavPane(false);
            self->m_tracked.clear();
            self->ReleaseSinks(false); // (le figlie sono già distrutte: si libera solo la memoria)
            self->m_buttons.Remove(false);
            self->m_main = nullptr;
            self->m_panel = nullptr;
            self->m_bar = nullptr;
            self->m_header = nullptr;
            RemoveWindowSubclass(hwnd, MainProc, id);
            return DefSubclassProc(hwnd, message, wParam, lParam);
        default:
            break;
        }
        return DefSubclassProc(hwnd, message, wParam, lParam);
    }
}
