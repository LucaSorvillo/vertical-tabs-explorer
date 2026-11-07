#pragma once

#include "ModernMenu.h"
#include "NavDrop.h"
#include "NavGroups.h"
#include "NavItems.h"

namespace xpx::mod
{
    class WindowLayout;

    // Il riquadro di spostamento come barra orizzontale sotto la barra dei comandi, come la barra dei
    // segnalibri di Chrome: le stesse voci di primo livello del riquadro (NavItems), con icona e nome, da
    // sinistra a destra, ognuna così com'è nel riquadro (nessuna reinterpretazione: anche quelle con nome
    // invisibile restano voci normali), con i divisori del riquadro di sistema (linee prima e dopo il blocco
    // delle cartelle aggiunte); quelle che non ci stanno finiscono nell'elenco "»" in fondo. Clic: la
    // voce si apre nella scheda attiva; clic centrale o Ctrl+clic: in una scheda nuova (verbo "opennewtab"
    // della shell, come "Apri in una nuova scheda"); clic destro: il menu contestuale nuovo di Windows 11, quello
    // del riquadro (ModernMenu; con Maiusc, o se non è disponibile, quello classico, che è anche "Mostra altre
    // opzioni"). Il riquadro nativo lo nasconde WindowLayout (solo in questa finestra); come il riquadro, la barra
    // sparisce nelle schede in cui è spento da Visualizza > Mostra > Riquadro di spostamento e torna quando lo si
    // riaccende (WindowLayout::SetBar).
    //
    // Gruppi (NavGroups), come le cartelle dei segnalibri: nel blocco delle cartelle aggiunte, con l'icona di una
    // pila di cartelle; clic: l'elenco del contenuto (i sottogruppi si aprono a lato, anche solo fermandosi sopra);
    // clic centrale: tutte le sue cartelle in schede nuove; clic destro: Apri tutte, Nuovo gruppo, Rinomina,
    // Elimina gruppo (il contenuto torna al livello di sopra). Clic destro sullo sfondo della barra: Nuovo gruppo;
    // nel menu classico di una cartella aggiunta ("Mostra altre opzioni"): "Sposta nel gruppo" e "Togli dal gruppo".
    // Le cartelle restano aggiunte ad Accesso rapido: il gruppo vive solo nella barra.
    //
    // Cartelle aggiunte e gruppi si spostano trascinandoli: nella barra, negli elenchi ("»" e gruppi) e dall'uno
    // all'altro; lasciati al centro di un gruppo ci entrano, e fermandosi sopra un gruppo (o su "»") il suo elenco
    // si apre, anche al posto di quello da cui parte la voce (come Chrome: la si lascia dove si vuole nel gruppo). L'ordine delle cartelle che restano al primo livello è quello di Windows (NavItems::MovePinned):
    // la barra lo cambia quando serve, il resto lo ricorda NavGroups.
    //
    // Cartelle trascinate da fuori (Esplora file, il desktop, un altro programma), come i link sulla barra dei segnalibri
    // di Chrome: lasciate nella barra si aggiungono ad Accesso rapido in quel punto, lasciate al centro di un gruppo ci
    // entrano, e fermandosi su un gruppo (o su "»") il suo elenco si apre per lasciarle nel punto voluto (NavDrop.h;
    // bersaglio OLE dell'isola e delle finestre degli elenchi, vedi OnExternalDragEnter).
    //
    // Una NOSTRA isola XAML (DesktopWindowXamlSource, Mica Alt come la barra del titolo) che WindowLayout
    // mette tra l'intestazione e il contenuto. Vive e si usa sul thread UI della finestra (la possiede
    // VerticalTabs); gli handler tengono solo un weak_ptr e riconoscono le voci dalla chiave (NavGroups.h), mai
    // da un indice che una lettura nuova potrebbe cambiare.
    class NavBar : public std::enable_shared_from_this<NavBar>
    {
    public:
        static constexpr int kHeightDip = 34;

        NavBar(HWND main, WindowLayout& layout);
        ~NavBar();
        NavBar(NavBar const&) = delete;
        NavBar& operator=(NavBar const&) = delete;

