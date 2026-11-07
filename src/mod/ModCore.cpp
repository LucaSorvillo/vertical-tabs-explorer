#include "pch.h"
#include "ModCore.h"

#include "CaptionButtons.h"
#include "DevDiagnostics.h"
#include "Discovery.h"
#include "Glass.h"
#include "Interop.h"
#include "Lifetime.h"
#include "ModLog.h"
#include "ModernMenu.h"
#include "NavItems.h"
#include "TextInput.h"
#include "TreeDump.h"
#include "VerticalTabs.h"
#include "../ModShared.h"

extern HMODULE g_module;

namespace xpx::mod
{
    namespace lifetime
    {
        namespace
        {
            std::atomic<bool> g_unloading{ false };
        }

        std::atomic<int>& ModalDepth()
        {
            static std::atomic<int> depth{ 0 };
            return depth;
        }

        bool Unloading()
        {
            return g_unloading.load();
        }
    }

    namespace
    {
        namespace mux = winrt::Microsoft::UI::Xaml;
        namespace muxc = winrt::Microsoft::UI::Xaml::Controls;
        using winrt::Microsoft::UI::Dispatching::DispatcherQueuePriority;

        // Il segnaposto della copia attiva della mod in questo processo (chiuso allo scaricamento).
        HANDLE g_activeMutex = nullptr;

        bool WaitUntil(std::function<bool()> const& condition, ULONGLONG timeoutMs)
        {
            const ULONGLONG deadline = GetTickCount64() + timeoutMs;
            while (!condition())
            {
                if (GetTickCount64() > deadline)
                {
                    return false;
                }
                Sleep(25);
            }
            return true;
        }

        // Esegue work sul thread di ogni finestra e aspetta che tutte abbiano finito (false allo scadere). Lo stato condiviso
        // sta in un shared_ptr: un lavoro arrivato dopo lo scadere non tocca memoria già liberata.
        template <typename Entry, typename Work>
        bool RunOnEachThread(std::vector<Entry> const& entries, ULONGLONG timeoutMs, Work work)
        {
            struct Pending
            {
                std::atomic<size_t> remaining{ 0 };
                HANDLE done = CreateEventW(nullptr, TRUE, FALSE, nullptr);
                ~Pending()
                {
                    CloseHandle(done);
                }
            };
            if (entries.empty())
            {
                return true;
            }
            const auto pending = std::make_shared<Pending>();
            pending->remaining = entries.size();
            for (auto const& entry : entries)
            {
                bool queued = false;
                try
                {
                    queued = work(entry, [pending] {
                        if (--pending->remaining == 0)
                        {
                            SetEvent(pending->done);
                        }
                    });
                }
                catch (...)
                {
                }
                if (!queued && --pending->remaining == 0)
                {
                    SetEvent(pending->done);
                }
            }
            return WaitForSingleObject(pending->done, static_cast<DWORD>(timeoutMs)) == WAIT_OBJECT_0;
        }

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

        // La DLL non è più fissata: con "Esci" della tray la mod si toglie e la scarica (ModCore::TryUnload, che controlla prima
        // che il runtime XAML non tenga più niente di nostro). Finché è caricata, il riferimento è quello dell'iniezione.

        bool WaitForModule(const wchar_t* name, ULONGLONG timeoutMs)
        {
            const ULONGLONG deadline = GetTickCount64() + timeoutMs;
            while (!GetModuleHandleW(name))
            {
                if (GetTickCount64() > deadline || ModSettings::Load().trayExited)
                {
                    return false;
                }
                Sleep(100);
            }
            return true;
        }

        // La DLL esce dal processo: ultimo atto del thread di controllo (TryUnload ha già tolto tutto).
        [[noreturn]] void UnloadNow(bool apartment)
        {
            RemoveDevExceptionLog();
            if (g_activeMutex)
            {
                CloseHandle(g_activeMutex);
                g_activeMutex = nullptr;
            }
            XPX_MLOG(L"Mod tolta da Esplora file (processo %lu): DLL scaricata.", GetCurrentProcessId());
            if (apartment)
            {
                winrt::clear_factory_cache();
                winrt::uninit_apartment();
            }
            FreeLibraryAndExitThread(g_module, 0);
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

        if (_wcsicmp(ImageName().c_str(), L"explorer.exe") != 0)
        {
            XPX_MLOG(L"Processo non supportato (%s): la mod si scarica.", ImageName().c_str());
            FreeLibraryAndExitThread(g_module, 0);
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
            XPX_MLOG(L"Un'altra copia della mod è già attiva nel processo %lu: questa si scarica.", GetCurrentProcessId());
            CloseHandle(active);
            FreeLibraryAndExitThread(g_module, 0);
        }
        // L'handle resta aperto finché la mod è nel processo (è il "segnaposto" della copia attiva).
        g_activeMutex = active;

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
                // Tray chiusa con "Esci" prima di qualunque finestra: c'è solo l'aggancio della zona vetro da togliere.
                if (ModSettings::Load().trayExited && Instance().TryUnload())
                {
                    UnloadNow(true);
                }
                Sleep(1000);
            }
        }
        if (DevSwitch("nodiscovery"))
        {
            XPX_MLOG(L"dev-mod.txt: nessuna finestra cercata.");
            return 0;
        }
        Instance().StartDiscovery();
        if (Instance().BackgroundLoop())
        {
            UnloadNow(true);
        }
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
            if (lifetime::Unloading() || ModSettings::Load().trayExited)
            {
                return; // tray chiusa con "Esci": ci pensa BackgroundLoop (TryUnload)
            }
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
        if (lifetime::Unloading())
        {
            return;
        }
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
        if (lifetime::Unloading())
        {
            return; // la mod si sta togliendo
        }
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
        if (!discovery::Installed() || DevSwitch("noprobe") || lifetime::Unloading())
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

