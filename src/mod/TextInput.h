#pragma once

namespace xpx::mod
{
    // Caselle di testo nostre (XAML) dentro Esplora file. Il ciclo dei messaggi di Esplora file passa ogni tasto dalle
    // sue scorciatoie prima di consegnarlo: Backspace diventa "Indietro" (la scheda torna alla cartella di prima e il
    // riquadro si chiude), e così altri tasti. Per le sue caselle (la ricerca nell'intestazione) lo sa; per le nostre no.
    //
    // Finché una nostra casella ha il fuoco (Begin al GotFocus, End al LostFocus), un hook WH_GETMESSAGE del thread
    // della finestra consegna da sé i tasti diretti alla finestra col fuoco (quella d'ingresso dell'isola, figlia della
    // finestra principale o di un suo popup) e al ciclo di Esplora file lascia un WM_NULL. Solo WM_KEYDOWN, WM_KEYUP,
    // WM_CHAR e WM_DEADCHAR: i tasti di sistema (Alt, F10...) restano a Esplora file. Un hook per thread, contato.
    class TextInput
    {
    public:
        // main: la finestra principale di Esplora file a cui appartiene la casella.
        static void Begin(HWND main);
        static void End(HWND main);
    };
}
