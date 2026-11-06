#include "Hotkey.h"

#include "Common.h"

#include <string>

namespace xpx
{
    namespace
    {
        constexpr int kHotkeyId = 1;
        constexpr UINT kWatchMs = 1000;            // controllo di sicurezza del primo piano (vedi Hotkey.h)
        constexpr ULONGLONG kRestoreWatchMs = 3000; // quanto resta acceso dopo un ripristino da icona
        constexpr UINT WM_APP_UPDATE = WM_APP + 1;
        constexpr wchar_t kFrameClass[] = L"CabinetWClass";

        Hotkey* g_instance = nullptr; // il gancio non ha un parametro di contesto

        std::wstring ClassOf(HWND hwnd)
        {
            wchar_t name[64]{};
            GetClassNameW(hwnd, name, ARRAYSIZE(name));
            return name;
        }
    }

    Hotkey::~Hotkey()
    {
        Stop();
    }

    void Hotkey::Start(std::function<bool(HWND)> accepts, std::function<void(HWND)> onPressed)
    {
        m_accepts = std::move(accepts);
        m_onPressed = std::move(onPressed);
        g_instance = this;
        const HANDLE ready = CreateEventW(nullptr, TRUE, FALSE, nullptr);
        m_thread = std::thread([this, ready] { Run(ready); });
        WaitForSingleObject(ready, 5000);
        CloseHandle(ready);
    }

    void Hotkey::Stop()
    {
        if (m_thread.joinable())
        {
            PostThreadMessageW(m_threadId, WM_QUIT, 0, 0);
            m_thread.join();
        }
        if (g_instance == this)
        {
            g_instance = nullptr;
        }
    }

    void Hotkey::SetEnabled(bool enabled)
    {
        if (m_enabled.exchange(enabled) != enabled)
        {
            if (const DWORD thread = m_threadId)
            {
                PostThreadMessageW(thread, WM_APP_UPDATE, 0, 0);
            }
        }
    }

    void Hotkey::Run(HANDLE ready)
    {
        MSG message;
        PeekMessageW(&message, nullptr, WM_USER, WM_USER, PM_NOREMOVE); // crea la coda dei messaggi
        m_threadId = GetCurrentThreadId();
        const DWORD flags = WINEVENT_OUTOFCONTEXT | WINEVENT_SKIPOWNPROCESS;
        m_hookForeground = SetWinEventHook(EVENT_SYSTEM_FOREGROUND, EVENT_SYSTEM_FOREGROUND, nullptr, OnEvent, 0, 0, flags);
        m_hookMinimize = SetWinEventHook(EVENT_SYSTEM_MINIMIZESTART, EVENT_SYSTEM_MINIMIZEEND, nullptr, OnEvent, 0, 0, flags);
        SetEvent(ready);
        Update(GetForegroundWindow());

        while (GetMessageW(&message, nullptr, 0, 0) > 0)
        {
            if (message.message == WM_HOTKEY && message.wParam == kHotkeyId)
            {
                // La finestra per cui la combinazione è registrata: è lei quella in primo piano.
                if (m_window && m_onPressed)
                {
                    m_onPressed(m_window);
                }
                continue;
            }
            if (message.message == WM_TIMER || message.message == WM_APP_UPDATE)
            {
                Update(GetForegroundWindow());
                continue;
            }
            TranslateMessage(&message);
            DispatchMessageW(&message);
        }

        if (m_timer)
        {
            KillTimer(nullptr, m_timer);
            m_timer = 0;
        }
        Unregister();
        for (HWINEVENTHOOK* hook : { &m_hookForeground, &m_hookMinimize })
        {
            if (*hook)
            {
                UnhookWinEvent(*hook);
                *hook = nullptr;
            }
        }
    }

