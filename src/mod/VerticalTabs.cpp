#include "pch.h"
#include "VerticalTabs.h"

#include "Interop.h"
#include "ModLog.h"
#include "../ModShared.h"
#include "NavOrder.h"

#include <winrt/Microsoft.UI.Windowing.h>
#include <dwmapi.h>

namespace xpx::mod
{
    namespace
    {
        namespace mux = winrt::Microsoft::UI::Xaml;
        namespace muxc = winrt::Microsoft::UI::Xaml::Controls;
        namespace muxp = winrt::Microsoft::UI::Xaml::Controls::Primitives;
        namespace muxm = winrt::Microsoft::UI::Xaml::Media;
        namespace muxma = winrt::Microsoft::UI::Xaml::Media::Animation;
        namespace muxi = winrt::Microsoft::UI::Xaml::Input;
        namespace wfc = winrt::Windows::Foundation::Collections;
        using winrt::Microsoft::UI::Dispatching::DispatcherQueuePriority;
        using winrt::Windows::Foundation::IInspectable;
        using winrt::Windows::Foundation::Point;
        using winrt::Windows::System::VirtualKey;

        // Pannello: intestazione (comprimi/espandi), elenco schede con il "+" subito sotto
        // l'ultima scheda, separatore e divisore trascinabile. I pennelli {ThemeResource} seguono il tema
        // della radice (impostato come quello di Esplora file). Lo sfondo di norma è la Mica Alt dell'isola
        // (ApplyBackdrop), come la barra del titolo; il pennello del XAML è il suo colore pieno di riserva.
        constexpr wchar_t kPanelXaml[] = LR"xaml(<Grid xmlns="http://schemas.microsoft.com/winfx/2006/xaml/presentation"
      xmlns:x="http://schemas.microsoft.com/winfx/2006/xaml" %BACKGROUND%>
  <Grid.RowDefinitions>
    <RowDefinition Height="Auto"/>
    <RowDefinition Height="*"/>
  </Grid.RowDefinitions>
  <Grid x:Name="Header" Grid.Row="0" Margin="6,6,6,2">
    <Grid.ColumnDefinitions>
      <ColumnDefinition Width="Auto"/>
      <ColumnDefinition Width="*"/>
    </Grid.ColumnDefinitions>
    <Grid.RowDefinitions>
      <RowDefinition Height="Auto"/>
    </Grid.RowDefinitions>
    <Button x:Name="Toggle" Width="40" Height="34" Padding="0" Background="Transparent" BorderThickness="0" TabIndex="3"
            AutomationProperties.Name="Comprimi schede">
      <FontIcon x:Name="ToggleIcon" Glyph="&#xEA49;" FontSize="16"/>
    </Button>
  </Grid>
  <!-- Corpo: l'elenco è alto quanto le sue righe (fino allo spazio disponibile, poi scorre: MaxHeight
       impostato dal codice) e il "+" lo segue subito sotto; con molte schede resta visibile in fondo. -->
  <Grid x:Name="Body" Grid.Row="1">
    <Grid.RowDefinitions>
      <RowDefinition Height="Auto"/>
      <RowDefinition Height="Auto"/>
      <RowDefinition Height="*"/>
    </Grid.RowDefinitions>
    <ListView x:Name="List" Grid.Row="0" VerticalAlignment="Top" SelectionMode="Single" IsItemClickEnabled="True" Padding="0,2,0,2"
              TabIndex="0" MaxHeight="0" AutomationProperties.Name="Schede"
              ScrollViewer.HorizontalScrollMode="Disabled" ScrollViewer.HorizontalScrollBarVisibility="Disabled">
      <!-- La scheda attiva la disegna la riga (fondo, bordino e stanghetta): fondo e stanghetta di
           selezione predefiniti spenti. Il passaggio del mouse resta quello di sistema. -->
      <ListView.Resources>
        <SolidColorBrush x:Key="ListViewItemBackgroundSelected" Color="Transparent"/>
        <SolidColorBrush x:Key="ListViewItemBackgroundSelectedPointerOver" Color="Transparent"/>
        <SolidColorBrush x:Key="ListViewItemBackgroundSelectedPressed" Color="Transparent"/>
        <SolidColorBrush x:Key="ListViewItemSelectionIndicatorBrush" Color="Transparent"/>
        <SolidColorBrush x:Key="ListViewItemSelectionIndicatorPointerOverBrush" Color="Transparent"/>
        <SolidColorBrush x:Key="ListViewItemSelectionIndicatorPressedBrush" Color="Transparent"/>
      </ListView.Resources>
      <!-- Solo animazione di riposizionamento: quelle di comparsa/rimozione tengono la riga rimossa
           agganciata all'elenco mentre sfuma, e le righe (contenitori di sé stesse) devono poter essere
           reinserite subito, ad esempio durante il trascinamento. -->
      <ListView.ItemContainerTransitions>
        <TransitionCollection>
          <RepositionThemeTransition IsStaggeringEnabled="False"/>
        </TransitionCollection>
      </ListView.ItemContainerTransitions>
      <ListView.ItemContainerStyle>
        <Style TargetType="ListViewItem" %BASEDON%>
          <Setter Property="HorizontalContentAlignment" Value="Stretch"/>
          <Setter Property="MinHeight" Value="36"/>
          <Setter Property="MinWidth" Value="0"/>
          <Setter Property="Padding" Value="0"/>
        </Style>
      </ListView.ItemContainerStyle>
    </ListView>
    <Button x:Name="NewTab" Grid.Row="1" Height="32" VerticalAlignment="Top" HorizontalAlignment="Stretch"
            HorizontalContentAlignment="Center" Padding="0" Background="Transparent" BorderThickness="0" TabIndex="2"
            ToolTipService.ToolTip="Nuova scheda" AutomationProperties.Name="Nuova scheda">
      <FontIcon Glyph="&#xE710;" FontSize="14"/>
    </Button>
  </Grid>
  <Border x:Name="Separator" Grid.RowSpan="2" Width="1" HorizontalAlignment="Right" %SEPARATOR%/>
  <Border x:Name="SeparatorActive" Grid.RowSpan="2" Width="2" HorizontalAlignment="Right" Visibility="Collapsed" %ACCENT%/>
  <Border x:Name="Splitter" Grid.RowSpan="2" Width="6" HorizontalAlignment="Right" Background="Transparent"
          AutomationProperties.Name="Larghezza pannello schede"/>
</Grid>)xaml";

        // Una riga = un ListViewItem che fa da contenitore di sé stesso (niente riciclo: lo stesso elemento
        // può essere tolto e rimesso nell'elenco dal filtro e dal trascinamento). Sotto il contenuto c'è il
        // fondo della riga, una "card" appena velata; la scheda attiva ha il velo più deciso, un bordino e
        // la stanghetta a sinistra. Icona e titolo sono quelli della scheda nativa.
        constexpr wchar_t kRowXaml[] = LR"xaml(<ListViewItem xmlns="http://schemas.microsoft.com/winfx/2006/xaml/presentation"
      xmlns:x="http://schemas.microsoft.com/winfx/2006/xaml">
  <Grid Height="36">
  <Border x:Name="RowTint" Margin="4,2,4,2" CornerRadius="4" BorderThickness="1" BackgroundSizing="OuterBorderEdge"/>
  <Border x:Name="Pill" Width="3" Height="16" Margin="5,0,0,0" CornerRadius="1.5" HorizontalAlignment="Left"
          VerticalAlignment="Center" %PILL% Visibility="Collapsed"/>
  <Border x:Name="PillColor" Width="3" Height="16" Margin="5,0,0,0" CornerRadius="1.5" HorizontalAlignment="Left"
          VerticalAlignment="Center" Visibility="Collapsed"/>
  <Grid x:Name="RowGrid" Margin="12,0,4,0" Background="Transparent" ColumnSpacing="10">
    <Grid.ColumnDefinitions>
      <ColumnDefinition Width="Auto"/>
      <ColumnDefinition Width="*"/>
      <ColumnDefinition Width="28"/>
    </Grid.ColumnDefinitions>
    <Image x:Name="Icon" Width="16" Height="16" VerticalAlignment="Center"/>
    <TextBlock x:Name="Title" Grid.Column="1" VerticalAlignment="Center" TextTrimming="CharacterEllipsis" TextWrapping="NoWrap"/>
    <Button x:Name="Close" Grid.Column="2" Width="28" Height="28" Padding="0" Background="Transparent" BorderThickness="0"
            Visibility="Collapsed" ToolTipService.ToolTip="Chiudi scheda" AutomationProperties.Name="Chiudi scheda">
      <FontIcon Glyph="&#xE711;" FontSize="10"/>
    </Button>
  </Grid>
  </Grid>
