#pragma once

#include "CaptionButtons.h"

namespace xpx::mod
{
    // Riserva una colonna a sinistra in una finestra di Esplora file per il pannello delle schede
    // verticali.
    //
    // Il contenuto di ogni scheda è una finestra figlia (ShellTabWindowClass) che Esplora file mette
    // sotto l'intestazione, a tutta larghezza, e riposiziona a ogni ridimensionamento o cambio scheda.
    // Un subclass ne corregge ogni spostamento (WM_WINDOWPOSCHANGING, quindi senza sfarfallio): il bordo
    // sinistro passa a destra della colonna. Il rettangolo chiesto da Esplora file resta il "naturale":
    // serve a ripristinare tutto e a dare al pannello la stessa altezza del contenuto.
    // La colonna va dalla cima della finestra al fondo, come le schede verticali di Chrome: l'isola XAML
    // dell'intestazione di Esplora file (riga del titolo, barra degli indirizzi, barra dei comandi) comincia
    // dal suo bordo destro, con lo stesso subclass del contenuto; il XAML dell'isola si adatta da sé alla
    // nuova larghezza. In cima alla colonna, alta quanto la zona dei pulsanti della finestra, c'è la fascia
    // del titolo del pannello (comprimi/espandi): dove non ci sono pulsanti l'isola del pannello non
    // c'è (la sua regione di finestra la esclude, vedi VerticalTabs: il mouse delle isole XAML non passa da
    // WM_NCHITTEST, quindi HTTRANSPARENT non basterebbe) e sotto la finestra principale risponde HTCAPTION, così
    // lì la finestra si trascina, si ingrandisce col doppio clic e ha il menu di sistema.
    // La zona di trascinamento della barra del titolo (TITLE_BAR_SCAFFOLDING_WINDOW_CLASS, a destra delle
    // schede orizzontali) copre tutta la riga del titolo a destra della colonna (con la striscia schede
    // nascosta), con il meccanismo di Esplora file stesso. Senza l'isola dell'intestazione (non trovata) la
    // colonna resta sotto l'intestazione e la zona di trascinamento va fino al bordo sinistro.
    //
    // Riga del titolo nascosta (come Edge con "nascondi la barra del titolo" e le schede verticali): con la
    // colonna in cima la riga del titolo dell'intestazione resta vuota (le schede sono nella colonna) e si
    // nasconde. L'isola sale della sua altezza: la parte sopra la finestra resta fuori, e una regione della
    // finestra la taglia insieme ai 3 px in cima (il bordo superiore, che ridimensiona). Barra degli indirizzi
    // in cima, accanto ai pulsanti della finestra (VerticalTabs le toglie a destra lo spazio per loro); barra
    // delle cartelle e contenuto salgono con lei. La zona di trascinamento si stringe sui pulsanti della
    // finestra più un tratto libero alla loro sinistra; la fascia del titolo della colonna è alta quanto la
    // barra degli indirizzi. Le due righe (in DIP) le legge VerticalTabs dal XAML dell'intestazione. I pulsanti della
    // finestra sono i nostri (CaptionButtons), alti quanto la barra degli indirizzi: quelli di DWM sono più bassi.
    //
    // Le nuove schede si agganciano appena create (WM_PARENTNOTIFY della finestra principale), più un
    // controllo periodico di sicurezza. Va creato e usato sul thread UI della finestra (lo stesso di tutte
    // le sue finestre figlie).
    class WindowLayout
    {
    public:
        WindowLayout() = default;
        ~WindowLayout();
        WindowLayout(WindowLayout const&) = delete;
        WindowLayout& operator=(WindowLayout const&) = delete;

        // Aggancia la finestra principale (CabinetWClass) e le sue figlie.
        bool Attach(HWND main);
        // Finestra della nostra isola XAML (il pannello).
        void SetPanel(HWND panel);
        // Toglie tutti i subclass; restore=true rimette le finestre di Esplora file dove le aveva messe
        // (false: la finestra si sta chiudendo).
        void Detach(bool restore = true);