        bool Create(winrt::Microsoft::UI::Xaml::ElementTheme theme);
        void Destroy();
        HWND Hwnd() const { return m_hwnd; }
        // Ogni 500 ms (dal controllo periodico della finestra): voci o gruppi nuovi, dimensione delle icone,
        // finestra appena attivata (si rilegge: una cartella può essere stata aggiunta da un'altra finestra).
        void OnTick();
        void SetTheme(winrt::Microsoft::UI::Xaml::ElementTheme theme);
        void SetVisible(bool shown);
        // WM_CONTEXTMENU arrivato alla finestra principale (es. il menu nuovo che si riapre da sé dopo un clic destro
        // mentre quello di prima si chiudeva): true se il punto è su una voce della barra (menu aperto lì).
        bool OnContextMenuAt(POINT screen);
        // Il contenuto di una scheda sta per essere distrutto: se il menu nuovo era suo, si chiude prima (la sua isola
        // XAML è una figlia della scheda).
        void OnTabDestroying(HWND tab);
        // Comandi di prova (mod-command.txt, solo per la finestra indicata), con i numeri delle voci della barra:
        // "navbar open <i>", "navbar newtab <i>", "navbar more" (apre l'elenco »), "navbar closemenu" (chiude elenchi
        // e menu), "navbar context <i>" (menu contestuale, come il clic destro), "navbar classic <i>" (menu
        // classico), "navbar move <i> <posto>" (come trascinare la voce i prima della voce posto; posto = numero di
        // voci: in fondo), "navbar info". Per i gruppi (per nome; "f:<nome>" una cartella, "g:<nome>" un gruppo):
        // vedi DevGroupCommand in NavBar.cpp.
        void DevCommand(std::wstring const& command);

        // Trascinamento da fuori, dal bersaglio OLE dell'isola o della finestra di un elenco (NavDrop.cpp), sul thread della
        // finestra. Restituiscono l'effetto: DROPEFFECT_LINK (o COPY se la sorgente non ammette il collegamento), mai MOVE:
        // Esplora file sposterebbe la cartella. Passando dalla barra a un elenco (o viceversa) Windows dà un'uscita e
        // un'entrata: il trascinamento continua (vedi OnExternalTick).
        DWORD OnExternalDragEnter(IDataObject* data, POINT screen, DWORD allowed);
        DWORD OnExternalDragOver(POINT screen, DWORD allowed);
        void OnExternalDragLeave();
        DWORD OnExternalDrop(IDataObject* data, POINT screen, DWORD allowed);

    private:
        struct Level;

        // Voce della barra (primo livello): una voce del riquadro o un gruppo.
        struct Entry
        {
            std::wstring key;   // chiave (NavGroups.h)
            std::wstring look;  // nome e icona: se non cambiano, il pulsante resta quello
            winrt::Microsoft::UI::Xaml::Controls::Button button{ nullptr };
            // Divisore prima della voce, come nel riquadro di sistema: prima della prima voce del blocco delle
            // cartelle aggiunte e prima della prima voce dopo. Segue la visibilità della voce.
            winrt::Microsoft::UI::Xaml::FrameworkElement divider{ nullptr };
            std::shared_ptr<Level> level; // gruppo: il suo elenco
            double width = 0;   // larghezza voluta (DIP), compreso lo spazio dopo (e il divisore prima)
        };

        struct Row
        {
            std::wstring key;
            std::wstring look;
            winrt::Microsoft::UI::Xaml::Controls::Button button{ nullptr };
            std::shared_ptr<Level> level; // gruppo: il suo elenco (si apre a lato)
        };

