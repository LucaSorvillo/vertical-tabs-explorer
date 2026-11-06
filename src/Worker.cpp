#include "Worker.h"

#include "Common.h"
#include "ModShared.h"
#include "TabColorFiles.h"

#include <algorithm>

namespace xpx
{
    namespace
    {
        // Ogni mezzo secondo un controllo locale, che non disturba Esplora file e costa microsecondi:
        // finestre, schede (finestre ShellTabWindowClass) e loro titoli, nell'ordine Z. Se qualcosa cambia
        // (finestra aperta o chiusa, scheda nuova o chiusa, cambio di cartella o di scheda attiva) si
        // rileggono le schede con le chiamate a Esplora file; altrimenti si aggiornano solo le posizioni.
        // In più una rilettura completa ogni tanto, per ciò che non cambia i titoli (schede riordinate).
        constexpr DWORD kLocalCheckMs = 500;
        constexpr ULONGLONG kPollMs = 30000;

        // Stato locale delle finestre di Esplora file (vedi sopra).
        std::vector<std::pair<HWND, std::wstring>> LocalState()
        {
            std::vector<std::pair<HWND, std::wstring>> state;
            const auto title = [](HWND hwnd) {
                // Per le finestre di un altro processo il titolo arriva dalla copia del sistema, senza messaggi.
                wchar_t text[512]{};
                GetWindowTextW(hwnd, text, ARRAYSIZE(text));
                return std::wstring(text);
            };
            for (HWND window = FindWindowExW(nullptr, nullptr, L"CabinetWClass", nullptr); window;
                 window = FindWindowExW(nullptr, window, L"CabinetWClass", nullptr))
            {
                if (!IsWindowVisible(window))
                {
                    continue;
                }
                state.emplace_back(window, title(window));
                for (HWND tab = FindWindowExW(window, nullptr, L"ShellTabWindowClass", nullptr); tab;
                     tab = FindWindowExW(window, tab, L"ShellTabWindowClass", nullptr))
                {
                    state.emplace_back(tab, title(tab));
                }
            }
            return state;
        }
        constexpr DWORD kDebounceMs = 250;        // gli eventi delle finestre arrivano a raffiche
        constexpr ULONGLONG kTogetherMs = 5000;   // finestre chiuse "insieme" all'ultima
        constexpr ULONGLONG kFleetingMs = 2000;   // finestra di passaggio durante una chiusura (vedi ClosedTogether)
        constexpr ULONGLONG kFreshWindowMs = 15'000; // il ripristino scatta solo per una finestra comparsa da poco
        constexpr ULONGLONG kStartupGraceMs = 90'000;
        constexpr ULONGLONG kVeiledReadyMs = 3000; // attesa massima per leggere la scheda di una finestra velata
        constexpr size_t kMaxWindows = 16;        // limiti di sicurezza per il ripristino
        constexpr size_t kMaxTabs = 40;
        constexpr size_t kMaxReopen = 25;         // schede e finestre chiuse ricordate per Ctrl+Maiusc+T
        constexpr int kMaxPendingReopen = 10;     // pressioni in coda servite al massimo (il resto si scarta)

        // Finestra di Esplora file ancora esistente (un HWND può essere riciclato da un'altra finestra).
        bool IsExplorerFrame(HWND window)
        {
            if (!window || !IsWindow(window))
            {
                return false;
            }
            wchar_t name[64]{};
            GetClassNameW(window, name, ARRAYSIZE(name));
            return EqualsIgnoreCase(name, L"CabinetWClass");
        }

        // Stesse schede, nello stesso ordine (per riconoscere nella pila una finestra tornata con la sessione).
        bool SameTabs(Window const& a, Window const& b)
        {
            return a.tabs.size() == b.tabs.size() && std::equal(a.tabs.begin(), a.tabs.end(), b.tabs.begin(), [](Tab const& x, Tab const& y) {
                       return EqualsIgnoreCase(x.path, y.path);
                   });
        }

        // Quante schede ha adesso una finestra (controllo locale, senza chiamare Esplora file: le schede
        // appena aperte si registrano nella shell solo dopo aver caricato la prima cartella).
        int TabCount(HWND window)
        {
            int count = 0;
            for (HWND tab = FindWindowExW(window, nullptr, L"ShellTabWindowClass", nullptr); tab;
                 tab = FindWindowExW(window, tab, L"ShellTabWindowClass", nullptr))
            {
                ++count;
            }
            return count;
        }

        // Solo ciò che si salva (niente HWND, titoli).
        Window Stored(Window const& live)
        {
            Window window;
            window.active = live.active;
            window.placement = live.placement;
            for (auto const& tab : live.tabs)
            {
                window.tabs.push_back(Tab{ tab.path, tab.idList, {}, nullptr, tab.color });
            }
            return window;
        }

        // Colori da rimettere nelle schede appena create in una finestra (dopo BuildTabs): tabs nell'ordine in
        // cui sono nella barra a partire dalla posizione first, tranne gli indici in missing (schede non
        // riaperte). La mod li applica quando la finestra ha le schede che ha adesso.
        // moveTo >= 0: la scheda appena aggiunta in posizione first va spostata lì (Ctrl+Maiusc+T: torna al suo posto). Lo fa
        // la mod insieme ai colori (schede verticali); senza la mod la scheda resta in fondo.
        void QueueTabColors(HWND window, std::vector<Tab> const& tabs, size_t first, std::vector<size_t> const& missing = {}, int moveTo = -1)
        {
            TabColorList list;
            list.count = static_cast<size_t>(TabCount(window));
            if (moveTo >= 0 && static_cast<size_t>(moveTo) != first)
            {
                list.moveFrom = static_cast<int>(first);
                list.moveTo = moveTo;
            }
            for (size_t i = 0; i < tabs.size(); ++i)
            {
                if (tabs[i].color > 0 && std::find(missing.begin(), missing.end(), i) == missing.end())
                {
                    list.colored.push_back(TabColorEntry{ first + i, tabs[i].color, {} });
                }
            }
            WritePendingColors(window, list);
        }

        // Esito della riapertura di una finestra salvata: la finestra (nullptr se non si è aperta) e le
        // schede che non è stato possibile riaprire (tutte, se la finestra non si è aperta).
        struct Reopened
        {
            Window saved;
            HWND window = nullptr;
            std::vector<Tab> missing;
        };

        Session FromLive(std::vector<Window> const& live)
        {
            Session session;
            for (auto const& window : live)
            {
                session.windows.push_back(Stored(window));
            }
            return session;
        }

        // Windows può riaprire da sé le cartelle all'accesso ("Ripristina le finestre delle cartelle
        // aperte all'accesso").
        bool PersistBrowsersEnabled()
        {
            DWORD value = 0;
            DWORD size = sizeof(value);
            return RegGetValueW(HKEY_CURRENT_USER, L"Software\\Microsoft\\Windows\\CurrentVersion\\Explorer\\Advanced", L"PersistBrowsers",
                                RRF_RT_REG_DWORD, nullptr, &value, &size) == ERROR_SUCCESS &&
                   value != 0;
        }

