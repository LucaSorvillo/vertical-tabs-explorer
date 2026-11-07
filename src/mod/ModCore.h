#pragma once

#include "ModSettings.h"

namespace xpx::mod
{
    class VerticalTabs;

    // Cuore della mod di Esplora file: trova le finestre (una per ogni barra delle schede, il TabView
    // dell'intestazione) con gli agganci di Discovery.h, senza la diagnostica XAML, e applica le feature sul
    // thread UI di ciascuna.
    //
    // Sicurezza: con l'impostazione predefinita di Windows le finestre di Esplora file vivono nel processo
    // della barra delle applicazioni. Lì la mod la porta solo la tray, con le schede verticali attive e la
    // sorveglianza dei crash (ModManager della tray). Ogni handler verso il runtime XAML è protetto
    // (nessuna eccezione esce dal nostro codice) e la mod non tocca altro che le finestre di Esplora file.
    class ModCore
    {
    public:
        static ModCore& Instance();

        // Thread avviato da DllMain: controlli di sicurezza, attesa del runtime WinUI, ricerca delle finestre,
        // poi resta in ascolto delle modifiche alle impostazioni.
        static DWORD WINAPI BootstrapThreadProc(void* parameter);

        // Solo sviluppo (dev-mod.txt, letto una volta alla partenza): vedi ModCore.cpp.
        static bool DevSwitch(const char* name);

    private:
        struct WindowEntry
        {
            uint64_t id = 0;     // identità del TabView (solo per i log)
            HWND main = nullptr; // finestra principale
            winrt::weak_ref<winrt::Microsoft::UI::Xaml::Controls::TabView> tabView;
            winrt::Microsoft::UI::Dispatching::DispatcherQueue dispatcher{ nullptr };
            std::shared_ptr<VerticalTabs> verticalTabs; // usato solo dal thread del dispatcher
            winrt::Microsoft::UI::Dispatching::DispatcherQueueTimer timer{ nullptr };
        };

        ModCore() = default;
        // Installa gli agganci di Discovery (sul thread della prima finestra con XAML pronto) e cerca le finestre già aperte.
        // Ritorna prima se la tray viene chiusa con "Esci" nel frattempo.
        void StartDiscovery();
        // "Esci" della tray (tray-exited.txt): smonta tutto quello che la mod ha messo in Esplora file (finestre sul loro
        // thread, agganci, thread delle letture, classi di finestra) e dice se la DLL si può scaricare: nessun ciclo modale
        // nostro aperto, nessuno dentro i nostri agganci, nessun oggetto nostro ancora vivo (contatore del modulo C++/WinRT,
        // che conta anche i delegati degli eventi XAML). Dal thread di controllo; si può richiamare (riprende da dove era).
        bool TryUnload();
        // Sul thread della finestra: stacca le feature (come alla chiusura, ma rimettendo tutto) e dimentica la finestra.
        void TeardownWindow(std::shared_ptr<WindowEntry> const& entry);
        // Tray riavviata prima che la DLL fosse scaricata: agganci e finestre di nuovo.
        void Resume();
        // Dagli agganci (thread della finestra, dentro il layout o una richiesta UI Automation): rimanda RegisterWindow.
        static void OnTabViewFound(winrt::Microsoft::UI::Xaml::Controls::TabView const& tabView) noexcept;
        // Sul thread della finestra: una barra delle schede nuova diventa una finestra gestita.
        void RegisterWindow(winrt::Microsoft::UI::Xaml::Controls::TabView const& tabView);
        // Dal thread di controllo: cerca con UI Automation le finestre visibili non ancora gestite (now: anche quelle appena
        // comparse, alla partenza).
        void ProbeUnmanaged(bool now);
        // Resta in ascolto di impostazioni, colori e comandi; ritorna true quando la mod si è tolta e la DLL va scaricata.
        bool BackgroundLoop();
        void AttachFeatures(std::shared_ptr<WindowEntry> const& entry);
        // Sul thread della finestra: ferma il timer e stacca le feature; poi dimentica la finestra.
        void ReleaseWindow(std::shared_ptr<WindowEntry> const& entry, bool restore);
        void ApplySettingsToAllWindows();
        void DumpAllWindows(uint64_t target);
        void SendDevCommand(std::wstring const& command);
        // Colori da rimettere (colors\pending-<hwnd>.txt, scritti dalla tray): ogni file va alla sua finestra,
        // di nuovo solo se cambia; quelli di finestre che non esistono più si cancellano.
        void DeliverPendingColors();
        std::unordered_map<std::wstring, ULONGLONG> m_deliveredPending; // nome -> momento di modifica consegnato
        std::unordered_map<std::wstring, ULONGLONG> m_deliveredAt;      // nome -> quando (solo dal thread di controllo)
        std::vector<std::shared_ptr<WindowEntry>> SnapshotWindows() const;
        ModSettings CurrentSettings() const;

        mutable std::mutex m_lock;
        ModSettings m_settings;
        std::vector<std::shared_ptr<WindowEntry>> m_windows;
        // Ricerche delle finestre non gestite (solo dal thread di controllo).
        struct ProbeState
        {
            ULONGLONG first = 0; // prima volta vista
            ULONGLONG last = 0;  // ultima ricerca
            int attempts = 0;
        };
        std::unordered_map<HWND, ProbeState> m_probes;
        // Scaricamento (solo dal thread di controllo): finestre staccate e agganci tolti; code dei thread delle finestre viste
        // (per la barriera: un aggancio entrato prima del ripristino ne è uscito quando il suo thread torna al ciclo).
        bool m_tornDown = false;
        std::atomic<int> m_unloadEpoch{ 0 }; // cresce a ogni Resume (i distacchi rimasti in coda non staccano più)
        std::vector<winrt::Microsoft::UI::Dispatching::DispatcherQueue> m_seenDispatchers;
    };
}