        // Elenco a discesa: "»" (le voci che non stanno nella barra) o il contenuto di un gruppo, in un flyout con i
        // nostri pulsanti (un MenuFlyout non si lascia trascinare). Ogni pulsante di un gruppo ha il suo; quelli
        // aperti stanno in m_levels, dal primo (aperto dalla barra) al più interno: m_levels[i]->depth == i.
        struct Level
        {
            std::wstring group;     // "" = "»"
            size_t depth = 0;       // 0 = aperto dalla barra; d + 1 = da una riga di un elenco di profondità d
            winrt::Microsoft::UI::Xaml::Controls::Flyout flyout{ nullptr };
            winrt::Microsoft::UI::Xaml::Controls::ScrollViewer scroll{ nullptr }; // alto al più fino al fondo dello schermo
            winrt::Microsoft::UI::Xaml::Controls::Grid root{ nullptr };
            winrt::Microsoft::UI::Xaml::Controls::StackPanel list{ nullptr };
            winrt::Microsoft::UI::Xaml::FrameworkElement marker{ nullptr };
            std::vector<Row> rows;
            size_t firstEntry = 0;  // "»": voce della barra della prima riga
            bool open = false;
            // Aperto durante un trascinamento da fuori: la sua finestra (Microsoft.UI.Content.PopupWindowSiteBridge) e se ha il
            // nostro bersaglio OLE (tolto alla chiusura).
            HWND dropWindow = nullptr;
            bool dropRegistered = false;
        };

        // Dove andrà la voce trascinata.
        struct DropTarget
        {
            bool valid = false;
            std::wstring container;  // "" = primo livello, altrimenti l'id del gruppo
            std::wstring before;     // prima di questa voce ("" = in fondo)
            int level = -1;          // la linea: nella barra (-1) o nell'elenco m_levels[level]
            size_t slot = SIZE_MAX;  // linea prima della voce o riga slot (= numero di voci o righe: dopo l'ultima)
            winrt::Microsoft::UI::Xaml::Controls::Button into{ nullptr }; // lasciata sul gruppo: dentro, in fondo
            std::shared_ptr<Level> hover; // elenco che si apre restando fermi (di un gruppo, o "»")
            winrt::Microsoft::UI::Xaml::Controls::Button hoverAnchor{ nullptr };
        };

        // Trascinamento di una cartella aggiunta o di un gruppo (puntatore catturato dalla radice della barra o
        // dell'elenco da cui parte). Fermi su un gruppo della profondità dell'elenco da cui parte la voce (o meno
        // profondo), si apre il suo elenco e quello di partenza si chiude (uno per profondità): prima il trascinamento si
        // "stacca" (DetachDrag) e da lì segue cursore e tasto letti ogni 16 ms, perché la finestra dell'elenco chiuso può
        // tenersi il puntatore e WinUI ne scarta l'input (anche il rilascio).
        struct Drag
        {
            bool armed = false;   // tasto sinistro premuto su una voce trascinabile
            bool active = false;  // oltre la soglia di Windows: si sta trascinando
            bool polled = false;  // staccato (o comando di prova): cursore e tasto letti dal timer
            bool test = false;    // comando di prova: punto e tasto di prova, mai il mouse vero
            std::wstring key;
            winrt::Microsoft::UI::Xaml::Controls::Button source{ nullptr };
            winrt::Microsoft::UI::Xaml::UIElement holder{ nullptr };
            int fromLevel = -1;   // -1 = dalla barra (o staccato), altrimenti profondità dell'elenco
            uint32_t pointer = 0;
            POINT start{};        // cursore alla pressione (pixel dello schermo)
            POINT lastCursor{ LONG_MIN, LONG_MIN }; // staccato: ultimo punto già considerato
            DropTarget target;
            ULONGLONG hoverSince = 0; // fermi su target.hover da allora
        };

