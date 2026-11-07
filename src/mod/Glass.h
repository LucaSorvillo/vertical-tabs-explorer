#pragma once

namespace xpx::mod::glass
{
    // Zona "vetro" della finestra di Esplora file: la cornice di sistema estesa nell'area client
    // (DwmExtendFrameIntoClientArea), dove dietro l'intestazione (riga del titolo, barra degli indirizzi, barra dei
    // comandi) si vede il materiale Mica. Esplora file la estende fino al fondo dell'intestazione. Lì DWM compone i pixel
    // con il loro alfa: il contenuto delle schede, disegnato senza alfa, si somma allo sfondo e diventa più chiaro
    // (#191919 + #202020 = #393939). Con la riga del titolo nascosta il contenuto sale dentro quella zona, quindi la si
    // accorcia fino al suo bordo.
    //
    // La zona la imposta ExplorerFrame.dll, che importa la funzione con il caricamento ritardato (delay-load): un aggancio
    // nella sua tabella delle importazioni ritardate (e in quella di ieframe.dll, che la usa anche lei) ricorda ogni
    // richiesta e, se per quella finestra c'è un limite, ne accorcia il margine superiore. Le richieste delle finestre
    // senza limite passano invariate.

    // Installa gli agganci nei moduli già caricati (si può richiamare: aggancia solo quelli nuovi; la DLL non viene mai
    // scaricata). true se almeno un aggancio è attivo.
    bool Install();
    bool Hooked();
    // Scaricamento della mod: rimette le importazioni di prima. false se una voce non è più la nostra (la DLL non si
    // scarica). Le finestre devono aver già tolto il loro limite (WindowLayout::Detach).
    bool Uninstall();
    // Chiamate dentro l'aggancio in questo momento.
    int InFlight();

    // Limite del margine superiore della zona vetro (px client) per una finestra, o nessun limite (torna quella di Esplora
    // file). fallbackTopPx è il margine di Esplora file da usare se la sua richiesta non è nota (finestra creata prima
    // dell'aggancio). Applica subito la zona risultante, solo se il limite cambia.
    void SetTopLimit(HWND hwnd, std::optional<int> limitPx, int fallbackTopPx);

    // La finestra non è più gestita.
    void Forget(HWND hwnd);
}
