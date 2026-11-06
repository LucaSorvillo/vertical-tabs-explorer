#include "Veil.h"

#include "Common.h"

namespace xpx
{
    namespace
    {
        constexpr wchar_t kCabinetClass[] = L"CabinetWClass";
        constexpr UINT kTimerMs = 250;
        // Velata alla creazione ma non comparsa entro questo tempo: è una finestra che Esplora file prepara
        // in anticipo (nascosta) e che mostrerà più tardi, quando servirà.
        constexpr ULONGLONG kUnshownMs = 3000;
        // In ogni caso una finestra non resta velata più di così: se il ripristino va per le lunghe (molte
        // schede, cartelle di rete lente) è meglio vederla e lasciare che le ultime schede arrivino a vista.
        constexpr ULONGLONG kMaxVeiledMs = 10000;
        // Le distruzioni delle finestre si ricordano per poco: servono solo al controllo successivo.
        constexpr ULONGLONG kDestroyedMemoryMs = 30000;

        // Il gancio degli eventi non ha un parametro di contesto: c'è un solo velo.
        Veil* g_instance = nullptr;

        DWORD ShellProcessId()
        {
            DWORD process = 0;
            if (const HWND tray = FindWindowW(L"Shell_TrayWnd", nullptr))
            {
                GetWindowThreadProcessId(tray, &process);
            }
            return process;
        }

        // Finestra principale di Esplora file (top-level, classe CabinetWClass).
        bool IsExplorerFrame(HWND hwnd)
        {
            wchar_t name[32]{};
            return GetAncestor(hwnd, GA_PARENT) == GetDesktopWindow() && GetClassNameW(hwnd, name, ARRAYSIZE(name)) &&
                   wcscmp(name, kCabinetClass) == 0;
        }

        // Finestra principale (proprietaria) di una finestra: per un menu o una finestra di dialogo, quella che li ha aperti.
        HWND RootOwner(HWND hwnd)
        {
            const HWND root = hwnd ? GetAncestor(hwnd, GA_ROOTOWNER) : nullptr;
            return root ? root : hwnd;
        }

        DWORD ProcessOf(HWND hwnd)
        {
            DWORD process = 0;
            GetWindowThreadProcessId(hwnd, &process);
            return process;
        }