    void ModCore::TeardownWindow(std::shared_ptr<WindowEntry> const& entry)
    {
        if (entry->timer)
        {
            entry->timer.Stop();
            entry->timer = nullptr;
        }
        if (entry->verticalTabs)
        {
            entry->verticalTabs->onWindowClosed = nullptr;
            try
            {
                entry->verticalTabs->HandOverColors(); // la mod che rientra li rimette
                entry->verticalTabs->Detach(/*restore*/ true); // colonna, barra, pulsanti, intestazione, agganci della finestra
            }
            catch (...)
            {
                XPX_MLOG(L"Scaricamento: errore staccando la finestra %llx (0x%08X).", entry->id, static_cast<unsigned>(winrt::to_hresult()));
            }
            entry->verticalTabs = nullptr; // (qui, sul thread della finestra: oggetti XAML compresi)
        }
        TextInput::Reset();
        std::scoped_lock lock(m_lock);
        std::erase(m_windows, entry);
    }

    bool ModCore::TryUnload()
    {
        if (!lifetime::g_unloading.exchange(true))
        {
            XPX_MLOG(L"Tray chiusa con \"Esci\": la mod si toglie da Esplora file.");
        }
        // 1. Un menu classico o una domanda della mod aperti: il nostro codice è sullo stack di quel thread (e i lavori del
        //    dispatcher arrivano dentro il loro ciclo). Si riprova più tardi (la finestra resta normale nel frattempo).
        if (lifetime::ModalDepth().load() > 0)
        {
            XPX_MLOG(L"Scaricamento rimandato: un menu o una domanda della mod è aperto.");
            return false;
        }
        if (!m_tornDown)
        {
            // 2. Ogni finestra sul suo thread: tutto come prima della mod.
            const auto windows = SnapshotWindows();
            for (auto const& entry : windows)
            {
                if (entry->dispatcher && std::find(m_seenDispatchers.begin(), m_seenDispatchers.end(), entry->dispatcher) == m_seenDispatchers.end())
                {
                    m_seenDispatchers.push_back(entry->dispatcher);
                }
            }
            const bool detached = RunOnEachThread(windows, 8000, [this](std::shared_ptr<WindowEntry> const& entry, auto done) {
                return entry->dispatcher && entry->dispatcher.TryEnqueue(DispatcherQueuePriority::High, [this, entry, done] {
                           TeardownWindow(entry);
                           done();
                       });
            });
            if (!detached)
            {
                XPX_MLOG(L"Scaricamento rimandato: non tutte le finestre si sono staccate in tempo.");
                return false;
            }
            {
                std::scoped_lock lock(m_lock);
                if (!m_windows.empty())
                {
                    // Il thread di una finestra non esiste più: i suoi oggetti XAML si possono solo abbandonare (e i nostri
                    // delegati che tengono fanno restare la DLL: vedi il contatore più sotto).
                    XPX_MLOG(L"Scaricamento: %zu finestre senza thread, abbandonate.", m_windows.size());
                    static auto* graveyard = new std::vector<std::shared_ptr<WindowEntry>>();
                    graveyard->insert(graveyard->end(), m_windows.begin(), m_windows.end());
                    m_windows.clear();
                }
            }
            // 3. Agganci: di nuovo le funzioni originali di Esplora file.
            if (!discovery::Uninstall() || !glass::Uninstall())
            {
                XPX_MLOG(L"Scaricamento impossibile: un aggancio non si può togliere (la mod resta, spenta).");
                return false;
            }
            m_tornDown = true;
            XPX_MLOG(L"Scaricamento: %zu finestre staccate, agganci tolti.", windows.size());
        }
        // 4. Nessuno dentro i nostri agganci: i contatori, poi un giro del ciclo di ogni thread delle finestre (chi era entrato
        //    prima del ripristino ne è uscito quando il suo thread torna al ciclo dei messaggi), poi un attimo di margine.
        if (!WaitUntil([] { return discovery::InFlight() == 0 && glass::InFlight() == 0; }, 3000))
        {
            XPX_MLOG(L"Scaricamento rimandato: chiamate ancora dentro i nostri agganci.");
            return false;
        }
        if (!RunOnEachThread(m_seenDispatchers, 5000,
                             [](winrt::Microsoft::UI::Dispatching::DispatcherQueue const& dispatcher, auto done) {
                                 return dispatcher.TryEnqueue(DispatcherQueuePriority::Low, [done] { done(); });
                             }))
        {
            XPX_MLOG(L"Scaricamento rimandato: un thread delle finestre non risponde.");
            return false;
        }
        Sleep(300);
        // 5. Il thread delle letture della barra delle cartelle.
        if (!NavItems::Instance().Stop(5000))
        {
            return false;
        }
        // 6. Oggetti nostri ancora vivi (delegati degli eventi XAML, oggetti COM dati alla shell): solo a zero la DLL può
        //    uscire, altrimenti Esplora file chiamerebbe codice che non c'è più.
        if (!WaitUntil([] { return static_cast<uint32_t>(winrt::get_module_lock()) == 0; }, 5000))
        {
            XPX_MLOG(L"Scaricamento rimandato: %u oggetti della mod ancora vivi in Esplora file.", static_cast<uint32_t>(winrt::get_module_lock()));
            return false;
        }
        // 7. Classi di finestra (falliscono se ne esiste ancora una finestra: allora la DLL resta).
        if (!CaptionButtons::UnregisterWindowClass() || !ModernMenu::UnregisterWindowClass())
        {
            XPX_MLOG(L"Scaricamento rimandato: una finestra della mod esiste ancora (%lu).", GetLastError());
            return false;
        }
        m_seenDispatchers.clear();
        return true;
    }

