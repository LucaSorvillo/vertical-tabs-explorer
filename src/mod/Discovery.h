#pragma once
// Come la mod trova la barra delle schede (TabView) di ogni finestra di Esplora file, SENZA la diagnostica XAML.
//
// La diagnostica XAML (InitializeXamlDiagnosticsEx + AdviseVisualTreeChange, il "TAP") tiene nella sua cache, con
// riferimenti forti, le radici delle isole XAML di tutte le finestre aperte dopo di lei; la cache si libera solo quando
// il thread della finestra si deinizializza, ma il thread aspetta proprio che quegli oggetti spariscano (contatore dei
// thread ref di CExplorerFrame): ogni finestra chiusa restava viva per sempre (vedi README, "Problema noto").
//
// Al suo posto due agganci a slot di interfacce WinRT pubbliche (ABI stabile), installati una volta per processo
// sostituendo una voce della tabella dei metodi (vtable) condivisa da tutte le istanze della classe:
//   - IFrameworkElementOverrides::OnApplyTemplate di TabView: ogni barra delle schede nuova si presenta da sé, sul suo
//     thread, quando applica il modello (prima di comparire);
//   - IAutomationPeerOverrides::GetAutomationControlTypeCore di TabViewItemAutomationPeer: per le finestre già aperte
//     quando arriva la mod, una ricerca UI Automation delle schede (Probe) fa chiamare questo metodo sul thread della
//     finestra; dal "peer" si risale alla scheda e alla barra. Fuori da una ricerca nostra l'aggancio non fa niente.
// Entrambi chiamano prima il metodo originale e non trattengono nulla: alla funzione arriva la barra, che il chiamante
// deve tenere solo con un riferimento debole.
// Microsoft.UI.Xaml.dll usa CFG (non XFG): un puntatore a una nostra funzione nella vtable è un bersaglio valido.

#include <windows.h>

namespace xpx::mod::discovery
{
    // Chiamata sul thread della finestra, dentro il layout XAML (OnApplyTemplate) o dentro una richiesta UI Automation:
    // deve solo prendere nota e rimandare il lavoro (DispatcherQueue). Mai eccezioni.
    using Callback = void (*)(winrt::Microsoft::UI::Xaml::Controls::TabView const& tabView) noexcept;

    // Installa gli agganci (una volta per processo; le chiamate successive ritornano subito) sul thread UI di window, una
    // finestra di Esplora file di questo processo. Da un thread che non sia quello UI. true se installati.
    bool Install(HWND window, Callback callback);
    bool Installed();
    // Agganci impossibili in questo processo (XFG attivo nel modulo di WinUI): la mod non fa nulla, senza riprovare.
    bool Blocked();

    // Fa trovare la barra delle schede di una finestra già aperta (ricerca UI Automation sull'isola dell'intestazione).
    // Da un thread MTA che non sia quello della finestra. true se ha trovato una scheda.
    bool Probe(HWND window);

    // Scaricamento della mod ("Esci" della tray): rimette nelle vtable le funzioni originali. false se una voce non è più
    // la nostra (un altro aggancio sopra il nostro chiamerebbe ancora il nostro codice): allora la DLL non si scarica.
    // Dopo, Install può ripartire da capo.
    bool Uninstall();
    // Chiamate dentro i nostri agganci in questo momento (prima di scaricare la DLL deve essere zero).
    int InFlight();
}
