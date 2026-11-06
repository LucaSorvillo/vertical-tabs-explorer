// VTabsExplorer.exe - icona nella tray che ricorda le finestre e le schede di Esplora file e le riapre
// quando Esplora file viene riaperto (anche dopo un riavvio del PC).
//
// Nessuna modifica a Esplora file: tutto passa dalle API della shell (IShellWindows), da UI Automation,
// da un controllo locale delle sue finestre (classe e titolo) e dai suoi eventi di creazione e comparsa
// delle finestre (per velarle mentre si ripristinano), tutto dall'esterno.
//
// Riga di comando:
//   VTabsExplorer.exe                avvia la tray (istanza singola)
//   VTabsExplorer.exe --restore-now  chiede alla tray di riaprire subito l'ultima sessione
//   VTabsExplorer.exe --reopen-tab   riapre l'ultima scheda chiusa (come Ctrl+Maiusc+T)
//   VTabsExplorer.exe --exit         chiude la tray in esecuzione
//   VTabsExplorer.exe --inject-pid N (sviluppo) inietta la mod delle schede verticali nell'explorer.exe N,
//                                 mai in quello della barra delle applicazioni
//
// Schede verticali: l'unica parte che lavora DENTRO Esplora file (VTabsExplorerMod.dll, vedi ModManager.h),
// solo se attivata dal menu.

#include "Common.h"
#include "Injector.h"
#include "ModManager.h"
#include "ModShared.h"
#include "Settings.h"
#include "TrayIcon.h"
#include "Worker.h"

#include <shellapi.h>
#include <windowsx.h>

#include <cstdlib>
#include <exception>
#include <mutex>
#include <string>
#include <vector>

namespace
{
    constexpr wchar_t kWindowClass[] = L"VTabsExplorer.Tray";
    constexpr wchar_t kMutexName[] = L"Local\\VTabsExplorer.Tray";
    constexpr wchar_t kRunKey[] = L"Software\\Microsoft\\Windows\\CurrentVersion\\Run";
    constexpr UINT WM_TRAY = WM_APP + 1;
    constexpr UINT WM_NOTIFY_USER = WM_APP + 2; // fumetto della tray (testo in g_pendingNotice)
    constexpr wchar_t kTrayGlyph = L'\xE81C'; // "History": solo se l'icona a contorno non si disegna

    enum MenuId : UINT
    {
        IdRestoreOnOpen = 100,
        IdRestoreNow,
        IdBackToTop,
        IdReopenTab,
        IdVerticalTabs,
        IdReopenNow,
        IdAutostart,
        IdOpenData,
        IdLeaveDevMode,
        IdExit,
        IdNavBar, // in fondo: i numeri di quelle prima li usano i test (101 "Riapri ora", 104 schede verticali)
        IdOpenInTabs,
    };

    xpx::Worker g_worker;
    xpx::ModManager g_mods;
    UINT g_taskbarCreatedMsg = 0;
    HICON g_icon = nullptr;
    HWND g_trayWindow = nullptr;
    std::mutex g_noticeLock;
    std::wstring g_pendingNotice;

    bool IsDevModeActive()
    {
        return GetFileAttributesW(xpx::DevFilterPath().c_str()) != INVALID_FILE_ATTRIBUTES;
    }

    // Finestra di Esplora file in primo piano, per --reopen-tab (con la scorciatoia la sa già il gancio).
    HWND ForegroundExplorerWindow()
    {
        const HWND foreground = GetForegroundWindow();
        wchar_t name[64]{};
        if (foreground)
        {
            GetClassNameW(foreground, name, ARRAYSIZE(name));
        }
        return xpx::EqualsIgnoreCase(name, L"CabinetWClass") ? foreground : nullptr;
    }

    bool IsAutostartEnabled()
    {
        wchar_t value[MAX_PATH * 2]{};
        DWORD size = sizeof(value);
        return RegGetValueW(HKEY_CURRENT_USER, kRunKey, xpx::kAppName, RRF_RT_REG_SZ, nullptr, value, &size) == ERROR_SUCCESS;
    }