        // Attiva (larghezza della colonna in DIP) o disattiva il layout verticale.
        void SetActive(bool active, int widthDip);
        // Barra orizzontale sotto l'intestazione (il riquadro di spostamento in alto, vedi NavBar.h): altezza in
        // DIP, 0 = nessuna. Con la barra il contenuto delle schede (e la colonna) scende sotto di lei, e il
        // riquadro di spostamento nativo di ogni scheda sparisce: dentro il contenuto (DirectUIHWND) la sua
        // finestra va fuori a sinistra e la vista dei file si allarga al suo posto (vedi le Sink), come con il
        // riquadro spento; la barra di stato in fondo, disegnata da DirectUI a tutta larghezza, resta com'è.
        // Vale solo per questa finestra: l'impostazione "Riquadro di spostamento" di Esplora file non cambia,
        // anzi la barra la segue: se nella scheda attiva il riquadro è spento (Visualizza > Mostra > Riquadro di
        // spostamento, che Esplora file applica scheda per scheda) la barra sparisce e il contenuto torna sotto
        // l'intestazione, come farebbe il riquadro; riacceso, la barra torna.
        void SetBar(int heightDip);
        // Finestra della nostra isola XAML della barra.
        void SetBarWindow(HWND bar);
        // Dimentica la barra senza spostare niente (la finestra si sta chiudendo).
        void ForgetBar();
        // Altezza attuale della barra in pixel (0 se non c'è o se è nascosta col riquadro spento).
        int BarHeightPx() const;
        // Riquadro di spostamento spento nella scheda attiva: la barra è nascosta (vedi SetBar).
        bool NavPaneOff() const { return m_navOff; }
        // Ricontrolla finestre figlie, scala e posizioni. Idempotente ed economico.
        void Check();

        bool IsActive() const { return m_active; }
        HWND Main() const { return m_main; }
        UINT Dpi() const { return m_dpi ? m_dpi : 96; }
        // La colonna parte dalla cima della finestra (accanto all'intestazione, che comincia dal suo bordo destro).
        bool Raised() const { return m_active && m_header != nullptr; }
        // Righe dell'intestazione in DIP, lette dal XAML: altezza della riga del titolo (dove comincia la barra degli
        // indirizzi) e della barra degli indirizzi. 0 = non note (la riga del titolo resta).
        void SetHeaderRows(double titleRowDip, double toolbarRowDip);
        // Con la colonna in cima la riga del titolo è nascosta (vedi sopra).
        bool TitleHidden() const { return Raised() && m_titleRowDip > 0 && m_toolbarRowDip > 0; }
        // Spazio a destra della barra degli indirizzi con la riga del titolo nascosta: i tre pulsanti della finestra
        // (46 DIP l'uno) più un tratto libero da cui trascinarla.
        static constexpr double kCaptionReserveDip = 3 * CaptionButtons::kButtonWidthDip + 16.0;
        // I nostri pulsanti della finestra (finestra null se ci sono quelli di DWM).
        CaptionButtons& Buttons() { return m_buttons; }
        // Fascia del titolo in cima alla colonna (coordinate client della finestra principale); vuota se la
        // colonna non è in cima o non si vede.
        RECT PanelCaptionRect() const;
        // Contenuto della scheda in primo piano (la prima ShellTabWindowClass in ordine Z).
        HWND ActiveContent() const;
        // Dove dare il focus della tastiera dopo un clic nel pannello: la vista dei file della scheda in
        // primo piano (o, se non c'è, il suo contenuto).
        HWND ContentFocusTarget() const;

        // Mostra/nasconde il pannello: lo fa l'isola XAML (non basta ShowWindow sul suo HWND).
        std::function<void(bool)> onPanelVisibility;
        // Geometria del pannello in cima alla finestra, in px dal suo bordo superiore: altezza della fascia del
        // titolo (i suoi pulsanti) e inizio del contenuto delle schede (da lì in giù il separatore e il divisore);
        // 0, 0 con la colonna sotto l'intestazione. Chiamato quando cambia.
        std::function<void(int captionPx, int contentTopPx)> onPanelGeometry;
        // Lo stesso per la barra.
        std::function<void(bool)> onBarVisibility;
        // Messaggi dei menu alla finestra principale (WM_INITMENUPOPUP, WM_DRAWITEM, WM_MEASUREITEM,
        // WM_MENUCHAR) mentre è aperto un nostro menu contestuale della shell, per i suoi sottomenu (Invia a,
        // Apri con...): true = gestito, con il risultato.
        std::function<bool(UINT, WPARAM, LPARAM, LRESULT&)> onMenuMessage;
        // WM_CONTEXTMENU alla finestra principale o al contenuto di una scheda con un punto dello schermo (es. il
        // menu nuovo della barra che si riapre da sé): true = gestito (il messaggio si ferma qui).
        std::function<bool(POINT)> onContextMenu;
        // Il contenuto di una scheda sta per essere distrutto (WM_DESTROY: le sue figlie ci sono ancora).
        std::function<void(HWND)> onContentDestroying;
        // La finestra sta per essere distrutta (WM_DESTROY: le figlie, compreso il pannello, sono ancora
        // vive). Chiamato una volta, sul suo thread; può staccare il layout.
        std::function<void()> onMainDestroying;

