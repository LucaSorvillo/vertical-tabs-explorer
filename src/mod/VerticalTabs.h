#pragma once

#include "ModSettings.h"
#include "NavBar.h"
#include "TabColors.h"
#include "UnifiedHeader.h"
#include "WindowLayout.h"
#include "../ModShared.h"

namespace xpx::mod
{
    // Feature "schede verticali" per una finestra di Esplora file.
    //
    // Il pannello è un controllo nostro (componenti WinUI di sistema) in una NOSTRA isola XAML
    // (DesktopWindowXamlSource, lo stesso meccanismo dell'intestazione di Esplora file), nella colonna
    // che WindowLayout riserva a sinistra sotto l'intestazione. È legato alla barra schede nativa (il
    // TabView dell'intestazione), che resta viva ma invisibile: elenco, selezione, titoli e icone si
    // leggono da lì; selezione, chiusura, "nuova scheda", menu contestuale e riordino passano dai suoi
    // controlli, quindi seguono esattamente la logica di Esplora file.
    //
    // Vive e viene usato interamente sul thread UI della finestra. Gli handler registrati su oggetti
    // nativi tengono solo un weak_ptr: anche se la finestra si chiude, non richiamano un oggetto distrutto.
    class VerticalTabs : public std::enable_shared_from_this<VerticalTabs>
    {
    public:
        explicit VerticalTabs(winrt::Microsoft::UI::Xaml::Controls::TabView const& tabView);
        ~VerticalTabs();

        VerticalTabs(VerticalTabs const&) = delete;
        VerticalTabs& operator=(VerticalTabs const&) = delete;

        void Apply(ModSettings const& settings);
        // Controllo periodico (500 ms): layout, elenco, aspetto dei titoli.
        void OnTimer();
        // restore=true: rimette Esplora file com'era; false: la finestra si sta chiudendo.
        void Detach(bool restore);
        // Comandi per i test automatici, che altrimenti richiederebbero il mouse vero (mod-command.txt):
        // "<hwnd> move <da> <a>" riordina come un trascinamento, "<hwnd> menu <indice>" apre il menu
        // contestuale della riga, "<hwnd> width <dip>" imposta la larghezza come il divisore,
        // "<hwnd> color <indice> <colore>" colora una scheda come "Colore scheda" ("none" lo toglie),
        // "<hwnd> closemenu" chiude il menu contestuale, "<hwnd> theme light|dark|default" cambia il tema della
        // sola intestazione (per provare un cambio di tema senza toccare quello di Windows; vale anche con la
        // colonna spenta), "<hwnd> navbar ..." va alla barra delle cartelle (vedi NavBar::DevCommand). <hwnd>
        // (in esadecimale) è la finestra di prova: tutte le altre ignorano il comando.
        void DevCommand(std::wstring const& line);

        bool IsAttached() const { return m_attached; }
        HWND Main() const { return m_main; }

        // Colori da rimettere nelle schede di una finestra appena riaperta dalla tray (pending-<hwnd>.txt):
        // valgono solo per questa finestra, e si applicano appena ha tutte le schede.
        void OfferPendingColors(HWND window, TabColorList const& list);
        // La mod esce dal processo ("Esci" della tray): i colori delle schede, solo in memoria, restano come "da rimettere" (lo
        // stesso file che la tray scrive per le finestre riaperte). La mod che rientra li rimette; intanto la tray li legge come i
        // colori della finestra.
        void HandOverColors();

        // La finestra di Esplora file si sta chiudendo (chiamato sul suo thread, una volta).
        std::function<void()> onWindowClosed;

    private:
        struct Row
        {
            winrt::Windows::Foundation::IInspectable item{ nullptr }; // modello della scheda nativa
            winrt::Microsoft::UI::Xaml::Controls::ListViewItem element{ nullptr }; // contenitore di sé stesso
            winrt::Microsoft::UI::Xaml::Controls::Border tint{ nullptr }; // fondo della riga ("card") e bordino
            winrt::Microsoft::UI::Xaml::Controls::Border pill{ nullptr };      // stanghetta della scheda attiva (grigio neutro)
            winrt::Microsoft::UI::Xaml::Controls::Border pillColor{ nullptr }; // la stessa, nel colore della scheda
            winrt::Microsoft::UI::Xaml::Controls::Grid grid{ nullptr };
            winrt::Microsoft::UI::Xaml::Controls::Image icon{ nullptr };
            winrt::Microsoft::UI::Xaml::Controls::TextBlock title{ nullptr };
            winrt::Microsoft::UI::Xaml::Controls::Button close{ nullptr };
            winrt::Microsoft::UI::Xaml::Data::INotifyPropertyChanged notifier{ nullptr };
            winrt::event_token propertyToken{};
            bool hovered = false;
        };

