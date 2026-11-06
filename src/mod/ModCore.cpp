#include "pch.h"
#include "ModCore.h"

#include "DevDiagnostics.h"
#include "Discovery.h"
#include "Glass.h"
#include "Interop.h"
#include "ModLog.h"
#include "TreeDump.h"
#include "VerticalTabs.h"
#include "../ModShared.h"

extern HMODULE g_module;

namespace xpx::mod
{
    namespace
    {
        namespace mux = winrt::Microsoft::UI::Xaml;
        namespace muxc = winrt::Microsoft::UI::Xaml::Controls;
        using winrt::Microsoft::UI::Dispatching::DispatcherQueuePriority;

        // Il TabView dell'intestazione di Esplora file sta dentro questo controllo.
        constexpr std::wstring_view kTabControlType = L"FileExplorerExtensions.FileExplorerTabControl";

        // Finestre principali di Esplora file di questo processo.
        std::vector<HWND> OurWindows(bool visibleOnly)
        {
            std::vector<HWND> windows;
            for (HWND window = FindWindowExW(nullptr, nullptr, L"CabinetWClass", nullptr); window;
                 window = FindWindowExW(nullptr, window, L"CabinetWClass", nullptr))
            {
                DWORD process = 0;
                GetWindowThreadProcessId(window, &process);
                if (process == GetCurrentProcessId() && (!visibleOnly || IsWindowVisible(window)))
                {
                    windows.push_back(window);
                }
            }
            return windows;
        }

        // Ha già l'isola XAML dell'intestazione (quindi XAML è pronto sul suo thread).
        bool HasHeaderIsland(HWND window)
        {
            for (HWND child : interop::DirectChildren(window))
            {
                if (interop::ClassOf(child) == L"Microsoft.UI.Content.DesktopChildSiteBridge" && !GetPropW(child, interop::kPanelProp) &&
                    !GetPropW(child, interop::kNavBarProp))
                {
                    return true;
                }
            }
            return false;
        }

        // La barra delle schede dell'intestazione (non altri TabView): ha FileExplorerTabControl fra gli antenati vicini.
        bool IsHeaderTabView(muxc::TabView const& tabView)
        {
            mux::DependencyObject node = tabView;
            for (int depth = 0; node && depth < 6; ++depth)
            {
                node = mux::Media::VisualTreeHelper::GetParent(node);
                if (node && winrt::get_class_name(node) == kTabControlType)
                {
                    return true;
                }
            }
            return false;
        }

        // Impedisce che la DLL venga scaricata: il runtime XAML conserva puntatori al nostro codice.
        void PinModule()
        {
            HMODULE pinned = nullptr;
            GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_PIN, reinterpret_cast<LPCWSTR>(&PinModule), &pinned);
        }

        bool WaitForModule(const wchar_t* name, ULONGLONG timeoutMs)
        {
            const ULONGLONG deadline = GetTickCount64() + timeoutMs;
            while (!GetModuleHandleW(name))
            {
                if (GetTickCount64() > deadline)
                {
                    return false;
                }
                Sleep(100);
            }
            return true;
        }

        std::wstring ImageName()
        {
            wchar_t path[MAX_PATH]{};
            GetModuleFileNameW(nullptr, path, ARRAYSIZE(path));
            const wchar_t* name = wcsrchr(path, L'\\');
            return name ? name + 1 : path;
        }