    private:
        enum class Role
        {
            Content, // ShellTabWindowClass: spostata a destra della colonna
            DragBar, // TITLE_BAR_SCAFFOLDING_WINDOW_CLASS: tutta la riga del titolo a destra della colonna
            Header   // isola XAML dell'intestazione (Microsoft.UI.Content.DesktopChildSiteBridge di Esplora file): dal bordo della colonna
        };
        struct Tracked
        {
            Role role = Role::Content;
            RECT natural{};
            RECT applied{};          // l'ultimo rettangolo dato da noi
            bool hasApplied = false;
        };

        // Finestre CtrlNotifySink dentro il contenuto di una scheda (DUIViewWndClassName > DirectUIHWND): una per
        // riquadro (quello di spostamento ha dentro NamespaceTreeControl, la vista dei file SHELLDLL_DefView, poi
        // anteprima e dettagli), posizionate da DirectUI. Con la barra (BarOn) la sink del riquadro di spostamento va fuori
        // a sinistra e quella accanto (la vista dei file) si allarga fino al bordo sinistro; la barra di stato, che DirectUI
        // disegna in fondo a tutta larghezza, resta al suo posto. DirectUI dà a ogni sink una regione grande quanto il
        // posto che le ha dato: per la vista allargata la regione si sposta e si allarga a sinistra (e torna com'era).
        // Anche le finestre dentro le sink (la vista dei file, SHELLDLL_DefView) si seguono: Esplora file le fa grandi quanto
        // il posto della sink, e con la sink allargata si allargano con lei (parent = la sink che le contiene).
        struct Sink
        {
            HWND direct = nullptr;    // DirectUIHWND che la contiene
            HWND parent = nullptr;    // per le finestre interne: la sink
            bool nav = false;         // riquadro di spostamento (ha dentro NamespaceTreeControl)
            RECT natural{};           // dove la mette DirectUI
            RECT applied{};           // l'ultimo rettangolo dato da noi
            bool hasApplied = false;
            int widened = 0;          // di quanto l'abbiamo allargata a sinistra (con la regione adattata)
            HRGN duiRegion = nullptr; // copia della regione di DirectUI (rimessa togliendo l'allargamento)
            bool hadRegion = false;   // DirectUI le aveva dato una regione
            RECT regionBox{};         // riquadro della regione messa da noi
        };