    void ModCore::Resume()
    {
        XPX_MLOG(L"Tray di nuovo avviata prima dello scaricamento: la mod riparte.");
        m_tornDown = false;
        m_seenDispatchers.clear();
        m_probes.clear();
        lifetime::g_unloading = false;
        if (!DevSwitch("noglass"))
        {
            glass::Install();
        }
        StartDiscovery();
    }

    bool ModCore::BackgroundLoop()
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
        if (!CurrentSettings().trayExited)
        {
            DeliverPendingColors();
        }
        bool first = true;
        for (;;)
        {
            if (first)
            {
                first = false; // (subito: la tray può essere già stata chiusa mentre si cercavano le finestre)
            }
            else if (watching)
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
            const ULONGLONG current = ModSettings::FileStamp();
            if (current != stamp)
            {
                stamp = current;
                const ModSettings loaded = ModSettings::Load();
                SetVerbose(loaded.verboseLog);
                bool changed = false;
                bool wasExited = false;
                {
                    std::scoped_lock lock(m_lock);
                    changed = !(loaded == m_settings);
                    wasExited = m_settings.trayExited;
                    m_settings = loaded;
                }
                if (loaded.trayExited != wasExited)
                {
                    XPX_MLOG(loaded.trayExited ? L"Tray chiusa con \"Esci\": la mod si spegne in tutte le finestre e si toglie da Esplora file."
                                               : L"Tray di nuovo avviata: la mod si riaccende con le impostazioni salvate.");
                }
                if (changed)
                {
                    XPX_MLOG(L"Impostazioni cambiate: schede verticali %s, barra delle cartelle %s, larghezza %d.", loaded.verticalTabs ? L"sì" : L"no",
                             loaded.navBar ? L"sì" : L"no", loaded.width);
                    ApplySettingsToAllWindows();
                }
            }
            if (CurrentSettings().trayExited)
            {
                // Le finestre sono già tornate normali (impostazioni spente); ora via tutto e la DLL esce. Se non si può
                // ancora (un menu aperto, un oggetto ancora vivo), si riprova al prossimo giro (al più 5 s).
                if (TryUnload())
                {
                    for (HANDLE change : changes)
                    {
                        if (change != INVALID_HANDLE_VALUE)
                        {
                            FindCloseChangeNotification(change);
                        }
                    }
                    return true;
                }
                continue;
            }
            if (m_tornDown || lifetime::Unloading())
            {
                Resume(); // la tray è ripartita prima che la DLL uscisse
            }
            DeliverPendingColors();
            // Rete di sicurezza: una finestra visibile che nessun aggancio ha presentato si cerca con UI Automation.
            ProbeUnmanaged(/*now*/ false);
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