        bool Attach();
        bool EnsurePanel();
        bool BuildPanel();
        void HookPanelEvents();
        void ApplyBackdrop();
        void ApplyAppearance();
        // Intestazione del pannello (comprimi/espandi) e linee del bordo destro secondo la geometria del layout: con la
        // colonna in cima alla finestra il pulsante sta nella fascia del titolo, accanto a quelli della finestra, e
        // separatore e divisore cominciano dove comincia il contenuto delle schede.
        void ApplyHeaderLayout(bool compact);
        void OnPanelGeometry(int captionPx, int contentTopPx);
        // Regione dell'isola del pannello: senza la fascia del titolo, salvo i suoi pulsanti. Lì sotto c'è la
        // finestra principale, che risponde come la sua barra del titolo (vedi WindowLayout): trascinamento, doppio
        // clic, menu di sistema. Aggiornata dopo ogni layout, solo se cambia.
        void UpdateCaptionRegion();
        // Riga del titolo nascosta (vedi WindowLayout): altezze della riga del titolo e della barra degli indirizzi lette
        // dal XAML dell'intestazione (NavigationBarControl), e margine destro della barra degli indirizzi per i pulsanti
        // della finestra, tenuto con un'animazione ferma come gli sfondi di UnifiedHeader (fermandola torna il valore
        // di Esplora file).
        void UpdateHeaderRows();
        void UpdateToolbarInset();
        winrt::Microsoft::UI::Xaml::FrameworkElement HeaderToolbar();
        void Enable();
        void Disable();
        // Intestazione con lo sfondo della barra del titolo: applicata (o ricontrollata) se le schede verticali
        // sono attive, altrimenti tolta.
        void UpdateUnifiedHeader();
        // Barra delle cartelle (riquadro di spostamento in alto, [NavBar] Enabled): indipendente dalle schede
        // verticali, usa lo stesso layout.
        void EnableNavBar();
        // restoreLayout=false: la finestra si sta chiudendo, il layout non si rimette a posto (niente da ripristinare).
        void DisableNavBar(bool restoreLayout = true);
        void Unsubscribe();
        void SetNativeStripHidden(bool hidden);
        // L'elenco è alto quanto le sue righe, fino allo spazio lasciato dal "+" sottostante.
        void UpdateListHeight();
        // Lo scorrimento orizzontale (rotella inclinata, due dita sul touchpad) non deve muovere l'elenco.
        void BlockHorizontalWheel();
        // Titoli come quelli delle schede orizzontali: carattere, dimensione, peso e colore letti dalle
        // schede native già realizzate (la striscia, anche invisibile, resta in layout). true se cambiati.
        bool ReadNativeTitleStyle();

        // Pannello compresso/espanso
        void SetCollapsed(bool collapsed);

        // Dati: una riga per ogni scheda nativa, nello stesso ordine
        void SyncRows();
        void ScheduleSync();
        std::unique_ptr<Row> CreateRow(winrt::Windows::Foundation::IInspectable const& item);
        void BuildRowElement(Row& row);
        void ReleaseRow(Row& row);
        void ClearRows();
        void RefreshRows();
        void RefreshRow(Row& row, int index);
        void UpdateRow(Row& row);
        void UpdateAllRows();
        void RefreshVisibleRows();
        void ResetVisibleList();
        Row* FindRow(winrt::Windows::Foundation::IInspectable const& sender);
        int IndexOf(Row const* row) const;

        // Selezione e focus
        void SyncSelectionFromNative();
        void OnListSelectionChanged();
        void FocusContent();

        // Riordino con trascinamento (gestito con il puntatore: il drag & drop OLE non raggiunge la
        // nostra isola). La riga si sposta dal vivo; al rilascio l'ordine passa a Esplora file.
        void OnListPointerPressed(winrt::Microsoft::UI::Xaml::Input::PointerRoutedEventArgs const& args);
        void OnListPointerMoved(winrt::Microsoft::UI::Xaml::Input::PointerRoutedEventArgs const& args);
        void OnListPointerReleased(winrt::Microsoft::UI::Xaml::Input::PointerRoutedEventArgs const& args);
        void BeginDrag(winrt::Microsoft::UI::Xaml::Input::Pointer const& pointer);
        void UpdateDrag();
        void EndDrag(bool commit);
        void AutoScrollStep();
        Row* RowFromSource(winrt::Windows::Foundation::IInspectable const& source, bool& onCloseButton);