        // Schede ancora utilizzabili di una finestra salvata e indice della scheda attiva tra queste.
        std::vector<Tab> RestorableTabs(Window const& saved, int& active)
        {
            std::vector<Tab> tabs;
            active = 0;
            for (size_t i = 0; i < saved.tabs.size() && tabs.size() < kMaxTabs; ++i)
            {
                if (!Explorer::IsRestorable(saved.tabs[i]))
                {
                    continue;
                }
                if (static_cast<int>(i) == saved.active)
                {
                    active = static_cast<int>(tabs.size());
                }
                tabs.push_back(saved.tabs[i]);
            }
            return tabs;
        }

        // Riapre una finestra salvata: nuova finestra sulla prima scheda, le altre schede, scheda attiva,
        // posizione.
        Reopened RestoreWindowWith(Explorer& explorer, Window const& saved)
        {
            Reopened result;
            result.saved = saved;
            int active = 0;
            const auto tabs = RestorableTabs(saved, active);
            if (tabs.empty())
            {
                return result;
            }
            result.window = explorer.OpenWindow(tabs.front());
            if (!result.window)
            {
                result.missing = tabs;
                return result;
            }
            std::vector<size_t> missing;
            explorer.BuildTabs(result.window, std::vector<Tab>(tabs.begin() + 1, tabs.end()), false, active, &missing);
            for (size_t& index : missing)
            {
                index += 1; // indici in tabs (la prima scheda è quella con cui si è aperta la finestra)
                result.missing.push_back(tabs[index]);
            }
            explorer.Place(result.window, saved.placement);
            QueueTabColors(result.window, tabs, 0, missing);
            return result;
        }

        // Esplora file ha un thread per ogni finestra: più finestre si riaprono in parallelo, ognuna su
        // un thread nostro con il suo collegamento (COM STA). Si attendono con Join() (comunque alla fine).
        class ParallelRestore
        {
        public:
            explicit ParallelRestore(std::vector<Window> windows) : m_results(windows.size())
            {
                for (size_t i = 0; i < windows.size(); ++i)
                {
                    m_threads.emplace_back([this, i, saved = std::move(windows[i])] {
                        const HRESULT init = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);
                        try
                        {
                            Explorer explorer;
                            m_results[i] = RestoreWindowWith(explorer, saved); // ogni thread ha il suo posto
                        }
                        catch (...)
                        {
                            Log(L"Errore inatteso riaprendo una finestra.");
                        }
                        if (SUCCEEDED(init))
                        {
                            CoUninitialize();
                        }
                    });
                }
            }
            ~ParallelRestore() { Join(); }
            ParallelRestore(ParallelRestore const&) = delete;
            ParallelRestore& operator=(ParallelRestore const&) = delete;

            void Join()
            {
                for (auto& thread : m_threads)
                {
                    thread.join();
                }
                m_threads.clear();
            }

            // L'esito di ogni finestra (dopo che tutte hanno finito).
            std::vector<Reopened> const& Results()
            {
                Join();
                return m_results;
            }

        private:
            std::vector<Reopened> m_results;
            std::vector<std::thread> m_threads;
        };

        // Durante un ripristino il velo copre ogni finestra nuova; alla fine (anche se qualcosa va storto)
        // tutte le finestre velate tornano visibili insieme, già complete.
        class VeiledRestore
        {
        public:
            explicit VeiledRestore(Veil& veil) : m_veil(veil) { m_veil.SetRestoring(true); }
            ~VeiledRestore() { Finish(); }
            VeiledRestore(VeiledRestore const&) = delete;
            VeiledRestore& operator=(VeiledRestore const&) = delete;

            void Finish()
            {
                if (!m_finished)
                {
                    m_finished = true;
                    m_veil.SetRestoring(false);
                    m_veil.LiftAll();
                }
            }

