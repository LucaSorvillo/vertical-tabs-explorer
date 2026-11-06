#pragma once

namespace xpx::mod::TreeDump
{
    // Esporta l'albero visuale che contiene l'elemento (dalla sua radice) in logs\tree-<pid>-<n>.txt:
    // tipi, nomi, dimensioni, testi, sorgenti dati e DataContext (quando cambia rispetto al genitore).
    // Serve a scoprire come è fatta l'intestazione di Esplora file. Restituisce il percorso del file.
    std::wstring DumpWindow(winrt::Microsoft::UI::Xaml::UIElement const& element, std::wstring_view reason);
}
