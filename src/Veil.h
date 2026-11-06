#pragma once
// Il velo: nasconde le finestre di Esplora file mentre VTabsExplorer le prepara (schede, scheda attiva,
// posizione), così compaiono già complete, come le schede del Blocco note o di un browser.
//
// Una finestra velata resta aperta e al suo posto, solo trasparente: stile WS_EX_LAYERED con opacità
// zero, l'unico modo di nascondere dall'esterno una finestra di un altro processo senza toglierla dalla
// barra delle applicazioni (DWM non lascia "mascherare" le finestre altrui). Con opacità zero il mouse
// la attraversa. Togliendo lo stile torna esattamente com'era.
//
// Un gancio sugli eventi delle finestre del processo di Esplora file (creazione e comparsa) vela:
//   - una finestra appena creata, se il ripristino potrebbe scattare (nessuna finestra aperta e una
//     sessione da riaprire): così non si vede mai, nemmeno per un istante;
//   - una finestra che compare senza essere stata velata prima (Esplora file può tenere pronta in
//     anticipo una finestra nascosta e mostrare quella): resta visibile per qualche millisecondo;
//   - durante un ripristino, ogni finestra nuova (quelle che apre VTabsExplorer).
// Il velo si toglie a ripristino finito, e comunque: se la finestra non compare entro pochi secondi
// (era una finestra preparata in anticipo da Esplora file), dopo un tempo massimo, all'uscita e in caso
// di errore fatale.
//
// Cartelle aperte da altri programmi in una scheda (come Chrome con i link, vedi Worker::MergeIntoTabs): una
// finestra di Esplora file che nasce mentre ce n'è già un'altra aperta, e mentre in primo piano c'è un altro
// programma (OBS "Mostra le registrazioni", IntelliJ "Mostra in Esplora file", il desktop, la ricerca di Start...),
// si vela subito e diventa "candidata": il Worker ne porta la cartella in una scheda della finestra usata per ultima
// e la chiude. Con in primo piano una finestra di Esplora file (Ctrl+N, "Apri in una nuova finestra") resta una
// finestra. Per sapere chi c'era in primo piano e quale finestra di Esplora file è stata usata per ultima il velo
// segue anche il primo piano di tutto il sistema. Conta il momento in cui la finestra compare: una velata già alla
// creazione (nascosta) si rivaluta lì, perché Esplora file prepara in anticipo una finestra nascosta e la mostra alla
// richiesta successiva. Le finestre possono nascere anche in altri explorer.exe (Windows apre in un processo
// "/factory" a sé le cartelle chieste da programmi con un altro contesto di sicurezza): di quelle si vede solo la
// comparsa, con un gancio di tutto il sistema.

#include <windows.h>

#include <atomic>
#include <functional>
#include <mutex>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

namespace xpx
{
    class Veil
    {
    public:
        Veil() = default;
        ~Veil();
        Veil(Veil const&) = delete;
        Veil& operator=(Veil const&) = delete;

        // onShown viene chiamata (dal thread del velo) quando compare una finestra velata.
        void Start(std::function<void()> onShown);
        // Ferma il gancio e toglie tutti i veli.
        void Stop();

        // Le finestre di Esplora file che compaiono da ora vanno velate (il ripristino potrebbe scattare).
        // Con marker non vuoto (modalità prova) solo mentre quel file esiste: la prova lo crea prima di
        // aprire la finestra che fa scattare il ripristino.
        void Arm(bool armed, std::wstring const& marker = {});
        // Durante un ripristino: si velano tutte le finestre nuove.
        void SetRestoring(bool restoring);
        // Cartelle in una scheda (vedi sopra): candidate le finestre nuove aperte da un altro programma. forceExternal
        // (modalità prova): ogni finestra nuova conta come aperta da un altro programma.
        void SetMergeArmed(bool armed, bool forceExternal = false);
        // Candidate già comparse (in attesa del Worker).
        std::vector<HWND> MergeCandidates() const;
        bool IsMergeCandidate(HWND window) const;
        void ForgetMergeCandidate(HWND window);
        // Quando la finestra di Esplora file è stata in primo piano l'ultima volta (GetTickCount64); 0 se mai da quando
        // la tray è partita.
        ULONGLONG LastActivated(HWND window) const;

        bool IsVeiled(HWND window) const;
        // Finestre velate già comparse.
        std::vector<HWND> Shown() const;
        // Toglie il velo a una finestra o a tutte.
        void Lift(HWND window);
        void LiftAll();
        // Per un errore fatale: toglie i veli senza aspettare nulla.
        void EmergencyLift();

        // Quando è stata distrutta una finestra del processo di Esplora file (GetTickCount64), 0 se non si
        // sa: il gancio vede anche le distruzioni, al millisecondo, mentre il controllo delle finestre
        // passa ogni mezzo secondo e può trovarne chiuse più di una insieme.
        ULONGLONG DestroyedAt(HWND window) const;

    private:
        struct Entry
        {
            ULONGLONG veiledTick = 0;
            bool shown = false;
            ULONGLONG shownTick = 0;
        };

        static void CALLBACK OnEvent(HWINEVENTHOOK hook, DWORD event, HWND hwnd, LONG object, LONG child, DWORD thread, DWORD time);
        void Run(HANDLE ready);
        void Handle(DWORD event, HWND hwnd, ULONGLONG when);
        void OnTimer();
        bool ShouldVeil() const;
        // La finestra nuova va portata in una scheda (vedi sopra): armato, niente ripristino in corso, aperta da un altro
        // programma e con un'altra finestra di Esplora file già aperta.
        bool WantsMerge(HWND window) const;
        bool IsExternalRequest(HWND window) const;
        static bool OtherFrameOpen(HWND window);
        void OnForeground(HWND window, ULONGLONG when);
        bool Cover(HWND window, bool shown);
        static void Uncover(HWND window);

        std::function<void()> m_onShown;
        std::thread m_thread;
        DWORD m_threadId = 0;
        std::atomic<bool> m_armed{ false };
        std::atomic<bool> m_restoring{ false };
        std::atomic<bool> m_mergeArmed{ false };
        std::atomic<bool> m_forceExternal{ false };

        mutable std::mutex m_lock;
        std::wstring m_marker;
        std::unordered_map<HWND, Entry> m_entries;
        std::unordered_map<HWND, ULONGLONG> m_destroyed; // finestre distrutte di recente e quando
        std::unordered_map<HWND, ULONGLONG> m_candidates; // da portare in una scheda (e da quando)
        std::unordered_map<HWND, ULONGLONG> m_activated;  // finestre di Esplora file: ultima volta in primo piano
        HWND m_foreground = nullptr;                      // finestra principale in primo piano adesso...
        HWND m_previousForeground = nullptr;              // ...e quella prima

        // Solo sul thread del velo
        HWINEVENTHOOK m_hook = nullptr;           // creazione, distruzione e comparsa nel processo della barra
        HWINEVENTHOOK m_foregroundHook = nullptr; // primo piano, tutto il sistema
        HWINEVENTHOOK m_showHook = nullptr;       // comparsa, tutto il sistema (per gli altri explorer.exe)
        DWORD m_hookedProcess = 0;
    };
}