        // Solo sviluppo: dev-mod.txt nella cartella dei dati spegne pezzi della mod alla sua partenza (per isolare un
        // problema in un explorer.exe di prova): "noglass" niente aggancio della zona vetro, "nodiscovery" nessuna finestra
        // cercata (la mod resta caricata ma non fa nulla), "noprobe" solo le finestre nuove (OnApplyTemplate), nessuna
        // ricerca UI Automation di quelle già aperte.
        bool DevSwitchImpl(const char* name)
        {
            static const std::string text = [] {
                std::string content;
                const HANDLE file = CreateFileW((DataDirectory() + L"\\dev-mod.txt").c_str(), GENERIC_READ,
                                                FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr, OPEN_EXISTING, 0, nullptr);
                if (file != INVALID_HANDLE_VALUE)
                {
                    char buffer[256]{};
                    DWORD read = 0;
                    ReadFile(file, buffer, sizeof(buffer) - 1, &read, nullptr);
                    CloseHandle(file);
                    content.assign(buffer, read);
                }
                return content;
            }();
            return text.find(name) != std::string::npos;
        }

        // Richiesta di esportazione dell'albero visuale (per lo sviluppo): basta creare questo file.
        std::wstring DumpRequestFile()
        {
            return DataDirectory() + L"\\dump-tree.txt";
        }

        // Comandi per i test automatici (una riga, es. "move 0 2"): il file viene letto e cancellato.
        std::wstring CommandFile()
        {
            return DataDirectory() + L"\\mod-command.txt";
        }

