#include "ModManager.h"

#include "Common.h"
#include "Injector.h"
#include "ModShared.h"
#include "Settings.h"

#include <algorithm>

namespace xpx
{
    namespace
    {
        constexpr DWORD kTickMs = 1500;
        constexpr ULONGLONG kRetryMs = 30'000;     // dopo un'iniezione non riuscita
        constexpr int kMaxFailures = 3;            // poi si lascia stare quel processo
        constexpr ULONGLONG kRecheckMs = 10'000;   // funzione spenta: ogni quanto si guarda se la mod c'è già
        constexpr size_t kMaxTracked = MAXIMUM_WAIT_OBJECTS - 2;
        // Una finestra appena comparsa si sta ancora costruendo: la mod (il cui aggancio XAML passa dal
        // thread della finestra) entra solo dopo questo tempo.
        constexpr ULONGLONG kSettleMs = 1500;

        // Finestra velata dal ripristino della sessione (WS_EX_LAYERED con opacità zero, vedi Veil.h): le
        // sue schede si stanno ricostruendo.
        bool IsVeiled(HWND window)
        {
            if (!(GetWindowLongW(window, GWL_EXSTYLE) & WS_EX_LAYERED))
            {
                return false;
            }
            COLORREF key = 0;
            BYTE alpha = 255;
            DWORD flags = 0;
            return GetLayeredWindowAttributes(window, &key, &alpha, &flags) && (flags & LWA_ALPHA) && alpha == 0;
        }

        std::wstring ModSourcePath()
        {
            wchar_t exe[MAX_PATH * 2]{};
            GetModuleFileNameW(nullptr, exe, ARRAYSIZE(exe));
            std::wstring directory{ exe };
            directory.resize(directory.find_last_of(L'\\'));
            return directory + L"\\" + kModDllName;
        }
    }

    ModManager::~ModManager()
    {
        Stop();
    }

    void ModManager::Start(std::function<void(std::wstring const&)> notify)
    {
        m_notify = std::move(notify);
        m_stop = CreateEventW(nullptr, TRUE, FALSE, nullptr);
        m_wake = CreateEventW(nullptr, FALSE, FALSE, nullptr);
        m_thread = std::thread([this] { Run(); });
    }