    void SetAutostart(bool enable)
    {
        HKEY key = nullptr;
        if (RegOpenKeyExW(HKEY_CURRENT_USER, kRunKey, 0, KEY_SET_VALUE, &key) != ERROR_SUCCESS)
        {
            return;
        }
        if (enable)
        {
            wchar_t exe[MAX_PATH * 2]{};
            GetModuleFileNameW(nullptr, exe, ARRAYSIZE(exe));
            const std::wstring command = L"\"" + std::wstring{ exe } + L"\"";
            RegSetValueExW(key, xpx::kAppName, 0, REG_SZ, reinterpret_cast<const BYTE*>(command.c_str()),
                           static_cast<DWORD>((command.size() + 1) * sizeof(wchar_t)));
        }
        else
        {
            RegDeleteValueW(key, xpx::kAppName);
        }
        RegCloseKey(key);
    }

    void UpdateTrayIcon(HWND hwnd, DWORD message)
    {
        const UINT dpi = GetDpiForWindow(hwnd);
        const int size = GetSystemMetricsForDpi(SM_CXSMICON, dpi ? dpi : USER_DEFAULT_SCREEN_DPI);
        // Cartella a contorno che richiama Esplora file (scelta dell'utente: monocroma, non l'icona di Windows);
        // il glifo solo se GDI+ non parte.
        HICON icon = xpx::CreateFolderOutlineIcon(size, xpx::IsTaskbarLight());
        if (!icon)
        {
            icon = xpx::CreateGlyphIcon(kTrayGlyph, size, xpx::IsTaskbarLight());
        }

        NOTIFYICONDATAW data{ sizeof(data) };
        data.hWnd = hwnd;
        data.uID = 1;
        data.uFlags = NIF_ICON | NIF_MESSAGE | NIF_TIP | NIF_SHOWTIP;
        data.uCallbackMessage = WM_TRAY;
        data.hIcon = icon;
        wcscpy_s(data.szTip, IsDevModeActive() ? xpx::Tr(L"VTabsExplorer - test mode on", L"VTabsExplorer - modalità prova attiva")
                                           : xpx::Tr(L"VTabsExplorer - remembers your File Explorer folders", L"VTabsExplorer - ricorda le cartelle di Esplora file"));
        Shell_NotifyIconW(message, &data);
        if (message == NIM_ADD)
        {
            data.uVersion = NOTIFYICON_VERSION_4;
            Shell_NotifyIconW(NIM_SETVERSION, &data);
        }
        if (g_icon)
        {
            DestroyIcon(g_icon);
        }
        g_icon = icon;
    }

    // Fumetto della tray (avvisi importanti, es. schede verticali sospese).
    void ShowNotice(HWND hwnd, std::wstring const& text)
    {
        NOTIFYICONDATAW data{ sizeof(data) };
        data.hWnd = hwnd;
        data.uID = 1;
        data.uFlags = NIF_INFO;
        data.dwInfoFlags = NIIF_WARNING;
        wcscpy_s(data.szInfoTitle, xpx::kAppName);
        wcsncpy_s(data.szInfo, text.c_str(), _TRUNCATE);
        Shell_NotifyIconW(NIM_MODIFY, &data);
    }

    void RemoveTrayIcon(HWND hwnd)
    {
        NOTIFYICONDATAW data{ sizeof(data) };
        data.hWnd = hwnd;
        data.uID = 1;
        Shell_NotifyIconW(NIM_DELETE, &data);
    }

    std::wstring Count(size_t windows, size_t tabs)
    {
        if (!xpx::UiItalian())
        {
            return std::to_wstring(windows) + (windows == 1 ? L" window, " : L" windows, ") + std::to_wstring(tabs) + (tabs == 1 ? L" tab" : L" tabs");
        }
        return std::to_wstring(windows) + (windows == 1 ? L" finestra, " : L" finestre, ") + std::to_wstring(tabs) + (tabs == 1 ? L" scheda" : L" schede");
    }

