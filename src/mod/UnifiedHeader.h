#pragma once

namespace xpx::mod
{
    // Intestazione dello stesso colore della barra del titolo (con le schede verticali attive): la fascia
    // della barra degli indirizzi (indietro, avanti, su, aggiorna, percorso, ricerca) e quella della barra
    // dei comandi (Nuovo, taglia, incolla, Ordina, Visualizza...) sono dipinte da Esplora file con pennelli
    // suoi sopra lo sfondo della finestra (Mica Alt), lo stesso che si vede nella barra del titolo e nel
    // pannello delle schede verticali. Resi trasparenti quei pennelli si vede quello. Si tolgono anche le
    // due linee sotto la striscia delle schede (nascosta dalle schede verticali), che resterebbero a metà.
    // Le caselle (percorso, ricerca), i pulsanti e le linee fra le barre restano come sono.
    //
    // Gli sfondi non si sostituiscono: sopra ognuno si tiene un'animazione ferma (Storyboard a durata zero,
    // HoldEnd) con il pennello trasparente. Le animazioni hanno la precedenza sul valore dell'elemento senza
    // cancellarlo: sotto resta quello di Esplora file, che continua a seguire il tema (un valore impostato
    // al suo posto perderebbe il legame con la risorsa di tema). Fermate le animazioni torna esattamente
    // com'era, anche dopo un cambio di tema.
    // Da usare solo sul thread UI della finestra (come VerticalTabs, che lo possiede).
    class UnifiedHeader
    {
    public:
        UnifiedHeader() = default;
        UnifiedHeader(UnifiedHeader const&) = delete;
        UnifiedHeader& operator=(UnifiedHeader const&) = delete;

        // Applica, o controlla che sia ancora applicato (elementi ricreati da Esplora file, animazione
        // fermata), partendo da un elemento qualunque dell'intestazione. Restituisce quanti sfondi ha
        // coperto (o ricoperto) in questa chiamata.
        size_t Apply(winrt::Microsoft::UI::Xaml::UIElement const& anchor);
        // Ferma le animazioni: tornano gli sfondi di Esplora file.
        void Restore();
        bool IsApplied() const { return !m_entries.empty(); }

    private:
        struct Entry
        {
            winrt::weak_ref<winrt::Microsoft::UI::Xaml::DependencyObject> element;
            winrt::Microsoft::UI::Xaml::DependencyProperty property{ nullptr };
            winrt::Microsoft::UI::Xaml::Media::Animation::Storyboard storyboard{ nullptr };
        };

        // Gli elementi che fanno da riferimento sono ancora vivi e nell'albero (niente da cercare).
        bool Complete() const;
        // Copre lo sfondo dell'elemento (se non è già coperto); true se ha fatto qualcosa.
        bool Cover(winrt::Microsoft::UI::Xaml::DependencyObject const& element);
        // Ricomincia le animazioni che non tengono più (ne restituisce il numero).
        size_t Refresh();

        std::vector<Entry> m_entries;
        winrt::Microsoft::UI::Xaml::Media::SolidColorBrush m_clear{ nullptr };
        // Riferimenti: fascia degli indirizzi, fascia dei comandi, barra dei comandi.
        winrt::weak_ref<winrt::Microsoft::UI::Xaml::FrameworkElement> m_navigationBand;
        winrt::weak_ref<winrt::Microsoft::UI::Xaml::FrameworkElement> m_commandBand;
        winrt::weak_ref<winrt::Microsoft::UI::Xaml::FrameworkElement> m_commandBar;
    };
}
