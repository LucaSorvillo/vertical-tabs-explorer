#pragma once
// Impostazioni di VTabsExplorer, salvate in %LOCALAPPDATA%\VTabsExplorer\settings.ini (modificate dal menu
// della tray, rilette a ogni controllo).

namespace xpx
{
    struct Settings
    {
        // [Session]
        bool restoreOnOpen = true; // aprendo Esplora file (con nessuna finestra aperta) torna l'ultima sessione

        // [BackToTop]
        bool backToTop = true;     // pulsante "Torna su" nella vista dei file scorsa verso il basso

        // [ReopenTab]
        bool reopenTab = true;     // Ctrl+Maiusc+T riapre l'ultima scheda (o finestra) chiusa

        // [Tabs]
        bool openInTabs = true;    // cartelle aperte da altri programmi: in una scheda della finestra usata per ultima (come Chrome)

        // [VerticalTabs] (la mod dentro Esplora file; larghezza e stato compresso li salva il pannello)
        bool verticalTabs = false;          // schede in una colonna a sinistra
        bool verticalTabsSuspended = false; // sospese dopo una chiusura anomala di Esplora file con la mod
        bool skipShellProcess = false;      // (sviluppo, solo lettura) mai nel processo della barra delle applicazioni

        // [NavBar] (anche questa nella mod)
        bool navBar = false; // riquadro di spostamento come barra orizzontale sotto la barra dei comandi

        // [Debug]
        bool verboseLog = false;

        static Settings Load();
        bool Save() const;

        bool operator==(Settings const&) const = default;
    };
}
