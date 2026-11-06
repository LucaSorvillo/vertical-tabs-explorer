#pragma once
// Pulsante "Torna su" per Esplora file: un pulsante tondo che compare (sfumando) nell'angolo in basso a
// destra della vista dei file della finestra in primo piano quando la vista è scorsa verso il basso, e
// la riporta all'inizio. Esplora file non si modifica: il pulsante è una nostra piccola finestra
// trasparente intorno al cerchio (layered, trasparenza per pixel), sempre sopra, che non prende mai il
// focus (il clic lascia attiva la finestra di Esplora file).
//
// Segnali, senza controlli continui a riposo:
//   - EVENT_OBJECT_CONTENTSCROLLED, che la vista dei file (DirectUIHWND) emette a ogni scorrimento: si
//     rilegge la posizione (UI Automation, ScrollPattern) e il pulsante compare o sparisce;
//   - EVENT_SYSTEM_FOREGROUND, MOVESIZESTART/END, MINIMIZESTART/END: il pulsante vale solo per la
//     finestra in primo piano e sparisce mentre la si sposta o la si riduce a icona;
//   - mentre una finestra di Esplora file è in primo piano, un controllo locale (finestre e rettangoli,
//     microsecondi) ogni 200 ms per i cambi di scheda e di cartella e per seguirne la posizione; anche
//     per qualche istante dopo il ripristino da icona di una finestra di Esplora file, perché il primo
//     piano può arrivarle senza un evento utile (a volte Windows lo segnala solo sulla scheda).
// Il clic riporta la vista in cima con ScrollPattern.SetScrollPercent, a piccoli passi (animazione).
// Tutto gira su un thread suo (con la sua coda di messaggi e COM STA per UI Automation).

#include <windows.h>
#include <ole2.h>
#include <uiautomation.h>
#include <wrl/client.h>

#include <atomic>
#include <functional>
#include <thread>

namespace xpx
{
    class BackToTop
    {
    public:
        BackToTop() = default;
        ~BackToTop();
        BackToTop(BackToTop const&) = delete;
        BackToTop& operator=(BackToTop const&) = delete;

        // accepts: la finestra di Esplora file è tra quelle considerate (in modalità prova, solo quelle
        // di prova). Chiamata dal thread del pulsante.
        void Start(std::function<bool(HWND)> accepts);
        void Stop();
        void SetEnabled(bool enabled);

        // Classe della finestra del pulsante (la cercano i test).
        static constexpr wchar_t kWindowClass[] = L"VTabsExplorer.BackToTop";

    private:
        static LRESULT CALLBACK WindowProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam);
        static void CALLBACK OnEvent(HWINEVENTHOOK hook, DWORD event, HWND hwnd, LONG object, LONG child, DWORD thread, DWORD time);

        void Run(HANDLE ready);
        LRESULT Handle(UINT message, WPARAM wParam, LPARAM lParam);
        void HookProcess(DWORD process);
        void OnForeground(HWND window);
        // Controllo locale attivo per qualche istante anche senza finestra seguita.
        void WatchFor(UINT milliseconds);
        // Vista dei file della scheda attiva (cambia con la scheda e con la cartella).
        void ResolveView();
        // Posizione della vista: il pulsante compare se è scorsa verso il basso.
        void Refresh();
        bool Place();
        void Show();
        void Hide(bool immediately);
        void Render();
        void ScrollToTop();
        void ScrollStep();

        std::function<bool(HWND)> m_accepts;
        std::atomic<bool> m_enabled{ true };
        std::thread m_thread;
        DWORD m_threadId = 0;

        // Solo sul thread del pulsante
        HWND m_hwnd = nullptr;     // il pulsante
        HWND m_tooltip = nullptr;
        HWINEVENTHOOK m_hookForeground = nullptr;
        HWINEVENTHOOK m_hookMoveSize = nullptr;
        HWINEVENTHOOK m_hookMinimize = nullptr;
        HWINEVENTHOOK m_hookScroll = nullptr;
        DWORD m_scrollProcess = 0;
        ULONG_PTR m_gdiplusToken = 0;
        Microsoft::WRL::ComPtr<IUIAutomation> m_automation;

        HWND m_window = nullptr;   // finestra di Esplora file seguita (in primo piano)
        HWND m_view = nullptr;     // vista dei file della scheda attiva (DirectUIHWND)
        Microsoft::WRL::ComPtr<IUIAutomationScrollPattern> m_scroll;
        int m_vbarWidth = 0;       // barre di scorrimento della vista (px), per stare accanto
        int m_hbarHeight = 0;
        RECT m_viewRect{};         // rettangolo della vista quando sono state misurate le barre
        bool m_moving = false;     // la finestra si sta spostando o ridimensionando
        bool m_lastIconic = false; // stato della finestra all'ultimo controllo locale (eventi persi: vedi kTimerWatch)
        bool m_lastForeground = false;
        HWND m_lastForegroundSeen = nullptr; // ultima finestra in primo piano vista (per il log)
        ULONGLONG m_watchUntil = 0; // fino ad allora il controllo locale resta attivo anche senza finestra seguita

        bool m_wanted = false;     // il pulsante deve essere visibile
        int m_alpha = 0;           // opacità attuale (0-255): sale e scende in dissolvenza
        int m_size = 0;            // lato in pixel
        UINT m_dpi = USER_DEFAULT_SCREEN_DPI;
        bool m_hover = false;
        bool m_pressed = false;
        UINT m_refreshTicks = 0;

        double m_scrollFrom = 0;   // animazione verso l'inizio
        ULONGLONG m_scrollStart = 0;
    };
}