    void ShowTrayMenu(HWND hwnd, POINT anchor)
    {
        const xpx::Settings settings = xpx::Settings::Load();
        const auto status = g_worker.GetStatus();

        HMENU menu = CreatePopupMenu();
        const std::wstring saved = status.savedWindows ? (status.liveWindows ? xpx::Tr(L"Current session: ", L"Sessione attuale: ")
                                                                             : xpx::Tr(L"Last session: ", L"Ultima sessione: ")) +
                                                             Count(status.savedWindows, status.savedTabs)
                                                       : std::wstring(xpx::Tr(L"No saved session", L"Nessuna sessione salvata"));
        AppendMenuW(menu, MF_STRING | MF_GRAYED, 0, saved.c_str());
        AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
        AppendMenuW(menu, MF_STRING | (settings.restoreOnOpen ? MF_CHECKED : 0), IdRestoreOnOpen,
                    xpx::Tr(L"Reopen your folders when File Explorer opens", L"Riapri le cartelle all'apertura di Esplora file"));
        AppendMenuW(menu, MF_STRING | (status.liveWindows == 0 && status.savedWindows > 0 ? 0 : MF_GRAYED), IdRestoreNow,
                    xpx::Tr(L"Reopen the last session now", L"Riapri ora l'ultima sessione"));
        AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
        AppendMenuW(menu, MF_STRING | (settings.backToTop ? MF_CHECKED : 0), IdBackToTop, xpx::Tr(L"Back-to-top button in the file view", L"Pulsante «Torna su» nella vista dei file"));
        AppendMenuW(menu, MF_STRING | (settings.reopenTab ? MF_CHECKED : 0), IdReopenTab, xpx::Tr(L"Reopen closed tabs with Ctrl+Shift+T", L"Riapri la scheda chiusa con Ctrl+Maiusc+T"));
        AppendMenuW(menu, MF_STRING | (settings.openInTabs ? MF_CHECKED : 0), IdOpenInTabs,
                    xpx::Tr(L"Folders opened by other apps go into a tab (like Chrome)", L"Cartelle aperte da altri programmi in una scheda (come Chrome)"));
        AppendMenuW(menu, MF_STRING | (settings.verticalTabs && !settings.verticalTabsSuspended ? MF_CHECKED : 0), IdVerticalTabs,
                    xpx::Tr(L"Vertical tabs (column on the left)", L"Schede verticali (colonna a sinistra)"));
        if (settings.verticalTabsSuspended)
        {
            AppendMenuW(menu, MF_STRING | MF_GRAYED, 0, xpx::Tr(L"      suspended: File Explorer closed unexpectedly", L"      sospese: Esplora file si è chiuso in modo anomalo"));
        }
        AppendMenuW(menu, MF_STRING | (settings.navBar && !settings.verticalTabsSuspended ? MF_CHECKED : 0), IdNavBar,
                    xpx::Tr(L"Navigation pane at the top (folders bar)", L"Riquadro di spostamento in alto (barra delle cartelle)"));
        AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
        AppendMenuW(menu, MF_STRING | (IsAutostartEnabled() ? MF_CHECKED : 0), IdAutostart, xpx::Tr(L"Start with Windows", L"Avvia con Windows"));
        AppendMenuW(menu, MF_STRING, IdOpenData, xpx::Tr(L"Open settings and log folder", L"Apri cartella impostazioni e log"));
        if (IsDevModeActive())
        {
            AppendMenuW(menu, MF_STRING, IdLeaveDevMode, xpx::Tr(L"Leave test mode (test windows only)", L"Esci dalla modalità prova (solo finestre di prova)"));
        }
        AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
        AppendMenuW(menu, MF_STRING, IdExit, xpx::Tr(L"Exit", L"Esci"));

        // Necessario perché il menu si chiuda cliccando altrove.
        SetForegroundWindow(hwnd);
        const UINT align = GetSystemMetrics(SM_MENUDROPALIGNMENT) ? TPM_RIGHTALIGN : TPM_LEFTALIGN;
        TrackPopupMenuEx(menu, TPM_RIGHTBUTTON | TPM_BOTTOMALIGN | align, anchor.x, anchor.y, hwnd, nullptr);
        PostMessageW(hwnd, WM_NULL, 0, 0);
        DestroyMenu(menu);
    }