</ListViewItem>)xaml";

        // "+" sotto l'ultima scheda: occupa lo spazio di una riga, più un margine in fondo; tutto questo si
        // sottrae allo spazio dell'elenco.
        constexpr double kNewTabHeight = 32.0;
        constexpr double kNewTabTopMargin = 2.0;
        constexpr double kNewTabBottomMargin = 6.0;
        // Rientro orizzontale (DIP) degli sfondi delle righe della ListView: il "+" vi si allinea.
        constexpr double kRowInset = 4.0;
        // Pulsanti dell'intestazione del pannello: altezza normale e margine (sopra e sotto) nella fascia del titolo.
        constexpr double kHeaderButtonHeight = 34.0;
        constexpr double kCaptionButtonInset = 2.0;

        // Fondo delle righe (opacità 0-255 sopra lo sfondo del pannello), come in VTabsNotepad: il colore della
        // scheda è una tinta tenue (tema scuro: tono chiaro; tema chiaro: tono saturo); senza colore, un velo
        // neutro appena visibile (bianco nel tema scuro, nero nel chiaro). La scheda attiva ha la tinta più
        // decisa e un bordino dello stesso velo neutro.
        struct CardStyle
        {
            uint8_t colored;
            uint8_t coloredSelected;
            uint8_t normal;
            uint8_t selected;
            uint8_t border;
        };
        constexpr CardStyle kCardDark{ 0x26, 0x45, 0x0B, 0x1A, 0x22 };
        constexpr CardStyle kCardLight{ 0x1F, 0x38, 0x08, 0x0F, 0x1F };
        // Stanghetta della scheda attiva nel tema scuro: il colore della scheda appena più profondo.
        constexpr double kPillShadeDark = 0.9;
        // Colori da rimettere in una finestra che non arriva al numero di schede atteso: dopo questo tempo
        // si applicano comunque alle schede che ci sono.
        constexpr ULONGLONG kPendingColorsTimeoutMs = 20'000;

        winrt::Windows::UI::Color Shade(winrt::Windows::UI::Color color, double factor)
        {
            const auto scale = [factor](uint8_t channel) { return static_cast<uint8_t>(std::lround(channel * factor)); };
            return { color.A, scale(color.R), scale(color.G), scale(color.B) };
        }
        // Controllo completo dell'elenco ogni N controlli periodici (da 500 ms: 4 s), aspetto dei titoli
        // riletto ogni M controlli completi (8 s).
        constexpr uint32_t kFullSyncEveryTicks = 8;
        constexpr uint32_t kTitleStyleEveryTicks = 2;

        constexpr double kSplitterGap = 6.0;     // spazio lasciato libero dall'elenco per il divisore
        constexpr double kDragThreshold = 6.0;   // DIP di movimento prima che una pressione diventi trascinamento
        constexpr double kAutoScrollEdge = 28.0; // fascia (DIP) ai bordi dell'elenco che lo fa scorrere
        constexpr double kAutoScrollMaxStep = 18.0;
        // Cache dei contenitori della striscia nativa mentre è nascosta: abbastanza grande da tenere
        // realizzate tutte le schede (titolo e icona si leggono da lì).
        constexpr double kNativeCacheLength = 1000.0;

        // Alza un flag per la durata di un blocco, anche se nel blocco viene lanciata un'eccezione.
        class FlagScope
        {
        public:
            explicit FlagScope(bool& flag) : m_flag(flag), m_previous(flag) { m_flag = true; }
            ~FlagScope() { m_flag = m_previous; }
            FlagScope(FlagScope const&) = delete;
            FlagScope& operator=(FlagScope const&) = delete;

        private:
            bool& m_flag;
            bool m_previous;
        };

        // Esegue un handler senza lasciar uscire eccezioni verso il runtime XAML di Esplora file.
        template <typename F>
        void Guard(const wchar_t* where, F&& action) noexcept
        {
            try
            {
                action();
            }
            catch (...)
            {
                XPX_MLOG(L"VerticalTabs: errore in %s (0x%08X).", where, static_cast<unsigned>(winrt::to_hresult()));
            }
        }

        std::wstring ReplaceToken(std::wstring text, std::wstring_view token, std::wstring_view value)
        {
            const size_t at = text.find(token);
            if (at != std::wstring::npos)
            {
                text.replace(at, token.size(), value);
            }
            return text;
        }

        // Il markup è scritto in italiano: con l'app in inglese se ne traducono i testi per l'utente (nomi per
        // l'accessibilità e suggerimenti), cercando gli attributi esatti.
        std::wstring Localized(std::wstring xaml)
        {
            if (UiItalian())
            {
                return xaml;
            }
            static constexpr std::pair<std::wstring_view, std::wstring_view> kEnglish[] = {
                { LR"(="Comprimi schede")", LR"(="Collapse tabs")" },
                { LR"(AutomationProperties.Name="Schede")", LR"(AutomationProperties.Name="Tabs")" },
                { LR"(="Nuova scheda")", LR"(="New tab")" },
                { LR"(="Larghezza pannello schede")", LR"(="Tab panel width")" },
                { LR"(="Chiudi scheda")", LR"(="Close tab")" },
            };
            for (auto const& [italian, english] : kEnglish)
            {
                for (size_t at = xaml.find(italian); at != std::wstring::npos; at = xaml.find(italian, at + english.size()))
                {
                    xaml.replace(at, italian.size(), english);
                }
            }
            return xaml;
        }

        mux::FrameworkElement LoadXaml(std::wstring const& xaml)
        {
            return mux::Markup::XamlReader::Load(Localized(xaml)).as<mux::FrameworkElement>();
        }

        template <typename T>
        T FindPart(mux::FrameworkElement const& root, wchar_t const* name)
        {
            return root.FindName(name).try_as<T>();
        }

        // Ricerca in profondità nell'albero visuale (per raggiungere i controlli nativi).
        template <typename T>
        T FindDescendant(mux::DependencyObject const& root, std::wstring_view name = {})
        {
            const int count = muxm::VisualTreeHelper::GetChildrenCount(root);
            for (int i = 0; i < count; ++i)
            {
                const auto child = muxm::VisualTreeHelper::GetChild(root, i);
                if (const auto typed = child.try_as<T>())
                {
                    if (name.empty())
                    {
                        return typed;
                    }
                    if (const auto element = child.try_as<mux::FrameworkElement>(); element && element.Name() == name)
                    {
                        return typed;
                    }
                }
                if (const auto found = FindDescendant<T>(child, name))
                {
                    return found;
                }
            }
            return nullptr;
        }

        // Gruppo di stati visivi "CommonStates" del modello di un controllo (sta sulla radice del modello).
        mux::VisualStateGroup CommonStates(muxc::Control const& control)
        {
            if (muxm::VisualTreeHelper::GetChildrenCount(control) == 0)
            {
                return nullptr;
            }
            const auto root = muxm::VisualTreeHelper::GetChild(control, 0).try_as<mux::FrameworkElement>();
            if (!root)
            {
                return nullptr;
            }
            for (auto const& group : mux::VisualStateManager::GetVisualStateGroups(root))
            {
                if (group.Name() == L"CommonStates")
                {
                    return group;
                }
            }
            return nullptr;
        }

        bool SameBrush(muxm::Brush const& a, muxm::Brush const& b)
        {
            if (a == b)
            {
                return true;
            }
            const auto solidA = a ? a.try_as<muxm::SolidColorBrush>() : nullptr;
            const auto solidB = b ? b.try_as<muxm::SolidColorBrush>() : nullptr;
            return solidA && solidB && solidA.Color() == solidB.Color() && solidA.Opacity() == solidB.Opacity();
        }

        // Colore effettivo del testo: il primo Foreground impostato risalendo dal testo alla scheda (il
        // testo creato da un ContentPresenter lo eredita, e il suo Foreground può risultare vuoto).
        muxm::Brush EffectiveForeground(muxc::TextBlock const& text, muxc::Control const& container)
        {
            for (mux::DependencyObject node = text; node && node != container; node = muxm::VisualTreeHelper::GetParent(node))
            {
                muxm::Brush brush{ nullptr };
                if (const auto block = node.try_as<muxc::TextBlock>())
                {
                    brush = block.Foreground();
                }
                else if (const auto presenter = node.try_as<muxc::ContentPresenter>())
                {
                    brush = presenter.Foreground();
                }
                else if (const auto control = node.try_as<muxc::Control>())
                {
                    brush = control.Foreground();
                }
                if (brush)
                {
                    return brush;
                }
            }
            return container.Foreground();
        }

        // Colore che il modello di una scheda nativa dà al titolo al passaggio del mouse. I Setter degli
        // stati visivi dei modelli di sistema non si lasciano leggere, quindi la scheda (invisibile nella
        // striscia nascosta) viene portata nello stato "PointerOver", letta e subito riportata allo stato
        // di prima, tutto nello stesso passaggio (nulla viene disegnato nel mezzo). Null se non cambia.
        muxm::Brush TitleBrushOnHover(muxc::TabViewItem const& container, muxc::TextBlock const& text, winrt::hstring const& currentState)
        {
            const muxm::Brush normal = EffectiveForeground(text, container);
            if (!mux::VisualStateManager::GoToState(container, L"PointerOver", false))
            {
                return nullptr;
            }
            muxm::Brush hover{ nullptr };
            try
            {
                hover = EffectiveForeground(text, container);
            }
            catch (...)
            {
            }
            mux::VisualStateManager::GoToState(container, currentState.empty() ? winrt::hstring{ L"Normal" } : currentState, false);
            return SameBrush(hover, normal) ? nullptr : hover;
        }

        // Colore di un pennello per il log (#AARRGGBB), o il suo tipo.
        std::wstring BrushText(muxm::Brush const& brush)
        {
            if (!brush)
            {
                return L"-";
            }
            if (const auto solid = brush.try_as<muxm::SolidColorBrush>())
            {
                const auto c = solid.Color();
                wchar_t text[32]{};
                swprintf_s(text, L"#%02X%02X%02X%02X", c.A, c.R, c.G, c.B);
                return text;
            }
            return std::wstring{ winrt::get_class_name(brush) };
        }

        // Primo elemento (in profondità) che ha un menu contestuale assegnato.
        mux::UIElement FindContextFlyoutOwner(mux::DependencyObject const& root)
        {
            const int count = muxm::VisualTreeHelper::GetChildrenCount(root);
            for (int i = 0; i < count; ++i)
            {
                const auto child = muxm::VisualTreeHelper::GetChild(root, i);
                if (const auto element = child.try_as<mux::UIElement>(); element && element.ContextFlyout())
                {
                    return element;
                }
                if (const auto found = FindContextFlyoutOwner(child))
                {
                    return found;
                }
            }
            return nullptr;
        }

        // "Preme" un pulsante tramite UI Automation: scatena la stessa logica di un clic dell'utente.
        bool InvokeButton(muxc::Button const& button)
        {
            if (!button)
            {
                return false;
            }
            try
            {
                winrt::Microsoft::UI::Xaml::Automation::Peers::ButtonAutomationPeer peer{ button };
                peer.Invoke();
                return true;
            }
            catch (...)
            {
                return false;
            }
        }

        UINT DpiOf(HWND hwnd)
        {
            const UINT dpi = GetDpiForWindow(hwnd);
            return dpi ? dpi : USER_DEFAULT_SCREEN_DPI;
        }

        winrt::Windows::UI::Color WithAlpha(winrt::Windows::UI::Color color, uint8_t alpha)
        {
            color.A = alpha;
            return color;
        }

        // Testo del titolo di una scheda nativa: il TextBlock visibile della sua intestazione (uno
        // StackPanel con un anello di avanzamento e il testo).
        muxc::TextBlock TitleBlockOf(muxc::TabViewItem const& container)
        {
            if (const auto panel = container.Header().try_as<muxc::Panel>())
            {
                for (auto const& child : panel.Children())
                {
                    if (const auto block = child.try_as<muxc::TextBlock>(); block && block.Visibility() == mux::Visibility::Visible)
                    {
                        return block;
                    }
                }
            }
            return nullptr;
        }

        winrt::hstring TitleOf(muxc::TabViewItem const& container)
        {
            if (const auto text = container.Header().try_as<winrt::Windows::Foundation::IReference<winrt::hstring>>())
            {
                return text.Value();
            }
            if (const auto block = TitleBlockOf(container))
            {
                return block.Text();
            }
            return winrt::Microsoft::UI::Xaml::Automation::AutomationProperties::GetName(container);
        }

        muxm::ImageSource IconOf(muxc::TabViewItem const& container)
        {
            if (const auto icon = container.IconSource().try_as<muxc::ImageIconSource>())
            {
                return icon.ImageSource();
            }
            return nullptr;
        }

        // Isola XAML dell'intestazione di Esplora file: tra le isole figlie della finestra (escluse le nostre:
        // pannello e barra delle cartelle), quella più in alto.
        HWND FindHeaderIsland(HWND main, HWND panel)
        {
            HWND best = nullptr;
            LONG bestTop = LONG_MAX;
            for (HWND child : interop::DirectChildren(main))
            {
                if (child == panel || interop::ClassOf(child) != L"Microsoft.UI.Content.DesktopChildSiteBridge" ||
                    GetPropW(child, interop::kPanelProp) || GetPropW(child, interop::kNavBarProp))
                {
                    continue;
                }
                const RECT rect = interop::RectInClientOf(child, main);
                if (rect.top < bestTop)
                {
                    bestTop = rect.top;
                    best = child;
                }
            }
            return best;
        }
    }

    VerticalTabs::VerticalTabs(muxc::TabView const& tabView) : m_tabView(winrt::make_weak(tabView))
    {
    }

    VerticalTabs::~VerticalTabs()
    {
        Detach(true);
    }

    // ---------------------------------------------------------------------------------------------
    // Ciclo di vita
    // ---------------------------------------------------------------------------------------------

    void VerticalTabs::Apply(ModSettings const& settings)
    {
        if (!m_attached && !Attach())
        {
            m_settings = settings;
            return;
        }
        // Test automatici (dev-windows.txt): le finestre non elencate, cioè quelle dell'utente, restano
        // esattamente come sono, qualunque cosa la prova cambi nelle impostazioni (accesa, spenta,
        // compressa, larghezza). Finita la prova (elenco tolto) seguono di nuovo le impostazioni.
        if (settings.devAllowlist && !settings.Allows(m_main))
        {
            return;
        }
        ModSettings wanted = settings;
        if (m_resizing)
        {
            wanted.width = m_settings.width; // durante il trascinamento del divisore vale il mouse
        }
        const bool geometryChanged = wanted.width != m_settings.width || wanted.collapsed != m_settings.collapsed;
        m_settings = wanted;
        if (wanted.verticalTabs && !m_enabled)
        {
            Enable();
        }
        else if (!wanted.verticalTabs && m_enabled)
        {
            Disable();
        }
        else if (m_enabled && geometryChanged)
        {
            ApplyAppearance();
            m_layout.SetActive(true, m_settings.PanelWidth());
            SyncSelectionFromNative(); // la scheda attiva resta in vista
        }
        if (wanted.navBar && !m_navBar)
        {
            Guard(L"barra delle cartelle", [&] { EnableNavBar(); });
        }
        else if (!wanted.navBar && m_navBar)
        {
            Guard(L"barra delle cartelle", [&] { DisableNavBar(); });
        }
    }

    void VerticalTabs::EnableNavBar()
    {
        if (m_navBar || !m_main)
        {
            return;
        }
        const auto tabView = NativeTabView();
        auto bar = std::make_shared<NavBar>(m_main, m_layout);
        if (!bar->Create(tabView ? tabView.ActualTheme() : mux::ElementTheme::Default))
        {
            return;
        }
        m_navBar = bar;
        const std::weak_ptr<NavBar> weakBar = bar;
        m_layout.onBarVisibility = [weakBar](bool shown) {
            if (const auto self = weakBar.lock())
            {
                Guard(L"visibilità della barra", [&] { self->SetVisible(shown); });
            }
        };
        m_layout.onContextMenu = [weakBar](POINT point) {
            bool handled = false;
            if (const auto self = weakBar.lock())
            {
                Guard(L"menu della barra", [&] { handled = self->OnContextMenuAt(point); });
            }
            return handled;
        };
        m_layout.onContentDestroying = [weakBar](HWND tab) {
            if (const auto self = weakBar.lock())
            {
                Guard(L"scheda chiusa (menu della barra)", [&] { self->OnTabDestroying(tab); });
            }
        };
        m_layout.SetBarWindow(bar->Hwnd());
        m_layout.SetBar(NavBar::kHeightDip);
        XPX_MLOG(L"VerticalTabs: barra delle cartelle attiva (riquadro di spostamento nascosto in questa finestra).");
    }

    void VerticalTabs::DisableNavBar(bool restoreLayout)
    {
        if (!m_navBar)
        {
            return;
        }
        m_layout.onBarVisibility = nullptr;
        m_layout.onMenuMessage = nullptr;
        m_layout.onContextMenu = nullptr;
        m_layout.onContentDestroying = nullptr;
        if (restoreLayout)
        {
            m_layout.SetBar(0); // il contenuto torna sotto l'intestazione, con il riquadro di spostamento
            m_layout.SetBarWindow(nullptr);
        }
        else
        {
            m_layout.ForgetBar(); // finestra in chiusura: niente da spostare
        }
        m_navBar->Destroy();
        m_navBar.reset();
        XPX_MLOG(L"VerticalTabs: barra delle cartelle tolta%s.", restoreLayout ? L"" : L" (finestra chiusa)");
    }

    bool VerticalTabs::Attach()
    {
        const auto tabView = m_tabView.get();
        if (!tabView)
        {
            return false;
        }
        const HWND main = interop::MainWindowOf(tabView);
        if (!main || interop::ClassOf(main) != L"CabinetWClass")
        {
            return false;
        }
        const auto list = interop::FindByName(tabView, L"TabListView").try_as<muxc::ListView>();
        if (!list)
        {
            return false; // modello del TabView non ancora applicato: si riprova al prossimo controllo
        }
        m_main = main;
        m_nativeList = winrt::make_weak(list);

        // Chiusura della finestra: si libera tutto mentre le finestre figlie sono ancora vive.
        const std::weak_ptr<VerticalTabs> weak = weak_from_this();
        m_layout.onMainDestroying = [weak] {
            if (const auto self = weak.lock())
            {
                self->Detach(false);
                if (const auto callback = self->onWindowClosed)
                {
                    callback();
                }
            }
        };
        if (!m_layout.Attach(main))
        {
            m_main = nullptr;
            return false;
        }
        m_themeRevoker = tabView.ActualThemeChanged(winrt::auto_revoke, [weak](auto&&, auto&&) {
            if (const auto self = weak.lock())
            {
                Guard(L"ActualThemeChanged", [&] {
                    self->ApplyAppearance();
                    self->m_titleStyleRead = false; // i colori dei titoli cambiano con il tema
                    if (self->m_navBar)
                    {
                        if (const auto native = self->NativeTabView())
                        {
                            self->m_navBar->SetTheme(native.ActualTheme());
                        }
                    }
                });
            }
        });
        m_attached = true;

        // Cosa offre questa versione di Esplora file (per capire subito eventuali limiti).
        const auto source = list.ItemsSource();
        XPX_MLOG(L"VerticalTabs: agganciata la finestra %llx (origine delle schede: %s; IBindableVector %s, IVector %s).",
                 reinterpret_cast<uintptr_t>(main), source ? winrt::get_class_name(source).c_str() : L"-",
                 source && source.try_as<mux::Interop::IBindableVector>() ? L"sì" : L"no",
                 source && source.try_as<wfc::IVector<IInspectable>>() ? L"sì" : L"no");
        return true;
    }

    void VerticalTabs::Detach(bool restore)
    {
        Guard(L"barra delle cartelle", [&] { DisableNavBar(restore); });
        if (m_enabled)
        {
            if (restore)
            {
                Disable();
            }
            else
            {
                if (m_autoScrollTimer)
                {
                    m_autoScrollTimer.Stop();
                }
                m_dragging = false;
                m_dragPending = false;
                m_flyoutClosedRevoker.revoke();
                RemoveColorMenu();
                Unsubscribe();
                ClearRows();
                // Esplora file può riusare la finestra (stesso albero): l'intestazione torna com'era.
                Guard(L"ripristino dell'intestazione", [&] { m_unifiedHeader.Restore(); });
                m_enabled = false;
            }
        }
        // Finestra chiusa (o staccata): i suoi file dei colori non servono più (la tray li ha già letti).
        if (m_main && !restore)
        {
            DeleteFileW(ColorFilePath(L"live").c_str());
            DeleteFileW(ColorFilePath(L"pending").c_str());
        }
        m_colors.clear();
        m_pendingColors.reset();
        m_lastExport.clear();
        m_themeRevoker.revoke();
        m_layout.onPanelVisibility = nullptr;
        m_layout.onPanelGeometry = nullptr;
        m_layout.onMainDestroying = nullptr;
        m_layout.Detach(restore);
        m_captionPx = 0;
        m_contentTopPx = 0;
        m_regionCaptionPx = 0;
        m_regionKeep.clear();
        if (m_panelHwnd && IsWindow(m_panelHwnd))
        {
            RemovePropW(m_panelHwnd, interop::kPanelProp);
        }
        if (m_source)
        {
            try
            {
                m_source.Close();
            }
            catch (...)
            {
            }
            m_source = nullptr;
        }
        m_root = nullptr;
        m_list = nullptr;
        m_visible = nullptr;
        m_panelHwnd = nullptr;
        m_attached = false;
    }

    void VerticalTabs::DevCommand(std::wstring const& line)
    {
        // Ogni comando nomina la finestra di prova a cui è rivolto (HWND esadecimale in testa): le altre
        // finestre, comprese quelle dell'utente, lo ignorano.
        wchar_t* rest = nullptr;
        const uint64_t target = wcstoull(line.c_str(), &rest, 16);
        if (!m_main || target != static_cast<uint64_t>(reinterpret_cast<uintptr_t>(m_main)) || !rest)
        {
            return;
        }
        std::wstring command{ rest };
        const size_t first = command.find_first_not_of(L' ');
        command = first == std::wstring::npos ? std::wstring{} : command.substr(first);
        if (command.rfind(L"navbar ", 0) == 0)
        {
            if (m_navBar)
            {
                m_navBar->DevCommand(command);
            }
            return;
        }
        // Con la colonna spenta vale solo "theme" (le prove dell'intestazione originale ne hanno bisogno).
        if (!m_enabled && command.rfind(L"theme ", 0) != 0)
        {
            return;
        }
        int a = -1;
        int b = -1;
        if (swscanf_s(command.c_str(), L"move %d %d", &a, &b) == 2)
        {
            const int count = static_cast<int>(m_rows.size());
            const bool ok = a >= 0 && b >= 0 && a < count && b < count && a != b && MoveNativeTab(a, b);
            XPX_MLOG(L"VerticalTabs: comando di prova \"move %d %d\": %s.", a, b, ok ? L"eseguito" : L"non eseguito");
        }
        else if (swscanf_s(command.c_str(), L"menu %d", &a) == 1)
        {
            if (a >= 0 && a < static_cast<int>(m_rows.size()))
            {
                Row const& row = *m_rows[static_cast<size_t>(a)];
                ShowNativeContextMenu(row, row.element, Point{ 24.0f, 18.0f });
                XPX_MLOG(L"VerticalTabs: comando di prova \"menu %d\".", a);
            }
        }
        else if (swscanf_s(command.c_str(), L"width %d", &a) == 1)
        {
            ApplyPanelWidth(a);
            PersistPanelState();
            XPX_MLOG(L"VerticalTabs: comando di prova \"width %d\".", a);
        }
        else if (command == L"closemenu")
        {
            // Chiude il menu contestuale aperto dal pannello (come un clic altrove).
            if (m_extendedMenu)
            {
                m_extendedMenu.Hide();
            }
        }
        else if (wchar_t key[32]{}; swscanf_s(command.c_str(), L"color %d %31s", &a, key, static_cast<unsigned>(ARRAYSIZE(key))) == 2)
        {
            // Come la voce "Colore scheda" del menu ("none" = nessuno).
            if (a >= 0 && a < static_cast<int>(m_rows.size()))
            {
                SetTabColor(m_rows[static_cast<size_t>(a)]->item, TabColorFromKey(key));
                XPX_MLOG(L"VerticalTabs: comando di prova \"color %d %s\".", a, key);
            }
        }
        else if (wchar_t verb[16]{}; swscanf_s(command.c_str(), L"caption %15s %d", verb, static_cast<unsigned>(ARRAYSIZE(verb)), &a) >= 1)
        {
            // Pulsanti della finestra (CaptionButtons) senza mouse: "hover i", "press i", "clear", "invoke i" (0 riduci, 1 ingrandisci, 2 chiudi).
            auto& buttons = m_layout.Buttons();
            const std::wstring_view name{ verb };
            if (name == L"hover")
            {
                buttons.DevSetState(a, -1);
            }
            else if (name == L"press")
            {
                buttons.DevSetState(a, a);
            }
            else if (name == L"clear")
            {
                buttons.DevSetState(-1, -1);
            }
            else if (name == L"invoke" && a >= 0 && a <= 2)
            {
                buttons.DevInvoke(a);
            }
            XPX_MLOG(L"VerticalTabs: comando di prova \"caption %s %d\" (pulsanti %s).", verb, a, buttons.Window() ? L"nostri" : L"di DWM");
        }
        else if (wchar_t mode[16]{}; swscanf_s(command.c_str(), L"titlebar %15s", mode, static_cast<unsigned>(ARRAYSIZE(mode))) == 1)
        {
            // Solo lettura ("titlebar info"): barra del titolo del Windows App SDK (AppWindow), stile della finestra, pulsanti di
            // DWM (DWMWA_CAPTION_BUTTON_BOUNDS, vuoto con i nostri: CaptionButtons) e figlie dirette in ordine Z. Le prove con
            // ExtendsContentIntoTitleBar/PreferredHeightOption Tall (pulsanti alti del Windows App SDK) sono state tolte: tornando
            // indietro la finestra restava con una barra nera sopra la barra degli indirizzi (vedi README).
            try
            {
                namespace muw = winrt::Microsoft::UI::Windowing;
                const auto window = muw::AppWindow::GetFromWindowId(interop::WindowIdFromHwnd(m_main));
                if (!window)
                {
                    XPX_MLOG(L"VerticalTabs: comando di prova \"titlebar\": nessuna AppWindow per la finestra.");
                    return;
                }
                const auto bar = window.TitleBar();
                using ColorRef = winrt::Windows::Foundation::IReference<winrt::Windows::UI::Color>;
                const auto color = [](ColorRef const& value) {
                    if (!value)
                    {
                        return std::wstring{ L"-" };
                    }
                    const auto c = value.Value();
                    wchar_t text[16]{};
                    swprintf_s(text, L"#%02X%02X%02X%02X", c.A, c.R, c.G, c.B);
                    return std::wstring{ text };
                };
                const HWND main = m_main;
                const auto describe = [&](const wchar_t* when) {
                    int theme = -1;
                    try
                    {
                        theme = static_cast<int>(bar.PreferredTheme());
                    }
                    catch (...)
                    {
                    }
                    XPX_MLOG(L"VerticalTabs: titlebar %s: contenuto esteso %s, altezza preferita %d, altezza %d, margini %d/%d, tema %d, icona %d.",
                             when, bar.ExtendsContentIntoTitleBar() ? L"sì" : L"no", static_cast<int>(bar.PreferredHeightOption()), bar.Height(),
                             bar.LeftInset(), bar.RightInset(), theme, static_cast<int>(bar.IconShowOptions()));
                    XPX_MLOG(L"VerticalTabs: titlebar %s: colori fondo %s/%s, pulsanti fondo %s/%s/%s/%s, pulsanti testo %s/%s/%s/%s.", when,
                             color(bar.BackgroundColor()).c_str(), color(bar.InactiveBackgroundColor()).c_str(),
                             color(bar.ButtonBackgroundColor()).c_str(), color(bar.ButtonInactiveBackgroundColor()).c_str(),
                             color(bar.ButtonHoverBackgroundColor()).c_str(), color(bar.ButtonPressedBackgroundColor()).c_str(),
                             color(bar.ButtonForegroundColor()).c_str(), color(bar.ButtonInactiveForegroundColor()).c_str(),
                             color(bar.ButtonHoverForegroundColor()).c_str(), color(bar.ButtonPressedForegroundColor()).c_str());
                    RECT window{}, client{}, buttons{};
                    GetWindowRect(main, &window);
                    GetClientRect(main, &client);
                    POINT origin{};
                    ClientToScreen(main, &origin);
                    const HRESULT hrButtons = DwmGetWindowAttribute(main, DWMWA_CAPTION_BUTTON_BOUNDS, &buttons, sizeof(buttons));
                    RECT scaffold{};
                    LONG scaffoldStyle = 0;
                    if (const HWND drag = FindWindowExW(main, nullptr, L"TITLE_BAR_SCAFFOLDING_WINDOW_CLASS", nullptr))
                    {
                        GetWindowRect(drag, &scaffold);
                        OffsetRect(&scaffold, -origin.x, -origin.y);
                        scaffoldStyle = GetWindowLongW(drag, GWL_STYLE);
                    }
                    XPX_MLOG(L"VerticalTabs: titlebar %s: stile %08X/%08X, finestra %dx%d, client da (%d,%d) %dx%d, pulsanti DWM 0x%08X [%d,%d,%d,%d], "
                             L"zona di trascinamento [%d,%d,%d,%d] stile %08X.",
                             when, static_cast<unsigned>(GetWindowLongW(main, GWL_STYLE)), static_cast<unsigned>(GetWindowLongW(main, GWL_EXSTYLE)),
                             window.right - window.left, window.bottom - window.top, origin.x - window.left, origin.y - window.top,
                             client.right, client.bottom, static_cast<unsigned>(hrButtons), buttons.left, buttons.top, buttons.right,
                             buttons.bottom, scaffold.left, scaffold.top, scaffold.right, scaffold.bottom, static_cast<unsigned>(scaffoldStyle));
                    // Figlie dirette in ordine Z (dall'alto): classe, rettangolo client, visibile
                    std::wstring children;
                    for (HWND child = GetWindow(main, GW_CHILD); child; child = GetWindow(child, GW_HWNDNEXT))
                    {
                        wchar_t cls[96]{};
                        GetClassNameW(child, cls, ARRAYSIZE(cls));
                        std::wstring_view name{ cls };
                        if (name.starts_with(L"Microsoft.UI.Content."))
                        {
                            name.remove_prefix(21);
                        }
                        RECT r{};
                        GetWindowRect(child, &r);
                        OffsetRect(&r, -origin.x, -origin.y);
                        wchar_t line[200]{};
                        swprintf_s(line, L" %.*s%s[%d,%d,%d,%d]%s;", static_cast<int>(name.size()), name.data(),
                                   GetPropW(child, L"VTabsExplorer.VerticalTabsPanel") ? L"(pannello)" : L"", r.left, r.top, r.right, r.bottom,
                                   IsWindowVisible(child) ? L"" : L" nascosta");
                        children += line;
                    }
                    XPX_MLOG(L"VerticalTabs: titlebar %s: figlie%s", when, children.c_str());
                };
                describe(L"stato");
            }
            catch (...)
            {
                XPX_MLOG(L"VerticalTabs: comando di prova \"titlebar\" non riuscito (0x%08X).", static_cast<unsigned>(winrt::to_hresult()));
            }
        }
        else if (wchar_t theme[16]{}; swscanf_s(command.c_str(), L"theme %15s", theme, static_cast<unsigned>(ARRAYSIZE(theme))) == 1)
        {
            // Tema della sola intestazione di questa finestra (la radice del suo albero), come se cambiasse
            // quello di Windows: le risorse di tema dell'intestazione si rivalutano.
            if (const auto tabView = NativeTabView())
            {
                mux::DependencyObject top = tabView;
                while (const auto parent = muxm::VisualTreeHelper::GetParent(top))
                {
                    top = parent;
                }
                const std::wstring_view name{ theme };
                const auto requested = name == L"light"  ? mux::ElementTheme::Light
                                       : name == L"dark" ? mux::ElementTheme::Dark
                                                         : mux::ElementTheme::Default;
                if (const auto root = top.try_as<mux::FrameworkElement>())
                {
                    root.RequestedTheme(requested);
                }
                XPX_MLOG(L"VerticalTabs: comando di prova \"theme %s\".", theme);
            }
        }
    }

    void VerticalTabs::UpdateUnifiedHeader()
    {
        if (m_enabled)
        {
            if (const auto tabView = NativeTabView())
            {
                const bool first = !m_unifiedHeader.IsApplied();
                if (const size_t changed = m_unifiedHeader.Apply(tabView); changed > 0)
                {
                    XPX_MLOG(L"VerticalTabs: intestazione con lo sfondo della barra del titolo (%zu sfondi%s).", changed,
                             first ? L"" : L" ricoperti: parti ricreate da Esplora file o animazioni fermate");
                }
            }
        }
        else if (m_unifiedHeader.IsApplied())
        {
            m_unifiedHeader.Restore();
            XPX_MLOG(L"VerticalTabs: intestazione con i suoi colori originali.");
        }
    }

    void VerticalTabs::KeepNativeOrder()
    {
        // Barra delle cartelle spenta: si vede il riquadro di spostamento di sistema. Con dei gruppi, le cartelle aggiunte
        // vanno in Windows nell'ordine dei gruppi (all'avvio, una volta per processo: NavOrder) e il riquadro delle schede
        // aperte si tiene in pari, perché da sé non rilegge mai l'ordine. Senza gruppi non si fa nulla (nemmeno si leggono
        // le voci).
        const auto groups = NavGroups::Instance().Latest();
        if (!groups || groups->groups.empty())
        {
            return;
        }
        NavItems::Instance().Start();
        const auto latest = NavItems::Instance().Latest();
        if (!latest || latest->version == m_orderVersion)
        {
            return;
        }
        m_orderVersion = latest->version;
        std::shared_ptr<const NavSnapshot> view = latest;
        if (TakeStartupOrder())
        {
            if (auto moved = SyncPinnedOrder(*latest, *groups, TopLevel(*groups, PinnedKeysOf(*latest))))
            {
                view = moved;
            }
        }
        SortNativePanes(m_main, *view);
    }

    void VerticalTabs::OnTimer()
    {
        if (!m_attached)
        {
            Apply(m_settings);
            return;
        }
        // Finestra nascosta (Esplora file tiene da parte le finestre chiuse per riusarle): niente da fare
        // finché non ricompare.
        if (!m_main || !IsWindowVisible(m_main))
        {
            return;
        }
        m_layout.Check();
        if (m_navBar)
        {
            Guard(L"barra delle cartelle", [&] { m_navBar->OnTick(); });
        }
        else
        {
            Guard(L"ordine del riquadro di spostamento", [&] { KeepNativeOrder(); });
        }
        if (!m_enabled)
        {
            return;
        }
        if (m_pendingColors)
        {
            Guard(L"colori da rimettere", [&] { TryApplyPendingColors(); }); // anche allo scadere dell'attesa
        }
        // Righe dell'intestazione (caricata dopo l'attivazione, ricreata, altra scala) e margine della barra degli indirizzi
        // (animazione fermata, elemento ricreato): controlli leggeri, a ogni giro.
        Guard(L"righe dell'intestazione", [&] {
            UpdateHeaderRows();
            UpdateToolbarInset();
        });
        // Elenco, selezione, titoli e icone arrivano con gli eventi di Esplora file (raccolta delle schede,
        // selezione, PropertyChanged dei modelli): il controllo completo qui è solo una rete di sicurezza.
        ++m_ticks;
        const bool full = m_ticks % kFullSyncEveryTicks == 0;
        // Aspetto dei titoli: a ogni controllo finché non è letto (schede native non ancora realizzate),
        // poi ogni tanto, così segue anche i cambi di tema.
        if (!m_titleStyleRead || (full && ++m_titleStyleTicks % kTitleStyleEveryTicks == 0))
        {
            Guard(L"lettura dell'aspetto dei titoli", [&] { ReadNativeTitleStyle(); });
        }
        if (!full)
        {
            return;
        }
        SyncRows();
        // La striscia nativa potrebbe essere stata ricreata (nuovo modello): si nasconde di nuovo.
        bool rehide = m_hidden.empty();
        for (auto const& part : m_hidden)
        {
            const auto element = part.element.get();
            rehide = rehide || !element || element.Opacity() != 0.0;
        }
        if (rehide)
        {
            SetNativeStripHidden(false);
            SetNativeStripHidden(true);
        }
        // Anche le fasce dell'intestazione possono essere ricreate (o non ancora caricate all'attivazione).
        Guard(L"intestazione", [&] { UpdateUnifiedHeader(); });
    }

    // La nostra isola XAML si crea solo alla prima attivazione: con la feature spenta la mod non
    // aggiunge nulla all'interfaccia di Esplora file.
    bool VerticalTabs::EnsurePanel()
    {
        if (m_source)
        {
            return true;
        }
        if (!m_main || !IsWindow(m_main) || !BuildPanel())
        {
            return false;
        }
        try
        {
            m_source = winrt::Microsoft::UI::Xaml::Hosting::DesktopWindowXamlSource();
            m_source.Initialize(interop::WindowIdFromHwnd(m_main));
            m_source.Content(m_root);
            ApplyBackdrop();
            const auto bridge = m_source.SiteBridge();
            m_panelHwnd = interop::HwndFromWindowId(bridge.WindowId());
            bridge.Hide();
            // Contrassegno per chi guarda la finestra da fuori (la tray cerca l'isola dell'intestazione).
            SetPropW(m_panelHwnd, interop::kPanelProp, reinterpret_cast<HANDLE>(1));
        }
        catch (...)
        {
            XPX_MLOG(L"VerticalTabs: impossibile creare l'isola XAML del pannello (0x%08X).", static_cast<unsigned>(winrt::to_hresult()));
            if (m_source)
            {
                try
                {
                    m_source.Close();
                }
                catch (...)
                {
                }
                m_source = nullptr;
            }
            m_root = nullptr;
            return false;
        }

        // Geometria: il layout decide quando il pannello è visibile.
        const std::weak_ptr<VerticalTabs> weak = weak_from_this();
        m_layout.onPanelVisibility = [weak](bool shown) {
            const auto self = weak.lock();
            // Isola già distrutta (finestra in chiusura): Show/Hide su un ponte chiuso è un accesso non valido.
            if (!self || !self->m_source || !self->m_panelHwnd || !IsWindow(self->m_panelHwnd))
            {
                return;
            }
            Guard(L"visibilità del pannello", [&] {
                const auto bridge = self->m_source.SiteBridge();
                shown ? bridge.Show() : bridge.Hide();
            });
        };
        m_layout.onPanelGeometry = [weak](int captionPx, int contentTopPx) {
            if (const auto self = weak.lock())
            {
                Guard(L"geometria del pannello", [&] { self->OnPanelGeometry(captionPx, contentTopPx); });
            }
        };
        // Fascia del titolo in cima alla colonna: la regione dell'isola si aggiorna dopo ogni layout (pulsanti
        // spostati, compressa/espansa), solo se cambia.
        m_root.LayoutUpdated([weak](auto&&, auto&&) {
            if (const auto self = weak.lock())
            {
                Guard(L"regione della fascia del titolo", [&] { self->UpdateCaptionRegion(); });
            }
        });
        m_layout.SetPanel(m_panelHwnd);
        ApplyAppearance();
        XPX_MLOG(L"VerticalTabs: pannello creato (finestra %llx, pannello %llx).", reinterpret_cast<uintptr_t>(m_main),
                 reinterpret_cast<uintptr_t>(m_panelHwnd));
        return true;
    }

    bool VerticalTabs::BuildPanel()
    {
        // Stile dei contenitori basato su quello Fluent di WinUI e pennelli del tema; se una chiave non
        // esistesse nelle risorse di Esplora file si ripiega per gradi.
        struct Variant
        {
            std::wstring_view basedOn;
            std::wstring_view background;
            std::wstring_view separator;
            std::wstring_view accent;
        };
        const Variant variants[] = {
            { LR"(BasedOn="{StaticResource DefaultListViewItemStyle}")", LR"(Background="{ThemeResource SolidBackgroundFillColorBaseBrush}")",
              LR"(Background="{ThemeResource DividerStrokeColorDefaultBrush}")", LR"(Background="{ThemeResource AccentFillColorDefaultBrush}")" },
            { L"", LR"(Background="{ThemeResource SolidBackgroundFillColorBaseBrush}")", LR"(Background="{ThemeResource DividerStrokeColorDefaultBrush}")",
              LR"(Background="{ThemeResource AccentFillColorDefaultBrush}")" },
            { L"", LR"(Background="{ThemeResource ApplicationPageBackgroundThemeBrush}")", LR"(Background="#30808080")",
              LR"(Background="#FF0078D4")" },
        };
        for (auto const& variant : variants)
        {
            try
            {
                std::wstring xaml = kPanelXaml;
                xaml = ReplaceToken(xaml, L"%BASEDON%", variant.basedOn);
                xaml = ReplaceToken(xaml, L"%BACKGROUND%", variant.background);
                xaml = ReplaceToken(xaml, L"%SEPARATOR%", variant.separator);
                xaml = ReplaceToken(xaml, L"%ACCENT%", variant.accent);
                m_root = LoadXaml(xaml).as<muxc::Grid>();
                break;
            }
            catch (winrt::hresult_error const& error)
            {
                XPX_MLOG(L"VerticalTabs: variante del pannello non caricata (0x%08X): %s", static_cast<unsigned>(error.code()),
                         error.message().c_str());
            }
            catch (...)
            {
                XPX_MLOG(L"VerticalTabs: variante del pannello non caricata (0x%08X).", static_cast<unsigned>(winrt::to_hresult()));
            }
        }
        if (!m_root)
        {
            return false;
        }
        m_header = FindPart<muxc::Grid>(m_root, L"Header");
        m_toggle = FindPart<muxc::Button>(m_root, L"Toggle");
        m_toggleIcon = FindPart<muxc::FontIcon>(m_root, L"ToggleIcon");
        m_body = FindPart<muxc::Grid>(m_root, L"Body");
        m_list = FindPart<muxc::ListView>(m_root, L"List");
        m_newTab = FindPart<muxc::Button>(m_root, L"NewTab");
        m_separator = FindPart<muxc::Border>(m_root, L"Separator");
        m_separatorActive = FindPart<muxc::Border>(m_root, L"SeparatorActive");
        m_splitter = FindPart<muxc::Border>(m_root, L"Splitter");
        if (!m_header || !m_toggle || !m_toggleIcon || !m_body || !m_list || !m_newTab || !m_separator || !m_separatorActive || !m_splitter)
        {
            XPX_MLOG(L"VerticalTabs: parti del pannello mancanti.");
            m_root = nullptr;
            return false;
        }
        m_visible = winrt::single_threaded_observable_vector<IInspectable>();
        m_list.ItemsSource(m_visible);
        HookPanelEvents();
        return true;
    }

    void VerticalTabs::HookPanelEvents()
    {
        const std::weak_ptr<VerticalTabs> weak = weak_from_this();
        m_list.SelectionChanged([weak](auto&&, auto&&) {
            if (const auto self = weak.lock())
            {
                Guard(L"SelectionChanged", [&] { self->OnListSelectionChanged(); });
            }
        });
        m_list.ItemClick([weak](auto&&, auto&&) {
            // Clic col mouse: dopo il cambio di scheda si torna alla vista dei file.
            if (const auto self = weak.lock())
            {
                Guard(L"ItemClick", [&] { self->FocusContent(); });
            }
        });
        m_list.KeyDown([weak](auto&&, muxi::KeyRoutedEventArgs const& args) {
            const auto self = weak.lock();
            if (!self)
            {
                return;
            }
            Guard(L"KeyDown", [&] {
                if (args.Key() == VirtualKey::Escape && self->m_dragging)
                {
                    self->EndDrag(false); // annulla il trascinamento
                    args.Handled(true);
                }
                else if (args.Key() == VirtualKey::Enter)
                {
                    self->FocusContent();
                    args.Handled(true);
                }
            });
        });

        // Riordino con trascinamento. I gestori ricevono anche gli eventi già gestiti dalle righe: la
        // ListView continua a occuparsi di clic e selezione.
        const auto addPointerHandler = [this, weak](mux::RoutedEvent const& routedEvent, void (VerticalTabs::*handler)(muxi::PointerRoutedEventArgs const&)) {
            m_list.AddHandler(routedEvent,
                              winrt::box_value(muxi::PointerEventHandler([weak, handler](IInspectable const&, muxi::PointerRoutedEventArgs const& args) {
                                  if (const auto self = weak.lock())
                                  {
                                      Guard(L"trascinamento", [&] { ((*self).*handler)(args); });
                                  }
                              })),
                              true);
        };
        addPointerHandler(mux::UIElement::PointerPressedEvent(), &VerticalTabs::OnListPointerPressed);
        addPointerHandler(mux::UIElement::PointerMovedEvent(), &VerticalTabs::OnListPointerMoved);
        addPointerHandler(mux::UIElement::PointerReleasedEvent(), &VerticalTabs::OnListPointerReleased);
        m_list.AddHandler(mux::UIElement::PointerCaptureLostEvent(),
                          winrt::box_value(muxi::PointerEventHandler([weak](IInspectable const&, muxi::PointerRoutedEventArgs const&) {
                              // Cattura persa senza rilascio (es. cambio di finestra): il trascinamento si annulla.
                              if (const auto self = weak.lock(); self && self->m_dragging)
                              {
                                  Guard(L"cattura persa", [&] { self->EndDrag(false); });
                              }
                          })),
                          true);

        m_toggle.Click([weak](auto&&, auto&&) {
            if (const auto self = weak.lock())
            {
                Guard(L"comprimi/espandi", [&] { self->SetCollapsed(!self->m_settings.collapsed); });
            }
        });
        m_newTab.Click([weak](auto&&, auto&&) {
            if (const auto self = weak.lock())
            {
                Guard(L"nuova scheda", [&] { self->NewTab(); });
            }
        });
        m_body.SizeChanged([weak](auto&&, auto&&) {
            if (const auto self = weak.lock())
            {
                Guard(L"SizeChanged", [&] { self->UpdateListHeight(); });
            }
        });
        // Il modello della ListView (con il suo ScrollViewer) esiste solo una volta caricata.
        m_list.Loaded([weak](auto&&, auto&&) {
            if (const auto self = weak.lock())
            {
                Guard(L"Loaded", [&] { self->BlockHorizontalWheel(); });
            }
        });

        // Divisore: cursore ↔ e trascinamento per cambiare la larghezza. ProtectedCursor è "protected"
        // in WinUI, ma è raggiungibile tramite l'interfaccia IUIElementProtected di ogni UIElement.
        try
        {
            if (const auto protectedSplitter = m_splitter.try_as<mux::IUIElementProtected>())
            {
                protectedSplitter.ProtectedCursor(
                    winrt::Microsoft::UI::Input::InputSystemCursor::Create(winrt::Microsoft::UI::Input::InputSystemCursorShape::SizeWestEast));
            }
        }
        catch (...)
        {
            XPX_MLOG(L"VerticalTabs: cursore del divisore non impostabile (0x%08X).", static_cast<unsigned>(winrt::to_hresult()));
        }
        m_splitter.PointerEntered([weak](auto&&, auto&&) {
            if (const auto self = weak.lock())
            {
                self->m_splitterHovered = true;
                Guard(L"divisore", [&] { self->SetSplitterHighlight(true); });
            }
        });
        m_splitter.PointerExited([weak](auto&&, auto&&) {
            if (const auto self = weak.lock())
            {
                self->m_splitterHovered = false;
                if (!self->m_resizing)
                {
                    Guard(L"divisore", [&] { self->SetSplitterHighlight(false); });
                }
            }
        });
        m_splitter.PointerPressed([weak](auto&&, muxi::PointerRoutedEventArgs const& args) {
            const auto self = weak.lock();
            if (!self)
            {
                return;
            }
            Guard(L"divisore premuto", [&] {
                if (!args.GetCurrentPoint(self->m_splitter).Properties().IsLeftButtonPressed())
                {
                    return;
                }
                // Larghezza e punto afferrato al momento della presa: il bordo segue il mouse senza scatti.
                POINT cursor{};
                GetCursorPos(&cursor);
                ScreenToClient(self->m_main, &cursor);
                const RECT panel = interop::RectInClientOf(self->m_panelHwnd, self->m_main);
                self->m_grabOffsetPx = (panel.right - panel.left) - cursor.x;
                self->m_resizing = self->m_splitter.CapturePointer(args.Pointer());
                self->SetSplitterHighlight(true);
                args.Handled(true);
            });
        });
        m_splitter.PointerMoved([weak](auto&&, muxi::PointerRoutedEventArgs const& args) {
            if (const auto self = weak.lock(); self && self->m_resizing)
            {
                Guard(L"divisore mosso", [&] {
                    self->ResizeFromCursor();
                    args.Handled(true);
                });
            }
        });
        m_splitter.PointerReleased([weak](auto&&, muxi::PointerRoutedEventArgs const& args) {
            if (const auto self = weak.lock(); self && self->m_resizing)
            {
                Guard(L"divisore rilasciato", [&] {
                    self->m_resizing = false;
                    self->m_splitter.ReleasePointerCapture(args.Pointer());
                    self->PersistPanelState();
                    self->SetSplitterHighlight(self->m_splitterHovered);
                    args.Handled(true);
                });
            }
        });
        m_splitter.PointerCaptureLost([weak](auto&&, auto&&) {
            if (const auto self = weak.lock(); self && self->m_resizing)
            {
                Guard(L"divisore", [&] {
                    self->m_resizing = false;
                    self->PersistPanelState();
                    self->SetSplitterHighlight(self->m_splitterHovered);
                });
            }
        });
        m_splitter.DoubleTapped([weak](auto&&, muxi::DoubleTappedRoutedEventArgs const& args) {
            if (const auto self = weak.lock())
            {
                Guard(L"divisore, doppio clic", [&] {
                    // Il secondo clic ha già avviato un ridimensionamento: lo si chiude, altrimenti il
                    // movimento del pannello che si restringe sotto il cursore cambierebbe di nuovo la larghezza.
                    self->m_resizing = false;
                    self->m_splitter.ReleasePointerCaptures();
                    self->ApplyPanelWidth(ModSettings{}.width);
                    self->PersistPanelState();
                    self->SetSplitterHighlight(self->m_splitterHovered);
                    args.Handled(true);
                });
            }
        });
    }

    void VerticalTabs::ApplyBackdrop()
    {
        // Lo stesso materiale della barra del titolo di Esplora file (Mica Alt, finestra a schede): se non
        // è disponibile resta il pennello del XAML, cioè il suo colore pieno.
        try
        {
            muxm::MicaBackdrop mica;
            mica.Kind(winrt::Microsoft::UI::Composition::SystemBackdrops::MicaKind::BaseAlt);
            m_source.SystemBackdrop(mica);
            m_root.Background(muxm::SolidColorBrush(winrt::Microsoft::UI::Colors::Transparent()));
        }
        catch (...)
        {
            XPX_MLOG(L"VerticalTabs: Mica non disponibile per il pannello (0x%08X): colore pieno.", static_cast<unsigned>(winrt::to_hresult()));
        }
    }

    void VerticalTabs::ApplyAppearance()
    {
        if (!m_root)
        {
            return;
        }
        if (const auto tabView = m_tabView.get())
        {
            m_root.RequestedTheme(tabView.ActualTheme());
        }
        const bool compact = m_settings.collapsed;

        // Intestazione: il pulsante a sinistra da espanso, centrato da compresso. La sua icona è un pannello che si
        // chiude (o si apre) verso il bordo sinistro della finestra.
        m_header.HorizontalAlignment(compact ? mux::HorizontalAlignment::Center : mux::HorizontalAlignment::Stretch);
        ApplyHeaderLayout(compact);
        m_toggleIcon.Glyph(compact ? L"" : L"");
        const winrt::hstring toggleName = compact ? Tr(L"Expand tabs", L"Espandi schede") : Tr(L"Collapse tabs", L"Comprimi schede");
        mux::Automation::AutomationProperties::SetName(m_toggle, toggleName);
        muxc::ToolTipService::SetToolTip(m_toggle, winrt::box_value(toggleName));

        // Divisore solo da espanso; l'elenco lascia libera la sua fascia (così la barra di scorrimento non
        // si sovrappone alla zona di trascinamento).
        m_splitter.Visibility(compact ? mux::Visibility::Collapsed : mux::Visibility::Visible);
        if (compact)
        {
            SetSplitterHighlight(false);
        }
        const double gap = compact ? 0.0 : kSplitterGap;
        m_list.Margin(mux::Thickness{ 0, 0, gap, 0 });
        m_newTab.Margin(mux::Thickness{ kRowInset, kNewTabTopMargin, kRowInset + gap, kNewTabBottomMargin });

        m_compactVisual = compact;
        UpdateAllRows();
    }

    void VerticalTabs::ApplyHeaderLayout(bool compact)
    {
        if (!m_header || !m_root)
        {
            return;
        }
        double scale = m_layout.Dpi() / 96.0;
        if (const auto root = m_root.XamlRoot())
        {
            scale = root.RasterizationScale();
        }
        const double caption = m_captionPx / scale;
        const double contentTop = m_contentTopPx / scale;
        const auto rows = m_header.RowDefinitions();
        if (caption > 0 && rows.Size() > 0)
        {
            // In cima alla finestra: l'intestazione è la fascia del titolo, con il pulsante centrato accanto a quelli
            // della finestra.
            const double button = std::clamp(caption - 2 * kCaptionButtonInset, 24.0, kHeaderButtonHeight);
            rows.GetAt(0).Height(mux::GridLength{ caption, mux::GridUnitType::Pixel });
            m_header.Margin(compact ? mux::Thickness{ 0, 0, 0, 2 } : mux::Thickness{ 6, 0, 6, 0 });
            m_toggle.Height(button);
        }
        else
        {
            if (rows.Size() > 0)
            {
                rows.GetAt(0).Height(mux::GridLengthHelper::Auto());
            }
            m_header.Margin(compact ? mux::Thickness{ 0, 6, 0, 2 } : mux::Thickness{ 6, 6, 6, 2 });
            m_toggle.Height(kHeaderButtonHeight);
        }
        // Linea del bordo destro e divisore solo accanto al contenuto delle schede: sopra, colonna e intestazione
        // sono la stessa superficie (la Mica della barra del titolo).
        const mux::Thickness lines{ 0, contentTop, 0, 0 };
        m_separator.Margin(lines);
        m_separatorActive.Margin(lines);
        m_splitter.Margin(lines);
    }

    void VerticalTabs::OnPanelGeometry(int captionPx, int contentTopPx)
    {
        m_captionPx = std::max(0, captionPx);
        m_contentTopPx = std::max(0, contentTopPx);
        ApplyHeaderLayout(m_settings.collapsed);
        UpdateCaptionRegion(); // subito (es. colonna tornata sotto l'intestazione); dopo il layout la ricalcola LayoutUpdated
        UpdateToolbarInset();  // riga del titolo nascosta o di nuovo visibile
    }

    mux::FrameworkElement VerticalTabs::HeaderToolbar()
    {
        if (auto toolbar = m_toolbar.get(); toolbar && muxm::VisualTreeHelper::GetParent(toolbar))
        {
            return toolbar;
        }
        m_toolbar = nullptr;
        const auto tabView = NativeTabView();
        if (!tabView)
        {
            return nullptr;
        }
        mux::DependencyObject root = tabView;
        while (const auto parent = muxm::VisualTreeHelper::GetParent(root))
        {
            root = parent;
        }
        const auto toolbar = interop::FindByName(root, L"NavigationBarControl");
        if (toolbar)
        {
            m_toolbar = winrt::make_weak(toolbar);
        }
        return toolbar;
    }

    void VerticalTabs::UpdateHeaderRows()
    {
        // La riga del titolo finisce dove comincia la barra degli indirizzi (NavigationBarControl, 38,4 DIP dalla cima
        // dell'isola); la barra degli indirizzi è la riga sotto (48 DIP). Il margine che le mettiamo non cambia nessuno dei due.
        const auto toolbar = HeaderToolbar();
        if (!toolbar || toolbar.ActualHeight() <= 0)
        {
            return;
        }
        mux::DependencyObject top = toolbar;
        while (const auto parent = muxm::VisualTreeHelper::GetParent(top))
        {
            top = parent;
        }
        const auto root = top.try_as<mux::UIElement>();
        if (!root)
        {
            return;
        }
        const auto origin = toolbar.TransformToVisual(root).TransformPoint(Point{ 0, 0 });
        m_layout.SetHeaderRows(origin.Y, toolbar.ActualHeight());
    }

    void VerticalTabs::UpdateToolbarInset()
    {
        // Con la riga del titolo nascosta la barra degli indirizzi è la prima riga: a destra lascia lo spazio ai pulsanti
        // della finestra (e al tratto libero da cui trascinarla), che altrimenti coprirebbero la casella di ricerca.
        const bool wanted = m_enabled && m_layout.TitleHidden();
        const auto stop = [this] {
            if (m_toolbarInset)
            {
                try
                {
                    m_toolbarInset.Stop(); // torna il margine di Esplora file
                }
                catch (...)
                {
                }
                m_toolbarInset = nullptr;
                m_toolbarInsetTarget = nullptr;
                XPX_MLOG(L"VerticalTabs: barra degli indirizzi di nuovo a tutta larghezza.");
            }
        };
        if (!wanted)
        {
            stop();
            return;
        }
        const auto toolbar = HeaderToolbar();
        if (!toolbar)
        {
            return;
        }
        if (m_toolbarInset && m_toolbarInsetTarget.get() == toolbar && m_toolbarInset.GetCurrentState() != muxma::ClockState::Stopped)
        {
            return; // già applicato
        }
        stop();
        muxma::DiscreteObjectKeyFrame frame;
        frame.KeyTime(muxma::KeyTimeHelper::FromTimeSpan(winrt::Windows::Foundation::TimeSpan{ 0 }));
        frame.Value(winrt::box_value(mux::Thickness{ 0, 0, WindowLayout::kCaptionReserveDip, 0 }));
        muxma::ObjectAnimationUsingKeyFrames animation;
        animation.KeyFrames().Append(frame);
        muxma::Storyboard::SetTarget(animation, toolbar);
        muxma::Storyboard::SetTargetProperty(animation, L"Margin");
        muxma::Storyboard storyboard;
        storyboard.FillBehavior(muxma::FillBehavior::HoldEnd);
        storyboard.Children().Append(animation);
        storyboard.Begin();
        m_toolbarInset = storyboard;
        m_toolbarInsetTarget = winrt::make_weak(toolbar);
        XPX_MLOG(L"VerticalTabs: barra degli indirizzi in cima, accanto ai pulsanti della finestra (%.0f DIP liberi a destra).",
                 WindowLayout::kCaptionReserveDip);
    }

    void VerticalTabs::UpdateCaptionRegion()
    {
        if (!m_panelHwnd || !IsWindow(m_panelHwnd) || !m_root)
        {
            return;
        }
        // Il mouse delle isole XAML non passa dall'hit test di Windows (WM_NCHITTEST): per lasciare la fascia del
        // titolo alla finestra principale l'isola non deve proprio esserci. La sua regione esclude la fascia, salvo
        // il pulsante comprimi/espandi; sotto c'è la finestra principale, che lì risponde HTCAPTION (WindowLayout). Si
        // vede lo stesso materiale (la Mica della finestra).
        std::vector<RECT> keep;
        if (m_captionPx > 0)
        {
            const auto root = m_root.XamlRoot();
            const double scale = root ? root.RasterizationScale() : m_layout.Dpi() / 96.0;
            const mux::FrameworkElement interactive[] = { m_toggle };
            for (auto const& element : interactive)
            {
                if (!element || element.Visibility() != mux::Visibility::Visible || element.ActualWidth() <= 0 || element.ActualHeight() <= 0)
                {
                    continue;
                }
                const auto bounds = element.TransformToVisual(m_root).TransformBounds(
                    winrt::Windows::Foundation::Rect{ 0, 0, static_cast<float>(element.ActualWidth()), static_cast<float>(element.ActualHeight()) });
                RECT rect{ static_cast<LONG>(std::floor(bounds.X * scale)), static_cast<LONG>(std::floor(bounds.Y * scale)),
                           static_cast<LONG>(std::ceil((bounds.X + bounds.Width) * scale)), static_cast<LONG>(std::ceil((bounds.Y + bounds.Height) * scale)) };
                // Fino al fondo della fascia: la regione taglia anche ciò che l'isola disegna, e sotto può esserci qualcosa
                // della finestra (nel Blocco note la sua icona, sotto "comprimi"); come in VTabsNotepad.
                rect.bottom = static_cast<LONG>(m_captionPx);
                if (rect.right > rect.left && rect.bottom > rect.top)
                {
                    keep.push_back(rect);
                }
            }
        }
        const bool same = m_regionCaptionPx == m_captionPx && keep.size() == m_regionKeep.size() &&
                          std::equal(keep.begin(), keep.end(), m_regionKeep.begin(), [](RECT const& a, RECT const& b) { return EqualRect(&a, &b) != FALSE; });
        if (same)
        {
            return;
        }
        m_regionCaptionPx = m_captionPx;
        m_regionKeep = keep;
        if (m_captionPx <= 0)
        {
            SetWindowRgn(m_panelHwnd, nullptr, TRUE);
            return;
        }
        constexpr int kFar = 32767; // a destra e in basso abbastanza per ogni larghezza e altezza futura
        HRGN region = CreateRectRgn(0, m_captionPx, kFar, kFar);
        for (RECT const& rect : keep)
        {
            if (HRGN button = CreateRectRgn(rect.left, rect.top, rect.right, rect.bottom))
            {
                CombineRgn(region, region, button, RGN_OR);
                DeleteObject(button);
            }
        }
        if (!region || !SetWindowRgn(m_panelHwnd, region, TRUE))
        {
            if (region)
            {
                DeleteObject(region);
            }
            m_regionKeep.clear();
            m_regionCaptionPx = -1; // si riprova al prossimo layout
        }
    }

    void VerticalTabs::Enable()
    {
        if (!EnsurePanel())
        {
            return;
        }
        const auto tabView = NativeTabView();
        const auto list = m_nativeList.get();
        if (!tabView || !list)
        {
            return;
        }
        m_items = list.Items();
        const std::weak_ptr<VerticalTabs> weak = weak_from_this();
        m_itemsToken = m_items.VectorChanged([weak](auto&&, wfc::IVectorChangedEventArgs const&) {
            if (const auto self = weak.lock())
            {
                Guard(L"VectorChanged", [&] { self->ScheduleSync(); });
            }
        });
        m_selectionRevoker = tabView.SelectionChanged(winrt::auto_revoke, [weak](auto&&, auto&&) {
            if (const auto self = weak.lock())
            {
                Guard(L"SelectionChanged nativo", [&] { self->SyncSelectionFromNative(); });
            }
        });
        m_enabled = true;
        SetNativeStripHidden(true);
        SyncRows();
        Guard(L"lettura dell'aspetto dei titoli", [&] { ReadNativeTitleStyle(); });
        ApplyAppearance();
        Guard(L"righe dell'intestazione", [&] { UpdateHeaderRows(); });
        m_layout.SetActive(true, m_settings.PanelWidth());
        Guard(L"barra degli indirizzi", [&] { UpdateToolbarInset(); });
        SyncSelectionFromNative();
        XPX_MLOG(L"VerticalTabs: schede verticali attive (%zu schede).", m_rows.size());
        Guard(L"intestazione", [&] { UpdateUnifiedHeader(); });
    }

    void VerticalTabs::Disable()
    {
        if (m_autoScrollTimer)
        {
            m_autoScrollTimer.Stop();
        }
        m_dragging = false;
        m_dragPending = false;
        m_flyoutClosedRevoker.revoke();
        m_layout.SetActive(false, m_settings.PanelWidth());
        Guard(L"barra degli indirizzi", [&] { UpdateToolbarInset(); }); // la riga del titolo torna: margine di Esplora file
        SetNativeStripHidden(false);
        RemoveColorMenu();
        Unsubscribe();
        ClearRows();
        m_enabled = false;
        Guard(L"intestazione", [&] { UpdateUnifiedHeader(); }); // a colonna spenta torna com'era
        // A colonna spenta i colori non si pubblicano (non seguirebbero più l'ordine delle schede): la tray
        // li ricorda per ogni scheda, e qui restano per quando la colonna si riaccende.
        DeleteFileW(ColorFilePath(L"live").c_str());
        m_lastExport.clear();
        XPX_MLOG(L"VerticalTabs: schede verticali disattivate.");
    }

    void VerticalTabs::Unsubscribe()
    {
        m_selectionRevoker.revoke();
        if (m_items && m_itemsToken)
        {
            try
            {
                m_items.VectorChanged(m_itemsToken);
            }
            catch (...)
            {
            }
        }
        m_items = nullptr;
        m_itemsToken = {};
    }

    void VerticalTabs::SetNativeStripHidden(bool hidden)
    {
        if (!hidden)
        {
            for (auto const& part : m_hidden)
            {
                if (const auto element = part.element.get())
                {
                    Guard(L"ripristino della striscia", [&] {
                        element.Opacity(part.opacity);
                        element.IsHitTestVisible(part.hitTest);
                    });
                }
            }
            m_hidden.clear();
            if (const auto panel = m_nativePanel.get())
            {
                Guard(L"ripristino della cache", [&] { panel.CacheLength(m_nativeCacheLength); });
            }
            m_nativePanel = nullptr;
            return;
        }
        const auto tabView = NativeTabView();
        if (!tabView || !m_hidden.empty())
        {
            return;
        }
        // Solo le schede e il "+": la parte destra (zona di trascinamento) resta com'è.
        for (const wchar_t* name : { L"TabListView", L"AddButton" })
        {
            if (const auto element = interop::FindByName(tabView, name))
            {
                m_hidden.push_back({ winrt::make_weak<mux::UIElement>(element), element.Opacity(), element.IsHitTestVisible() });
                element.Opacity(0.0);
                element.IsHitTestVisible(false);
            }
        }
        // Tutte le schede realizzate (anche quelle fuori dalla striscia): titolo e icona si leggono da lì.
        if (const auto list = m_nativeList.get())
        {
            if (const auto panel = list.ItemsPanelRoot().try_as<muxc::ItemsStackPanel>())
            {
                m_nativeCacheLength = panel.CacheLength();
                m_nativePanel = winrt::make_weak(panel);
                panel.CacheLength(kNativeCacheLength);
            }
        }
    }

    void VerticalTabs::UpdateListHeight()
    {
        if (!m_body || !m_list)
        {
            return;
        }
        // Finché le righe ci stanno l'elenco è alto quanto loro e il "+" le segue; oltre, l'elenco scorre
        // e il "+" resta visibile in fondo al pannello.
        m_list.MaxHeight(std::max(0.0, m_body.ActualHeight() - kNewTabTopMargin - kNewTabHeight - kNewTabBottomMargin));
    }

    void VerticalTabs::BlockHorizontalWheel()
    {
        if (m_horizontalWheelBlocked || !m_list)
        {
            return;
        }
        // Lo ScrollViewer di WinUI, se non può scorrere in orizzontale, converte la rotella orizzontale
        // (anche lo scorrimento a due dita del touchpad) in scorrimento verticale: l'evento si ferma sul
        // contenitore delle righe, prima di raggiungerlo. La rotella verticale passa come sempre.
        const auto presenter = FindDescendant<muxc::ItemsPresenter>(m_list);
        if (!presenter)
        {
            return;
        }
        presenter.AddHandler(mux::UIElement::PointerWheelChangedEvent(),
                             winrt::box_value(muxi::PointerEventHandler([](IInspectable const& sender, muxi::PointerRoutedEventArgs const& args) {
                                 const auto element = sender.try_as<mux::UIElement>();
                                 if (element && args.GetCurrentPoint(element).Properties().IsHorizontalMouseWheel())
                                 {
                                     args.Handled(true);
                                 }
                             })),
                             false);
        m_horizontalWheelBlocked = true;
    }

    bool VerticalTabs::ReadNativeTitleStyle()
    {
        const auto tabView = NativeTabView();
        const auto list = m_nativeList.get();
        if (!tabView || !list || !m_items)
        {
            return false;
        }
        // Schede già realizzate dalla striscia; valgono solo schede "a riposo" (stato Normal o Selected):
        // il passaggio del mouse o la pressione ne cambiano il colore.
        TitleStyle style = m_titleStyle;
        bool haveNormal = false;
        bool haveSelected = false;
        const int selectedIndex = tabView.SelectedIndex();
        const uint32_t count = m_items.Size();
        for (uint32_t i = 0; i < count && !(haveNormal && haveSelected); ++i)
        {
            const bool isSelected = static_cast<int>(i) == selectedIndex;
            if (isSelected ? haveSelected : haveNormal)
            {
                continue;
            }
            const auto container = list.ContainerFromIndex(static_cast<int32_t>(i)).try_as<muxc::TabViewItem>();
            const auto text = container ? TitleBlockOf(container) : nullptr;
            if (!text || text.Text().empty())
            {
                continue;
            }
            const auto states = CommonStates(container);
            const auto current = states ? states.CurrentState() : nullptr;
            const winrt::hstring state = current ? current.Name() : winrt::hstring{};
            if (!state.empty() && state != (isSelected ? L"Selected" : L"Normal"))
            {
                continue;
            }
            const auto brush = EffectiveForeground(text, container);
            if (isSelected)
            {
                style.selected = brush;
                style.selectedWeight = text.FontWeight();
                haveSelected = true;
                continue;
            }
            // Il colore al passaggio del mouse si rilegge solo se è cambiato quello normale (tema).
            if (!m_titleStyleRead || !SameBrush(brush, m_titleStyle.normal))
            {
                style.hover = TitleBrushOnHover(container, text, state);
            }
            style.normal = brush;
            style.normalWeight = text.FontWeight();
            style.size = text.FontSize();
            style.family = text.FontFamily();
            haveNormal = true;
        }
        if (!haveNormal && !haveSelected)
        {
            return false;
        }
        m_titleStyleRead = m_titleStyleRead || (haveNormal && haveSelected) || (haveSelected && count == 1);

        const auto& old = m_titleStyle;
        const bool changed = style.size != old.size || style.normalWeight.Weight != old.normalWeight.Weight ||
                             style.selectedWeight.Weight != old.selectedWeight.Weight || !SameBrush(style.normal, old.normal) ||
                             !SameBrush(style.hover, old.hover) || !SameBrush(style.selected, old.selected) ||
                             (style.family ? style.family.Source() : winrt::hstring{}) != (old.family ? old.family.Source() : winrt::hstring{});
        if (!changed)
        {
            return false;
        }
        m_titleStyle = style;
        XPX_MLOG(L"VerticalTabs: titoli come le schede native: %.1f DIP %s; normale peso %u %s, al passaggio del mouse %s, attiva peso %u %s.",
                 style.size, style.family ? style.family.Source().c_str() : L"(predefinito)", static_cast<unsigned>(style.normalWeight.Weight),
                 BrushText(style.normal).c_str(), style.hover ? BrushText(style.hover).c_str() : L"invariato",
                 static_cast<unsigned>(style.selectedWeight.Weight), BrushText(style.selected).c_str());
        UpdateAllRows();
        return true;
    }

    // ---------------------------------------------------------------------------------------------
    // Pannello compresso/espanso
    // ---------------------------------------------------------------------------------------------

    void VerticalTabs::SetCollapsed(bool collapsed)
    {
        if (m_settings.collapsed == collapsed)
        {
            return;
        }
        m_settings.collapsed = collapsed;
        ApplyAppearance();
        if (m_enabled)
        {
            m_layout.SetActive(true, m_settings.PanelWidth());
        }
        SyncSelectionFromNative(); // la scheda attiva resta in vista
        PersistPanelState();       // le altre finestre si adeguano rileggendo settings.ini
        XPX_MLOG(L"VerticalTabs: pannello %s.", collapsed ? L"compresso" : L"espanso");
    }

    // ---------------------------------------------------------------------------------------------
    // Dati
    // ---------------------------------------------------------------------------------------------

    void VerticalTabs::ScheduleSync()
    {
        if (m_syncQueued || !m_root)
        {
            return;
        }
        m_syncQueued = true;
        const std::weak_ptr<VerticalTabs> weak = weak_from_this();
        // Al prossimo giro: le notifiche arrivano a raffiche (un riordino è "rimossa" + "inserita") e i
        // contenitori delle schede nuove nascono con il layout.
        const bool queued = m_root.DispatcherQueue().TryEnqueue(DispatcherQueuePriority::Low, [weak] {
            if (const auto self = weak.lock())
            {
                self->m_syncQueued = false;
                Guard(L"SyncRows", [&] { self->SyncRows(); });
            }
        });
        if (!queued)
        {
            m_syncQueued = false;
        }
    }

    void VerticalTabs::SyncRows()
    {
        if (!m_enabled || !m_items || !m_list)
        {
            return;
        }
        std::vector<IInspectable> wanted;
        const uint32_t count = m_items.Size();
        wanted.reserve(count);
        for (uint32_t i = 0; i < count; ++i)
        {
            wanted.push_back(m_items.GetAt(i));
        }

        bool same = wanted.size() == m_rows.size();
        for (size_t i = 0; same && i < wanted.size(); ++i)
        {
            same = m_rows[i]->item == wanted[i];
        }
        if (!same)
        {
            if (m_dragging)
            {
                EndDrag(false); // le schede sono cambiate sotto il trascinamento: si annulla
            }
            // 1) Via le righe delle schede che non ci sono più.
            for (size_t i = m_rows.size(); i-- > 0;)
            {
                if (std::find(wanted.begin(), wanted.end(), m_rows[i]->item) == wanted.end())
                {
                    ReleaseRow(*m_rows[i]);
                    m_rows.erase(m_rows.begin() + static_cast<ptrdiff_t>(i));
                }
            }
            // 2) Ordine e schede nuove (le righe esistenti si riusano, elemento compreso).
            for (size_t i = 0; i < wanted.size(); ++i)
            {
                if (i < m_rows.size() && m_rows[i]->item == wanted[i])
                {
                    continue;
                }
                const auto found = std::find_if(m_rows.begin() + static_cast<ptrdiff_t>(std::min(i, m_rows.size())), m_rows.end(),
                                                [&](auto const& candidate) { return candidate->item == wanted[i]; });
                std::unique_ptr<Row> row;
                if (found != m_rows.end())
                {
                    row = std::move(*found);
                    m_rows.erase(found);
                }
                else
                {
                    row = CreateRow(wanted[i]);
                }
                m_rows.insert(m_rows.begin() + static_cast<ptrdiff_t>(i), std::move(row));
            }
        }
        else if (m_dragging)
        {
            return; // l'elenco visibile lo sta muovendo il trascinamento
        }
        if (!same)
        {
            PruneColors(); // schede chiuse: i loro colori li ricorda la tray (sessione, Ctrl+Maiusc+T)
        }
        // Prima i colori da rimettere (finestra appena riaperta), poi la pubblicazione: la tray li vede subito.
        TryApplyPendingColors();
        ExportColors();
        RefreshRows();
        RefreshVisibleRows();
        SyncSelectionFromNative();
    }

    std::unique_ptr<VerticalTabs::Row> VerticalTabs::CreateRow(IInspectable const& item)
    {
        auto row = std::make_unique<Row>();
        row->item = item;
        BuildRowElement(*row);

        // Il modello della scheda avvisa quando cambiano titolo o icona (navigazione): riga aggiornata subito.
        const std::weak_ptr<VerticalTabs> weak = weak_from_this();
        if (const auto notifier = item.try_as<mux::Data::INotifyPropertyChanged>())
        {
            row->notifier = notifier;
            row->propertyToken = notifier.PropertyChanged([weak](auto&&, mux::Data::PropertyChangedEventArgs const&) {
                if (const auto self = weak.lock())
                {
                    Guard(L"PropertyChanged", [&] { self->ScheduleSync(); });
                }
            });
        }
        if (!m_notifierLogged)
        {
            m_notifierLogged = true;
            XPX_MLOG(L"VerticalTabs: modello delle schede %s, INotifyPropertyChanged %s.", winrt::get_class_name(item).c_str(),
                     row->notifier ? L"sì" : L"no");
        }
        return row;
    }

    void VerticalTabs::BuildRowElement(Row& row)
    {
        mux::FrameworkElement element{ nullptr };
        for (std::wstring_view pill : { LR"(Background="{ThemeResource ControlStrongFillColorDefaultBrush}")", LR"(Background="#FF8A8A8A")" })
        {
            try
            {
                element = LoadXaml(ReplaceToken(kRowXaml, L"%PILL%", pill));
                break;
            }
            catch (...)
            {
            }
        }
        if (!element)
        {
            throw winrt::hresult_error(E_FAIL, L"XAML della riga non caricabile");
        }
        row.element = element.as<muxc::ListViewItem>();
        row.tint = FindPart<muxc::Border>(row.element, L"RowTint");
        row.pill = FindPart<muxc::Border>(row.element, L"Pill");
        row.pillColor = FindPart<muxc::Border>(row.element, L"PillColor");
        row.grid = FindPart<muxc::Grid>(row.element, L"RowGrid");
        row.icon = FindPart<muxc::Image>(row.element, L"Icon");
        row.title = FindPart<muxc::TextBlock>(row.element, L"Title");
        row.close = FindPart<muxc::Button>(row.element, L"Close");
        row.hovered = false;

        // Gli handler trovano la riga dall'elemento che li ha chiamati: sicuri anche se la riga sparisce.
        const std::weak_ptr<VerticalTabs> weak = weak_from_this();
        row.close.Click([weak](IInspectable const& sender, auto&&) {
            if (const auto self = weak.lock())
            {
                Guard(L"chiudi scheda", [&] { self->CloseTab(self->IndexOf(self->FindRow(sender))); });
            }
        });
        row.element.PointerEntered([weak](IInspectable const& sender, auto&&) {
            if (const auto self = weak.lock())
            {
                Guard(L"PointerEntered", [&] {
                    if (Row* r = self->FindRow(sender))
                    {
                        r->hovered = true;
                        self->UpdateRow(*r);
                    }
                });
            }
        });
        row.element.PointerExited([weak](IInspectable const& sender, auto&&) {
            if (const auto self = weak.lock())
            {
                Guard(L"PointerExited", [&] {
                    if (Row* r = self->FindRow(sender))
                    {
                        r->hovered = false;
                        self->UpdateRow(*r);
                    }
                });
            }
        });
        // Clic col tasto centrale = chiudi (anche se il ListViewItem ha già gestito l'evento).
        row.element.AddHandler(mux::UIElement::PointerReleasedEvent(),
                               winrt::box_value(muxi::PointerEventHandler([weak](IInspectable const& sender, muxi::PointerRoutedEventArgs const& args) {
                                   const auto self = weak.lock();
                                   const auto element = sender.try_as<mux::UIElement>();
                                   if (!self || !element)
                                   {
                                       return;
                                   }
                                   Guard(L"clic centrale", [&] {
                                       if (args.GetCurrentPoint(element).Properties().PointerUpdateKind() ==
                                           winrt::Microsoft::UI::Input::PointerUpdateKind::MiddleButtonReleased)
                                       {
                                           self->CloseTab(self->IndexOf(self->FindRow(sender)));
                                           args.Handled(true);
                                       }
                                   });
                               })),
                               true);
        // Clic destro (o Maiusc+F10 / tasto Menu): il menu contestuale di Esplora file per questa scheda.
        row.element.ContextRequested([weak](IInspectable const& sender, muxi::ContextRequestedEventArgs const& args) {
            const auto self = weak.lock();
            const auto element = sender.try_as<mux::UIElement>();
            if (!self || !element)
            {
                return;
            }
            Guard(L"menu contestuale", [&] {
                const Row* r = self->FindRow(sender);
                if (!r)
                {
                    return;
                }
                Point point{};
                if (!args.TryGetPosition(element, point))
                {
                    point = Point{ 24.0f, 18.0f }; // da tastiera: vicino all'inizio della riga
                }
                self->ShowNativeContextMenu(*r, element, point);
                args.Handled(true);
            });
        });
        UpdateRow(row);
    }

    void VerticalTabs::ReleaseRow(Row& row)
    {
        if (row.notifier && row.propertyToken)
        {
            try
            {
                row.notifier.PropertyChanged(row.propertyToken);
            }
            catch (...)
            {
            }
        }
        row.notifier = nullptr;
        row.propertyToken = {};
    }

    void VerticalTabs::ClearRows()
    {
        for (auto const& row : m_rows)
        {
            ReleaseRow(*row);
        }
        m_rows.clear();
        if (m_visible)
        {
            const FlagScope syncing{ m_syncing };
            Guard(L"svuotamento dell'elenco", [&] { m_visible.Clear(); });
        }
    }

    void VerticalTabs::RefreshRows()
    {
        for (size_t i = 0; i < m_rows.size(); ++i)
        {
            RefreshRow(*m_rows[i], static_cast<int>(i));
        }
    }

    void VerticalTabs::RefreshRow(Row& row, int index)
    {
        const auto list = m_nativeList.get();
        const auto container = list ? list.ContainerFromIndex(index).try_as<muxc::TabViewItem>() : nullptr;
        if (!container)
        {
            return; // scheda nativa non ancora realizzata: resta l'ultimo valore letto
        }
        const winrt::hstring title = TitleOf(container);
        if (row.title.Text() != title)
        {
            row.title.Text(title);
            muxc::ToolTipService::SetToolTip(row.element, winrt::box_value(title));
            mux::Automation::AutomationProperties::SetName(row.element, title);
        }
        const auto icon = IconOf(container);
        if (row.icon.Source() != icon)
        {
            row.icon.Source(icon);
        }
    }

    void VerticalTabs::UpdateRow(Row& row)
    {
        const bool selected = m_list && m_list.SelectedItem() == row.element;
        const bool dark = m_root ? m_root.ActualTheme() == mux::ElementTheme::Dark : true;

        // Fondo della riga: tinta tenue del colore della scheda (o velo neutro), più decisa e con il bordino
        // per la scheda attiva, che ha anche la stanghetta a sinistra (nel suo colore, o grigio neutro) e il
        // titolo come quello della scheda attiva nativa.
        const CardStyle& style = dark ? kCardDark : kCardLight;
        const winrt::Windows::UI::Color veil = dark ? winrt::Microsoft::UI::Colors::White() : winrt::Microsoft::UI::Colors::Black();
        const tabcolors::Swatch* swatch = tabcolors::Find(ColorOf(row.item));
        const auto card = swatch ? WithAlpha(dark ? swatch->fillDark : swatch->fillLight, selected ? style.coloredSelected : style.colored)
                                 : WithAlpha(veil, selected ? style.selected : style.normal);
        if (selected && swatch)
        {
            const auto pillColor = dark ? Shade(swatch->fillDark, kPillShadeDark) : swatch->fillLight;
            const auto currentPill = row.pillColor.Background().try_as<muxm::SolidColorBrush>();
            if (!currentPill || currentPill.Color() != pillColor)
            {
                row.pillColor.Background(muxm::SolidColorBrush(pillColor));
            }
        }
        row.pillColor.Visibility(selected && swatch ? mux::Visibility::Visible : mux::Visibility::Collapsed);
        mux::Automation::AutomationProperties::SetItemStatus(row.element, swatch ? winrt::hstring{ Tr(L"Color: ", L"Colore: ") } + swatch->DisplayName() : winrt::hstring{});
        const auto current = row.tint.Background().try_as<muxm::SolidColorBrush>();
        if (!current || current.Color() != card)
        {
            row.tint.Background(muxm::SolidColorBrush(card));
        }
        const auto border = row.tint.BorderBrush().try_as<muxm::SolidColorBrush>();
        if (selected && (!border || border.Color() != WithAlpha(veil, style.border)))
        {
            row.tint.BorderBrush(muxm::SolidColorBrush(WithAlpha(veil, style.border)));
        }
        else if (!selected && border)
        {
            row.tint.BorderBrush(nullptr);
        }
        row.pill.Visibility(selected && !swatch ? mux::Visibility::Visible : mux::Visibility::Collapsed);

        // Titolo come nelle schede orizzontali: stesso carattere, peso e colore (normale, al passaggio del
        // mouse, attiva). Finché non sono stati letti il colore resta quello ereditato.
        const TitleStyle& titleStyle = m_titleStyle;
        row.title.FontSize(titleStyle.size);
        if (titleStyle.family)
        {
            row.title.FontFamily(titleStyle.family);
        }
        row.title.FontWeight(selected ? titleStyle.selectedWeight : titleStyle.normalWeight);
        const muxm::Brush& titleBrush = selected ? titleStyle.selected : (row.hovered && titleStyle.hover ? titleStyle.hover : titleStyle.normal);
        if (titleBrush)
        {
            row.title.Foreground(titleBrush);
        }
        else
        {
            row.title.ClearValue(muxc::TextBlock::ForegroundProperty());
        }

        // Da espanso: icona, titolo e X (al passaggio del mouse o sulla scheda attiva). Da compresso resta
        // solo l'icona, centrata.
        const bool compact = m_compactVisual;
        const bool showClose = !compact && (row.hovered || selected);
        row.close.Visibility(showClose ? mux::Visibility::Visible : mux::Visibility::Collapsed);
        row.title.Visibility(compact ? mux::Visibility::Collapsed : mux::Visibility::Visible);
        const auto columns = row.grid.ColumnDefinitions();
        columns.GetAt(1).Width(compact ? mux::GridLength{ 0, mux::GridUnitType::Pixel } : mux::GridLength{ 1, mux::GridUnitType::Star });
        columns.GetAt(2).Width(mux::GridLength{ compact ? 0.0 : 28.0, mux::GridUnitType::Pixel });
        row.grid.ColumnSpacing(compact ? 0.0 : 10.0);
        row.grid.Margin(compact ? mux::Thickness{ 0, 0, 0, 0 } : mux::Thickness{ 12, 0, 4, 0 });
        row.grid.HorizontalAlignment(compact ? mux::HorizontalAlignment::Center : mux::HorizontalAlignment::Stretch);
    }

    void VerticalTabs::UpdateAllRows()
    {
        for (auto const& row : m_rows)
        {
            UpdateRow(*row);
        }
    }

    void VerticalTabs::RefreshVisibleRows()
    {
        if (!m_visible)
        {
            return;
        }
        std::vector<IInspectable> desired;
        desired.reserve(m_rows.size());
        for (auto const& row : m_rows)
        {
            desired.push_back(row->element);
        }

        // Allinea l'elenco visibile con inserimenti/rimozioni minimi (niente Reset: la ListView conserva
        // scorrimento e selezione). Dopo un trascinamento è già allineato: nessuna modifica.
        const FlagScope syncing{ m_syncing };
        try
        {
            for (uint32_t i = 0; i < desired.size(); ++i)
            {
                if (i < m_visible.Size() && m_visible.GetAt(i) == desired[i])
                {
                    continue;
                }
                uint32_t found = UINT32_MAX;
                for (uint32_t j = i + 1; j < m_visible.Size(); ++j)
                {
                    if (m_visible.GetAt(j) == desired[i])
                    {
                        found = j;
                        break;
                    }
                }
                if (found != UINT32_MAX)
                {
                    for (uint32_t k = i; k < found; ++k)
                    {
                        m_visible.RemoveAt(i);
                    }
                }
                else
                {
                    m_visible.InsertAt(i, desired[i]);
                }
            }
            while (m_visible.Size() > desired.size())
            {
                m_visible.RemoveAt(m_visible.Size() - 1);
            }
        }
        catch (...)
        {
            // La ListView ha rifiutato la modifica (es. elemento non ancora staccato): elenco da ricostruire.
            XPX_MLOG(L"VerticalTabs: modifica dell'elenco rifiutata (0x%08X).", static_cast<unsigned>(winrt::to_hresult()));
            ResetVisibleList();
        }
    }

    void VerticalTabs::ResetVisibleList()
    {
        // Ricostruzione completa, con una raccolta nuova ed elementi nuovi per ogni riga: rimedio per il
        // caso in cui la ListView non sia più allineata alla raccolta.
        if (!m_list)
        {
            return;
        }
        const FlagScope syncing{ m_syncing };
        try
        {
            m_list.ItemsSource(nullptr);
            m_visible = winrt::single_threaded_observable_vector<IInspectable>();
            for (auto const& row : m_rows)
            {
                const winrt::hstring title = row->title ? row->title.Text() : winrt::hstring{};
                const auto icon = row->icon ? row->icon.Source() : nullptr;
                BuildRowElement(*row);
                row->title.Text(title);
                row->icon.Source(icon);
                m_visible.Append(row->element);
            }
            m_list.ItemsSource(m_visible);
            XPX_MLOG(L"VerticalTabs: elenco ricostruito (%u righe).", m_visible.Size());
        }
        catch (...)
        {
            XPX_MLOG(L"VerticalTabs: ricostruzione dell'elenco non riuscita (0x%08X).", static_cast<unsigned>(winrt::to_hresult()));
        }
    }

    VerticalTabs::Row* VerticalTabs::FindRow(IInspectable const& sender)
    {
        if (!sender)
        {
            return nullptr;
        }
        for (auto const& row : m_rows)
        {
            if (row->element == sender || row->close == sender)
            {
                return row.get();
            }
        }
        return nullptr;
    }

    int VerticalTabs::IndexOf(Row const* row) const
    {
        for (size_t i = 0; i < m_rows.size(); ++i)
        {
            if (m_rows[i].get() == row)
            {
                return static_cast<int>(i);
            }
        }
        return -1;
    }

    // ---------------------------------------------------------------------------------------------
    // Selezione e focus
    // ---------------------------------------------------------------------------------------------

    void VerticalTabs::SyncSelectionFromNative()
    {
        const auto tabView = NativeTabView();
        if (!m_enabled || !tabView || !m_list || !m_visible || m_dragging)
        {
            return;
        }
        const int index = tabView.SelectedIndex();
        Row* selected = index >= 0 && index < static_cast<int>(m_rows.size()) ? m_rows[static_cast<size_t>(index)].get() : nullptr;
        const auto before = m_list.SelectedItem();
        {
            const FlagScope syncing{ m_syncing };
            try
            {
                uint32_t position = 0;
                if (selected && m_visible.IndexOf(selected->element, position))
                {
                    if (m_list.SelectedIndex() != static_cast<int>(position))
                    {
                        m_list.SelectedIndex(static_cast<int>(position));
                        m_list.ScrollIntoView(selected->element);
                    }
                }
                else if (m_list.SelectedIndex() != -1)
                {
                    m_list.SelectedIndex(-1); // la scheda attiva è esclusa dal filtro
                }
            }
            catch (...)
            {
                XPX_MLOG(L"VerticalTabs: selezione non sincronizzata (0x%08X).", static_cast<unsigned>(winrt::to_hresult()));
            }
        }
        if (m_list.SelectedItem() != before)
        {
            UpdateAllRows();
        }
    }

    void VerticalTabs::OnListSelectionChanged()
    {
        // Durante il trascinamento la ListView sposta l'elemento e la selezione cambia solo in apparenza.
        if (m_syncing || m_dragging || !m_enabled)
        {
            return;
        }
        const auto tabView = NativeTabView();
        const int index = IndexOf(FindRow(m_list.SelectedItem()));
        if (tabView && index >= 0)
        {
            if (tabView.SelectedIndex() != index)
            {
                tabView.SelectedIndex(index);
            }
            UpdateAllRows();
        }
        else
        {
            // Nessuna riga selezionata (es. Ctrl+clic sulla scheda attiva): si torna a quella di Esplora file.
            SyncSelectionFromNative();
            UpdateAllRows();
        }
    }

    void VerticalTabs::FocusContent()
    {
        if (!m_root)
        {
            return;
        }
        // Dopo che Esplora file ha gestito il cambio di scheda: il focus va alla vista dei file.
        const std::weak_ptr<VerticalTabs> weak = weak_from_this();
        m_root.DispatcherQueue().TryEnqueue(DispatcherQueuePriority::Low, [weak] {
            if (const auto self = weak.lock())
            {
                if (const HWND target = self->m_layout.ContentFocusTarget())
                {
                    SetFocus(target);
                }
            }
        });
    }

    // ---------------------------------------------------------------------------------------------
    // Riordino con trascinamento
    // ---------------------------------------------------------------------------------------------

    VerticalTabs::Row* VerticalTabs::RowFromSource(IInspectable const& source, bool& onCloseButton)
    {
        // Dall'elemento colpito si risale fino alla riga, annotando se si è passati dal pulsante X.
        onCloseButton = false;
        for (auto current = source.try_as<mux::DependencyObject>(); current; current = muxm::VisualTreeHelper::GetParent(current))
        {
            for (auto const& row : m_rows)
            {
                if (row->element == current)
                {
                    return row.get();
                }
                if (row->close == current)
                {
                    onCloseButton = true;
                }
            }
        }
        return nullptr;
    }

    void VerticalTabs::OnListPointerPressed(muxi::PointerRoutedEventArgs const& args)
    {
        if (m_dragging || m_dragPending)
        {
            return;
        }
        const auto point = args.GetCurrentPoint(m_list);
        // Al tocco la ListView scorre; il riordino è per mouse e penna, col tasto principale.
        if (point.PointerDeviceType() == winrt::Microsoft::UI::Input::PointerDeviceType::Touch || !point.Properties().IsLeftButtonPressed())
        {
            return;
        }
        bool onCloseButton = false;
        const Row* row = RowFromSource(args.OriginalSource(), onCloseButton);
        if (!row || onCloseButton)
        {
            return;
        }
        m_dragPending = true;
        m_dragPointerId = args.Pointer().PointerId();
        m_dragElement = row->element;
        m_dragStart = point.Position();
    }

    void VerticalTabs::OnListPointerMoved(muxi::PointerRoutedEventArgs const& args)
    {
        if ((!m_dragPending && !m_dragging) || args.Pointer().PointerId() != m_dragPointerId)
        {
            return;
        }
        const auto point = args.GetCurrentPoint(m_list);
        if (!point.Properties().IsLeftButtonPressed())
        {
            // Rilascio non ricevuto (es. avvenuto fuori dalla finestra senza cattura).
            if (m_dragging)
            {
                EndDrag(true);
            }
            else
            {
                m_dragPending = false;
            }
            return;
        }
        const Point position = point.Position();
        if (m_dragPending)
        {
            if (std::abs(position.Y - m_dragStart.Y) < kDragThreshold && std::abs(position.X - m_dragStart.X) < kDragThreshold)
            {
                return;
            }
            BeginDrag(args.Pointer());
            if (!m_dragging)
            {
                return;
            }
        }
        m_dragY = position.Y;
        UpdateDrag();
        args.Handled(true);
    }

    void VerticalTabs::OnListPointerReleased(muxi::PointerRoutedEventArgs const& args)
    {
        if (args.Pointer().PointerId() != m_dragPointerId)
        {
            return;
        }
        m_dragPending = false; // semplice clic: ci pensa la ListView
        if (m_dragging)
        {
            EndDrag(true);
            args.Handled(true);
        }
    }

    void VerticalTabs::BeginDrag(muxi::Pointer const& pointer)
    {
        m_dragPending = false;
        m_dragFromIndex = IndexOf(FindRow(m_dragElement));
        // La cattura passa dalla riga all'elenco: la riga non riceverà il rilascio (niente clic) e l'elenco
        // continua a ricevere il puntatore anche mentre le righe vengono spostate.
        if (m_dragFromIndex < 0 || !m_list.CapturePointer(pointer))
        {
            m_dragFromIndex = -1;
            return;
        }
        m_dragging = true;

        // Scorrimento automatico quando il puntatore resta vicino ai bordi dell'elenco.
        if (!m_autoScrollTimer && m_root)
        {
            m_autoScrollTimer = m_root.DispatcherQueue().CreateTimer();
            m_autoScrollTimer.Interval(std::chrono::milliseconds(30));
            m_autoScrollTimer.IsRepeating(true);
            const std::weak_ptr<VerticalTabs> weak = weak_from_this();
            m_autoScrollTimer.Tick([weak](auto&&, auto&&) {
                if (const auto self = weak.lock())
                {
                    Guard(L"scorrimento automatico", [&] { self->AutoScrollStep(); });
                }
            });
        }
        if (m_autoScrollTimer)
        {
            m_autoScrollTimer.Start();
        }
    }

    void VerticalTabs::UpdateDrag()
    {
        Row* row = FindRow(m_dragElement);
        uint32_t current = 0;
        if (!row || !m_visible.IndexOf(row->element, current))
        {
            EndDrag(false); // la scheda è sparita durante il trascinamento
            return;
        }

        // La riga trascinata segue il puntatore: si sposta di tante posizioni quante sono le righe superate
        // per più di metà (stesso passo per tutte le righe).
        m_list.UpdateLayout();
        const auto slot = muxp::LayoutInformation::GetLayoutSlot(row->element);
        const double pitch = slot.Height;
        if (pitch <= 1.0)
        {
            return;
        }
        double middle = 0;
        try
        {
            middle = row->element.TransformToVisual(m_list).TransformPoint(Point{ 0, static_cast<float>(row->element.ActualHeight() / 2) }).Y;
        }
        catch (...)
        {
            return;
        }
        const int count = static_cast<int>(m_visible.Size());
        const int target = std::clamp(static_cast<int>(current) + static_cast<int>(std::lround((m_dragY - middle) / pitch)), 0, count - 1);
        if (target == static_cast<int>(current))
        {
            return;
        }
        const bool selected = m_list.SelectedItem() == row->element;
        const FlagScope syncing{ m_syncing };
        try
        {
            m_visible.RemoveAt(current);
            m_visible.InsertAt(static_cast<uint32_t>(target), row->element);
            if (selected)
            {
                m_list.SelectedItem(row->element); // la scheda attiva resta evidenziata mentre si sposta
            }
        }
        catch (...)
        {
            XPX_MLOG(L"VerticalTabs: spostamento della riga rifiutato (0x%08X): trascinamento annullato.", static_cast<unsigned>(winrt::to_hresult()));
            ResetVisibleList();
            EndDrag(false);
        }
    }

    void VerticalTabs::AutoScrollStep()
    {
        if (!m_dragging || !m_list)
        {
            return;
        }
        const double height = m_list.ActualHeight();
        double delta = 0;
        if (m_dragY < kAutoScrollEdge)
        {
            delta = -std::min(kAutoScrollMaxStep, (kAutoScrollEdge - m_dragY) * 0.6);
        }
        else if (m_dragY > height - kAutoScrollEdge)
        {
            delta = std::min(kAutoScrollMaxStep, (m_dragY - (height - kAutoScrollEdge)) * 0.6);
        }
        const auto scroller = delta != 0 ? FindDescendant<muxc::ScrollViewer>(m_list) : nullptr;
        if (!scroller)
        {
            return;
        }
        const double offset = std::clamp(scroller.VerticalOffset() + delta, 0.0, scroller.ScrollableHeight());
        if (std::abs(offset - scroller.VerticalOffset()) < 0.5)
        {
            return;
        }
        scroller.ChangeView(nullptr, winrt::Windows::Foundation::IReference<double>{ offset }, nullptr, true);
        UpdateDrag();
    }

    void VerticalTabs::EndDrag(bool commit)
    {
        m_dragPending = false;
        if (!m_dragging)
        {
            return;
        }
        if (m_autoScrollTimer)
        {
            m_autoScrollTimer.Stop();
        }
        // L'elenco mostra già il nuovo ordine: lo stesso spostamento si applica alle schede vere, nella
        // raccolta di Esplora file (come fa la striscia orizzontale quando si trascina una scheda).
        const int from = m_dragFromIndex;
        uint32_t to = 0;
        const bool moved = commit && from >= 0 && m_dragElement && m_visible.IndexOf(m_dragElement, to) && static_cast<int>(to) != from;
        m_dragging = false;
        m_dragFromIndex = -1;
        m_dragElement = nullptr;
        try
        {
            m_list.ReleasePointerCaptures();
        }
        catch (...)
        {
        }
        if (moved)
        {
            if (MoveNativeTab(from, static_cast<int>(to)))
            {
                XPX_MLOG(L"VerticalTabs: scheda spostata da %d a %u.", from, to);
            }
        }
        // In ogni caso l'elenco si riallinea all'ordine reale di Esplora file (annullamento compreso).
        ScheduleSync();
        FocusContent();
    }

    // ---------------------------------------------------------------------------------------------
    // Menu contestuale nativo
    // ---------------------------------------------------------------------------------------------

    void VerticalTabs::ShowNativeContextMenu(Row const& row, mux::UIElement const& element, Point const& localPoint)
    {
        const int index = IndexOf(&row);
        const auto container = NativeContainer(index);
        if (!container || !m_panelHwnd)
        {
            XPX_MLOG(L"VerticalTabs: menu contestuale non disponibile (scheda nativa %d non trovata).", index);
            return;
        }
        // Il menu è quello di Esplora file, aperto sulla sua vera scheda come farebbe il framework al clic
        // destro: le voci agiscono su quella scheda, con la logica di Esplora file.
        mux::UIElement owner = container;
        muxp::FlyoutBase flyout = container.ContextFlyout();
        if (!flyout)
        {
            if (const auto inner = FindContextFlyoutOwner(container))
            {
                owner = inner;
                flyout = inner.ContextFlyout();
            }
        }
        if (!flyout)
        {
            XPX_MLOG(L"VerticalTabs: la scheda nativa non ha un menu contestuale.");
            return;
        }
        const HWND header = FindHeaderIsland(m_main, m_panelHwnd);
        if (!header)
        {
            return;
        }

        // Punto del clic sullo schermo e posizione corrispondente rispetto alla scheda nativa (nella
        // striscia invisibile in alto): il menu, che è una finestra a sé, compare sotto il puntatore.
        const auto inPanel = element.TransformToVisual(nullptr).TransformPoint(localPoint);
        const double panelScale = element.XamlRoot().RasterizationScale();
        RECT panelRect{};
        GetWindowRect(m_panelHwnd, &panelRect);
        const POINT click{ panelRect.left + std::lround(inPanel.X * panelScale), panelRect.top + std::lround(inPanel.Y * panelScale) };
        const auto origin = owner.TransformToVisual(nullptr).TransformPoint(Point{ 0, 0 });
        const double nativeScale = owner.XamlRoot().RasterizationScale();
        RECT headerRect{};
        GetWindowRect(header, &headerRect);
        const double ownerX = headerRect.left + origin.X * nativeScale;
        const double ownerY = headerRect.top + origin.Y * nativeScale;
        const Point position{ static_cast<float>((click.x - ownerX) / nativeScale), static_cast<float>((click.y - ownerY) / nativeScale) };

        const bool constrained = flyout.ShouldConstrainToRootBounds();
        if (constrained)
        {
            flyout.ShouldConstrainToRootBounds(false); // il menu deve poter uscire dall'intestazione
        }
        const std::weak_ptr<VerticalTabs> weak = weak_from_this();
        m_flyoutClosedRevoker = flyout.Closed(winrt::auto_revoke, [weak, constrained](IInspectable const& sender, auto&&) {
            if (const auto self = weak.lock())
            {
                Guard(L"menu chiuso", [&] {
                    if (constrained)
                    {
                        if (const auto closed = sender.try_as<muxp::FlyoutBase>())
                        {
                            closed.ShouldConstrainToRootBounds(true); // il menu nativo torna com'era
                        }
                    }
                    self->RemoveColorMenu(); // fuori dal pannello il menu nativo resta quello originale
                    self->m_flyoutOpeningRevoker.revoke();
                    self->m_flyoutOpenedRevoker.revoke();
                    self->m_flyoutClosedRevoker.revoke();
                });
            }
        });
        // In fondo al menu di Esplora file: "Colore scheda" (solo quando lo apre il pannello). Esplora file
        // può ricomporre le sue voci quando il menu si apre: le nostre si aggiungono subito, poi di nuovo
        // dopo di lui (Opening, gestore registrato per ultimo) e si ricontrollano a menu aperto.
        if (const auto menu = flyout.try_as<muxc::MenuFlyout>())
        {
            const IInspectable item = row.item;
            AddColorMenu(menu, item);
            m_flyoutOpeningRevoker = flyout.Opening(winrt::auto_revoke, [weak, menu, item](auto&&, auto&&) {
                if (const auto self = weak.lock())
                {
                    Guard(L"menu in apertura", [&] { self->EnsureColorMenu(menu, item, L"apertura"); });
                }
            });
            m_flyoutOpenedRevoker = flyout.Opened(winrt::auto_revoke, [weak, menu, item](auto&&, auto&&) {
                if (const auto self = weak.lock())
                {
                    Guard(L"menu aperto", [&] { self->EnsureColorMenu(menu, item, L"menu aperto"); });
                }
            });
        }
        muxp::FlyoutShowOptions options;
        options.Position(winrt::Windows::Foundation::IReference<Point>{ position });
        options.ShowMode(muxp::FlyoutShowMode::Standard);
        flyout.ShowAt(owner, options);
        XPX_MVLOG(L"VerticalTabs: menu contestuale della scheda %d (%s, vincolato %d).", index, winrt::get_class_name(flyout).c_str(), constrained ? 1 : 0);
    }

    // ---------------------------------------------------------------------------------------------
    // Divisore trascinabile
    // ---------------------------------------------------------------------------------------------

    void VerticalTabs::SetSplitterHighlight(bool on)
    {
        if (m_separatorActive)
        {
            m_separatorActive.Visibility(on ? mux::Visibility::Visible : mux::Visibility::Collapsed);
        }
    }

    void VerticalTabs::ResizeFromCursor()
    {
        // Il bordo destro del pannello segue il cursore.
        POINT cursor{};
        GetCursorPos(&cursor);
        ScreenToClient(m_main, &cursor);
        const int px = m_grabOffsetPx + cursor.x;
        ApplyPanelWidth(MulDiv(px, USER_DEFAULT_SCREEN_DPI, static_cast<int>(DpiOf(m_main))));
    }

    void VerticalTabs::ApplyPanelWidth(int widthDip)
    {
        // Limiti delle impostazioni; WindowLayout garantisce in più una larghezza minima al contenuto.
        widthDip = std::clamp(widthDip, ModSettings::kMinWidth, ModSettings::kMaxWidth);
        if (widthDip == m_settings.width)
        {
            return;
        }
        m_settings.width = widthDip;
        if (m_enabled)
        {
            m_layout.SetActive(true, m_settings.PanelWidth());
        }
    }

    void VerticalTabs::PersistPanelState()
    {
        // Salvati in settings.ini: le altre finestre li adottano rileggendo il file.
        ModSettings stored = ModSettings::Load();
        if (stored.width != m_settings.width || stored.collapsed != m_settings.collapsed)
        {
            stored.width = m_settings.width;
            stored.collapsed = m_settings.collapsed;
            stored.SavePanelState();
            XPX_MLOG(L"VerticalTabs: pannello salvato (larghezza %d, %s).", m_settings.width, m_settings.collapsed ? L"compresso" : L"espanso");
        }
    }

    // ---------------------------------------------------------------------------------------------
    // Comandi sui controlli nativi
    // ---------------------------------------------------------------------------------------------

    muxc::TabView VerticalTabs::NativeTabView() const
    {
        return m_tabView.get();
    }

    muxc::TabViewItem VerticalTabs::NativeContainer(int index)
    {
        const auto list = m_nativeList.get();
        if (!list || index < 0)
        {
            return nullptr;
        }
        auto container = list.ContainerFromIndex(index).try_as<muxc::TabViewItem>();
        if (!container && static_cast<uint32_t>(index) < list.Items().Size())
        {
            // Contenitore non ancora realizzato: lo si fa realizzare nella striscia (invisibile).
            list.ScrollIntoView(list.Items().GetAt(static_cast<uint32_t>(index)));
            list.UpdateLayout();
            container = list.ContainerFromIndex(index).try_as<muxc::TabViewItem>();
        }
        return container;
    }

    void VerticalTabs::CloseTab(int index)
    {
        // Si preme la X della scheda nativa: stessa logica di Esplora file.
        const auto container = NativeContainer(index);
        const auto button = container ? interop::FindByName(container, L"CloseButton").try_as<muxc::Button>() : nullptr;
        if (!InvokeButton(button))
        {
            XPX_MLOG(L"VerticalTabs: impossibile chiudere la scheda %d (pulsante nativo non disponibile).", index);
        }
    }

    void VerticalTabs::NewTab()
    {
        const auto tabView = NativeTabView();
        const auto button = tabView ? interop::FindByName(tabView, L"AddButton").try_as<muxc::Button>() : nullptr;
        if (InvokeButton(button))
        {
            FocusContent();
        }
        else
        {
            XPX_MLOG(L"VerticalTabs: impossibile aprire una nuova scheda (pulsante nativo non disponibile).");
        }
    }

    // ---------------------------------------------------------------------------------------------
    // Colori delle schede
    // ---------------------------------------------------------------------------------------------

    std::wstring VerticalTabs::ColorFilePath(const wchar_t* kind) const
    {
        const std::wstring directory = DataDirectory() + L"\\colors";
        CreateDirectoryW(directory.c_str(), nullptr);
        return directory + L"\\" + ColorFileName(kind, m_main);
    }

    int VerticalTabs::ColorOf(IInspectable const& item) const
    {
        for (auto const& [model, color] : m_colors)
        {
            if (model == item)
            {
                return color;
            }
        }
        return 0;
    }

    void VerticalTabs::SetTabColor(IInspectable const& item, int color)
    {
        const auto existing = std::find_if(m_colors.begin(), m_colors.end(), [&item](auto const& entry) { return entry.first == item; });
        if (color > 0 && color < kTabColorCount)
        {
            if (existing != m_colors.end())
            {
                existing->second = color;
            }
            else
            {
                m_colors.emplace_back(item, color);
            }
        }
        else if (existing != m_colors.end())
        {
            m_colors.erase(existing);
        }
        for (auto const& row : m_rows)
        {
            if (row->item == item)
            {
                UpdateRow(*row);
            }
        }
        ExportColors();
        const tabcolors::Swatch* swatch = tabcolors::Find(color);
        XPX_MLOG(L"VerticalTabs: colore della scheda: %s.", swatch ? kTabColorKeys[color] : L"nessuno");
    }

    void VerticalTabs::PruneColors()
    {
        if (!m_items)
        {
            return;
        }
        std::erase_if(m_colors, [this](auto const& entry) {
            uint32_t index = 0;
            return !m_items.IndexOf(entry.first, index);
        });
    }

    void VerticalTabs::ExportColors()
    {
        if (!m_enabled || !m_items || !m_main)
        {
            return;
        }
        TabColorList list;
        list.count = m_items.Size();
        for (uint32_t i = 0; i < list.count; ++i)
        {
            const auto item = m_items.GetAt(i);
            if (const int color = ColorOf(item))
            {
                // Con il titolo: la tray abbina il colore solo se in quella posizione trova quella scheda.
                winrt::hstring title;
                if (i < m_rows.size() && m_rows[i]->item == item && m_rows[i]->title)
                {
                    title = m_rows[i]->title.Text();
                }
                list.colored.push_back(TabColorEntry{ i, color, Utf8Of(title) });
            }
        }
        // Una finestra mai colorata non scrive nulla; una che lo è stata continua a pubblicare (anche
        // "nessun colore"), perché la tray deve sapere che i colori sono stati tolti.
        if (list.colored.empty() && m_lastExport.empty())
        {
            return;
        }
        const std::string text = FormatTabColors(list);
        if (text == m_lastExport)
        {
            return;
        }
        if (WriteTabColors(ColorFilePath(L"live"), list))
        {
            m_lastExport = text;
        }
    }

    void VerticalTabs::HandOverColors()
    {
        if (m_colors.empty() || !m_main)
        {
            return;
        }
        const auto native = m_nativeList.get();
        if (!native)
        {
            return;
        }
        const auto items = native.Items();
        TabColorList list;
        list.count = items.Size();
        for (uint32_t i = 0; i < list.count; ++i)
        {
            if (const int color = ColorOf(items.GetAt(i)))
            {
                list.colored.push_back(TabColorEntry{ i, color, {} });
            }
        }
        if (!list.colored.empty() && WriteTabColors(ColorFilePath(L"pending"), list))
        {
            XPX_MLOG(L"VerticalTabs: %zu colori lasciati da rimettere (la mod esce dal processo).", list.colored.size());
        }
    }

    void VerticalTabs::OfferPendingColors(HWND window, TabColorList const& list)
    {
        if (window != m_main)
        {
            return;
        }
        if (!m_pendingColors)
        {
            m_pendingSince = GetTickCount64();
        }
        m_pendingColors = list;
        TryApplyPendingColors();
    }

    void VerticalTabs::TryApplyPendingColors()
    {
        // Con la colonna spenta si aspetta: intanto vale il file stesso (la tray lo legge come i colori
        // di quella finestra).
        if (!m_pendingColors || !m_enabled || !m_items)
        {
            return;
        }
        const uint32_t count = m_items.Size();
        const bool overdue = GetTickCount64() - m_pendingSince > kPendingColorsTimeoutMs;
        if (count != m_pendingColors->count && !overdue)
        {
            return; // la tray sta ancora aggiungendo schede
        }
        size_t applied = 0;
        for (auto const& pending : m_pendingColors->colored)
        {
            if (pending.index < count)
            {
                const auto item = m_items.GetAt(static_cast<uint32_t>(pending.index));
                const auto existing = std::find_if(m_colors.begin(), m_colors.end(), [&item](auto const& entry) { return entry.first == item; });
                if (existing != m_colors.end())
                {
                    existing->second = pending.color;
                }
                else
                {
                    m_colors.emplace_back(item, pending.color);
                }
                ++applied;
            }
        }
        XPX_MLOG(L"VerticalTabs: colori rimessi a %zu schede su %u%s.", applied, count, count != m_pendingColors->count ? L" (finestra incompleta)" : L"");
        // Scheda riaperta con Ctrl+Maiusc+T: dal fondo al posto che aveva (i colori, già dati, la seguono). Solo con la finestra
        // come l'ha lasciata la tray: se intanto le schede sono cambiate, resta dov'è.
        const int moveFrom = m_pendingColors->moveFrom;
        const int moveTo = m_pendingColors->moveTo;
        if (moveFrom >= 0 && moveTo >= 0 && moveFrom != moveTo && count == m_pendingColors->count && moveFrom < static_cast<int>(count) &&
            moveTo < static_cast<int>(count))
        {
            const bool moved = MoveNativeTab(moveFrom, moveTo);
            XPX_MLOG(L"VerticalTabs: scheda riaperta rimessa al suo posto (%d -> %d): %s.", moveFrom, moveTo, moved ? L"fatto" : L"non riuscito");
        }
        m_pendingColors.reset();
        DeleteFileW(ColorFilePath(L"pending").c_str());
        UpdateAllRows();
        ExportColors();
    }

    void VerticalTabs::AddColorMenu(muxc::MenuFlyout const& menu, IInspectable const& item)
    {
        RemoveColorMenu();
        try
        {
            const bool dark = m_root && m_root.ActualTheme() == mux::ElementTheme::Dark;
            const int current = ColorOf(item);
            const std::weak_ptr<VerticalTabs> weak = weak_from_this();

            muxc::MenuFlyoutSubItem colors;
            colors.Text(Tr(L"Tab color", L"Colore scheda"));
            muxc::FontIcon colorsIcon;
            colorsIcon.Glyph(L""); // tavolozza
            colors.Icon(colorsIcon);

            const auto addItem = [&](int color, wchar_t const* text, std::optional<winrt::Windows::UI::Color> fill) {
                muxc::RadioMenuFlyoutItem entry;
                entry.GroupName(L"VTabsExplorerTabColor");
                entry.Text(text);
                entry.IsChecked(color == current);
                muxc::FontIcon icon;
                icon.Glyph(fill ? L"" : L""); // cerchio pieno del colore / cerchio vuoto
                if (fill)
                {
                    icon.Foreground(muxm::SolidColorBrush(*fill));
                }
                entry.Icon(icon);
                entry.Click([weak, item, color](auto&&, auto&&) {
                    if (const auto self = weak.lock())
                    {
                        Guard(L"colore della scheda", [&] { self->SetTabColor(item, color); });
                    }
                });
                colors.Items().Append(entry);
            };
            addItem(0, Tr(L"None", L"Nessuno"), std::nullopt);
            for (auto const& swatch : tabcolors::Palette())
            {
                addItem(swatch.color, swatch.DisplayName(), dark ? swatch.fillDark : swatch.fillLight);
            }

            muxc::MenuFlyoutSeparator separator;
            menu.Items().Append(separator);
            menu.Items().Append(colors);
            m_extendedMenu = menu;
            m_menuExtras = { separator, colors };
        }
        catch (...)
        {
            XPX_MLOG(L"VerticalTabs: voce \"Colore scheda\" non aggiunta al menu (0x%08X).", static_cast<unsigned>(winrt::to_hresult()));
            RemoveColorMenu();
        }
    }

    void VerticalTabs::EnsureColorMenu(muxc::MenuFlyout const& menu, IInspectable const& item, const wchar_t* when)
    {
        // Le nostre voci ci sono ancora (Esplora file non ha ricomposto il menu)?
        bool present = m_extendedMenu == menu && !m_menuExtras.empty();
        if (present)
        {
            const auto items = menu.Items();
            for (auto const& extra : m_menuExtras)
            {
                uint32_t index = 0;
                present = present && items.IndexOf(extra, index);
            }
        }
        if (!present)
        {
            XPX_MLOG(L"VerticalTabs: menu ricomposto da Esplora file (%s, %u voci): \"Colore scheda\" aggiunta di nuovo.", when,
                     menu.Items().Size());
            AddColorMenu(menu, item);
        }
    }

    void VerticalTabs::RemoveColorMenu()
    {
        if (m_extendedMenu)
        {
            try
            {
                const auto items = m_extendedMenu.Items();
                for (auto const& extra : m_menuExtras)
                {
                    uint32_t index = 0;
                    if (items.IndexOf(extra, index))
                    {
                        items.RemoveAt(index);
                    }
                }
            }
            catch (...)
            {
            }
        }
        m_extendedMenu = nullptr;
        m_menuExtras.clear();
    }

    bool VerticalTabs::MoveNativeTab(int from, int to)
    {
        // Lo stesso spostamento che fa la striscia orizzontale quando vi si trascina una scheda: la scheda
        // tolta e reinserita nella raccolta di Esplora file. La scheda attiva resta quella.
        const auto list = m_nativeList.get();
        const auto tabView = NativeTabView();
        if (!list || !tabView || from < 0 || to < 0)
        {
            return false;
        }
        const auto source = list.ItemsSource();
        const int selected = tabView.SelectedIndex();
        IInspectable selectedItem{ nullptr };
        try
        {
            if (selected >= 0)
            {
                selectedItem = list.Items().GetAt(static_cast<uint32_t>(selected));
            }
            if (const auto bindable = source.try_as<mux::Interop::IBindableVector>())
            {
                const auto item = bindable.GetAt(static_cast<uint32_t>(from));
                bindable.RemoveAt(static_cast<uint32_t>(from));
                bindable.InsertAt(static_cast<uint32_t>(to), item);
            }
            else if (const auto vector = source.try_as<wfc::IVector<IInspectable>>())
            {
                const auto item = vector.GetAt(static_cast<uint32_t>(from));
                vector.RemoveAt(static_cast<uint32_t>(from));
                vector.InsertAt(static_cast<uint32_t>(to), item);
            }
            else
            {
                XPX_MLOG(L"VerticalTabs: la raccolta delle schede non si lascia riordinare.");
                return false;
            }
        }
        catch (...)
        {
            XPX_MLOG(L"VerticalTabs: riordino non riuscito (0x%08X).", static_cast<unsigned>(winrt::to_hresult()));
            return false;
        }
        // Togliere la scheda attiva dalla raccolta sposta la selezione: si rimette quella di prima.
        if (selectedItem && tabView.SelectedItem() != selectedItem)
        {
            XPX_MLOG(L"VerticalTabs: dopo il riordino la scheda attiva era cambiata: ripristinata.");
            tabView.SelectedItem(selectedItem);
        }
        return true;
    }
}
