#pragma once
// Sessione di Esplora file: finestre, schede (nell'ordine visivo), scheda attiva e posizione delle
// finestre. Salvata in un file di testo UTF-8 nella cartella dei dati.

#include <windows.h>

#include <string>
#include <vector>

namespace xpx
{
    struct Tab
    {
        std::wstring path;            // nome di analisi (C:\..., \\server\..., ::{GUID} per le cartelle virtuali)
        std::vector<BYTE> idList;     // PIDL assoluto, di riserva se il nome non si può più analizzare
        std::wstring title;           // nome della scheda (solo dal vivo)
        HWND tabWindow = nullptr;     // finestra ShellTabWindowClass (solo dal vivo)
        int color = 0;                // colore della scheda (schede verticali): indice in kTabColorKeys, 0 = nessuno
    };

    struct Window
    {
        HWND hwnd = nullptr;          // solo dal vivo
        std::vector<Tab> tabs;        // ordine visivo
        int active = 0;               // indice della scheda attiva
        WINDOWPLACEMENT placement{};  // posizione e stato (length == 0 se sconosciuta)
    };

    struct Session
    {
        std::vector<Window> windows;  // dalla più recente

        bool Empty() const { return windows.empty(); }
        size_t TabCount() const;
    };

    // Uguali come contenuto da salvare (percorsi, scheda attiva, posizione).
    bool SameSession(Session const& a, Session const& b);

    bool SaveSession(std::wstring const& file, Session const& session);
    Session LoadSession(std::wstring const& file);

    // Pila di Ctrl+Maiusc+T su disco (come le schede chiuse di Chrome: resta riavviando la tray o il PC). Stesso formato
    // della sessione, con il tipo di ogni voce: una finestra intera o delle schede. La prima voce è la più vecchia. Lista
    // vuota = file cancellato.
    struct ClosedEntry
    {
        bool wholeWindow = false;
        Window window;
    };
    bool SaveClosedStack(std::wstring const& file, std::vector<ClosedEntry> const& entries);
    std::vector<ClosedEntry> LoadClosedStack(std::wstring const& file);

    // "2 finestre, 5 schede" e, se richiesto, l'elenco dei percorsi (solo per la modalità prova: i
    // percorsi dell'utente non finiscono nel log).
    std::wstring Describe(Session const& session, bool withPaths);
}