    void OnMenuCommand(HWND hwnd, UINT id)
    {
        xpx::Settings settings = xpx::Settings::Load();
        switch (id)
        {
        case IdRestoreOnOpen:
            settings.restoreOnOpen = !settings.restoreOnOpen;
            settings.Save();
            xpx::Log(L"Ripristino all'apertura di Esplora file: %s.", settings.restoreOnOpen ? L"attivo" : L"spento");
            g_worker.Wake();
            break;
        case IdRestoreNow:
            g_worker.RequestRestore();
            break;
        case IdBackToTop:
            settings.backToTop = !settings.backToTop;
            settings.Save();
            xpx::Log(L"Pulsante \"Torna su\": %s.", settings.backToTop ? L"attivo" : L"spento");
            g_worker.Wake();
            break;
        case IdReopenTab:
            settings.reopenTab = !settings.reopenTab;
            settings.Save();
            xpx::Log(L"Ctrl+Maiusc+T (riapri la scheda chiusa): %s.", settings.reopenTab ? L"attiva" : L"spenta");
            g_worker.Wake();
            break;
        case IdVerticalTabs:
            // Da sospese, il clic le riattiva (l'utente ha visto l'avviso e sceglie di riprovare).
            settings.verticalTabs = settings.verticalTabsSuspended ? true : !settings.verticalTabs;
            settings.verticalTabsSuspended = false;
            settings.Save();
            xpx::Log(L"Schede verticali: %s.", settings.verticalTabs ? L"attive" : L"spente");
            g_mods.Wake();
            break;
        case IdNavBar:
            // Anche questa sta nella mod (che rilegge settings.ini da sé); da sospese, il clic la riattiva.
            settings.navBar = settings.verticalTabsSuspended ? true : !settings.navBar;
            settings.verticalTabsSuspended = false;
            settings.Save();
            xpx::Log(L"Riquadro di spostamento in alto: %s.", settings.navBar ? L"sì" : L"no");
            g_mods.Wake();
            break;
        case IdOpenInTabs:
            settings.openInTabs = !settings.openInTabs;
            settings.Save();
            xpx::Log(L"Cartelle aperte da altri programmi in una scheda: %s.", settings.openInTabs ? L"sì" : L"no");
            g_worker.Wake();
            break;
        case IdReopenNow:
            g_worker.RequestReopen(ForegroundExplorerWindow());
            break;
        case IdAutostart:
            SetAutostart(!IsAutostartEnabled());
            break;
        case IdOpenData:
            ShellExecuteW(hwnd, L"open", xpx::DataDirectory().c_str(), nullptr, nullptr, SW_SHOWNORMAL);
            break;
        case IdLeaveDevMode:
            DeleteFileW(xpx::DevFilterPath().c_str());
            UpdateTrayIcon(hwnd, NIM_MODIFY);
            g_worker.Wake();
            xpx::Log(L"Modalità prova disattivata (dev-filter.txt rimosso).");
            break;
        case IdExit:
            DestroyWindow(hwnd);
            break;
        default:
            break;
        }
    }