    void CALLBACK Hotkey::OnEvent(HWINEVENTHOOK, DWORD event, HWND hwnd, LONG, LONG, DWORD, DWORD)
    {
        Hotkey* self = g_instance;
        if (!self || !hwnd)
        {
            return;
        }
        switch (event)
        {
        case EVENT_SYSTEM_MINIMIZESTART:
            // La finestra si sta riducendo a icona: la combinazione torna subito agli altri programmi.
            if (hwnd == self->m_window)
            {
                self->Unregister();
            }
            self->EnsureTimer();
            break;

        case EVENT_SYSTEM_MINIMIZEEND:
            // Ripristinando una finestra, Windows può darle il primo piano senza segnalarlo (vedi
            // BackToTop.h): per qualche istante si ricontrolla con il timer.
            if (ClassOf(hwnd) == kFrameClass)
            {
                self->m_watchUntil = GetTickCount64() + kRestoreWatchMs;
            }
            self->Update(GetForegroundWindow());
            break;

        default: // EVENT_SYSTEM_FOREGROUND
        {
            // Vale la finestra principale: Esplora file segnala il primo piano anche sulla scheda attiva
            // (ShellTabWindowClass, una finestra figlia).
            const HWND root = GetAncestor(hwnd, GA_ROOT);
            self->Update(root ? root : GetForegroundWindow());
            break;
        }
        }
    }

    void Hotkey::Update(HWND foreground)
    {
        // Una finestra ridotta a icona può restare "in primo piano" per Windows (vedi BackToTop.h): lì
        // non si sta lavorando, quindi la combinazione torna agli altri programmi.
        m_frameForeground = foreground && IsWindow(foreground) && !IsIconic(foreground) && ClassOf(foreground) == kFrameClass;
        const bool wanted = m_enabled && m_frameForeground && (!m_accepts || m_accepts(foreground));
        if (!wanted)
        {
            Unregister();
        }
        else if (m_window != foreground)
        {
            Register(foreground); // un'altra finestra di Esplora file: la combinazione segue questa
        }
        EnsureTimer();
    }

    void Hotkey::EnsureTimer()
    {
        // Il controllo serve mentre si sta in Esplora file: se la combinazione è nostra per accorgersi
        // che la finestra è stata ridotta a icona, chiusa o non è più in primo piano senza un evento
        // utile; se non lo è ancora per riprovare (una finestra appena aperta la tray la legge poco
        // dopo, e la combinazione può essere momentaneamente tenuta da un altro programma). Serve anche
        // per qualche istante dopo un ripristino da icona. Altrove si spegne: a riposo niente controlli.
        const bool wanted = m_window != nullptr || (m_enabled && m_frameForeground) || GetTickCount64() < m_watchUntil;
        if (wanted && !m_timer)
        {
            m_timer = SetTimer(nullptr, 0, kWatchMs, nullptr); // timer del thread (WM_TIMER senza finestra)
        }
        else if (!wanted && m_timer)
        {
            KillTimer(nullptr, m_timer);
            m_timer = 0;
        }
    }

    void Hotkey::Register(HWND window)
    {
        Unregister();
        // MOD_NOREPEAT: tenendo premuto non si ripete (una scheda per pressione).
        if (!RegisterHotKey(nullptr, kHotkeyId, MOD_CONTROL | MOD_SHIFT | MOD_NOREPEAT, 'T'))
        {
            if (!m_failedLogged)
            {
                m_failedLogged = true;
                Log(L"Ctrl+Maiusc+T non disponibile (errore %lu): forse la tiene un altro programma.", GetLastError());
            }
            return;
        }
        m_window = window;
        m_failedLogged = false;
        XPX_VLOG(L"Ctrl+Maiusc+T attiva per la finestra %p.", window);
    }

    void Hotkey::Unregister()
    {
        if (!m_window)
        {
            return;
        }
        UnregisterHotKey(nullptr, kHotkeyId);
        XPX_VLOG(L"Ctrl+Maiusc+T restituita (finestra %p non più in primo piano).", m_window);
        m_window = nullptr;
    }
}