        // Menu contestuale nativo della scheda
        void ShowNativeContextMenu(Row const& row, winrt::Microsoft::UI::Xaml::UIElement const& element,
                                   winrt::Windows::Foundation::Point const& localPoint);

        // Divisore trascinabile per la larghezza del pannello
        void SetSplitterHighlight(bool on);
        void ResizeFromCursor();
        void ApplyPanelWidth(int widthDip);
        void PersistPanelState();

        // Comandi sui controlli nativi
        winrt::Microsoft::UI::Xaml::Controls::TabView NativeTabView() const;
        winrt::Microsoft::UI::Xaml::Controls::TabViewItem NativeContainer(int index);
        void CloseTab(int index);
        void NewTab();
        bool MoveNativeTab(int from, int to);

        // Colori delle schede (come in VTabsNotepad: "Colore scheda" nel menu contestuale). Sono legati al
        // modello della scheda e restano anche se la colonna si spegne e si riaccende; la tray li salva
        // nella sessione (live-<hwnd>.txt) e li rimette nelle finestre che riapre (pending-<hwnd>.txt).
        int ColorOf(winrt::Windows::Foundation::IInspectable const& item) const;
        void SetTabColor(winrt::Windows::Foundation::IInspectable const& item, int color);
        void PruneColors();
        void ExportColors();
        void TryApplyPendingColors();
        std::wstring ColorFilePath(const wchar_t* kind) const;
        void AddColorMenu(winrt::Microsoft::UI::Xaml::Controls::MenuFlyout const& menu, winrt::Windows::Foundation::IInspectable const& item);
        void EnsureColorMenu(winrt::Microsoft::UI::Xaml::Controls::MenuFlyout const& menu, winrt::Windows::Foundation::IInspectable const& item,
                             const wchar_t* when);
        void RemoveColorMenu();

        winrt::weak_ref<winrt::Microsoft::UI::Xaml::Controls::TabView> m_tabView;
        winrt::weak_ref<winrt::Microsoft::UI::Xaml::Controls::ListView> m_nativeList;
        HWND m_main = nullptr;
        WindowLayout m_layout;
        UnifiedHeader m_unifiedHeader; // fasce degli indirizzi e dei comandi con lo sfondo della barra del titolo
        std::shared_ptr<NavBar> m_navBar; // barra delle cartelle (se accesa)
        // Barra spenta: il riquadro di spostamento di sistema nell'ordine dei gruppi (NavOrder), ultima lettura vista.
        void KeepNativeOrder();
        uint64_t m_orderVersion = 0;
        ModSettings m_settings;
        bool m_attached = false;
        bool m_enabled = false;
        bool m_syncing = false;
        bool m_syncQueued = false;

        // La nostra isola XAML
        winrt::Microsoft::UI::Xaml::Hosting::DesktopWindowXamlSource m_source{ nullptr };
        HWND m_panelHwnd = nullptr;
        // Geometria del pannello in cima alla finestra (px dal suo bordo superiore; 0 = colonna sotto l'intestazione)
        int m_captionPx = 0;
        int m_contentTopPx = 0;
        // Regione applicata all'isola (UpdateCaptionRegion): fascia e pulsanti che restano (px dell'isola)
        int m_regionCaptionPx = 0;
        std::vector<RECT> m_regionKeep;
        // Barra degli indirizzi dell'intestazione e animazione del suo margine destro (riga del titolo nascosta)
        winrt::weak_ref<winrt::Microsoft::UI::Xaml::FrameworkElement> m_toolbar;
        winrt::weak_ref<winrt::Microsoft::UI::Xaml::FrameworkElement> m_toolbarInsetTarget;
        winrt::Microsoft::UI::Xaml::Media::Animation::Storyboard m_toolbarInset{ nullptr };

        // Interfaccia del pannello
        winrt::Microsoft::UI::Xaml::Controls::Grid m_root{ nullptr };
        winrt::Microsoft::UI::Xaml::Controls::Grid m_header{ nullptr };
        winrt::Microsoft::UI::Xaml::Controls::Button m_toggle{ nullptr };
        winrt::Microsoft::UI::Xaml::Controls::FontIcon m_toggleIcon{ nullptr };
        winrt::Microsoft::UI::Xaml::Controls::Grid m_body{ nullptr };
        winrt::Microsoft::UI::Xaml::Controls::ListView m_list{ nullptr };
        winrt::Microsoft::UI::Xaml::Controls::Button m_newTab{ nullptr };
        winrt::Microsoft::UI::Xaml::Controls::Border m_separator{ nullptr };
        winrt::Microsoft::UI::Xaml::Controls::Border m_separatorActive{ nullptr };
        winrt::Microsoft::UI::Xaml::Controls::Border m_splitter{ nullptr };
        winrt::Windows::Foundation::Collections::IObservableVector<winrt::Windows::Foundation::IInspectable> m_visible{ nullptr };
        bool m_horizontalWheelBlocked = false;
        bool m_compactVisual = false; // aspetto compresso attualmente applicato a righe e intestazione