    LRESULT CALLBACK WindowProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam)
    {
        switch (message)
        {
        case WM_CREATE:
            UpdateTrayIcon(hwnd, NIM_ADD);
            return 0;

        case WM_TRAY:
            switch (LOWORD(lParam))
            {
            case WM_CONTEXTMENU:
            case NIN_SELECT:
            case NIN_KEYSELECT:
                // Con NOTIFYICON_VERSION_4 le coordinate del punto di ancoraggio arrivano in wParam.
                ShowTrayMenu(hwnd, POINT{ GET_X_LPARAM(wParam), GET_Y_LPARAM(wParam) });
                break;
            default:
                break;
            }
            return 0;

        case WM_COMMAND:
            OnMenuCommand(hwnd, LOWORD(wParam));
            return 0;

        case WM_NOTIFY_USER:
        {
            std::wstring text;
            {
                std::scoped_lock lock(g_noticeLock);
                text.swap(g_pendingNotice);
            }
            if (!text.empty())
            {
                ShowNotice(hwnd, text);
            }
            return 0;
        }

        case WM_QUERYENDSESSION:
            // Spegnimento o disconnessione: le finestre si chiudono in ordine sparso, la sessione resta com'è.
            g_worker.SetEnding(true);
            return TRUE;

        case WM_ENDSESSION:
            if (!wParam)
            {
                g_worker.SetEnding(false);
            }
            return 0;

        case WM_SETTINGCHANGE:
            if (lParam && wcscmp(reinterpret_cast<const wchar_t*>(lParam), L"ImmersiveColorSet") == 0)
            {
                UpdateTrayIcon(hwnd, NIM_MODIFY);
            }
            return 0;

        case WM_DPICHANGED:
            UpdateTrayIcon(hwnd, NIM_MODIFY);
            return 0;

        case WM_DESTROY:
            RemoveTrayIcon(hwnd);
            PostQuitMessage(0);
            return 0;

        default:
            break;
        }

        if (message == g_taskbarCreatedMsg && g_taskbarCreatedMsg != 0)
        {
            // Esplora file riavviato: l'icona va rifatta, e le finestre ricontrollate.
            UpdateTrayIcon(hwnd, NIM_ADD);
            g_worker.Wake();
            g_mods.Wake();
            return 0;
        }
        return DefWindowProcW(hwnd, message, wParam, lParam);
    }

    // Un errore fatale non deve lasciare invisibile una finestra di Esplora file velata.
    LONG WINAPI OnCrash(EXCEPTION_POINTERS*)
    {
        g_worker.EmergencyLift();
        return EXCEPTION_CONTINUE_SEARCH;
    }

    // Valore che segue un'opzione, es. "--inject-pid 1234" -> 1234. 0 se assente.
    DWORD ArgumentValue(const wchar_t* name)
    {
        int count = 0;
        LPWSTR* args = CommandLineToArgvW(GetCommandLineW(), &count);
        DWORD value = 0;
        for (int i = 1; i + 1 < count; ++i)
        {
            if (xpx::EqualsIgnoreCase(args[i], name))
            {
                value = static_cast<DWORD>(wcstoul(args[i + 1], nullptr, 10));
                break;
            }
        }
        LocalFree(args);
        return value;
    }

    // Sviluppo delle schede verticali: inietta la mod in un explorer.exe di prova (aperto con /separate).
    int InjectInto(DWORD pid)
    {
        if (xpx::IsModLoaded(pid))
        {
            xpx::Log(L"--inject-pid %lu: mod già presente.", pid);
            return 0;
        }
        wchar_t exe[MAX_PATH * 2]{};
        GetModuleFileNameW(nullptr, exe, ARRAYSIZE(exe));
        std::wstring directory{ exe };
        directory.resize(directory.find_last_of(L'\\'));
        const std::wstring dll = xpx::PrepareModDll(directory + L"\\" + xpx::kModDllName);
        if (dll.empty())
        {
            xpx::Log(L"--inject-pid %lu: %s non trovata accanto a VTabsExplorer.exe.", pid, xpx::kModDllName);
            return 1;
        }
        std::wstring error;
        if (!xpx::InjectMod(pid, dll, error))
        {
            xpx::Log(L"--inject-pid %lu: mod non iniettata (%s).", pid, error.c_str());
            return 1;
        }
        xpx::Log(L"--inject-pid %lu: mod iniettata.", pid);
        return 0;
    }

    bool HasArgument(const wchar_t* argument)
    {
        int count = 0;
        LPWSTR* args = CommandLineToArgvW(GetCommandLineW(), &count);
        bool found = false;
        for (int i = 1; i < count && !found; ++i)
        {
            found = xpx::EqualsIgnoreCase(args[i], argument);
        }
        LocalFree(args);
        return found;
    }
}

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR, int)
{
    if (const DWORD pid = ArgumentValue(L"--inject-pid"))
    {
        return InjectInto(pid);
    }
    UINT forward = 0;
    if (HasArgument(L"--exit"))
    {
        forward = IdExit;
    }
    else if (HasArgument(L"--restore-now"))
    {
        forward = IdRestoreNow;
    }
    else if (HasArgument(L"--reopen-tab"))
    {
        forward = IdReopenNow;
    }
    if (forward != 0)
    {
        if (HWND existing = FindWindowW(kWindowClass, nullptr))
        {
            PostMessageW(existing, WM_COMMAND, forward, 0);
        }
        return 0;
    }

    HANDLE mutex = CreateMutexW(nullptr, TRUE, kMutexName);
    if (GetLastError() == ERROR_ALREADY_EXISTS)
    {
        return 0; // già in esecuzione
    }

    // Primo avvio: impostazioni predefinite (facili da modificare a mano) e avvio con Windows, perché
    // la sessione possa tornare anche dopo un riavvio del PC (si toglie dal menu della tray).
    if (GetFileAttributesW(xpx::SettingsPath().c_str()) == INVALID_FILE_ATTRIBUTES)
    {
        xpx::Settings{}.Save();
        SetAutostart(true);
        xpx::Log(L"Primo avvio: impostazioni create e avvio con Windows attivato.");
    }
    xpx::SetVerbose(xpx::Settings::Load().verboseLog);
    SetUnhandledExceptionFilter(OnCrash);
    std::set_terminate([] {
        g_worker.EmergencyLift();
        std::abort();
    });

    g_taskbarCreatedMsg = RegisterWindowMessageW(L"TaskbarCreated");

    WNDCLASSEXW windowClass{ sizeof(windowClass) };
    windowClass.lpfnWndProc = WindowProc;
    windowClass.hInstance = instance;
    windowClass.lpszClassName = kWindowClass;
    RegisterClassExW(&windowClass);

    // Finestra top-level mai mostrata: serve per tray, eventi e timer.
    HWND hwnd = CreateWindowExW(WS_EX_TOOLWINDOW, kWindowClass, xpx::kAppName, WS_OVERLAPPED, 0, 0, 0, 0, nullptr, nullptr, instance, nullptr);
    if (!hwnd)
    {
        return 1;
    }
    wchar_t exe[MAX_PATH * 2]{};
    GetModuleFileNameW(nullptr, exe, ARRAYSIZE(exe));
    xpx::Log(L"VTabsExplorer avviato (%s).", exe);
    g_trayWindow = hwnd;
    // Avvisi per l'utente da altri thread: il fumetto lo mostra la finestra della tray.
    const auto notify = [](std::wstring const& text) {
        {
            std::scoped_lock lock(g_noticeLock);
            g_pendingNotice = text;
        }
        PostMessageW(g_trayWindow, WM_NOTIFY_USER, 0, 0);
    };
    g_worker.Start(notify);
    g_mods.Start(notify);

    MSG message;
    while (GetMessageW(&message, nullptr, 0, 0) > 0)
    {
        TranslateMessage(&message);
        DispatchMessageW(&message);
    }

    g_mods.Stop();
    g_worker.Stop();
    xpx::Log(L"VTabsExplorer chiuso.");
    if (g_icon)
    {
        DestroyIcon(g_icon);
    }
    ReleaseMutex(mutex);
    CloseHandle(mutex);
    return 0;
}
