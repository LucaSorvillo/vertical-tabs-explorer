#pragma once

namespace xpx::mod
{
    // Impostazioni lette dalla mod in %LOCALAPPDATA%\VTabsExplorer\settings.ini (le scrive la tray; il
    // pannello vi salva larghezza e stato compresso, che valgono per tutte le finestre).
    struct ModSettings
    {
        // [VerticalTabs]
        bool verticalTabs = false; // schede in una colonna a sinistra al posto della striscia orizzontale
        int width = 240;           // larghezza della colonna espansa (DIP)
        bool collapsed = false;    // colonna compressa: solo le icone delle schede

        // [NavBar]
        bool navBar = false;       // riquadro di spostamento come barra orizzontale sotto la barra dei comandi

        // [Debug]
        bool verboseLog = false;   // log dettagliato della mod (come quello della tray)

        // Test automatici: se esiste dev-windows.txt (un HWND per riga, in esadecimale), le schede verticali
        // valgono solo per quelle finestre; le altre (dell'utente) restano come sono.
        bool devAllowlist = false;
        std::vector<uint64_t> devWindows;

        static constexpr int kMinWidth = 140;
        static constexpr int kMaxWidth = 600;
        static constexpr int kCollapsedWidth = 56; // larghezza fissa della colonna compressa (DIP)

        static ModSettings Load();
        // Salva larghezza e stato compresso (le scelte fatte dal pannello).
        bool SavePanelState() const;
        // Momento dell'ultima modifica dei file letti (per accorgersi dei cambiamenti senza rileggerli).
        static ULONGLONG FileStamp();

        // Larghezza effettiva della colonna (compressa o espansa).
        int PanelWidth() const { return collapsed ? kCollapsedWidth : width; }
        // La funzione vale per questa finestra (sempre, salvo l'elenco dei test).
        bool Allows(HWND window) const;

        bool operator==(ModSettings const&) const = default;
    };
}