        // --- barra ---
        void Rebuild();
        Entry MakeEntry(std::wstring const& key, std::wstring const& look);
        void UpdateOverflow();
        // --- voci ---
        void SetSnapshot(std::shared_ptr<const NavSnapshot> snapshot);
        std::optional<size_t> ItemOf(std::wstring const& key) const;
        std::wstring LookOf(std::wstring const& key) const;
        std::wstring NameOf(std::wstring const& key) const;
        std::vector<std::wstring> PinnedKeys() const;
        std::unordered_set<std::wstring> PresentKeys() const;
        bool IsDraggable(std::wstring const& key) const;
        winrt::Microsoft::UI::Xaml::Media::ImageSource MakeIcon(NavItem const& item) const;
        winrt::Microsoft::UI::Xaml::UIElement IconElement(std::wstring const& key) const;
        std::wstring TooltipOfKey(std::wstring const& key) const;
        // Il suggerimento di un gruppo (nome e contenuto) rimesso sul pulsante se è cambiato (il pulsante resta).
        void RefreshGroupTooltip(winrt::Microsoft::UI::Xaml::Controls::Button const& button, std::wstring const& key) const;
        int IconPixels() const;
        // --- elenchi ---
        std::shared_ptr<Level> MakeLevel(std::wstring const& group, size_t depth);
        void BuildLevel(Level& level);
        void OnLevelOpening(std::shared_ptr<Level> const& level);
        void OnLevelClosed(std::shared_ptr<Level> const& level);
        std::vector<std::pair<std::wstring, std::wstring>> LevelContent(Level const& level) const;
        void FillLevel(std::shared_ptr<Level> const& level);
        void RefreshLevels(bool barRebuilt);
        Row MakeRow(std::shared_ptr<Level> const& level, std::wstring const& key, std::wstring const& look);
        void CloseLevels(size_t fromDepth);
        void ShowLevel(std::shared_ptr<Level> const& level, winrt::Microsoft::UI::Xaml::FrameworkElement const& anchor, bool transient);
        void HoverRow(std::shared_ptr<Level> const& level, std::shared_ptr<Level> const& rowLevel, winrt::Microsoft::UI::Xaml::Controls::Button const& button);
        // Con un elenco aperto dalla barra: appena il puntatore è su un altro gruppo della barra (o su "»"), si apre il suo
        // al posto di quello aperto, senza animazioni (come la barra dei segnalibri di Chrome). Il puntatore si legge da
        // qui, ogni 16 ms: con un elenco aperto i pulsanti della barra non lo ricevono.
        void OnSwitchTimer();
        // Pulsante della barra di un elenco aperto dalla barra (gruppo o "»") evidenziato finché l'elenco è aperto, come
        // la cartella aperta nella barra dei segnalibri di Chrome: lo sfondo del passaggio del mouse.
        void SetOpenHighlight(std::shared_ptr<Level> const& level, bool on);
        winrt::Microsoft::UI::Xaml::Media::SolidColorBrush OpenHighlightBrush() const;
        // Comando di prova "navbar group hover <gruppo>": il controllo del cambio usa questo punto invece del cursore vero
        // (si prova senza muovere il mouse dell'utente); "hover off" lo toglie.
        std::optional<POINT> m_testCursor;
        ULONGLONG m_testCursorSince = 0;
        // Comandi di prova "dragto <x>|<y>" e "dragup": cursore (pixel dello schermo) e tasto del trascinamento di prova.
        std::optional<POINT> m_testDragCursor;
        bool m_testDragDown = false;
        bool IsUnder(winrt::Microsoft::UI::Xaml::FrameworkElement const& element, POINT screen) const;
        // Dove si vede il gruppo (pulsante della barra o riga di un elenco aperto), se si vede.
        winrt::Microsoft::UI::Xaml::FrameworkElement AnchorOf(std::wstring const& key) const;
        // Apre gli elenchi fino a quello del gruppo (dalla barra o da "»", passando dai gruppi che lo contengono).
        bool RevealGroup(std::wstring const& id);
        // --- azioni ---
        // Apre la voce nella scheda attiva o in una nuova (dalla scheda indicata, altrimenti da quella attiva).
        void Open(size_t index, bool newTab, HWND fromTab = nullptr);
        // "Apri tutte": le cartelle del gruppo in schede nuove, una alla volta (OpenNextQueued).
        void OpenAll(std::wstring const& id, UINT settleMs = 0);
        // Ogni apertura in una scheda nuova passa da qui: una alla volta, ognuna quando la scheda di prima ha la sua vista
        // dei file (due schede nuove create una sopra l'altra fanno cadere Esplora file).
        void QueueNewTab(std::wstring const& key);
        void EnsureOpenTimer();
        void OpenNextQueued();
        size_t ReadyTabs() const;
        void OnItemClick(std::wstring const& key, bool newTab);
        void OnRightTapped(std::wstring const& key, winrt::Microsoft::UI::Xaml::FrameworkElement const& anchor,
                           winrt::Microsoft::UI::Xaml::Input::RightTappedRoutedEventArgs const& args);
        // Clic destro: il menu nuovo (inputFlags: ModernMenu::kFrom...), con Maiusc o in mancanza quello classico.
        void ShowContextMenu(size_t index, POINT screen, DWORD inputFlags = ModernMenu::kFromRightClick, bool classic = false);
        ModernMenu::ShowResult ShowModernMenu(size_t index, POINT screen, DWORD inputFlags);
        void ShowClassicMenu(std::vector<uint8_t> const& idList, std::wstring const& key, POINT screen);
        // Esegue la voce scelta (indice per InvokeCommand) con il browser della scheda come sito.
        void InvokeMenuCommand(IContextMenu* menu, UINT command, POINT point, std::wstring const& key);
        void RestoreFocusAfterMenu();
        // Voci dei gruppi nel menu di una cartella aggiunta ("Sposta nel gruppo", "Togli dal gruppo"), con id da
        // firstId; RunGroupCommand esegue quella scelta (id meno firstId).
        void AddGroupCommands(HMENU menu, IContextMenu* shellMenu, UINT shellFirstId, UINT firstId, UINT lastId, std::wstring const& key);
        void RunGroupCommand(UINT offset, std::wstring const& key);
        void ShowGroupMenu(std::wstring const& id, winrt::Microsoft::UI::Xaml::FrameworkElement const& anchor,
                           std::optional<winrt::Windows::Foundation::Point> position);
        void ShowBarMenu(winrt::Windows::Foundation::Point position);
        void NewGroup(std::wstring const& container, std::wstring const& before, std::wstring const& first, bool edit);
        void EditGroupName(std::wstring const& id, bool created);
        void DeleteGroup(std::wstring const& id);
        void TakeOutOfGroup(std::wstring const& key);
        bool PlaceNode(std::wstring const& key, std::wstring const& container, std::wstring const& before);
        // Stato nuovo dei gruppi: l'ordine delle cartelle di primo livello in Windows (se serve) e la barra.
        void Apply(std::shared_ptr<const NavGroupsState> const& state);
        // L'ordine di Windows delle cartelle aggiunte come la barra letta in profondità (top: il primo livello voluto).
        void SyncWindowsOrder(NavGroupsState const& state, std::vector<std::wstring> const& top);
        // Il riquadro di spostamento di sistema delle schede della finestra non rilegge mai l'ordine delle cartelle aggiunte
        // (lo prendono solo le schede aperte dopo): finché la barra lo nasconde lo si mette in pari, riordinando le voci di
        // primo livello del suo albero come le dà Windows. Spegnendo la barra si ritrova l'ordine giusto.
        void SyncNativePanes();
        winrt::Microsoft::UI::Xaml::Style MenuPresenterStyle() const;
        // Voce della barra sotto il punto (schermo), se c'è; centro della voce (schermo) se è nella barra.
        std::optional<size_t> EntryAt(POINT screen) const;
        std::optional<POINT> CenterOf(size_t index) const;
        std::optional<POINT> CenterOfElement(winrt::Microsoft::UI::Xaml::FrameworkElement const& element) const;