        bool IsExplorerProcess(DWORD process)
        {
            const HANDLE handle = process ? OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, process) : nullptr;
            if (!handle)
            {
                return false;
            }
            wchar_t image[MAX_PATH * 2]{};
            DWORD size = ARRAYSIZE(image);
            const bool ok = QueryFullProcessImageNameW(handle, 0, image, &size) != FALSE;
            CloseHandle(handle);
            if (!ok)
            {
                return false;
            }
            const wchar_t* name = wcsrchr(image, L'\\');
            return _wcsicmp(name ? name + 1 : image, L"explorer.exe") == 0;
        }
    }

    Veil::~Veil()
    {
        Stop();
    }

    void Veil::Start(std::function<void()> onShown)
    {
        m_onShown = std::move(onShown);
        g_instance = this;
        const HANDLE ready = CreateEventW(nullptr, TRUE, FALSE, nullptr);
        m_thread = std::thread([this, ready] { Run(ready); });
        WaitForSingleObject(ready, 5000);
        CloseHandle(ready);
    }

    void Veil::Stop()
    {
        if (m_thread.joinable())
        {
            PostThreadMessageW(m_threadId, WM_QUIT, 0, 0);
            m_thread.join();
        }
        LiftAll();
        if (g_instance == this)
        {
            g_instance = nullptr;
        }
    }

    void Veil::Run(HANDLE ready)
    {
        MSG message;
        PeekMessageW(&message, nullptr, WM_USER, WM_USER, PM_NOREMOVE); // crea la coda dei messaggi
        m_threadId = GetCurrentThreadId();
        SetEvent(ready);

        const UINT_PTR timer = SetTimer(nullptr, 0, kTimerMs, nullptr);
        OnTimer(); // aggancio subito
        // Primo piano di tutto il sistema (cartelle in una scheda: chi c'era prima di una finestra nuova, e quale finestra di
        // Esplora file è stata usata per ultima).
        OnForeground(GetForegroundWindow(), GetTickCount64());
        m_foregroundHook = SetWinEventHook(EVENT_SYSTEM_FOREGROUND, EVENT_SYSTEM_FOREGROUND, nullptr, OnEvent, 0, 0,
                                           WINEVENT_OUTOFCONTEXT | WINEVENT_SKIPOWNPROCESS);
        // Comparsa delle finestre in tutto il sistema: le finestre di Esplora file possono nascere anche in un explorer.exe
        // diverso da quello della barra delle applicazioni (es. aperte da un programma con un altro contesto di sicurezza:
        // Windows le apre in un processo "/factory" a sé). Quelle del processo della barra arrivano già dal suo gancio.
        m_showHook = SetWinEventHook(EVENT_OBJECT_SHOW, EVENT_OBJECT_SHOW, nullptr, OnEvent, 0, 0,
                                     WINEVENT_OUTOFCONTEXT | WINEVENT_SKIPOWNPROCESS);
        while (GetMessageW(&message, nullptr, 0, 0) > 0)
        {
            if (message.message == WM_TIMER && message.hwnd == nullptr)
            {
                OnTimer();
                continue;
            }
            TranslateMessage(&message);
            DispatchMessageW(&message);
        }
        KillTimer(nullptr, timer);
        for (HWINEVENTHOOK* hook : { &m_hook, &m_foregroundHook, &m_showHook })
        {
            if (*hook)
            {
                UnhookWinEvent(*hook);
                *hook = nullptr;
            }
        }
    }

    void CALLBACK Veil::OnEvent(HWINEVENTHOOK hook, DWORD event, HWND hwnd, LONG object, LONG child, DWORD, DWORD time)
    {
        if (object == OBJID_WINDOW && child == CHILDID_SELF && hwnd && g_instance)
        {
            // Gancio di tutto il sistema: solo le finestre principali di Esplora file degli altri explorer.exe (quelle del
            // processo della barra delle applicazioni arrivano dal suo gancio). Il filtro sulla classe viene prima: costa poco
            // e scarta quasi tutto.
            if (hook == g_instance->m_showHook && hook &&
                (!IsExplorerFrame(hwnd) || (g_instance->m_hook && ProcessOf(hwnd) == g_instance->m_hookedProcess) ||
                 !IsExplorerProcess(ProcessOf(hwnd))))
            {
                return;
            }
            // Momento dell'evento (non della sua consegna, che può arrivare dopo).
            const ULONGLONG now = GetTickCount64();
            const DWORD age = GetTickCount() - time;
            g_instance->Handle(event, hwnd, age < now ? now - age : now);
        }
    }

    void Veil::Handle(DWORD event, HWND hwnd, ULONGLONG when)
    {
        if (event == EVENT_SYSTEM_FOREGROUND)
        {
            OnForeground(hwnd, when);
            return;
        }
        if (event == EVENT_OBJECT_DESTROY)
        {
            std::scoped_lock lock(m_lock);
            m_entries.erase(hwnd);
            m_candidates.erase(hwnd);
            m_activated.erase(hwnd);
            m_destroyed[hwnd] = when;
            return;
        }
        if ((event != EVENT_OBJECT_CREATE && event != EVENT_OBJECT_SHOW) || !IsExplorerFrame(hwnd))
        {
            return;
        }
        if (IsVerbose())
        {
            const HWND foreground = GetForegroundWindow();
            wchar_t name[64]{};
            GetClassNameW(RootOwner(foreground), name, ARRAYSIZE(name));
            XPX_VLOG(L"Velo: %s %p (processo %lu%s, visibile %d, stile esteso %08X); in primo piano %p (%s); scheda: armato %d, esterna %d, altre finestre %d.",
                     event == EVENT_OBJECT_CREATE ? L"creata" : L"comparsa", hwnd, ProcessOf(hwnd),
                     ProcessOf(hwnd) == m_hookedProcess ? L" della barra" : L"", IsWindowVisible(hwnd) ? 1 : 0,
                     static_cast<unsigned>(GetWindowLongPtrW(hwnd, GWL_EXSTYLE)), foreground, name, m_mergeArmed.load() ? 1 : 0,
                     IsExternalRequest(hwnd) ? 1 : 0, OtherFrameOpen(hwnd) ? 1 : 0);
        }
        if (event == EVENT_OBJECT_CREATE)
        {
            if (ShouldVeil() && Cover(hwnd, false))
            {
                XPX_VLOG(L"Velo: finestra di Esplora file velata appena creata.");
            }
            else if (WantsMerge(hwnd) && Cover(hwnd, false))
            {
                {
                    std::scoped_lock lock(m_lock);
                    m_candidates[hwnd] = when;
                }
                XPX_VLOG(L"Velo: finestra di Esplora file aperta da un altro programma, velata appena creata (andrà in una scheda).");
            }
            return;
        }

        // Comparsa
        bool notify = false;
        bool recheck = false;
        {
            std::scoped_lock lock(m_lock);
            const auto entry = m_entries.find(hwnd);
            if (entry != m_entries.end() && !entry->second.shown)
            {
                entry->second.shown = true;
                entry->second.shownTick = GetTickCount64();
                notify = true;
                recheck = m_candidates.contains(hwnd);
            }
            else if (entry != m_entries.end())
            {
                return;
            }
        }
        // Candidata dalla creazione: la richiesta vera è quella per cui compare (Esplora file crea in anticipo una finestra
        // nascosta e la mostra alla richiesta dopo, che può essere un Ctrl+N).
        if (recheck && !WantsMerge(hwnd))
        {
            if (ShouldVeil())
            {
                ForgetMergeCandidate(hwnd); // resta velata per il ripristino
            }
            else
            {
                Lift(hwnd);
                XPX_VLOG(L"Velo: la finestra velata alla creazione è comparsa per una richiesta di Esplora file: resta una finestra.");
                return;
            }
        }
        if (!notify && ShouldVeil() && Cover(hwnd, true))
        {
            XPX_VLOG(L"Velo: finestra di Esplora file velata alla comparsa (era già pronta).");
            notify = true;
        }
        else if (!notify && WantsMerge(hwnd) && Cover(hwnd, true))
        {
            // Esplora file teneva pronta una finestra nascosta e ha mostrato quella.
            {
                std::scoped_lock lock(m_lock);
                m_candidates[hwnd] = when;
            }
            XPX_VLOG(L"Velo: finestra di Esplora file aperta da un altro programma, velata alla comparsa (andrà in una scheda).");
            notify = true;
        }
        if (notify && m_onShown)
        {
            m_onShown();
        }
    }

    void Veil::OnTimer()
    {
        // Esplora file riavviato (o non ancora partito): il gancio va sul suo nuovo processo.
        const DWORD process = ShellProcessId();
        if (process != m_hookedProcess)
        {
            if (m_hook)
            {
                UnhookWinEvent(m_hook);
                m_hook = nullptr;
            }
            m_hookedProcess = process;
            if (process)
            {
                // Creazione, distruzione e comparsa delle finestre (EVENT_OBJECT_CREATE..EVENT_OBJECT_SHOW).
                m_hook = SetWinEventHook(EVENT_OBJECT_CREATE, EVENT_OBJECT_SHOW, nullptr, OnEvent, process, 0, WINEVENT_OUTOFCONTEXT);
                XPX_VLOG(L"Velo: gancio sul processo di Esplora file %lu %s.", process, m_hook ? L"attivo" : L"non riuscito");
            }
        }

        // Veli scaduti
        const ULONGLONG now = GetTickCount64();
        std::vector<HWND> expired;
        {
            std::scoped_lock lock(m_lock);
            std::erase_if(m_entries, [](auto const& entry) { return !IsWindow(entry.first); });
            std::erase_if(m_candidates, [](auto const& entry) { return !IsWindow(entry.first); });
            std::erase_if(m_activated, [](auto const& entry) { return !IsWindow(entry.first); });
            std::erase_if(m_destroyed, [now](auto const& entry) { return now - entry.second > kDestroyedMemoryMs; });
            for (auto const& [window, entry] : m_entries)
            {
                if ((!entry.shown && now - entry.veiledTick > kUnshownMs) || (entry.shown && now - entry.shownTick > kMaxVeiledMs))
                {
                    expired.push_back(window);
                }
            }
        }
        for (HWND window : expired)
        {
            const bool shown = IsWindowVisible(window) != FALSE;
            Lift(window);
            if (shown)
            {
                Log(L"Velo tolto dopo il tempo massimo.");
            }
            else
            {
                XPX_VLOG(L"Velo tolto a una finestra preparata in anticipo da Esplora file.");
            }
        }
    }

    void Veil::Arm(bool armed, std::wstring const& marker)
    {
        {
            std::scoped_lock lock(m_lock);
            m_marker = marker;
        }
        m_armed = armed;
    }

    void Veil::SetRestoring(bool restoring)
    {
        m_restoring = restoring;
    }

    void Veil::SetMergeArmed(bool armed, bool forceExternal)
    {
        m_mergeArmed = armed;
        m_forceExternal = forceExternal;
    }

    std::vector<HWND> Veil::MergeCandidates() const
    {
        std::vector<HWND> result;
        std::scoped_lock lock(m_lock);
        for (auto const& [window, since] : m_candidates)
        {
            const auto entry = m_entries.find(window);
            if (entry != m_entries.end() && entry->second.shown)
            {
                result.push_back(window);
            }
        }
        return result;
    }

    bool Veil::IsMergeCandidate(HWND window) const
    {
        std::scoped_lock lock(m_lock);
        return m_candidates.contains(window);
    }

    void Veil::ForgetMergeCandidate(HWND window)
    {
        std::scoped_lock lock(m_lock);
        m_candidates.erase(window);
    }

    ULONGLONG Veil::LastActivated(HWND window) const
    {
        std::scoped_lock lock(m_lock);
        const auto found = m_activated.find(window);
        return found != m_activated.end() ? found->second : 0;
    }

    void Veil::OnForeground(HWND window, ULONGLONG when)
    {
        // Vale la finestra principale: Esplora file segnala il primo piano anche sulla scheda attiva (figlia).
        const HWND root = window ? GetAncestor(window, GA_ROOT) : nullptr;
        if (!root)
        {
            return;
        }
        const bool frame = IsExplorerFrame(root);
        std::scoped_lock lock(m_lock);
        if (root != m_foreground)
        {
            m_previousForeground = m_foreground;
            m_foreground = root;
        }
        if (frame)
        {
            m_activated[root] = when;
        }
    }

    bool Veil::WantsMerge(HWND window) const
    {
        return m_mergeArmed && !m_restoring && IsExternalRequest(window) && OtherFrameOpen(window);
    }

    bool Veil::IsExternalRequest(HWND window) const
    {
        if (m_forceExternal)
        {
            return true;
        }
        // Chi ha chiesto la finestra è in primo piano quando nasce (Windows la attiva subito dopo); se il primo piano è già
        // passato a lei, conta quello di prima.
        HWND requester = RootOwner(GetForegroundWindow());
        if (!requester || requester == window)
        {
            std::scoped_lock lock(m_lock);
            requester = m_foreground != window ? m_foreground : m_previousForeground;
            requester = RootOwner(requester);
        }
        // Una finestra di Esplora file (Ctrl+N, "Apri in una nuova finestra", la nostra barra delle cartelle): resta una
        // finestra. Tutto il resto (altri programmi, desktop, barra delle applicazioni, Start) va in una scheda.
        return !(requester && requester != window && IsWindow(requester) && IsExplorerFrame(requester));
    }

    bool Veil::OtherFrameOpen(HWND window)
    {
        for (HWND other = FindWindowExW(nullptr, nullptr, kCabinetClass, nullptr); other; other = FindWindowExW(nullptr, other, kCabinetClass, nullptr))
        {
            if (other != window && IsWindowVisible(other))
            {
                return true;
            }
        }
        return false;
    }

    bool Veil::ShouldVeil() const
    {
        if (m_restoring)
        {
            return true;
        }
        if (!m_armed)
        {
            return false;
        }
        std::wstring marker;
        {
            std::scoped_lock lock(m_lock);
            marker = m_marker;
        }
        return marker.empty() || GetFileAttributesW(marker.c_str()) != INVALID_FILE_ATTRIBUTES;
    }

    bool Veil::Cover(HWND window, bool shown)
    {
        // Già trasparente per conto suo: non si tocca (non si saprebbe come rimetterla).
        const LONG_PTR style = GetWindowLongPtrW(window, GWL_EXSTYLE);
        if (style & WS_EX_LAYERED)
        {
            return false;
        }
        // Una finestra layered non compare finché non riceve la sua opacità: nessun istante visibile.
        SetWindowLongPtrW(window, GWL_EXSTYLE, style | WS_EX_LAYERED);
        SetLayeredWindowAttributes(window, 0, 0, LWA_ALPHA);
        const ULONGLONG now = GetTickCount64();
        std::scoped_lock lock(m_lock);
        m_entries[window] = Entry{ now, shown, shown ? now : 0 };
        return true;
    }

    void Veil::Uncover(HWND window)
    {
        if (IsWindow(window))
        {
            SetWindowLongPtrW(window, GWL_EXSTYLE, GetWindowLongPtrW(window, GWL_EXSTYLE) & ~static_cast<LONG_PTR>(WS_EX_LAYERED));
        }
    }

    ULONGLONG Veil::DestroyedAt(HWND window) const
    {
        std::scoped_lock lock(m_lock);
        const auto found = m_destroyed.find(window);
        return found != m_destroyed.end() ? found->second : 0;
    }

    bool Veil::IsVeiled(HWND window) const
    {
        std::scoped_lock lock(m_lock);
        return m_entries.contains(window);
    }

    std::vector<HWND> Veil::Shown() const
    {
        std::vector<HWND> result;
        std::scoped_lock lock(m_lock);
        for (auto const& [window, entry] : m_entries)
        {
            if (entry.shown)
            {
                result.push_back(window);
            }
        }
        return result;
    }

    void Veil::Lift(HWND window)
    {
        {
            std::scoped_lock lock(m_lock);
            m_candidates.erase(window);
            if (m_entries.erase(window) == 0)
            {
                return;
            }
        }
        Uncover(window);
    }

    void Veil::LiftAll()
    {
        std::vector<HWND> windows;
        {
            std::scoped_lock lock(m_lock);
            for (auto const& [window, entry] : m_entries)
            {
                windows.push_back(window);
            }
            m_entries.clear();
            m_candidates.clear();
        }
        for (HWND window : windows)
        {
            Uncover(window);
        }
    }

    void Veil::EmergencyLift()
    {
        // Senza lock: il thread che lo tiene potrebbe essere quello andato in errore.
        if (m_lock.try_lock())
        {
            for (auto const& [window, entry] : m_entries)
            {
                Uncover(window);
            }
            m_entries.clear();
            m_lock.unlock();
        }
        else
        {
            for (auto const& [window, entry] : m_entries)
            {
                Uncover(window);
            }
        }
    }
}