        // Aspetto dei titoli (ReadNativeTitleStyle). Finché non è letto: dimensione delle schede del
        // TabView (12), pesi 400/600 e colore ereditato.
        struct TitleStyle
        {
            double size = 12.0;
            winrt::Microsoft::UI::Xaml::Media::FontFamily family{ nullptr };
            winrt::Windows::UI::Text::FontWeight normalWeight{ 400 };
            winrt::Windows::UI::Text::FontWeight selectedWeight{ 600 };
            winrt::Microsoft::UI::Xaml::Media::Brush normal{ nullptr };
            winrt::Microsoft::UI::Xaml::Media::Brush hover{ nullptr }; // null: il passaggio del mouse non lo cambia
            winrt::Microsoft::UI::Xaml::Media::Brush selected{ nullptr };
        };
        TitleStyle m_titleStyle;
        bool m_titleStyleRead = false;
        uint32_t m_titleStyleTicks = 0;
        uint32_t m_ticks = 0; // controlli periodici (500 ms) da quando la finestra è agganciata

        // Dati nativi: gli elementi della lista schede (ItemCollection del TabListView)
        std::vector<std::unique_ptr<Row>> m_rows;
        winrt::Windows::Foundation::Collections::IObservableVector<winrt::Windows::Foundation::IInspectable> m_items{ nullptr };
        winrt::event_token m_itemsToken{};
        winrt::Microsoft::UI::Xaml::Controls::TabView::SelectionChanged_revoker m_selectionRevoker;
        winrt::Microsoft::UI::Xaml::FrameworkElement::ActualThemeChanged_revoker m_themeRevoker;

        // Trascinamento in corso
        bool m_dragPending = false; // tasto premuto su una riga, soglia di movimento non ancora superata
        bool m_dragging = false;
        uint32_t m_dragPointerId = 0;
        winrt::Microsoft::UI::Xaml::Controls::ListViewItem m_dragElement{ nullptr };
        int m_dragFromIndex = -1;
        winrt::Windows::Foundation::Point m_dragStart{}; // DIP, rispetto all'elenco
        double m_dragY = 0;                              // ultima posizione verticale del puntatore (DIP)
        winrt::Microsoft::UI::Dispatching::DispatcherQueueTimer m_autoScrollTimer{ nullptr };

        // Ridimensionamento in corso
        bool m_resizing = false;
        bool m_splitterHovered = false;
        int m_grabOffsetPx = 0; // larghezza alla presa meno la x del cursore: larghezza = offset + x attuale

        // Parti della striscia nativa rese invisibili (con i valori originali da rimettere)
        struct HiddenPart
        {
            winrt::weak_ref<winrt::Microsoft::UI::Xaml::UIElement> element;
            double opacity = 1.0;
            bool hitTest = true;
        };
        std::vector<HiddenPart> m_hidden;
        // Cache dei contenitori della striscia nativa allargata (tutte le schede realizzate, per titoli e icone)
        winrt::weak_ref<winrt::Microsoft::UI::Xaml::Controls::ItemsStackPanel> m_nativePanel;
        double m_nativeCacheLength = 0;
        bool m_notifierLogged = false;

        // Colori delle schede
        std::vector<std::pair<winrt::Windows::Foundation::IInspectable, int>> m_colors; // modello della scheda -> colore
        std::optional<TabColorList> m_pendingColors;
        ULONGLONG m_pendingSince = 0;
        std::string m_lastExport; // ultimo contenuto scritto in live-<hwnd>.txt
        winrt::Microsoft::UI::Xaml::Controls::MenuFlyout m_extendedMenu{ nullptr };
        std::vector<winrt::Microsoft::UI::Xaml::Controls::MenuFlyoutItemBase> m_menuExtras;
        winrt::Microsoft::UI::Xaml::Controls::Primitives::FlyoutBase::Closed_revoker m_flyoutClosedRevoker;
        winrt::Microsoft::UI::Xaml::Controls::Primitives::FlyoutBase::Opening_revoker m_flyoutOpeningRevoker;
        winrt::Microsoft::UI::Xaml::Controls::Primitives::FlyoutBase::Opened_revoker m_flyoutOpenedRevoker;
    };
}