        static LRESULT CALLBACK MainProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam, UINT_PTR id, DWORD_PTR refData);
        static LRESULT CALLBACK ChildProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam, UINT_PTR id, DWORD_PTR refData);
        static LRESULT CALLBACK SinkProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam, UINT_PTR id, DWORD_PTR refData);
        static void CALLBACK NavEventProc(HWINEVENTHOOK hook, DWORD event, HWND hwnd, LONG idObject, LONG idChild, DWORD thread, DWORD time);
        static std::optional<Role> RoleOf(HWND hwnd);
        // Riquadro di spostamento nativo di una scheda acceso (true) o spento da Visualizza (false); nullopt se
        // la scheda non ce l'ha ancora (appena creata).
        static std::optional<bool> NavPaneShown(HWND content);

        void TrackChildren();
        void Track(HWND child, Role role);
        // L'isola dell'intestazione di Esplora file: un'isola XAML figlia diretta che non è una delle nostre.
        bool IsHeaderIsland(HWND child) const;
        void OnChildPosChanging(HWND child, WINDOWPOS& pos);
        RECT Wanted(HWND hwnd, Tracked const& tracked) const;
        void Move(HWND child, RECT const& rect);
        // Rettangolo voluto e, per il contenuto, la regione che nasconde il riquadro di spostamento.
        void Enforce(HWND hwnd, Tracked const& tracked);
        void ApplyAll();
        void PositionPanel();
        void PositionBar();
        // Zona vetro della finestra fino all'inizio del contenuto salito (riga del titolo nascosta), vedi Glass.h.
        void UpdateGlass();
        // Pulsanti della finestra alti quanto la barra degli indirizzi con la riga del titolo nascosta (vedi CaptionButtons),
        // altrimenti quelli di DWM.
        void UpdateCaptionButtons();
        void RequestPanelUpdate();
        int PanelWidthPx() const;
        // Bordo superiore della colonna: in cima, quello dell'intestazione; altrimenti dove comincia il contenuto.
        int PanelTopPx() const;
        // Altezza della fascia del titolo in cima alla colonna: fino al fondo della zona di trascinamento (la zona dei
        // pulsanti della finestra), o 32 DIP se non c'è; con la riga del titolo nascosta, quanto la barra degli
        // indirizzi; 0 con la colonna sotto l'intestazione.
        int CaptionHeightPx() const;
        // Riga del titolo nascosta: di quanto salgono intestazione, barra e contenuto (0 se non è nascosta).
        int TitleRowPx() const;
        int ToolbarRowPx() const;
        int CaptionReservePx() const { return MulDiv(static_cast<int>(kCaptionReserveDip), static_cast<int>(Dpi()), 96); }
        // Regione dell'isola dell'intestazione: con la riga del titolo nascosta ne taglia la parte sopra la cima della
        // finestra più i 3 px del bordo (topPx, coordinate dell'isola); 0 = nessuna regione.
        void UpdateHeaderRegion(HWND header, int topPx);
        // Il layout cambia qualcosa (colonna o barra).
        bool Engaged() const { return m_active || m_barDip > 0; }
        // La barra occupa il suo spazio (c'è e il riquadro di spostamento della scheda attiva è acceso).
        bool BarOn() const { return m_barDip > 0 && !m_navOff; }
        // Sink dei contenuti delle schede (vedi Sink): aggancio delle nuove, posizioni e regioni volute.
        void TrackSinks();
        void ApplySinks();
        // Di quanto si allargano le sink accanto al riquadro di spostamento in questo DirectUIHWND (da dove comincia la
        // vista dei file, fino al bordo sinistro); 0 = niente da nascondere (barra spenta, riquadro spento o non ancora creato).
        int SinkShift(HWND direct) const;
        RECT SinkWanted(Sink const& sink) const;
        int AdjacentLeft(HWND direct) const;
        void OnSinkPosChanging(HWND sink, WINDOWPOS& pos);
        void FixSinkRegion(HWND hwnd, Sink& sink);
        // Toglie i subclass delle sink; restore=true le rimette dove le vuole DirectUI, con la sua regione.
        void ReleaseSinks(bool restore);
        // Con la barra: eventi di comparsa/scomparsa delle finestre di questo thread, per seguire subito il
        // riquadro di spostamento acceso o spento da Visualizza (il controllo periodico resta come rete).
        void WatchNavPane(bool watch);
        // Rilegge lo stato del riquadro nella scheda attiva; true se è cambiato (layout da rifare).
        bool RefreshNavPane(const wchar_t* why); // why: da dove (per il log)

        HWND m_main = nullptr;
        HWND m_panel = nullptr;
        HWND m_bar = nullptr;
        HWND m_header = nullptr;   // isola dell'intestazione di Esplora file (seguita con il ruolo Header)
        RECT m_headerNatural{};    // dove Esplora file mette l'intestazione
        double m_titleRowDip = 0;  // righe dell'intestazione (SetHeaderRows)
        double m_toolbarRowDip = 0;
        int m_headerRegionTop = 0; // regione applicata all'isola dell'intestazione (UpdateHeaderRegion)
        RECT m_dragNatural{};      // dove Esplora file mette la zona di trascinamento (vuoto: non trovata)
        std::pair<int, int> m_geometry{ -1, -1 }; // ultima geometria comunicata (onPanelGeometry)
        int m_barDip = 0;
        bool m_barShown = false;
        bool m_navOff = false;               // riquadro di spostamento spento nella scheda attiva: barra nascosta
        HWINEVENTHOOK m_navHook = nullptr;   // vedi WatchNavPane
        std::unordered_map<HWND, Sink> m_sinks;  // sink dei contenuti delle schede (vedi Sink)
        HWND m_regionFixing = nullptr;           // sink di cui stiamo cambiando la regione (il suo WM_WINDOWPOSCHANGED è nostro)
        bool m_active = false;
        bool m_panelShown = false;
        bool m_updatePosted = false;
        int m_widthDip = 240;
        UINT m_dpi = 96;
        HWND m_moving = nullptr; // finestra che stiamo spostando noi (la sua richiesta non è "naturale")
        std::unordered_map<HWND, Tracked> m_tracked;
        RECT m_contentNatural{}; // dove Esplora file mette il contenuto delle schede
        bool m_haveContent = false;
        bool m_driftLogged = false;
        CaptionButtons m_buttons;
    };
}