        // --- trascinamento ---
        void ArmDrag(std::wstring const& key, winrt::Microsoft::UI::Xaml::Controls::Button const& source, winrt::Microsoft::UI::Xaml::UIElement const& holder,
                     int fromLevel, winrt::Microsoft::UI::Xaml::Input::PointerRoutedEventArgs const& args);
        void OnDragMoved(winrt::Microsoft::UI::Xaml::Input::PointerRoutedEventArgs const& args);
        void OnDragReleased(winrt::Microsoft::UI::Xaml::Input::PointerRoutedEventArgs const& args);
        void EndDrag();
        bool BeginDrag(winrt::Microsoft::UI::Xaml::Input::PointerRoutedEventArgs const& args);
        // Ogni 16 ms durante il trascinamento: staccato, cursore e tasto (rilascio = si lascia lì, anche fuori dalla barra e
        // dagli elenchi); fermi su un gruppo o su "»", il suo elenco.
        void OnDragTimer();
        // Fermi da kOpenListAfterMs su un gruppo (o su "»"): si apre il suo elenco (trascinamento della barra e da fuori).
        void OpenHoveredLevel();
        void DetachDrag();
        // Fine del trascinamento staccato: si lascia sul bersaglio dell'ultimo punto; DropDragged sposta la voce.
        void FinishDrag();
        void DropDragged(Drag const& drag);
        // Comando di prova "drag <voce>": trascinamento guidato da m_testDragCursor e m_testDragDown.
        bool StartTestDrag(std::wstring const& key);
        // Bersaglio per la posizione del puntatore (data come punto relativo a un elemento: così vale sia per il
        // puntatore vero sia per i comandi di prova).
        DropTarget HitTest(std::function<winrt::Windows::Foundation::Point(winrt::Microsoft::UI::Xaml::UIElement const&)> const& pointIn) const;
        // Lo stesso per un punto dello schermo (pixel fisici), portato nelle coordinate della radice della barra: quelle
        // in cui WinUI dà anche le posizioni negli elenchi (finestre a parte), come GetCurrentPoint.
        DropTarget HitTestScreen(POINT screen);
        DropTarget HitBar(double x) const;
        DropTarget HitLevel(size_t index, double y) const;
        DropTarget TopSlot(size_t entry, int level, size_t row) const;
        bool CanDropInto(std::wstring const& id) const;
        void ShowTarget(DropTarget const& target);
        void ClearTargetVisuals(DropTarget const& target);
        void AttachDragHandlers(winrt::Microsoft::UI::Xaml::UIElement const& holder);

