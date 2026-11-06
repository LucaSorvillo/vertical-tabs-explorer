#pragma once
// Il lavoro vero di VTabsExplorer, su un thread a sé (le chiamate a Esplora file sono tra processi e
// possono rallentare: la tray resta sempre reattiva).
//
// Sessione da ricordare (stesso modello del Blocco note):
//   - mentre ci sono finestre aperte, la sessione sono quelle finestre (salvate a ogni cambiamento);
//   - una finestra chiusa mentre altre restano aperte viene dimenticata;
//   - quando si chiude l'ultima, restano lei e le finestre chiuse insieme a lei (a pochi secondi l'una
//     dall'altra: "chiudi tutte le finestre", spegnimento del PC, riavvio di Esplora file).
// Quando Esplora file viene riaperto (una finestra, senza altre aperte) la sessione torna: se la
// finestra si è aperta nella cartella iniziale diventa la prima finestra della sessione; se si è aperta
// su una cartella precisa (es. "Mostra nella cartella"), quella resta la scheda attiva e le schede della
// sessione si aggiungono accanto. Le altre finestre si riaprono nella loro posizione.
// Mentre le schede si costruiscono le finestre restano velate (vedi Veil.h): compaiono già complete.
//
// Una finestra è "chiusa" solo quando non esiste più: se per un momento non se ne riescono a leggere le
// schede (Esplora file occupato, scheda in caricamento) resta com'era. E il ripristino scatta solo per
// una finestra comparsa da poco, mai per una che c'era già.

#include "BackToTop.h"
#include "Explorer.h"
#include "Hotkey.h"
#include "ModShared.h"
#include "Settings.h"
#include "Veil.h"

#include <atomic>
#include <functional>
#include <memory>
#include <mutex>
#include <thread>
#include <unordered_map>

namespace xpx
{
    class Worker
    {
    public:
        struct Status
        {
            size_t liveWindows = 0;  // finestre aperte ora (considerate)
            size_t savedWindows = 0; // sessione salvata
            size_t savedTabs = 0;
            bool devMode = false;
        };

        Worker() = default;
        ~Worker();
        Worker(Worker const&) = delete;
        Worker& operator=(Worker const&) = delete;

        // notify: avvisi per l'utente (fumetto della tray), chiamato dal thread di lavoro.
        void Start(std::function<void(std::wstring const&)> notify = {});
        void Stop();
        // Ricontrolla subito le finestre (es. Esplora file appena riavviato).
        void Wake();
        // "Riapri ora l'ultima sessione" dal menu della tray (solo se non ci sono finestre aperte).
        void RequestRestore();
        // Ctrl+Maiusc+T (o --reopen-tab): riapre l'ultima scheda chiusa, o l'ultima finestra chiusa;
        // premendolo più volte si va a ritroso. window = la finestra di Esplora file in primo piano in
        // quel momento (vi si riapre una scheda la cui finestra non c'è più); si può chiamare da
        // qualunque thread.
        void RequestReopen(HWND window);
        // Fine della sessione di Windows: da qui in poi la sessione salvata non cambia più.
        void SetEnding(bool ending);
        Status GetStatus() const;
        // Errore fatale: le finestre velate tornano subito visibili.
        void EmergencyLift();
        // Le nostre aggiunte (pulsante "Torna su", Ctrl+Maiusc+T) valgono per questa finestra: in
        // modalità prova solo per le finestre di prova. Si può chiamare da qualunque thread.
        bool AcceptsForFeatures(HWND window) const;

    private:
        // Scheda o finestra chiusa di recente, da riaprire con Ctrl+Maiusc+T. Come la sessione salvata,
        // tiene solo i percorsi delle cartelle.
        struct ClosedItem
        {
            bool wholeWindow = false; // tutta la finestra (chiudendola si riapre lei, non le sue schede)
            HWND from = nullptr;      // finestra da cui veniva la scheda (può essere già chiusa)
            Window window;            // la finestra chiusa, o la sola scheda chiusa in window.tabs[0]
        };