    void ModManager::Stop()
    {
        if (m_thread.joinable())
        {
            SetEvent(m_stop);
            m_thread.join();
        }
        for (auto const& tracked : m_tracked)
        {
            CloseHandle(tracked.process);
        }
        m_tracked.clear();
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

    void ModManager::Wake()
    {
        if (m_wake)
        {
            SetEvent(m_wake);
        }
    }

    void ModManager::Run()
    {
        for (;;)
        {
            std::vector<HANDLE> handles{ m_stop, m_wake };
            for (auto const& tracked : m_tracked)
            {
                handles.push_back(tracked.process);
            }
            const DWORD result = WaitForMultipleObjects(static_cast<DWORD>(handles.size()), handles.data(), FALSE, kTickMs);
            if (result == WAIT_OBJECT_0)
            {
                return;
            }
            if (result >= WAIT_OBJECT_0 + 2 && result < WAIT_OBJECT_0 + handles.size())
            {
                // Un processo sorvegliato è terminato.
                const size_t index = result - WAIT_OBJECT_0 - 2;
                const Tracked tracked = m_tracked[index];
                m_tracked.erase(m_tracked.begin() + static_cast<ptrdiff_t>(index));
                OnProcessExit(tracked);
                CloseHandle(tracked.process);
                continue;
            }
            try
            {
                Tick();
            }
            catch (...)
            {
                Log(L"Schede verticali: errore nel controllo dei processi.");
            }
        }
    }

    ModManager::Attempt& ModManager::AttemptFor(DWORD pid)
    {
        for (auto& attempt : m_attempts)
        {
            if (attempt.pid == pid)
            {
                return attempt;
            }
        }
        m_attempts.push_back(Attempt{ pid });
        return m_attempts.back();
    }

    bool ModManager::IsTracked(DWORD pid) const
    {
        return std::any_of(m_tracked.begin(), m_tracked.end(), [pid](Tracked const& tracked) { return tracked.pid == pid; });
    }

    void ModManager::Track(DWORD pid, bool injectedByUs)
    {
        if (IsTracked(pid) || m_tracked.size() >= kMaxTracked)
        {
            return;
        }
        const HANDLE process = OpenProcess(SYNCHRONIZE | PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
        if (!process)
        {
            return;
        }
        m_tracked.push_back(Tracked{ pid, process, injectedByUs });
    }

    std::vector<ModManager::ProcessWindows> ModManager::ScanWindows(ULONGLONG now)
    {
        std::vector<ProcessWindows> processes;
        std::unordered_map<HWND, ULONGLONG> seen;
        for (HWND window = FindWindowExW(nullptr, nullptr, L"CabinetWClass", nullptr); window;
             window = FindWindowExW(nullptr, window, L"CabinetWClass", nullptr))
        {
            if (!IsWindowVisible(window))
            {
                continue;
            }
            const auto known = m_firstSeen.find(window);
            const ULONGLONG first = known != m_firstSeen.end() ? known->second : now;
            seen[window] = first;
            DWORD pid = 0;
            GetWindowThreadProcessId(window, &pid);
            if (!pid)
            {
                continue;
            }
            auto entry = std::find_if(processes.begin(), processes.end(), [pid](ProcessWindows const& p) { return p.pid == pid; });
            if (entry == processes.end())
            {
                processes.push_back(ProcessWindows{ pid, true });
                entry = processes.end() - 1;
            }
            if (now - first < kSettleMs || IsVeiled(window))
            {
                entry->settled = false;
            }
        }
        m_firstSeen = std::move(seen);
        return processes;
    }

    void ModManager::Tick()
    {
        const Settings settings = Settings::Load();
        // La mod porta sia le schede verticali sia la barra delle cartelle; sospesa, niente iniezioni.
        const bool wanted = (settings.verticalTabs || settings.navBar) && !settings.verticalTabsSuspended;
        const DWORD shell = ShellProcessId();
        const ULONGLONG now = GetTickCount64();
        const std::vector<ProcessWindows> processes = ScanWindows(now);

        // Tentativi di processi che non ci sono più: dimenticati.
        std::erase_if(m_attempts, [&processes](Attempt const& attempt) {
            return std::none_of(processes.begin(), processes.end(), [&attempt](ProcessWindows const& p) { return p.pid == attempt.pid; });
        });

        // La mod si toglie da sola dopo "Esci": un processo sorvegliato può non averla più (tray ripartita mentre usciva).
        // Ogni 5 s si controlla; senza mod il processo torna fra quelli da iniettare (subito qui sotto).
        if (wanted && now - m_lastVerify >= 5000)
        {
            m_lastVerify = now;
            for (auto it = m_tracked.begin(); it != m_tracked.end();)
            {
                if (WaitForSingleObject(it->process, 0) == WAIT_TIMEOUT && !IsModLoaded(it->pid))
                {
                    Log(L"Schede verticali: la mod non è più nel processo %lu, la si rimette.", it->pid);
                    CloseHandle(it->process);
                    it = m_tracked.erase(it);
                    continue;
                }
                ++it;
            }
        }

        std::wstring dll;
        for (auto const& [pid, settled] : processes)
        {
            if (IsTracked(pid))
            {
                continue;
            }
            Attempt& attempt = AttemptFor(pid);
            // La mod c'è già (iniettata prima di questo avvio della tray): basta sorvegliare il processo.
            // A funzione spenta il controllo si ripete solo ogni tanto (costa un'istantanea dei moduli).
            if (wanted || !attempt.checkedWithoutMod || now - attempt.at >= kRecheckMs)
            {
                if (IsModLoaded(pid))
                {
                    Track(pid, false);
                    continue;
                }
                if (!wanted)
                {
                    attempt.checkedWithoutMod = true;
                    attempt.at = now;
                }
            }
            if (!wanted || (settings.skipShellProcess && pid == shell))
            {
                continue;
            }
            // Finestre appena aperte o in ricostruzione (sessione velata): si aspetta che siano complete.
            if (!settled)
            {
                continue;
            }
            if (attempt.failures >= kMaxFailures || (attempt.failures > 0 && now - attempt.at < kRetryMs))
            {
                continue;
            }
            if (dll.empty())
            {
                dll = PrepareModDll(ModSourcePath());
                if (dll.empty())
                {
                    Log(L"Schede verticali: %s non trovata accanto a VTabsExplorer.exe.", kModDllName);
                    return;
                }
            }
            std::wstring error;
            if (InjectMod(pid, dll, error, /*allowShell*/ true))
            {
                Log(L"Schede verticali: mod caricata nel processo %lu%s.", pid, pid == shell ? L" (quello della barra delle applicazioni)" : L"");
                Track(pid, true);
            }
            else
            {
                ++attempt.failures;
                attempt.at = now;
                Log(L"Schede verticali: mod non caricata nel processo %lu (%s).", pid, error.c_str());
            }
        }
    }

    void ModManager::OnProcessExit(Tracked const& tracked)
    {
        DWORD code = 0;
        GetExitCodeProcess(tracked.process, &code);
        // Codici d'uscita "di errore" (NTSTATUS 0xC...): eccezione non gestita (0xC0000005), fail fast
        // (0xC0000409, 0xC000027B)... Un'uscita normale (disconnessione, riavvio da Gestione attività) no,
        // e nemmeno la chiusura forzata da uno strumento (TerminateProcess con -1, cioè 0xFFFFFFFF).
        const bool crash = (code & 0xF0000000) == 0xC0000000;
        Log(L"Schede verticali: explorer.exe %lu (con la mod) terminato, codice 0x%08X%s.", tracked.pid, code, crash ? L": chiusura anomala" : L"");
        if (!crash)
        {
            return;
        }
        Settings settings = Settings::Load();
        if (!settings.verticalTabs && settings.verticalTabsSuspended)
        {
            return; // già sospese
        }
        settings.verticalTabs = false;
        settings.verticalTabsSuspended = true;
        settings.Save();
        Log(L"Schede verticali sospese per sicurezza (si riattivano dal menu della tray).");
        if (!DevFilterRoot().empty())
        {
            Log(L"Modalità prova: avviso all'utente non mostrato.");
        }
        else if (m_notify)
        {
            m_notify(Tr(L"File Explorer closed unexpectedly while the vertical tabs were on: they are suspended for safety. "
                        L"You can turn them back on from the VTabsExplorer menu.",
                        L"Esplora file si è chiuso in modo anomalo mentre erano attive le schede verticali: le ho sospese per sicurezza. "
                        L"Puoi riattivarle dal menu di VTabsExplorer."));
        }
    }
}