        private:
            Veil& m_veil;
            bool m_finished = false;
        };
    }

    Worker::~Worker()
    {
        Stop();
    }

    void Worker::Start(std::function<void(std::wstring const&)> notify)
    {
        m_notify = std::move(notify);
        m_stop = CreateEventW(nullptr, TRUE, FALSE, nullptr);
        m_wake = CreateEventW(nullptr, FALSE, FALSE, nullptr);
        m_veil.Start([this] {
            m_veiledShown = true;
            Wake();
        });
        const Settings settings = Settings::Load();
        m_backToTop.SetEnabled(settings.backToTop);
        m_backToTop.Start([this](HWND window) { return AcceptsForFeatures(window); });
        m_hotkey.SetEnabled(settings.reopenTab);
        m_hotkey.Start([this](HWND window) { return AcceptsForFeatures(window); }, [this](HWND window) { RequestReopen(window); });
        m_thread = std::thread([this] { Run(); });
    }

    void Worker::Stop()
    {
        if (m_thread.joinable())
        {
            SetEvent(m_stop);
            m_thread.join();
        }
        m_hotkey.Stop();
        m_backToTop.Stop();
        m_veil.Stop();
        if (m_stop)
        {
            CloseHandle(m_stop);
            m_stop = nullptr;
        }
        if (m_wake)
        {
            CloseHandle(m_wake);
            m_wake = nullptr;
        }
    }

    void Worker::Wake()
    {
        if (m_wake)
        {
            SetEvent(m_wake);
        }
    }

    void Worker::RequestRestore()
    {
        m_restoreRequested = true;
        Wake();
    }

    void Worker::RequestReopen(HWND window)
    {
        // Ogni pressione conta: una riapertura può durare qualche secondo e intanto se ne possono
        // chiedere altre (nel browser premendo tre volte tornano tre schede).
        m_reopenWindow = window;
        if (m_reopenRequests.fetch_add(1) > kMaxPendingReopen)
        {
            m_reopenRequests = kMaxPendingReopen;
        }
        Wake();
    }

    void Worker::SetEnding(bool ending)
    {
        m_ending = ending;
        Log(ending ? L"Fine della sessione di Windows: sessione salvata bloccata." : L"Fine della sessione di Windows annullata.");
    }

    Worker::Status Worker::GetStatus() const
    {
        std::scoped_lock lock(m_statusLock);
        return m_status;
    }

    void Worker::EmergencyLift()
    {
        m_veil.EmergencyLift();
    }

    bool Worker::AcceptsForFeatures(HWND window) const
    {
        std::scoped_lock lock(m_statusLock);
        // In modalità normale tutte le finestre (anche una appena aperta, non ancora letta); in modalità
        // prova solo quelle di prova, che sono le uniche considerate.
        return !m_status.devMode || std::find(m_publishedWindows.begin(), m_publishedWindows.end(), window) != m_publishedWindows.end();
    }

    void Worker::Publish()
    {
        std::scoped_lock lock(m_statusLock);
        m_status.liveWindows = m_live.size();
        m_status.savedWindows = m_session.windows.size();
        m_status.savedTabs = m_session.TabCount();
        m_status.devMode = !m_root.empty();
        m_publishedWindows.clear();
        for (auto const& window : m_live)
        {
            m_publishedWindows.push_back(window.hwnd);
        }
    }

    void Worker::Run()
    {
        // STA: i verbi della shell (ShellExecuteEx) lo richiedono.
        const HRESULT init = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);
        m_explorer = std::make_unique<Explorer>();
        m_startTick = GetTickCount64();

        ULONGLONG lastTick = 0;
        auto lastState = LocalState();
        const auto devFilterPresent = [] { return GetFileAttributesW(DevFilterPath().c_str()) != INVALID_FILE_ATTRIBUTES; };
        bool lastDevFilter = devFilterPresent();
        // Impostazioni cambiate (menu della tray o a mano): il velo va aggiornato subito, non al prossimo controllo.
        const std::wstring settingsPath = SettingsPath();
        const auto settingsStamp = [&settingsPath] {
            WIN32_FILE_ATTRIBUTE_DATA data{};
            return GetFileAttributesExW(settingsPath.c_str(), GetFileExInfoStandard, &data)
                       ? (static_cast<ULONGLONG>(data.ftLastWriteTime.dwHighDateTime) << 32) | data.ftLastWriteTime.dwLowDateTime
                       : 0;
        };
        ULONGLONG lastSettings = settingsStamp();
        // Colori delle schede pubblicati dalla mod (schede verticali): un cambiamento non tocca titoli e
        // ordine Z, quindi si guarda la cartella dei colori (una lettura di attributi a controllo).
        CleanupColorFiles();
        ULONGLONG lastColors = ColorsStamp();
        const HANDLE handles[] = { m_stop, m_wake };
        for (;;)
        {
            const DWORD wait = lastTick == 0 ? 0 : kLocalCheckMs;
            const DWORD result = MsgWaitForMultipleObjectsEx(2, handles, wait, QS_ALLINPUT, MWMO_INPUTAVAILABLE);
            if (result == WAIT_OBJECT_0)
            {
                break;
            }
            if (result == WAIT_OBJECT_0 + 2)
            {
                MSG message;
                while (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE))
                {
                    TranslateMessage(&message);
                    DispatchMessageW(&message);
                }
                continue;
            }
            try
            {
                const bool veiled = m_veiledShown.exchange(false); // comparsa una finestra velata
                auto state = LocalState();
                const bool changed = state != lastState;
                const bool devFilter = devFilterPresent(); // modalità prova accesa o spenta
                const bool modeChanged = devFilter != lastDevFilter;
                lastDevFilter = devFilter;
                const ULONGLONG colors = ColorsStamp();
                const bool colorsChanged = colors != lastColors;
                lastColors = colors;
                const bool colorsRetry = std::exchange(m_colorsRetry, false);
                if (colorsChanged || colorsRetry)
                {
                    // Colori pubblicati di nuovo (es. dopo un riordino dal pannello): l'ordine delle schede si
                    // rilegge dalla barra invece di usare quello tenuto da parte.
                    m_explorer->ForgetOrder();
                }
                if (veiled || changed || modeChanged || colorsChanged || colorsRetry || result == WAIT_OBJECT_0 + 1 || lastTick == 0 ||
                    GetTickCount64() - lastTick >= kPollMs)
                {
                    if (veiled)
                    {
                        // Nessuna pausa: la finestra aspetta invisibile, appena se ne legge la scheda si decide.
                        WaitForVeiled();
                        MergeIntoTabs(); // aperte da un altro programma: in una scheda (prima che entrino nella sessione)
                        state = LocalState();
                    }
                    else if (changed && lastTick != 0)
                    {
                        Sleep(kDebounceMs); // i cambiamenti arrivano a raffiche (finestra che si apre, schede)
                        state = LocalState();
                    }
                    Tick();
                    // Finestre velate già considerate e non ripristinate (es. aperte con altre finestre già
                    // aperte): tornano subito visibili.
                    for (HWND window : m_veil.Shown())
                    {
                        if (std::find(m_considered.begin(), m_considered.end(), window) != m_considered.end())
                        {
                            m_veil.Lift(window);
                        }
                    }
                    lastTick = GetTickCount64();
                    lastState = std::move(state);
                }
                else
                {
                    RefreshPlacements();
                }
                if (const ULONGLONG stamp = settingsStamp(); stamp != lastSettings)
                {
                    lastSettings = stamp;
                    const Settings settings = Settings::Load();
                    m_backToTop.SetEnabled(settings.backToTop);
                    m_hotkey.SetEnabled(settings.reopenTab);
                    if (m_modeReady)
                    {
                        UpdateVeil(settings);
                    }
                }
                if (m_restoreRequested.exchange(false))
                {
                    RestoreAll();
                }
                // Una pressione per volta, finché ce ne sono: se non c'è più niente da riaprire le
                // altre si lasciano cadere.
                for (int pending = m_reopenRequests.exchange(0); pending > 0; --pending)
                {
                    if (!ReopenClosed(m_reopenWindow.load()))
                    {
                        break;
                    }
                }
                if (m_sessionUnsaved)
                {
                    const Session unsaved = m_session; // l'ultimo salvataggio non è riuscito: si riprova
                    UpdateSession(unsaved);
                }
                SaveReopenStack(); // pila di Ctrl+Maiusc+T cambiata (chiusure, riaperture, ripristini): su disco
            }
            catch (...)
            {
                Log(L"Errore inatteso durante il controllo delle finestre.");
                lastTick = GetTickCount64();
            }
        }

        SaveReopenStack(); // uscendo: l'ultima chiusura vista resta riapribile al prossimo avvio
        m_explorer.reset();
        if (SUCCEEDED(init))
        {
            CoUninitialize();
        }
    }

    void Worker::RefreshPlacements()
    {
        // Solo finestre visibili: chiudendosi, una finestra viene prima nascosta, e quello stato non va ricordato.
        bool changed = false;
        for (auto& window : m_live)
        {
            WINDOWPLACEMENT placement{ sizeof(placement) };
            if (!IsWindow(window.hwnd) || !IsWindowVisible(window.hwnd) || !GetWindowPlacement(window.hwnd, &placement))
            {
                continue;
            }
            if (placement.showCmd != window.placement.showCmd || !EqualRect(&placement.rcNormalPosition, &window.placement.rcNormalPosition))
            {
                window.placement = placement;
                changed = true;
            }
        }
        if (changed)
        {
            UpdateSession(FromLive(m_live));
        }
    }

    void Worker::UpdateVeil(Settings const& settings)
    {
        const bool grace = m_root.empty() && PersistBrowsersEnabled() && GetTickCount64() - m_startTick < kStartupGraceMs;
        const bool armed = settings.restoreOnOpen && !m_session.Empty() && m_live.empty() && !m_ending && !grace;
        m_veil.Arm(armed, m_root.empty() ? std::wstring{} : DevArmPath());
        // Cartelle in una scheda; in modalità prova solo con dev-merge.txt (vedi Common.h).
        const DevMerge devMerge = m_root.empty() ? DevMerge::On : DevMergeMode();
        m_veil.SetMergeArmed(settings.openInTabs && !m_ending && devMerge != DevMerge::Off, devMerge == DevMerge::External);
    }

    HWND Worker::MergeTarget(HWND exclude) const
    {
        HWND best = nullptr;
        ULONGLONG bestTick = 0;
        for (auto const& window : m_live) // nell'ordine Z: a pari tempo vince quella più in alto
        {
            const HWND hwnd = window.hwnd;
            if (hwnd == exclude || !IsExplorerFrame(hwnd) || !IsWindowVisible(hwnd) || m_veil.IsMergeCandidate(hwnd) ||
                !Explorer::OnCurrentDesktop(hwnd))
            {
                continue;
            }
            const auto seen = m_seen.find(hwnd);
            const ULONGLONG tick = std::max(m_veil.LastActivated(hwnd), seen != m_seen.end() ? seen->second : 0);
            if (!best || tick > bestTick)
            {
                best = hwnd;
                bestTick = tick;
            }
        }
        return best;
    }

    void Worker::MergeIntoTabs()
    {
        const auto candidates = m_veil.MergeCandidates();
        if (candidates.empty() || !m_modeReady)
        {
            return;
        }
        const Settings settings = Settings::Load();
        for (HWND window : candidates)
        {
            const ULONGLONG start = GetTickCount64();
            // Resta una finestra (senza velo, la candidatura se ne va con lui).
            const auto keep = [&](wchar_t const* why) {
                m_veil.Lift(window);
                Log(L"Cartella aperta da un altro programma: resta una finestra (%s).", why);
            };
            if (!IsWindow(window))
            {
                m_veil.ForgetMergeCandidate(window);
                continue;
            }
            if (!settings.openInTabs)
            {
                keep(L"funzione spenta");
                continue;
            }
            const auto tabs = m_explorer->TabsIn(window);
            if (tabs.size() != 1 || tabs.front().path.empty())
            {
                keep(L"cartella non letta");
                continue;
            }
            Window opened;
            opened.hwnd = window;
            opened.tabs = tabs;
            if (!Accepts(opened))
            {
                m_veil.Lift(window); // modalità prova: non è una finestra di prova, non si tocca
                continue;
            }
            if (IsStart(tabs.front().path))
            {
                keep(L"cartella iniziale, come con Win+E");
                continue;
            }
            const HWND target = MergeTarget(window);
            if (!target)
            {
                keep(L"nessuna finestra di Esplora file in cui aprirla in questo desktop");
                continue;
            }
            DWORD_PTR ignored = 0;
            if (!SendMessageTimeoutW(target, WM_NULL, 0, 0, SMTO_ABORTIFHUNG | SMTO_BLOCK, 1500, &ignored))
            {
                keep(L"la finestra usata per ultima non risponde");
                continue;
            }
            if (IsIconic(target))
            {
                ShowWindow(target, SW_RESTORE);
            }
            // La scheda nuova compare in fondo alla barra e diventa attiva (come con Ctrl+Maiusc+T).
            const size_t position = static_cast<size_t>(TabCount(target));
            std::vector<size_t> missing;
            m_explorer->BuildTabs(target, tabs, false, static_cast<int>(position), &missing);
            if (!missing.empty())
            {
                keep(L"la scheda non si è aperta");
                continue;
            }
            // Elementi selezionati nella finestra nuova (il file da mostrare nella cartella, explorer /select): di nuovo
            // selezionati nella scheda.
            const auto selection = m_explorer->SelectedItems(window);
            const bool selected = !selection.empty() && m_explorer->SelectItems(target, tabs.front().path, selection);
            m_explorer->Activate(target);
            // Via la finestra nuova, ancora velata; chiusa (o nascosta da Esplora file per riusarla) perde anche il velo.
            PostMessageW(window, WM_SYSCOMMAND, SC_CLOSE, 0);
            const ULONGLONG closing = GetTickCount64();
            while (IsWindow(window) && IsWindowVisible(window) && GetTickCount64() - closing < 3000)
            {
                Sleep(20);
            }
            const bool closed = !IsWindow(window) || !IsWindowVisible(window);
            m_veil.Lift(window);
            Log(L"Cartella aperta da un altro programma: in una scheda della finestra usata per ultima (%llu ms%s%s).", GetTickCount64() - start,
                selection.empty() ? L"" : (selected ? L", con l'elemento selezionato" : L", selezione non riportata"),
                closed ? L"" : L"; la finestra nuova non si è chiusa");
        }
    }

    void Worker::WaitForVeiled()
    {
        const ULONGLONG start = GetTickCount64();
        for (HWND window : m_veil.Shown())
        {
            while (IsWindow(window) && !m_explorer->HasReadableTab(window) && GetTickCount64() - start < kVeiledReadyMs)
            {
                Sleep(20);
            }
        }
        XPX_VLOG(L"Finestra velata pronta in %llu ms.", GetTickCount64() - start);
    }

    bool Worker::IsStart(std::wstring const& path) const
    {
        // In modalità prova la "cartella iniziale" è la cartella della prova stessa.
        return m_root.empty() ? Explorer::IsStartLocation(path) : EqualsIgnoreCase(path, m_root);
    }

    bool Worker::Accepts(Window const& window) const
    {
        return m_root.empty() ||
               std::any_of(window.tabs.begin(), window.tabs.end(), [&](Tab const& tab) { return IsInsideFolder(tab.path, m_root); });
    }

    std::vector<Window> Worker::AdoptAll()
    {
        std::vector<Window> live;
        for (auto& window : m_explorer->Snapshot())
        {
            if (!window.tabs.empty() && Accepts(window))
            {
                live.push_back(std::move(window));
            }
        }
        return live;
    }

    void Worker::SwitchMode(std::wstring const& root)
    {
        if (std::exchange(m_sessionUnsaved, false) && m_modeReady && !SaveSession(SessionPath(!m_root.empty()), m_session))
        {
            Log(L"Sessione della modalità precedente non salvata (errore %lu).", GetLastError());
        }
        SaveReopenStack(); // pila della modalità che finisce, se cambiata e non ancora scritta (nel suo file: m_root è ancora quello)
        m_root = root;
        m_modeReady = true;
        m_modeTick = GetTickCount64();
        m_session = LoadSession(SessionPath(!m_root.empty()));
        m_closed.clear();
        m_seen.clear();
        // Le schede chiuse di prima non c'entrano con la modalità nuova (in modalità prova si
        // riaprirebbe una cartella dell'utente, e viceversa): ogni modalità ha la sua pila su disco, e torna
        // quella (anche dopo un riavvio della tray o del PC, come le schede chiuse di Chrome).
        m_reopenStack.clear();
        for (auto& entry : LoadClosedStack(ClosedStackPath(!root.empty())))
        {
            m_reopenStack.push_back(ClosedItem{ entry.wholeWindow, nullptr, std::move(entry.window), entry.index });
        }
        if (m_reopenStack.size() > kMaxReopen)
        {
            m_reopenStack.erase(m_reopenStack.begin(), m_reopenStack.end() - static_cast<std::ptrdiff_t>(kMaxReopen));
        }
        m_reopenDirty = false;
        m_recordedTabs.clear();
        // Le finestre già aperte sono "vecchie" (mai un ripristino su di loro) e sono la sessione attuale.
        m_live.clear();
        for (auto& window : m_explorer->Snapshot())
        {
            m_seen[window.hwnd] = m_modeTick;
            if (!window.tabs.empty() && Accepts(window))
            {
                m_live.push_back(std::move(window));
            }
        }
        Log(L"%s; sessione salvata: %s; finestre aperte: %zu.", m_root.empty() ? L"Modalità normale" : (L"Modalità prova su " + m_root).c_str(),
            Describe(m_session, !m_root.empty()).c_str(), m_live.size());
        if (!m_live.empty())
        {
            UpdateSession(FromLive(m_live));
        }
        Publish();
    }

    void Worker::UpdateSession(Session const& session)
    {
        const bool changed = !SameSession(session, m_session);
        if (m_ending || (!changed && !m_sessionUnsaved))
        {
            return;
        }
        m_session = session;
        // Il file può essere aperto per un attimo da un altro programma (antivirus, indicizzazione, chi lo
        // sta leggendo) e allora non si può sostituire: si riprova subito qualche volta, poi a ogni
        // controllo (vedi Run) finché non è salvato.
        bool saved = false;
        DWORD error = 0;
        for (int attempt = 0; attempt < 4 && !saved; ++attempt)
        {
            if (attempt)
            {
                Sleep(30);
            }
            saved = SaveSession(SessionPath(!m_root.empty()), m_session);
            error = saved ? 0 : GetLastError();
        }
        if (!saved && !m_sessionUnsaved)
        {
            Log(L"Sessione non salvata (errore %lu): si riprova.", error);
        }
        else if (saved && m_sessionUnsaved)
        {
            Log(L"Sessione salvata al nuovo tentativo.");
        }
        m_sessionUnsaved = !saved;
        if (changed)
        {
            XPX_VLOG(L"Sessione aggiornata: %s.", Describe(m_session, !m_root.empty()).c_str());
        }
    }

    Session Worker::ClosedTogether() const
    {
        Session session;
        ULONGLONG last = 0;
        for (auto const& entry : m_closed)
        {
            last = std::max(last, entry.closed);
        }
        std::vector<Closed> together;
        for (auto const& entry : m_closed)
        {
            if (entry.closed + kTogetherMs >= last)
            {
                together.push_back(entry);
            }
        }
        // Una finestra comparsa quando le altre avevano già cominciato a chiudersi e sparita subito dopo
        // (osservato: Esplora file, chiudendo una finestra con più schede, ne ha mostrata per un attimo
        // un'altra con parte delle schede) non fa parte della sessione da ricordare.
        const auto fleeting = [&together](Closed const& entry) {
            return entry.seen && entry.closed >= entry.seen && entry.closed - entry.seen < kFleetingMs &&
                   std::any_of(together.begin(), together.end(), [&](Closed const& other) { return &other != &entry && other.closed <= entry.seen; });
        };
        std::vector<Closed> kept;
        for (auto const& entry : together)
        {
            if (!fleeting(entry))
            {
                kept.push_back(entry);
            }
        }
        if (!kept.empty())
        {
            together = std::move(kept);
        }
        // Dalla chiusa per ultima; a parità, nell'ordine Z che avevano.
        std::stable_sort(together.begin(), together.end(), [](Closed const& a, Closed const& b) { return a.closed > b.closed; });
        for (auto const& entry : together)
        {
            session.windows.push_back(Stored(entry.window));
        }
        return session;
    }

    bool Worker::ShouldRestore(Window const& opened, Settings const& settings, ULONGLONG now) const
    {
        if (!settings.restoreOnOpen || m_session.Empty() || m_ending)
        {
            return false;
        }
        // Solo una finestra appena comparsa: mai una che c'era già (ad esempio una di cui finora non si
        // riuscivano a leggere le schede).
        const auto seen = m_seen.find(opened.hwnd);
        if (seen == m_seen.end() || seen->second <= m_modeTick || now - seen->second > kFreshWindowMs)
        {
            XPX_VLOG(L"Finestra non appena aperta: niente ripristino.");
            return false;
        }
        if (m_root.empty() && PersistBrowsersEnabled() && now - m_startTick < kStartupGraceMs)
        {
            Log(L"Windows ripristina da sé le cartelle all'accesso: sessione non ripristinata.");
            return false;
        }
        return true;
    }

    void Worker::Tick()
    {
        const Settings settings = Settings::Load();
        SetVerbose(settings.verboseLog);
        m_backToTop.SetEnabled(settings.backToTop);
        m_hotkey.SetEnabled(settings.reopenTab);
        const std::wstring root = DevFilterRoot();
        if (!m_modeReady || !EqualsIgnoreCase(root, m_root))
        {
            SwitchMode(root);
            UpdateVeil(settings);
            return;
        }

        const auto windows = m_explorer->Snapshot();
        const ULONGLONG now = GetTickCount64();
        const bool wasEmpty = m_live.empty();
        m_considered.clear();
        for (auto const& window : windows)
        {
            m_seen.try_emplace(window.hwnd, now);
            m_considered.push_back(window.hwnd);
        }

        // Chiuse: solo le finestre che non esistono più, con l'ora esatta della chiusura se il gancio l'ha
        // vista (un controllo può trovarne chiuse più d'una e conta quale è stata chiusa per ultima).
        std::vector<HWND> closedWithWindow; // schede sparite insieme alla loro finestra
        for (auto it = m_live.begin(); it != m_live.end();)
        {
            if (IsWindow(it->hwnd))
            {
                ++it;
                continue;
            }
            const ULONGLONG destroyed = m_veil.DestroyedAt(it->hwnd);
            const auto seen = m_seen.find(it->hwnd);
            XPX_VLOG(L"Finestra chiusa %p (%zu schede).", it->hwnd, it->tabs.size());
            for (auto const& tab : it->tabs)
            {
                if (tab.tabWindow)
                {
                    closedWithWindow.push_back(tab.tabWindow);
                }
            }
            // Ctrl+Maiusc+T riapre la finestra intera, non le sue schede una per una.
            PushClosed(ClosedItem{ true, nullptr, *it });
            m_closed.push_back(Closed{ std::move(*it), destroyed && destroyed <= now ? destroyed : now, seen != m_seen.end() ? seen->second : 0 });
            it = m_live.erase(it);
        }
        std::erase_if(m_seen, [](auto const& entry) { return !IsWindow(entry.first); });
        std::erase_if(m_closed, [&](Closed const& entry) { return entry.closed + 3 * kTogetherMs < now; });

        // Finestre che si stanno smontando con tutto Esplora file (chiusura forzata, crash): il sistema ne
        // distrugge le schede una dopo l'altra, ma l'utente non le ha chiuse. Si riconoscono perché il loro
        // thread non risponde più; il loro ultimo stato resta congelato, così quando la finestra sparisce
        // viene ricordata intera (prima perdeva una scheda a ogni riavvio forzato di Esplora file).
        std::vector<HWND> dying;
        for (auto const& window : m_live)
        {
            const bool lostTab = std::any_of(window.tabs.begin(), window.tabs.end(), [](Tab const& tab) { return tab.tabWindow && !IsWindow(tab.tabWindow); });
            DWORD_PTR result = 0;
            if (lostTab && IsWindow(window.hwnd) &&
                !SendMessageTimeoutW(window.hwnd, WM_NULL, 0, 0, SMTO_ABORTIFHUNG | SMTO_BLOCK, 300, &result))
            {
                XPX_VLOG(L"Finestra %p senza risposta mentre sparisce una scheda: Esplora file si sta chiudendo, stato congelato.", window.hwnd);
                dying.push_back(window.hwnd);
            }
        }

        // Schede chiuse con la finestra ancora aperta: la loro finestra (ShellTabWindowClass) non esiste
        // più. È un controllo locale, come per le finestre: una scheda di cui per un momento non si
        // leggono i dati resta com'era.
        struct ClosedTab
        {
            HWND window;
            Tab tab;
            ULONGLONG closed; // dal gancio sulle distruzioni (0 se non si sa)
            int index;        // posto che aveva nella finestra (per riaprirla lì)
        };
        std::vector<ClosedTab> closedTabs;
        for (auto const& window : m_live)
        {
            if (std::find(dying.begin(), dying.end(), window.hwnd) != dying.end())
            {
                continue;
            }
            for (size_t t = 0; t < window.tabs.size(); ++t)
            {
                Tab const& tab = window.tabs[t];
                if (!tab.tabWindow || IsWindow(tab.tabWindow) ||
                    std::find(closedWithWindow.begin(), closedWithWindow.end(), tab.tabWindow) != closedWithWindow.end() ||
                    std::find(m_recordedTabs.begin(), m_recordedTabs.end(), tab.tabWindow) != m_recordedTabs.end())
                {
                    continue;
                }
                closedTabs.push_back(ClosedTab{ window.hwnd, tab, m_veil.DestroyedAt(tab.tabWindow), static_cast<int>(t) });
                m_recordedTabs.push_back(tab.tabWindow);
            }
        }
        // Più schede chiuse tra un controllo e l'altro: in cima alla pila va quella chiusa per ultima
        // (il gancio vede le distruzioni al millisecondo).
        std::stable_sort(closedTabs.begin(), closedTabs.end(), [](ClosedTab const& a, ClosedTab const& b) { return a.closed < b.closed; });
        for (auto& entry : closedTabs)
        {
            Window one;
            one.tabs.push_back(std::move(entry.tab));
            PushClosed(ClosedItem{ false, entry.window, std::move(one), entry.index });
        }

        // Stato attuale, nell'ordine Z: le finestre note si aggiornano (se non se ne leggono le schede
        // resta l'ultimo stato), le nuove entrano quando se ne conoscono le schede.
        std::vector<Window> live;
        std::vector<HWND> adopted;
        for (auto const& window : windows)
        {
            const auto known = std::find_if(m_live.begin(), m_live.end(), [&](Window const& w) { return w.hwnd == window.hwnd; });
            if (known != m_live.end())
            {
                const bool frozen = std::find(dying.begin(), dying.end(), window.hwnd) != dying.end();
                live.push_back(window.tabs.empty() || frozen ? *known : window);
            }
            else if (!window.tabs.empty() && Accepts(window) && !m_veil.IsMergeCandidate(window.hwnd)) // (candidate: diventano schede)
            {
                XPX_VLOG(L"Finestra nuova %p (%zu schede%s).", window.hwnd, window.tabs.size(), IsWindowVisible(window.hwnd) ? L"" : L", nascosta");
                live.push_back(window);
                adopted.push_back(window.hwnd);
            }
        }
        for (auto const& old : m_live)
        {
            // Esistono ancora ma non sono visibili (es. si stanno chiudendo): restano finché esistono.
            if (std::none_of(live.begin(), live.end(), [&](Window const& w) { return w.hwnd == old.hwnd; }))
            {
                live.push_back(old);
            }
        }

        // Esplora file aperto quando non c'era nessuna finestra: torna l'ultima sessione.
        if (wasEmpty && adopted.size() == 1 && live.size() == 1 && ShouldRestore(live.front(), settings, now))
        {
            RestoreInto(live.front());
            live = AdoptAll(); // le finestre riaperte fanno parte della sessione
        }

        ApplyTabColors(live);
        if (!live.empty())
        {
            UpdateSession(FromLive(live));
        }
        else if (!wasEmpty)
        {
            // Chiusa l'ultima finestra: restano lei e quelle chiuse insieme a lei.
            const Session closed = ClosedTogether();
            if (!closed.Empty())
            {
                UpdateSession(closed);
            }
            m_closed.clear();
            Log(L"Esplora file chiuso: ricordate %s.", Describe(m_session, !m_root.empty()).c_str());
        }
        m_live = std::move(live);
        // Una scheda chiusa si dimentica appena la sua finestra si rilegge senza di lei (così un handle
        // riusato da una scheda nuova viene di nuovo considerato).
        std::erase_if(m_recordedTabs, [this](HWND tab) {
            return std::none_of(m_live.begin(), m_live.end(), [&](Window const& window) {
                return std::any_of(window.tabs.begin(), window.tabs.end(), [&](Tab const& t) { return t.tabWindow == tab; });
            });
        });
        UpdateVeil(settings);
        Publish();
    }

    void Worker::PushClosed(ClosedItem item)
    {
        if (item.window.tabs.empty())
        {
            return;
        }
        // Come la sessione salvata: solo i percorsi delle cartelle, niente altro.
        for (auto& tab : item.window.tabs)
        {
            tab.title.clear();
            tab.tabWindow = nullptr;
        }
        const bool wholeWindow = item.wholeWindow;
        const size_t tabs = item.window.tabs.size();
        m_reopenStack.push_back(std::move(item));
        if (m_reopenStack.size() > kMaxReopen)
        {
            m_reopenStack.erase(m_reopenStack.begin());
        }
        m_reopenDirty = true;
        const std::wstring what = wholeWindow ? L"una finestra di " + std::to_wstring(tabs) + L" schede"
                                  : tabs == 1 ? std::wstring(L"una scheda")
                                              : std::to_wstring(tabs) + L" schede";
        XPX_VLOG(L"Riapri scheda: ricordata %s (%zu in memoria).", what.c_str(), m_reopenStack.size());
    }

    void Worker::KeepUnreopened(HWND window, Window const& saved, std::vector<Tab> const& missing)
    {
        if (missing.empty())
        {
            return;
        }
        ClosedItem item;
        if (window)
        {
            // Le schede mancanti, insieme: un Ctrl+Maiusc+T le riapre tutte nella loro finestra.
            item.from = window;
            item.window.tabs = missing;
        }
        else
        {
            // La finestra non si è aperta: torna tutta, nella sua posizione.
            item.wholeWindow = true;
            item.window = saved;
        }
        m_unreopened += missing.size();
        PushClosed(std::move(item));
    }

    void Worker::ReportUnreopened()
    {
        const size_t count = std::exchange(m_unreopened, 0);
        if (count == 0)
        {
            return;
        }
        Log(L"Ripristino: %zu %s (Esplora file occupato), da riaprire con Ctrl+Maiusc+T.", count,
            count == 1 ? L"scheda non riaperta" : L"schede non riaperte");
        if (m_notify && m_root.empty()) // in modalità prova niente fumetti
        {
            std::wstring text;
            if (!UiItalian())
            {
                text = count == 1 ? std::wstring(L"A tab didn't reopen") : std::to_wstring(count) + L" tabs didn't reopen";
                text += L": File Explorer was still busy. ";
                text += Settings::Load().reopenTab ? (count == 1 ? L"Press Ctrl+Shift+T in File Explorer to reopen it."
                                                                 : L"Press Ctrl+Shift+T in File Explorer to reopen them.")
                                                   : L"Turn on \"Reopen closed tabs with Ctrl+Shift+T\" in the VTabsExplorer menu and press it in File Explorer.";
            }
            else
            {
                text = count == 1 ? std::wstring(L"Una scheda non si è riaperta") : std::to_wstring(count) + L" schede non si sono riaperte";
                text += L": Esplora file era ancora occupato. ";
                text += Settings::Load().reopenTab
                            ? (count == 1 ? L"Premi Ctrl+Maiusc+T in Esplora file per riaprirla."
                                          : L"Premi Ctrl+Maiusc+T in Esplora file per riaprirle.")
                            : L"Attiva «Riapri la scheda chiusa con Ctrl+Maiusc+T» nel menu di VTabsExplorer e premilo in Esplora file.";
            }
            m_notify(text);
        }
    }

    void Worker::ForgetRestored(Session const& restored)
    {
        // Le finestre tornate con la sessione non sono più "chiuse": senza toglierle dalla pila,
        // Ctrl+Maiusc+T riaprirebbe la stessa finestra una seconda volta. Ognuna corrisponde all'ultima
        // finestra intera con le stesse schede (sono copie dello stesso stato, prese alla chiusura).
        size_t forgotten = 0;
        for (auto const& window : restored.windows)
        {
            const auto same = std::find_if(m_reopenStack.rbegin(), m_reopenStack.rend(),
                                           [&](ClosedItem const& item) { return item.wholeWindow && SameTabs(item.window, window); });
            if (same != m_reopenStack.rend())
            {
                m_reopenStack.erase(std::next(same).base());
                ++forgotten;
            }
        }
        if (forgotten)
        {
            m_reopenDirty = true;
            XPX_VLOG(L"Riapri scheda: %zu finestre tornate con la sessione tolte dalla pila (%zu in memoria).", forgotten, m_reopenStack.size());
        }
    }

    void Worker::SaveReopenStack()
    {
        if (!m_reopenDirty || !m_modeReady)
        {
            return; // (all'avvio, prima di aver letto la pila dal disco, non si scrive: la si cancellerebbe)
        }
        std::vector<ClosedEntry> entries;
        entries.reserve(m_reopenStack.size());
        for (auto const& item : m_reopenStack)
        {
            entries.push_back(ClosedEntry{ item.wholeWindow, item.window, item.index });
        }
        if (SaveClosedStack(ClosedStackPath(!m_root.empty()), entries))
        {
            m_reopenDirty = false;
            m_reopenSaveLogged = false;
        }
        else if (!m_reopenSaveLogged)
        {
            m_reopenSaveLogged = true; // si riprova a ogni giro (resta "da scrivere")
            Log(L"Riapri scheda: pila delle schede chiuse non salvata (errore %lu): si riprova.", GetLastError());
        }
    }

    bool Worker::ReopenClosed(HWND active)
    {
        const Settings settings = Settings::Load();
        if (!settings.reopenTab)
        {
            return false; // spenta nel frattempo
        }
        while (!m_reopenStack.empty())
        {
            const ClosedItem item = m_reopenStack.back();
            m_reopenStack.pop_back();
            m_reopenDirty = true; // (anche quando la voce torna in pila: il file si riscrive uguale)
            const ULONGLONG start = GetTickCount64();

            if (item.wholeWindow)
            {
                int ignored = 0;
                if (RestorableTabs(item.window, ignored).empty())
                {
                    continue; // cartelle non più utilizzabili: si prova con la precedente
                }
                Reopened result;
                {
                    // Velata mentre si costruisce: compare già con tutte le sue schede.
                    VeiledRestore veil(m_veil);
                    result = RestoreWindowWith(*m_explorer, item.window);
                }
                const HWND opened = result.window;
                if (!opened)
                {
                    // Niente è stato creato: la scheda chiusa resta in pila, la pressione non si spreca.
                    m_reopenStack.push_back(item);
                    Log(L"Riapri scheda: la finestra chiusa non si è riaperta.");
                    return false;
                }
                // Come le finestre che c'erano già all'avvio: non è un'"apertura" da cui far scattare il
                // ripristino della sessione (anche se le sue schede non si leggessero subito).
                m_seen[opened] = m_modeTick;
                m_explorer->Activate(opened);
                Log(L"Riaperta la finestra chiusa (%zu schede) in %llu ms.", item.window.tabs.size(), GetTickCount64() - start);
                // Da qui le finestre aperte sono la sessione attuale, come se l'avesse aperta l'utente.
                m_live = AdoptAll();
                ApplyTabColors(m_live);
                if (!m_live.empty())
                {
                    UpdateSession(FromLive(m_live));
                }
                UpdateVeil(settings);
                Publish();
                KeepUnreopened(opened, item.window, result.missing);
                ReportUnreopened();
                return true;
            }

            if (!Explorer::IsRestorable(item.window.tabs.front()))
            {
                continue; // cartella non più utilizzabile: si prova con la scheda chiusa prima
            }
            // Dove riaprirla: la sua finestra se c'è ancora, altrimenti quella in uso. Solo finestre
            // considerate: in modalità prova mai una finestra dell'utente.
            HWND target = nullptr;
            for (HWND candidate : { item.from, active })
            {
                if (IsExplorerFrame(candidate) && AcceptsForFeatures(candidate))
                {
                    target = candidate;
                    break;
                }
            }
            if (!target)
            {
                // La scheda resta in pila: appena si torna in una finestra di Esplora file si riapre.
                m_reopenStack.push_back(item);
                Log(L"Riapri scheda: nessuna finestra di Esplora file in cui riaprire la scheda chiusa.");
                return false;
            }
            // La scheda nuova compare in fondo alla barra (Esplora file non sa metterla altrove) e
            // diventa attiva; le schede si contano localmente, non con una chiamata a Esplora file.
            const size_t position = static_cast<size_t>(TabCount(target));
            std::vector<size_t> missing;
            m_explorer->BuildTabs(target, item.window.tabs, false, static_cast<int>(position), &missing);
            // Torna con il suo colore e, nella sua finestra, al posto che aveva, come nel browser e in VTabsNotepad: lo
            // spostamento lo fa la mod insieme ai colori (schede verticali); senza la mod la scheda resta in fondo.
            const int place = target == item.from && item.index >= 0 && item.window.tabs.size() == 1 && missing.empty()
                                  ? std::min(item.index, static_cast<int>(position))
                                  : -1;
            QueueTabColors(target, item.window.tabs, position, missing, place);
            if (!missing.empty())
            {
                // Quelle che non si sono riaperte restano in pila (come prima nella loro finestra).
                ClosedItem rest{ false, item.from, {} };
                for (const size_t index : missing)
                {
                    rest.window.tabs.push_back(item.window.tabs[index]);
                }
                m_reopenStack.push_back(std::move(rest));
                if (missing.size() == item.window.tabs.size())
                {
                    Log(L"Riapri scheda: la scheda chiusa non si è riaperta (resta da riaprire).");
                    return false;
                }
                Log(L"Riapri scheda: %zu schede non riaperte (restano da riaprire).", missing.size());
            }
            const size_t reopened = item.window.tabs.size() - missing.size();
            const std::wstring what = reopened == 1 ? std::wstring(L"Riaperta una scheda chiusa")
                                                    : L"Riaperte " + std::to_wstring(reopened) + L" schede chiuse";
            Log(L"%s in %llu ms%s.", what.c_str(), GetTickCount64() - start,
                target == item.from ? L"" : L" (nella finestra in uso: la sua non c'è più)");
            return true;
        }
        Log(L"Riapri scheda: niente da riaprire.");
        return false;
    }

    void Worker::RestoreInto(Window const& opened)
    {
        const ULONGLONG start = GetTickCount64();
        Session session = m_session;
        if (session.windows.size() > kMaxWindows)
        {
            session.windows.resize(kMaxWindows);
        }
        const bool atStart = opened.tabs.size() == 1 && IsStart(opened.tabs.front().path);
        Log(L"Esplora file riaperto (%s, %s): ripristino di %s.", atStart ? L"cartella iniziale" : L"cartella precisa",
            m_veil.IsVeiled(opened.hwnd) ? L"finestra velata" : L"finestra già visibile", Describe(session, !m_root.empty()).c_str());
        ULONGLONG last = start;
        const auto step = [&last](wchar_t const* what) {
            const ULONGLONG now = GetTickCount64();
            XPX_VLOG(L"Ripristino, %s: %llu ms.", what, now - last);
            last = now;
        };

        Window const& first = session.windows.front();
        int active = 0;
        const auto tabs = RestorableTabs(first, active);
        const HWND target = opened.hwnd;
        std::vector<size_t> missing;    // schede di tabs non riaperte
        std::vector<Reopened> reopened; // le altre finestre
        {
            // Le altre finestre si riaprono in parallelo, mentre qui si costruisce quella aperta
            // dall'utente; tutte restano velate fino alla fine, poi compaiono insieme.
            VeiledRestore veil(m_veil);
            ParallelRestore others(std::vector<Window>(session.windows.begin() + 1, session.windows.end()));

            // Ancora nella cartella iniziale (l'utente non ha già cominciato a spostarsi)? Una finestra velata
            // l'utente non l'ha ancora vista: è com'era appena letta.
            bool reuse = atStart;
            if (reuse && !m_veil.IsVeiled(target))
            {
                const auto now = m_explorer->TabsIn(target);
                reuse = now.size() == 1 && IsStart(now.front().path);
            }
            step(L"controllo della finestra aperta");

            if (reuse && !tabs.empty())
            {
                // La finestra appena aperta diventa la prima finestra della sessione.
                m_explorer->BuildTabs(target, tabs, true, active, &missing);
                QueueTabColors(target, tabs, 0, missing);
            }
            else
            {
                // Cartella chiesta dall'utente: resta la prima scheda, attiva; la sessione si aggiunge accanto.
                const size_t before = static_cast<size_t>(TabCount(target));
                m_explorer->BuildTabs(target, tabs, false, 0, &missing);
                QueueTabColors(target, tabs, before, missing);
            }
            step(L"schede della finestra aperta");
            m_explorer->Place(target, first.placement);
            reopened = others.Results();
            step(L"altre finestre");
        }
        // Quella aperta dall'utente davanti alle altre.
        m_explorer->Activate(target);
        ForgetRestored(session);
        std::vector<Tab> lost;
        for (const size_t index : missing)
        {
            lost.push_back(tabs[index]);
        }
        KeepUnreopened(target, first, lost);
        for (auto const& other : reopened)
        {
            KeepUnreopened(other.window, other.saved, other.missing);
        }
        ReportUnreopened();
        Log(L"Ripristino completato in %llu ms.", GetTickCount64() - start);
    }

    void Worker::RestoreAll()
    {
        if (!m_live.empty())
        {
            Log(L"Riapri ora: ci sono già finestre aperte, niente da fare.");
            return;
        }
        Session session = m_session;
        if (session.Empty())
        {
            return;
        }
        if (session.windows.size() > kMaxWindows)
        {
            session.windows.resize(kMaxWindows);
        }
        const ULONGLONG start = GetTickCount64();
        Log(L"Riapri ora: ripristino di %s.", Describe(session, !m_root.empty()).c_str());
        HWND first = nullptr;
        std::vector<Reopened> reopened;
        {
            // In parallelo e velate; alla fine compaiono insieme, la più recente davanti.
            VeiledRestore veil(m_veil);
            ParallelRestore others(std::vector<Window>(session.windows.begin() + 1, session.windows.end()));
            reopened.push_back(RestoreWindowWith(*m_explorer, session.windows.front()));
            first = reopened.front().window;
            for (auto const& other : others.Results())
            {
                reopened.push_back(other);
            }
        }
        if (first)
        {
            m_explorer->Activate(first);
        }
        // Le finestre appena riaperte sono la sessione attuale (e non un'"apertura" da ripristinare).
        m_live = AdoptAll();
        ApplyTabColors(m_live);
        if (!m_live.empty())
        {
            UpdateSession(FromLive(m_live));
        }
        UpdateVeil(Settings::Load());
        Publish();
        ForgetRestored(session);
        for (auto const& result : reopened)
        {
            KeepUnreopened(result.window, result.saved, result.missing);
        }
        ReportUnreopened();
        Log(L"Riapri ora completato in %llu ms.", GetTickCount64() - start);
    }

    void Worker::ApplyTabColors(std::vector<Window>& windows)
    {
        if (const ULONGLONG stamp = ColorsStamp(); stamp != m_colorsStamp)
        {
            m_colorsStamp = stamp;
            m_colorsRetries = 0;
            ReadColorFiles(m_liveColors, m_pendingColors);
        }
        for (auto& window : windows)
        {
            // 1) Il colore noto di ogni scheda: la segue anche se cambia posizione o cartella.
            for (auto& tab : window.tabs)
            {
                if (const auto known = tab.tabWindow ? m_tabColors.find(tab.tabWindow) : m_tabColors.end(); known != m_tabColors.end())
                {
                    tab.color = known->second;
                }
            }
            // 2) Quelli pubblicati dalla mod valgono per tutte le schede (per posizione), se la finestra ha
            //    ancora quel numero di schede e in ogni posizione colorata c'è la scheda con quel titolo (il
            //    nostro ordine può essere ancora quello di prima di un riordino: allora si aspetta);
            //    3) sopra, quelli da rimettere che la mod non ha ancora preso (finestra appena riaperta, o
            //    schede verticali spente), solo per le schede indicate.
            const size_t count = window.tabs.size();
            if (const auto live = m_liveColors.find(window.hwnd); live != m_liveColors.end() && live->second.count == count)
            {
                const bool aligned = std::all_of(live->second.colored.begin(), live->second.colored.end(), [&](TabColorEntry const& entry) {
                    return entry.index < count && (entry.title.empty() || WideOf(entry.title) == window.tabs[entry.index].title);
                });
                if (aligned)
                {
                    for (auto& tab : window.tabs)
                    {
                        tab.color = 0;
                    }
                    for (auto const& entry : live->second.colored)
                    {
                        window.tabs[entry.index].color = entry.color;
                    }
                }
                else if (m_colorsRetries < 3)
                {
                    // Si rilegge l'ordine al prossimo controllo (tra mezzo secondo); intanto restano i colori noti.
                    XPX_VLOG(L"Colori delle schede: ordine non ancora aggiornato per la finestra %p, si riprova.", window.hwnd);
                    ++m_colorsRetries;
                    m_colorsRetry = true;
                    m_explorer->ForgetOrder();
                }
            }
            if (const auto pending = m_pendingColors.find(window.hwnd); pending != m_pendingColors.end() && pending->second.count == count)
            {
                for (auto const& entry : pending->second.colored)
                {
                    if (entry.index < count)
                    {
                        window.tabs[entry.index].color = entry.color;
                    }
                }
            }
            for (auto const& tab : window.tabs)
            {
                if (tab.tabWindow)
                {
                    m_tabColors[tab.tabWindow] = tab.color;
                }
            }
        }
        std::erase_if(m_tabColors, [](auto const& entry) { return !IsWindow(entry.first); });
    }
}