        // --- cartelle trascinate da fuori ---
        struct ExternalDrag
        {
            bool active = false;                 // un trascinamento da fuori è sopra la barra
            std::vector<DroppedFolder> folders;  // le sue cartelle (vuoto: niente da aggiungere)
            winrt::com_ptr<IDataObject> data;    // per la descrizione accanto all'immagine
            std::wstring described;              // descrizione messa ("" = nessuna)
            ULONGLONG leftAt = 0;                // uscito da barra o elenco (0 = sopra uno dei due): finito se non rientra
        };
        // Cartella appena aggiunta da un trascinamento: quando la lettura nuova la porta va nel posto scelto (in un gruppo, o
        // accanto a un gruppo), e l'ordine di Windows segue, come trascinandola dalla barra.
        struct PendingDrop
        {
            std::wstring key;
            std::wstring container;
            std::wstring before;
            ULONGLONG until = 0;
        };
        DWORD ExternalEffect(DropTarget const& target, DWORD allowed) const;
        // Il testo accanto all'immagine del trascinamento: sempre "Aggiungi ad Accesso rapido", come Windows.
        void DescribeExternal(DWORD effect);
        void DropFolders(std::vector<DroppedFolder> const& folders, DropTarget const& target);
        // Cartella aggiunta prima della quale va, nell'ordine di Windows, una cartella lasciata prima di "before" al primo
        // livello: la prima cartella da lì in poi (vuoto: in fondo).
        std::vector<uint8_t> WindowsAnchorFor(std::wstring const& before) const;
        void PlacePendingDrops();
        // Ogni 16 ms durante un trascinamento da fuori: bersaglio sulle finestre degli elenchi appena aperti, elenco che si
        // apre restando fermi, e la fine se il trascinamento è uscito da barra ed elenchi senza rientrare.
        void OnExternalTick();
        // Fine (rilascio, o uscita): linea e descrizione via, e gli elenchi aperti si chiudono appena finita la chiamata OLE.
        void EndExternalDrag();
        void AttachLevelDropTargets();
        void DetachLevelDropTarget(Level& level);
        // La finestra a comparsa dell'elenco aperto (della finestra principale, sul nostro thread), se si trova.
        HWND FindLevelWindow(Level const& level) const;
        // Dove si vede l'elenco aperto (pixel dello schermo).
        std::optional<RECT> LevelScreenRect(Level const& level) const;
        // Il punto è sulla barra o su un elenco aperto, o lì accanto (lo spazio tra la barra e un elenco).
        bool NearBarOrLevels(POINT screen) const;

        void DevGroupCommand(std::wstring const& command);
        std::wstring KeyByName(std::wstring const& reference) const;