        // Richiesta dei test (prima riga: l'HWND esadecimale della finestra di prova, poi il comando): la prende e la
        // cancella solo il processo che ha quella finestra. Gli altri processi con la mod (anche quello della barra delle
        // applicazioni, con le finestre dell'utente) la lasciano lì: prima la cancellava chi arrivava primo, e il comando
        // per la finestra di prova si perdeva.
        std::wstring ReadIfOurs(std::wstring const& path)
        {
            std::wstring text;
            const HANDLE file = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_DELETE, nullptr, OPEN_EXISTING, 0, nullptr);
            if (file == INVALID_HANDLE_VALUE)
            {
                return text;
            }
            char buffer[256]{};
            DWORD read = 0;
            ReadFile(file, buffer, sizeof(buffer) - 1, &read, nullptr);
            CloseHandle(file);
            for (DWORD i = 0; i < read; ++i)
            {
                if (buffer[i] == '\r' || buffer[i] == '\n')
                {
                    break;
                }
                text.push_back(static_cast<wchar_t>(static_cast<unsigned char>(buffer[i])));
            }
            DWORD owner = 0;
            const HWND target = reinterpret_cast<HWND>(static_cast<uintptr_t>(wcstoull(text.c_str(), nullptr, 16)));
            if (!target || !GetWindowThreadProcessId(target, &owner) || owner != GetCurrentProcessId())
            {
                return {};
            }
            DeleteFileW(path.c_str());
            return text;
        }
    }

    bool ModCore::DevSwitch(const char* name)
    {
        return DevSwitchImpl(name);
    }

    ModCore& ModCore::Instance()
    {
        // Mai distrutto: all'uscita del processo il runtime XAML è già stato smontato.
        static ModCore* instance = new ModCore();
        return *instance;
    }

    DWORD WINAPI ModCore::BootstrapThreadProc(void*)
    {
        LogInit();
        PinModule();

        if (_wcsicmp(ImageName().c_str(), L"explorer.exe") != 0)
        {
            XPX_MLOG(L"Processo non supportato (%s): la mod resta inattiva.", ImageName().c_str());
            return 0;
        }
        // Nel processo della barra delle applicazioni la porta solo la tray, con le schede verticali attive
        // e la sorveglianza dei crash (ModManager): qui si annota soltanto dove ci si trova.
        const bool shellProcess = ShellProcessId() == GetCurrentProcessId();
        // Una sola copia attiva per processo (una seconda iniezione dopo una ricompilazione non deve
        // avviare un secondo osservatore).
        const std::wstring mutexName = L"Local\\VTabsExplorerMod.Active." + std::to_wstring(GetCurrentProcessId());
        const HANDLE active = CreateMutexW(nullptr, FALSE, mutexName.c_str());
        if (active && GetLastError() == ERROR_ALREADY_EXISTS)
        {
            XPX_MLOG(L"Un'altra copia della mod è già attiva nel processo %lu: questa resta inattiva.", GetCurrentProcessId());
            CloseHandle(active);
            return 0;
        }
        // L'handle resta aperto per tutta la vita del processo (è il "segnaposto" della copia attiva).

        SetVerbose(ModSettings::Load().verboseLog); // log dettagliato solo se chiesto ([Debug] VerboseLog)
        InstallDevExceptionLog(shellProcess);       // solo nelle prove (dev-exceptions.txt)
        // Zona vetro delle finestre (riga del titolo nascosta): finché nessuna finestra ha un limite, l'aggancio la lascia com'è.
        if (!DevSwitch("noglass"))
        {
            glass::Install();
        }
        winrt::init_apartment(winrt::apartment_type::multi_threaded);
        XPX_MLOG(L"VTabsExplorerMod caricata nel processo %lu (%s).", GetCurrentProcessId(),
                 shellProcess ? L"quello della barra delle applicazioni" : L"explorer.exe separato");
        {
            std::scoped_lock lock(Instance().m_lock);
            Instance().m_settings = ModSettings::Load();
        }

        // Il runtime WinUI 3 arriva con la prima finestra di Esplora file: si aspetta senza limite (un
        // controllo al secondo, dopo il primo minuto).
        if (!WaitForModule(L"Microsoft.Internal.FrameworkUdk.dll", 60'000))
        {
            XPX_MLOG(L"Runtime WinUI 3 non ancora presente nel processo: resto in attesa.");
            while (!GetModuleHandleW(L"Microsoft.Internal.FrameworkUdk.dll"))
            {
                Sleep(1000);
            }
        }
        if (DevSwitch("nodiscovery"))
        {
            XPX_MLOG(L"dev-mod.txt: nessuna finestra cercata.");
            return 0;
        }
        Instance().StartDiscovery();
        Instance().BackgroundLoop();
        return 0;
    }

    void ModCore::StartDiscovery()
    {
        // Gli agganci (vedi Discovery.h) vanno installati sul thread di una finestra con XAML già pronto: si aspetta la
        // prima finestra con l'isola dell'intestazione (nel processo della barra delle applicazioni può non essercene
        // nessuna per molto tempo).
        const ULONGLONG started = GetTickCount64();
        int failures = 0;
        while (!discovery::Installed())
        {
            bool tried = false;
            for (HWND window : OurWindows(/*visibleOnly*/ false))
            {
                if (!HasHeaderIsland(window))
                {
                    continue;
                }
                tried = true;
                if (discovery::Install(window, &ModCore::OnTabViewFound))
                {
                    break;
                }
            }
            if (discovery::Installed())
            {
                break;
            }
            if (discovery::Blocked())
            {
                XPX_MLOG(L"Ricerca delle finestre impossibile in questo processo: la mod resta inattiva.");
                return;
            }
            // Nessuna finestra pronta: si riguarda presto. Tentativo fallito: si riprova con calma (e dopo 5 volte di
            // rado), così un impedimento che non passa non diventa una raffica di tentativi e di righe nel log.
            if (tried && ++failures == 5)
            {
                XPX_MLOG(L"Ricerca delle finestre: agganci non installati dopo 5 tentativi, si riprova ogni minuto.");
            }
            Sleep(!tried ? (GetTickCount64() - started < 60'000 ? 250 : 1000) : (failures < 5 ? 5'000 : 60'000));
        }
        XPX_MLOG(L"Ricerca delle finestre pronta in %llu ms.", GetTickCount64() - started);
        // Le finestre già aperte non applicheranno più il modello della barra: le si cerca subito.
        ProbeUnmanaged(/*now*/ true);
    }

    void ModCore::OnTabViewFound(muxc::TabView const& tabView) noexcept
    {
        // Siamo dentro il layout XAML (OnApplyTemplate) o dentro una richiesta UI Automation: qui solo un riferimento
        // debole, il resto al prossimo giro del thread della finestra (con l'intestazione ormai costruita).
        try
        {
            const auto dispatcher = tabView.DispatcherQueue();
            if (!dispatcher)
            {
                return;
            }
            dispatcher.TryEnqueue(DispatcherQueuePriority::Low, [weak = winrt::make_weak(tabView)] {
                try
                {
                    if (const auto found = weak.get())
                    {
                        Instance().RegisterWindow(found);
                    }
                }
                catch (...)
                {
                    XPX_MLOG(L"Nuova finestra: errore 0x%08X", static_cast<unsigned>(winrt::to_hresult()));
                }
            });
        }
        catch (...)
        {
        }
    }

    void ModCore::RegisterWindow(muxc::TabView const& tabView)
    {
        // Solo la barra schede dell'intestazione di Esplora file (non altri TabView).
        if (!IsHeaderTabView(tabView))
        {
            XPX_MVLOG(L"TabView ignorato (non è nell'intestazione di Esplora file).");
            return;
        }
        const uint64_t id = reinterpret_cast<uint64_t>(winrt::get_abi(tabView.as<winrt::Windows::Foundation::IInspectable>()));
        const HWND main = interop::MainWindowOf(tabView);
        std::shared_ptr<WindowEntry> entry;
        size_t count = 0;
        {
            std::scoped_lock lock(m_lock);
            for (auto const& existing : m_windows)
            {
                if (existing->tabView.get() == tabView)
                {
                    return; // già nota (modello riapplicato o ricerca ripetuta)
                }
            }
            entry = std::make_shared<WindowEntry>();
            entry->id = id;
            entry->main = main;
            entry->tabView = winrt::make_weak(tabView);
            entry->dispatcher = tabView.DispatcherQueue();
            m_windows.push_back(entry);
            count = m_windows.size();
        }
        XPX_MLOG(L"Nuova finestra di Esplora file (TabView %llx, finestra %llx), finestre gestite: %zu.", id, reinterpret_cast<uintptr_t>(main), count);
        AttachFeatures(entry);
    }

    void ModCore::ProbeUnmanaged(bool now)
    {
        if (!discovery::Installed() || DevSwitch("noprobe"))
        {
            return;
        }
        std::vector<HWND> managed;
        {
            std::scoped_lock lock(m_lock);
            for (auto const& entry : m_windows)
            {
                managed.push_back(entry->main);
            }
        }
        const ULONGLONG tick = GetTickCount64();
        std::vector<HWND> windows = OurWindows(/*visibleOnly*/ true);
        std::erase_if(m_probes, [&windows](auto const& known) { return std::find(windows.begin(), windows.end(), known.first) == windows.end(); });
        for (HWND window : windows)
        {
            if (std::find(managed.begin(), managed.end(), window) != managed.end() || !HasHeaderIsland(window))
            {
                continue;
            }
            // Una finestra appena comparsa la trova da sé l'aggancio a OnApplyTemplate: la ricerca serve a quelle già
            // aperte quando è arrivata la mod (e come rete di sicurezza), al più ogni 2 s e 10 volte per finestra.
            ProbeState& state = m_probes[window];
            if (!state.first)
            {
                state.first = tick;
            }
            if ((!now && tick - state.first < 1500) || (state.last && tick - state.last < 2000) || state.attempts >= 10)
            {
                continue;
            }
            state.last = tick;
            ++state.attempts;
            const bool found = discovery::Probe(window);
            XPX_MLOG(L"Ricerca della barra delle schede nella finestra %llx (tentativo %d): %s.", reinterpret_cast<uintptr_t>(window), state.attempts,
                     found ? L"trovata" : L"non trovata");
        }
    }

    void ModCore::AttachFeatures(std::shared_ptr<WindowEntry> const& entry)
    {
        const auto tabView = entry->tabView.get();
        if (!tabView)
        {
            return;
        }
        // Intestazione ricreata per una finestra già gestita (stesso thread): si stacca la vecchia, così
        // la finestra non ha mai due pannelli né due subclass.
        if (const HWND main = interop::MainWindowOf(tabView))
        {
            for (auto const& other : SnapshotWindows())
            {
                if (other != entry && other->dispatcher == entry->dispatcher && other->verticalTabs && other->verticalTabs->Main() == main)
                {
                    XPX_MLOG(L"Intestazione ricreata per la finestra %llx: si riparte dalla nuova.", reinterpret_cast<uintptr_t>(main));
                    ReleaseWindow(other, /*restore*/ true);
                }
            }
        }
        if (!entry->verticalTabs)
        {
            entry->verticalTabs = std::make_shared<VerticalTabs>(tabView);
            // Chiusura della finestra (WM_DESTROY, sul suo thread): si dimentica la finestra subito dopo.
            const std::weak_ptr<WindowEntry> weakEntry = entry;
            entry->verticalTabs->onWindowClosed = [this, weakEntry] {
                const auto locked = weakEntry.lock();
                if (!locked)
                {
                    return;
                }
                // Non dentro la catena di chiamate della feature stessa: al prossimo giro del thread.
                locked->dispatcher.TryEnqueue([this, locked] {
                    XPX_MLOG(L"Finestra di Esplora file chiusa (TabView %llx): risorse liberate.", locked->id);
                    ReleaseWindow(locked, /*restore*/ false);
                });
            };
        }
        entry->verticalTabs->Apply(CurrentSettings());

        // Controllo periodico: layout (recupera i casi sfuggiti agli eventi), titoli, icone, selezione.
        if (!entry->timer)
        {
            entry->timer = entry->dispatcher.CreateTimer();
            entry->timer.Interval(std::chrono::milliseconds(500));
            const std::weak_ptr<WindowEntry> weakEntry = entry;
            entry->timer.Tick([weakEntry](auto&&, auto&&) {
                const auto locked = weakEntry.lock();
                if (!locked || !locked->verticalTabs)
                {
                    return;
                }
                try
                {
                    locked->verticalTabs->OnTimer();
                }
                catch (...)
                {
                    XPX_MLOG(L"Controllo periodico: errore 0x%08X", static_cast<unsigned>(winrt::to_hresult()));
                }
            });
            entry->timer.Start();
        }
    }

    void ModCore::ReleaseWindow(std::shared_ptr<WindowEntry> const& entry, bool restore)
    {
        if (entry->timer)
        {
            entry->timer.Stop();
            entry->timer = nullptr;
        }
        if (entry->verticalTabs)
        {
            try
            {
                entry->verticalTabs->Detach(restore);
            }
            catch (...)
            {
            }
            entry->verticalTabs = nullptr;
        }
        std::scoped_lock lock(m_lock);
        std::erase(m_windows, entry);
    }

    std::vector<std::shared_ptr<ModCore::WindowEntry>> ModCore::SnapshotWindows() const
    {
        std::scoped_lock lock(m_lock);
        return m_windows;
    }

    ModSettings ModCore::CurrentSettings() const
    {
        std::scoped_lock lock(m_lock);
        return m_settings;
    }

    void ModCore::ApplySettingsToAllWindows()
    {
        for (auto const& entry : SnapshotWindows())
        {
            entry->dispatcher.TryEnqueue([entry] {
                try
                {
                    // Le modifiche a settings.ini arrivano a raffiche (es. comprimi e subito espandi): ogni
                    // finestra rilegge il file quando applica, così vale sempre l'ultima scelta salvata.
                    if (entry->verticalTabs)
                    {
                        entry->verticalTabs->Apply(ModSettings::Load());
                    }
                }
                catch (...)
                {
                    XPX_MLOG(L"Impostazioni: errore 0x%08X", static_cast<unsigned>(winrt::to_hresult()));
                }
            });
        }
    }

    void ModCore::DumpAllWindows(uint64_t target)
    {
        // Solo la finestra indicata (una finestra di prova): l'albero contiene i nomi delle cartelle.
        for (auto const& entry : SnapshotWindows())
        {
            entry->dispatcher.TryEnqueue([entry, target] {
                try
                {
                    const auto tabView = entry->tabView.get();
                    const HWND main = tabView ? interop::MainWindowOf(tabView) : nullptr;
                    if (main && static_cast<uint64_t>(reinterpret_cast<uintptr_t>(main)) == target)
                    {
                        TreeDump::DumpWindow(tabView, L"richiesta (dump-tree.txt)");
                    }
                }
                catch (...)
                {
                    XPX_MLOG(L"Esportazione: errore 0x%08X", static_cast<unsigned>(winrt::to_hresult()));
                }
            });
        }
    }

    void ModCore::SendDevCommand(std::wstring const& command)
    {
        for (auto const& entry : SnapshotWindows())
        {
            entry->dispatcher.TryEnqueue([entry, command] {
                try
                {
                    if (entry->verticalTabs)
                    {
                        entry->verticalTabs->DevCommand(command);
                    }
                }
                catch (...)
                {
                    XPX_MLOG(L"Comando di prova: errore 0x%08X", static_cast<unsigned>(winrt::to_hresult()));
                }
            });
        }
    }

    void ModCore::DeliverPendingColors()
    {
        const std::wstring directory = DataDirectory() + L"\\colors";
        std::unordered_map<std::wstring, ULONGLONG> seen;
        WIN32_FIND_DATAW find{};
        const HANDLE search = FindFirstFileW((directory + L"\\pending-*.txt").c_str(), &find);
        if (search != INVALID_HANDLE_VALUE)
        {
            do
            {
                const std::wstring name = find.cFileName;
                const ULONGLONG stamp = (static_cast<ULONGLONG>(find.ftLastWriteTime.dwHighDateTime) << 32) | find.ftLastWriteTime.dwLowDateTime;
                constexpr size_t kPrefix = 8; // "pending-"
                if (name.size() <= kPrefix + 4)
                {
                    continue;
                }
                const std::wstring hex = name.substr(kPrefix, name.size() - kPrefix - 4);
                const HWND window = reinterpret_cast<HWND>(static_cast<uintptr_t>(wcstoull(hex.c_str(), nullptr, 16)));
                if (!window || !IsWindow(window))
                {
                    DeleteFileW((directory + L"\\" + name).c_str()); // finestra chiusa prima di riceverli
                    continue;
                }
                DWORD pid = 0;
                GetWindowThreadProcessId(window, &pid);
                if (pid != GetCurrentProcessId())
                {
                    continue; // finestra di un altro explorer.exe: ci pensa la sua copia della mod
                }
                // Si consegna di nuovo finché il file c'è (la finestra lo cancella quando li ha applicati): può
                // arrivare prima che la mod abbia agganciato la finestra. Al massimo una volta al secondo,
                // subito se il file è cambiato.
                const ULONGLONG now = GetTickCount64();
                seen[name] = stamp;
                if (const auto delivered = m_deliveredPending.find(name);
                    delivered != m_deliveredPending.end() && delivered->second == stamp && now - m_deliveredAt[name] < 1000)
                {
                    continue;
                }
                TabColorList list;
                if (!ReadTabColors(directory + L"\\" + name, list))
                {
                    continue;
                }
                m_deliveredPending[name] = stamp;
                m_deliveredAt[name] = now;
                XPX_MVLOG(L"Colori da rimettere per la finestra %llx: %zu schede colorate su %zu.", reinterpret_cast<uintptr_t>(window),
                          list.colored.size(), list.count);
                for (auto const& entry : SnapshotWindows())
                {
                    entry->dispatcher.TryEnqueue([entry, window, list] {
                        try
                        {
                            if (entry->verticalTabs)
                            {
                                entry->verticalTabs->OfferPendingColors(window, list);
                            }
                        }
                        catch (...)
                        {
                            XPX_MLOG(L"Colori da rimettere: errore 0x%08X", static_cast<unsigned>(winrt::to_hresult()));
                        }
                    });
                }
            } while (FindNextFileW(search, &find));
            FindClose(search);
        }
        std::erase_if(m_deliveredPending, [&seen](auto const& entry) { return !seen.contains(entry.first); });
        std::erase_if(m_deliveredAt, [&seen](auto const& entry) { return !seen.contains(entry.first); });
    }

    void ModCore::BackgroundLoop()
    {
        ULONGLONG stamp = ModSettings::FileStamp();
        // Ci si sveglia quando cambia qualcosa nella cartella dei dati (impostazioni, elenco e comandi dei
        // test; i log stanno in una sottocartella, non osservata) o in quella dei colori: a riposo nessun
        // costo. Ogni 5 s un controllo comunque, per sicurezza.
        const std::wstring colorsDirectory = DataDirectory() + L"\\colors";
        CreateDirectoryW(colorsDirectory.c_str(), nullptr);
        constexpr DWORD kFilter = FILE_NOTIFY_CHANGE_LAST_WRITE | FILE_NOTIFY_CHANGE_FILE_NAME | FILE_NOTIFY_CHANGE_SIZE;
        HANDLE changes[] = { FindFirstChangeNotificationW(DataDirectory().c_str(), FALSE, kFilter),
                             FindFirstChangeNotificationW(colorsDirectory.c_str(), FALSE, kFilter) };
        const bool watching = changes[0] != INVALID_HANDLE_VALUE && changes[1] != INVALID_HANDLE_VALUE;
        if (!watching)
        {
            XPX_MLOG(L"Notifiche della cartella dei dati non disponibili (%lu): controllo ogni secondo.", GetLastError());
        }
        DeliverPendingColors();
        for (;;)
        {
            if (watching)
            {
                const DWORD result = WaitForMultipleObjects(2, changes, FALSE, 5000);
                if (result == WAIT_OBJECT_0 || result == WAIT_OBJECT_0 + 1)
                {
                    FindNextChangeNotification(changes[result - WAIT_OBJECT_0]);
                    Sleep(60); // le scritture arrivano a raffiche
                }
            }
            else
            {
                Sleep(1000);
            }
            DeliverPendingColors();
            // Rete di sicurezza: una finestra visibile che nessun aggancio ha presentato si cerca con UI Automation.
            ProbeUnmanaged(/*now*/ false);
            const ULONGLONG current = ModSettings::FileStamp();
            if (current != stamp)
            {
                stamp = current;
                const ModSettings loaded = ModSettings::Load();
                SetVerbose(loaded.verboseLog);
                bool changed = false;
                {
                    std::scoped_lock lock(m_lock);
                    changed = !(loaded == m_settings);
                    m_settings = loaded;
                }
                if (changed)
                {
                    XPX_MLOG(L"Impostazioni cambiate: schede verticali %s, larghezza %d.", loaded.verticalTabs ? L"sì" : L"no", loaded.width);
                    ApplySettingsToAllWindows();
                }
            }
            if (GetFileAttributesW(DumpRequestFile().c_str()) != INVALID_FILE_ATTRIBUTES)
            {
                // Contenuto: l'HWND (esadecimale) della finestra di prova da esportare.
                const std::wstring request = ReadIfOurs(DumpRequestFile());
                if (const uint64_t target = wcstoull(request.c_str(), nullptr, 16))
                {
                    DumpAllWindows(target);
                }
            }
            if (GetFileAttributesW(CommandFile().c_str()) != INVALID_FILE_ATTRIBUTES)
            {
                const std::wstring command = ReadIfOurs(CommandFile());
                if (!command.empty())
                {
                    SendDevCommand(command);
                }
            }
        }
    }
}