        void Run();
        void Tick();
        // Solo la posizione delle finestre (lettura locale, senza chiamare Esplora file).
        void RefreshPlacements();
        void SwitchMode(std::wstring const& root);
        void UpdateSession(Session const& session);
        Session ClosedTogether() const;
        // La finestra fa parte della sessione (in modalità prova: ha una scheda nella cartella della prova).
        bool Accepts(Window const& window) const;
        // Finestre aperte con schede note e accettate (dopo un ripristino).
        std::vector<Window> AdoptAll();
        bool ShouldRestore(Window const& opened, Settings const& settings, ULONGLONG now) const;
        // Mette una scheda o una finestra chiusa nella pila di Ctrl+Maiusc+T.
        void PushClosed(ClosedItem item);
        // Scrive la pila su disco (closed.txt / closed-dev.txt) se è cambiata: resta riavviando la tray o il PC.
        void SaveReopenStack();
        // Toglie dalla pila le finestre appena tornate con il ripristino della sessione.
        void ForgetRestored(Session const& restored);
        // Schede che un ripristino non è riuscito a riaprire (Esplora file troppo occupato, es. appena
        // riavviato): finiscono nella pila di Ctrl+Maiusc+T invece di andare perse. window = la finestra in
        // cui mancano (nullptr se non si è aperta: torna tutta saved). Dopo ForgetRestored, che altrimenti
        // le toglierebbe; alla fine ReportUnreopened avvisa l'utente.
        void KeepUnreopened(HWND window, Window const& saved, std::vector<Tab> const& missing);
        void ReportUnreopened();
        // Riapre l'ultima della pila (active: la finestra in primo piano, per le schede senza più
        // finestra). false se non c'era niente da riaprire: le pressioni in attesa non si usano.
        bool ReopenClosed(HWND active);
        bool IsStart(std::wstring const& path) const;
        void RestoreInto(Window const& opened);
        void RestoreAll();
        void Publish();
        // Il velo va teso se aprendo Esplora file scatterebbe il ripristino.
        void UpdateVeil(Settings const& settings);
        // Finestre appena comparse sotto il velo: si aspetta di poterne leggere la scheda.
        void WaitForVeiled();
        // Cartelle aperte da altri programmi in una scheda (vedi Veil.h), come Chrome con i link: ogni finestra candidata già
        // comparsa porta la sua cartella (e gli elementi selezionati, es. il file da mostrare) in una scheda nuova della finestra
        // usata o aperta per ultima, che passa in primo piano, e si chiude. Resta una finestra se si è aperta nella cartella
        // iniziale (Win+E, "Esplora file" da Start), se non c'è una finestra in cui metterla (nel desktop virtuale attuale),
        // se quella non risponde o se la scheda non si apre.
        void MergeIntoTabs();
        // Finestra in cui aprire la scheda: tra quelle della sessione, la più recente fra ultima volta in primo piano e apertura.
        HWND MergeTarget(HWND exclude) const;

        HANDLE m_stop = nullptr;
        HANDLE m_wake = nullptr;
        std::thread m_thread;
        std::atomic<bool> m_restoreRequested{ false };
        std::atomic<int> m_reopenRequests{ 0 }; // pressioni di Ctrl+Maiusc+T da servire (riaprire può durare secondi)
        std::atomic<HWND> m_reopenWindow{ nullptr };
        std::atomic<bool> m_ending{ false };
        std::atomic<bool> m_veiledShown{ false };
        mutable std::mutex m_statusLock;
        Status m_status;
        std::vector<HWND> m_publishedWindows; // finestre considerate (sotto m_statusLock)
        Veil m_veil;
        BackToTop m_backToTop;
        Hotkey m_hotkey;
        std::function<void(std::wstring const&)> m_notify;
        size_t m_unreopened = 0; // schede non riaperte dall'ultimo ripristino, per l'avviso

        // Solo sul thread di lavoro
        std::unique_ptr<Explorer> m_explorer;
        bool m_modeReady = false;
        std::wstring m_root;                               // cartella della modalità prova ("" = normale)
        Session m_session;                                 // quella salvata su disco
        bool m_sessionUnsaved = false;                     // salvataggio non riuscito (file occupato): si riprova
        std::vector<Window> m_live;                        // finestre della sessione con l'ultimo stato noto (ordine Z)
        struct Closed
        {
            Window window;
            ULONGLONG closed = 0; // quando è stata chiusa
            ULONGLONG seen = 0;   // quando era comparsa (0 se non si sa)
        };
        std::vector<Closed> m_closed;                      // finestre chiuse di recente
        std::vector<ClosedItem> m_reopenStack;             // da riaprire con Ctrl+Maiusc+T (l'ultima in fondo); copia su disco
        bool m_reopenDirty = false;                        // pila cambiata e non ancora scritta
        bool m_reopenSaveLogged = false;
        std::vector<HWND> m_recordedTabs;                  // schede già messe nella pila (per non contarle due volte)
        std::unordered_map<HWND, ULONGLONG> m_seen;        // quando è comparsa ogni finestra di Esplora file
        std::vector<HWND> m_considered;                    // finestre viste dall'ultimo controllo completo
        ULONGLONG m_startTick = 0;
        ULONGLONG m_modeTick = 0;                          // inizio della modalità attuale (le finestre di allora sono "vecchie")

        // Colori delle schede (schede verticali, vedi ModShared.h): si leggono dai file della mod e da quelli
        // da rimettere, e seguono ogni scheda (finestra ShellTabWindowClass) anche se cambia posizione.
        void ApplyTabColors(std::vector<Window>& windows);
        std::unordered_map<HWND, int> m_tabColors;              // colore noto di ogni scheda
        std::unordered_map<HWND, TabColorList> m_liveColors;    // pubblicati dalla mod, per finestra
        std::unordered_map<HWND, TabColorList> m_pendingColors; // da rimettere, non ancora presi dalla mod
        ULONGLONG m_colorsStamp = 0;
        int m_colorsRetries = 0;   // riletture dell'ordine tentate per gli stessi colori pubblicati
        bool m_colorsRetry = false; // rileggere l'ordine e riapplicare i colori al prossimo controllo
    };
}