        HWND m_main = nullptr;
        WindowLayout& m_layout;
        HWND m_hwnd = nullptr;
        winrt::Microsoft::UI::Xaml::ElementTheme m_theme = winrt::Microsoft::UI::Xaml::ElementTheme::Default;
        winrt::Microsoft::UI::Xaml::Hosting::DesktopWindowXamlSource m_source{ nullptr };
        winrt::Microsoft::UI::Xaml::Controls::Grid m_root{ nullptr };
        winrt::Microsoft::UI::Xaml::Controls::StackPanel m_items{ nullptr };
        winrt::Microsoft::UI::Xaml::Controls::Button m_overflow{ nullptr };
        winrt::Microsoft::UI::Xaml::Controls::Canvas m_barOverlay{ nullptr };
        winrt::Microsoft::UI::Xaml::FrameworkElement m_barMarker{ nullptr };
        winrt::Microsoft::UI::Xaml::Style m_listStyle{ nullptr };  // degli elenchi (come un menu)
        std::shared_ptr<Level> m_overflowLevel;          // elenco "»"
        std::vector<std::shared_ptr<Level>> m_levels;     // elenchi aperti, dal primo al più interno
        winrt::Microsoft::UI::Dispatching::DispatcherQueueTimer m_hoverTimer{ nullptr };
        std::function<void()> m_hoverAction;               // al passare del puntatore su una riga, dopo un attimo
        winrt::Microsoft::UI::Dispatching::DispatcherQueueTimer m_dragTimer{ nullptr };
        winrt::Microsoft::UI::Dispatching::DispatcherQueueTimer m_switchTimer{ nullptr };
        winrt::Microsoft::UI::Xaml::Controls::Flyout m_nameEditor{ nullptr };
        winrt::Microsoft::UI::Xaml::Controls::TextBox m_nameBox{ nullptr };
        std::shared_ptr<const NavSnapshot> m_snapshot;
        std::shared_ptr<const NavGroupsState> m_groups;
        std::unordered_map<std::wstring, size_t> m_itemByKey; // chiave -> voce dello snapshot
        std::vector<Entry> m_entries;
        size_t m_visibleCount = 0;
        size_t m_blockFirst = 0; // blocco delle cartelle aggiunte e dei gruppi: voci [m_blockFirst, m_blockEnd)
        size_t m_blockEnd = 0;
        Drag m_drag;
        ExternalDrag m_external;
        std::vector<PendingDrop> m_pendingDrops;
        winrt::com_ptr<IDropTarget> m_dropTarget; // registrato sull'isola (RegisterDragDrop)
        int m_iconPixels = 0;
        bool m_wasForeground = false;
        ULONGLONG m_lastActivationRefresh = 0;
        bool m_menuOpen = false; // menu classico (modale) aperto
        // Menu nuovo: il presenter della scheda attiva (come quelli di Windows, uno per scheda: la sua isola XAML è
        // figlia della scheda; si rifà cambiando scheda) e la voce del menu aperto.
        std::unique_ptr<ModernMenu> m_modernMenu;
        HWND m_modernMenuTab = nullptr;
        winrt::com_ptr<IContextMenu> m_menuContext;
        std::vector<uint8_t> m_menuItem;
        std::wstring m_menuKey;
        bool m_menuRetryPending = false; // menu rimandato: il presenter manderà WM_CONTEXTMENU (OnContextMenuAt)
        std::vector<std::wstring> m_groupCommands; // "Sposta nel gruppo": gruppo di ogni voce del sottomenu
        // Schede nuove in coda: cartelle ancora da aprire, la scheda da cui si aprono, la pausa dopo che la scheda di prima
        // è pronta, e l'attesa in corso (schede pronte prima, inizio, prossima apertura non prima di).
        std::vector<std::wstring> m_openQueue;
        HWND m_openTab = nullptr;
        UINT m_openSettle = 0;
        bool m_openWaiting = false;
        size_t m_openReadyBefore = 0;
        ULONGLONG m_openStarted = 0;
        ULONGLONG m_openNextAt = 0;
        winrt::Microsoft::UI::Dispatching::DispatcherQueueTimer m_openTimer{ nullptr };
    };
}
