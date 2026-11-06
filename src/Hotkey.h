#pragma once
// Scorciatoia "riapri la scheda chiusa" (Ctrl+Maiusc+T) per Esplora file.
//
// La combinazione si chiede a Windows (RegisterHotKey) SOLO mentre è in primo piano una finestra di
// Esplora file considerata, e si restituisce appena il primo piano passa ad altro: così resta libera
// per gli altri programmi che la usano (nei browser, per esempio) e noi della tastiera non vediamo
// nulla — Windows ci consegna soltanto questa combinazione, nessun altro tasto.
//
// Il primo piano si segue con EVENT_SYSTEM_FOREGROUND e con gli eventi di riduzione a icona
// (MINIMIZESTART/END), più un controllo locale ogni secondo (due chiamate, microsecondi) per i casi in
// cui l'evento non arriva: ripristinando da icona una finestra, Windows può darle il primo piano senza
// segnalarlo (vedi BackToTop.h). Quel controllo gira solo mentre si sta in Esplora file (per accorgersi
// che la finestra è stata ridotta a icona, chiusa o non è più in primo piano, e per riprovare se la
// combinazione non è ancora nostra) e per qualche istante dopo il ripristino di una finestra da icona:
// a riposo, con altri programmi in primo piano, nessun controllo.
// Tutto su un thread suo, con la sua coda di messaggi: WM_HOTKEY arriva lì.

#include <windows.h>

#include <atomic>
#include <functional>
#include <thread>

namespace xpx
{
    class Hotkey
    {
    public:
        Hotkey() = default;
        ~Hotkey();
        Hotkey(Hotkey const&) = delete;
        Hotkey& operator=(Hotkey const&) = delete;

        // accepts: la finestra di Esplora file è tra quelle considerate (in modalità prova, solo quelle
        // di prova). onPressed: combinazione premuta, con la finestra in primo piano in quel momento.
        // Entrambe vengono chiamate dal thread della scorciatoia.
        void Start(std::function<bool(HWND)> accepts, std::function<void(HWND)> onPressed);
        void Stop();
        void SetEnabled(bool enabled);

    private:
        static void CALLBACK OnEvent(HWINEVENTHOOK hook, DWORD event, HWND hwnd, LONG object, LONG child, DWORD thread, DWORD time);

        void Run(HANDLE ready);
        // Registra o restituisce la combinazione secondo la finestra in primo piano.
        void Update(HWND foreground);
        // Accende o spegne il controllo locale secondo lo stato (vedi sopra).
        void EnsureTimer();
        void Register(HWND window);
        void Unregister();

        std::function<bool(HWND)> m_accepts;
        std::function<void(HWND)> m_onPressed;
        std::atomic<bool> m_enabled{ true };
        std::thread m_thread;
        std::atomic<DWORD> m_threadId{ 0 };

        // Solo sul thread della scorciatoia
        HWINEVENTHOOK m_hookForeground = nullptr;
        HWINEVENTHOOK m_hookMinimize = nullptr;
        UINT_PTR m_timer = 0;         // controllo locale (0: spento)
        ULONGLONG m_watchUntil = 0;   // fino ad allora il controllo resta acceso (ripristino da icona)
        bool m_frameForeground = false; // in primo piano c'è una finestra di Esplora file (non ridotta a icona)
        HWND m_window = nullptr;     // finestra per cui la combinazione è registrata (null: non registrata)
        bool m_failedLogged = false;
    };
}
