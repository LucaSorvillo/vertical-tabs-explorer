#include "pch.h"
#include "NavBar.h"

#include "../ModShared.h"
#include "Interop.h"
#include "MenuArray.h"
#include "ModLog.h"
#include "NavOrder.h"
#include "TextInput.h"
#include "WindowLayout.h"

#include <shellapi.h>
#include <shlobj.h>
#include <shobjidl.h>
#include <shlwapi.h>
#include <winrt/Windows.Storage.Streams.h>

namespace xpx::mod
{
    namespace
    {
        namespace mux = winrt::Microsoft::UI::Xaml;
        namespace muxc = winrt::Microsoft::UI::Xaml::Controls;
        namespace muxcp = winrt::Microsoft::UI::Xaml::Controls::Primitives;
        namespace muxm = winrt::Microsoft::UI::Xaml::Media;
        namespace muxmi = winrt::Microsoft::UI::Xaml::Media::Imaging;
        namespace muxi = winrt::Microsoft::UI::Xaml::Input;
        using winrt::com_ptr;
        using winrt::Windows::Foundation::IInspectable;
        using winrt::Windows::Foundation::Point;

        using interop::kNavBarProp; // proprietà di finestra sull'isola della barra
        constexpr double kItemHeight = 28;
        constexpr double kItemSpacing = 2;
        constexpr double kSideMargin = 4;
        constexpr double kOverflowWidth = 36;
        constexpr double kMaxTextWidth = 160;
        constexpr double kListItemHeight = 32;
        constexpr double kListMaxTextWidth = 360;
        // Testo delle voci, uguale nella barra e negli elenchi (come Chrome: barra dei segnalibri e menu con lo stesso
        // testo; fino al 2026-11-06 gli elenchi avevano il 14 dei menu di Windows 11).
        constexpr double kTextSize = 12;
        constexpr double kMarkerLength = 20;
        constexpr double kDropTolerance = 12;           // DIP sopra e sotto la barra in cui si lascia ancora nella barra
        constexpr ULONGLONG kOpenListAfterMs = 600;      // fermi su "»" o su un gruppo trascinando: si apre il suo elenco
        constexpr auto kHoverDelay = std::chrono::milliseconds(400); // fermi su una riga: si apre il sottogruppo (o si chiude)
        constexpr int kIconDip = 16;
        constexpr UINT kGetShellBrowser = WM_USER + 7; // CWM_GETISHELLBROWSER: IShellBrowser della finestra (stesso processo)
        constexpr uint32_t kPinnedSortIndex = 0x48;      // posto delle cartelle aggiunte nel riquadro (NavItems.h)
        // Voci nostre nel menu classico di una cartella: quelle della shell vanno da 1 a kExtraFirstId - 1.
        constexpr UINT kExtraFirstId = 0x7F00;
        constexpr UINT kExtraLastId = 0x7FFF;
        constexpr size_t kOpenAllConfirm = 15;           // "Apri tutte" con più cartelle chiede conferma (come Chrome)
        // Cartella trascinata da fuori e aggiunta ad Accesso rapido: entro questo tempo la lettura nuova la porta nella barra,
        // che la mette nel posto scelto; poi si rinuncia (aggiunta non riuscita, o cartella fuori dalla modalità dimostrativa).
        constexpr ULONGLONG kPendingDropMs = 15'000;
        // Icona dei gruppi: due cartelle sovrapposte, solo contorno (16x16, tratto di 1 DIP), come le icone della
        // barra dei comandi: la cartella davanti intera, di quella dietro solo la parte che si vede.
        constexpr wchar_t kGroupIconData[] =
            L"M1.5,13.5 L1.5,6.5 Q1.5,5.5 2.5,5.5 L5,5.5 L6.5,7 L11.5,7 Q12.5,7 12.5,8 L12.5,13.5 Q12.5,14.5 11.5,14.5 L2.5,14.5 "
            L"Q1.5,14.5 1.5,13.5 Z M1.5,7 L6.5,7 "
            L"M4.5,4.5 L4.5,3.5 Q4.5,2.5 5.5,2.5 L8,2.5 L9.5,4 L14.5,4 Q15.5,4 15.5,5 L15.5,10.5 Q15.5,11.5 14.5,11.5 L13.5,11.5";
        // Registro dettagliato del trascinamento (comando di prova "navbar dragdebug on|off"): pressioni,
        // primi movimenti, rilasci. Spento di norma: una riga per pressione sarebbe rumore.
        bool g_dragDebug = false;

        // Modalità dimostrativa (solo sviluppo: screenshot e video). Con dev-navbar-demo.txt nella cartella dei dati, in un
        // explorer.exe che NON è quello della barra delle applicazioni (le finestre di prova separate), la barra mostra solo
        // Home, Galleria, Questo PC e Rete più le cartelle aggiunte sotto la cartella scritta nella prima riga del file; le
        // altre cartelle aggiunte (quelle dell'utente) restano fuori vista, e l'ordine di Windows non si tocca mai.
        std::optional<std::wstring> const& DemoRoot()
        {
            static const std::optional<std::wstring> root = []() -> std::optional<std::wstring> {
                const DWORD shell = ShellProcessId();
                if (shell == 0 || shell == GetCurrentProcessId())
                {
                    return std::nullopt;
                }
                const HANDLE file = CreateFileW((DataDirectory() + L"\\dev-navbar-demo.txt").c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr,
                                                OPEN_EXISTING, 0, nullptr);
                if (file == INVALID_HANDLE_VALUE)
                {
                    return std::nullopt;
                }
                char bytes[2048]{};
                DWORD read = 0;
                ReadFile(file, bytes, sizeof(bytes) - 1, &read, nullptr);
                CloseHandle(file);
                std::string text(bytes, read);
                if (text.starts_with("\xEF\xBB\xBF"))
                {
                    text.erase(0, 3);
                }
                text = text.substr(0, text.find_first_of("\r\n"));
                std::wstring line(text.size(), L'\0');
                line.resize(static_cast<size_t>(std::max(0, MultiByteToWideChar(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), line.data(),
                                                                                 static_cast<int>(line.size())))));
                while (!line.empty() && (line.back() == L'\\' || iswspace(line.back())))
                {
                    line.pop_back();
                }
                if (line.empty())
                {
                    return std::nullopt;
                }
                XPX_MLOG(L"NavBar: modalità dimostrativa (solo le voci di sistema e le cartelle dimostrative).");
                return line;
            }();
            return root;
        }

        bool IsDemoItem(NavItem const& item, std::wstring const& root)
        {
            if (item.pinned)
            {
                // Solo le cartelle sotto la radice dimostrativa (confronto senza maiuscole, e al confine di una cartella).
                return item.parsing.size() > root.size() && _wcsnicmp(item.parsing.c_str(), root.c_str(), root.size()) == 0 &&
                       item.parsing[root.size()] == L'\\';
            }
            // Home, Galleria, Questo PC, Rete
            static constexpr const wchar_t* kShellItems[] = { L"F874310E-B6B7-47DC-BC84-B9E6B38F5903", L"E88865EA-0E1C-4E20-9AA6-EDCD0212C87C",
                                                              L"20D04FE0-3AEA-1069-A2D8-08002B30309D", L"F02C1A0D-BE21-4350-88B0-7367FC96EF3C" };
            for (const wchar_t* clsid : kShellItems)
            {
                if (StrStrIW(item.parsing.c_str(), clsid))
                {
                    return true;
                }
            }
            return false;
        }

        template <typename F>
        void Guard(const wchar_t* where, F&& action) noexcept
        {
            try
            {
                action();
            }
            catch (...)
            {
                XPX_MLOG(L"NavBar: errore in %s (0x%08X).", where, static_cast<unsigned>(winrt::to_hresult()));
            }
        }

        // PIDL assoluto dai suoi byte (da liberare con CoTaskMemFree).
        PIDLIST_ABSOLUTE PidlOf(std::vector<uint8_t> const& bytes)
        {
            if (bytes.size() < 2)
            {
                return nullptr;
            }
            auto* pidl = static_cast<PIDLIST_ABSOLUTE>(CoTaskMemAlloc(bytes.size()));
            if (pidl)
            {
                memcpy(pidl, bytes.data(), bytes.size());
            }
            return pidl;
        }

        // Browser della scheda (la sua finestra ShellTabWindowClass): il messaggio dà il puntatore senza
        // riferimento, da usare subito e sullo stesso thread.
        IShellBrowser* BrowserOf(HWND tab)
        {
            return tab ? reinterpret_cast<IShellBrowser*>(SendMessageW(tab, kGetShellBrowser, 0, 0)) : nullptr;
        }

        com_ptr<IShellItem> ShellItemOf(std::vector<uint8_t> const& idList)
        {
            com_ptr<IShellItem> item;
            if (const PIDLIST_ABSOLUTE pidl = PidlOf(idList))
            {
                SHCreateItemFromIDList(pidl, IID_PPV_ARGS(item.put()));
                CoTaskMemFree(pidl);
            }
            return item;
        }

        com_ptr<IContextMenu> ContextMenuOf(IShellItem* item)
        {
            com_ptr<IContextMenu> menu;
            if (item)
            {
                item->BindToHandler(nullptr, BHID_SFUIObject, IID_PPV_ARGS(menu.put()));
            }
            return menu;
        }

        com_ptr<IContextMenu> ContextMenuOf(std::vector<uint8_t> const& idList)
        {
            return ContextMenuOf(ShellItemOf(idList).get());
        }

        // Il menu di una voce come lo compone il riquadro di spostamento: quello della voce più quello "di
        // sfondo" della cartella (Nuovo...), senza verbi doppi.
        com_ptr<IContextMenu> ItemMenuOf(IShellItem* item, HWND owner)
        {
            return MakeMenuArray({ ContextMenuOf(item), BackgroundMenuOf(item, owner) });
        }

        // Da dove viene il clic destro, per il menu nuovo (come lo dice il riquadro al presenter).
        DWORD MenuInputFlags(muxi::RightTappedRoutedEventArgs const& args)
        {
            switch (args.PointerDeviceType())
            {
            case winrt::Microsoft::UI::Input::PointerDeviceType::Touch:
                return ModernMenu::kFromRightClick | ModernMenu::kFromTouch;
            case winrt::Microsoft::UI::Input::PointerDeviceType::Pen:
                return ModernMenu::kFromRightClick | ModernMenu::kFromPen;
            default:
                return ModernMenu::kFromRightClick;
            }
        }

        muxm::SolidColorBrush Transparent()
        {
            return muxm::SolidColorBrush(winrt::Microsoft::UI::Colors::Transparent());
        }

        // Linea che segna dove andrà la voce trascinata, col colore d'accento del tema (ripiego: il blu di Windows).
        mux::FrameworkElement LoadMarker(bool horizontal)
        {
            for (const wchar_t* brush : { L"{ThemeResource AccentFillColorDefaultBrush}", L"#FF0078D4" })
            {
                try
                {
                    std::wstring xaml = LR"(<Border xmlns="http://schemas.microsoft.com/winfx/2006/xaml/presentation" CornerRadius="1" )"
                                        LR"(IsHitTestVisible="False" Visibility="Collapsed" )";
                    xaml += horizontal ? L"Height=\"2\" " : L"Width=\"2\" Height=\"20\" ";
                    xaml += L"Background=\"" + std::wstring(brush) + L"\"/>";
                    return mux::Markup::XamlReader::Load(xaml).as<mux::FrameworkElement>();
                }
                catch (...)
                {
                }
            }
            return nullptr;
        }

        // Divisore come quelli del riquadro di sistema (che li disegna prima e dopo il gruppo delle cartelle
        // aggiunte): nella barra una linea verticale alta come i separatori della barra dei comandi, nell'elenco
        // "»" una orizzontale. Pennello dei divisori del tema (ripiego: grigio trasparente).
        mux::FrameworkElement LoadDivider(bool vertical)
        {
            for (const wchar_t* brush : { L"{ThemeResource DividerStrokeColorDefaultBrush}", L"#30808080" })
            {
                try
                {
                    std::wstring xaml = LR"(<Border xmlns="http://schemas.microsoft.com/winfx/2006/xaml/presentation" IsHitTestVisible="False" )";
                    xaml += vertical ? LR"(Width="1" Height="20" Margin="4,0,4,0" VerticalAlignment="Center" )"
                                     : LR"(Height="1" Margin="12,4,12,4" HorizontalAlignment="Stretch" )";
                    xaml += L"Background=\"" + std::wstring(brush) + L"\"/>";
                    return mux::Markup::XamlReader::Load(xaml).as<mux::FrameworkElement>();
                }
                catch (...)
                {
                }
            }
            return nullptr;
        }

        // Icona dei gruppi, del colore del testo del tema (bianca nel tema scuro, nera in quello chiaro).
        mux::FrameworkElement GroupIcon()
        {
            for (const wchar_t* brush : { L"{ThemeResource TextFillColorPrimaryBrush}", L"#FF808080" })
            {
                try
                {
                    std::wstring xaml = LR"(<Path xmlns="http://schemas.microsoft.com/winfx/2006/xaml/presentation" Width="16" Height="16" )"
                                        LR"(VerticalAlignment="Center" IsHitTestVisible="False" StrokeThickness="1" StrokeLineJoin="Round" )"
                                        LR"(StrokeStartLineCap="Round" StrokeEndLineCap="Round" Data=")";
                    xaml += kGroupIconData;
                    xaml += L"\" Stroke=\"" + std::wstring(brush) + L"\"/>";
                    return mux::Markup::XamlReader::Load(xaml).as<mux::FrameworkElement>();
                }
                catch (...)
                {
                }
            }
            return nullptr;
        }

        // Elenchi a discesa con l'aspetto di un menu: margini interni stretti, larghezza da menu.
        mux::Style ListPresenterStyle()
        {
            try
            {
                return mux::Markup::XamlReader::Load(LR"(<Style xmlns="http://schemas.microsoft.com/winfx/2006/xaml/presentation" TargetType="FlyoutPresenter">)"
                                                     LR"(<Setter Property="Padding" Value="0,4,0,4"/><Setter Property="MinWidth" Value="200"/>)"
                                                     LR"(<Setter Property="MaxWidth" Value="440"/></Style>)")
                    .as<mux::Style>();
            }
            catch (...)
            {
                XPX_MLOG(L"NavBar: stile dell'elenco non caricato (0x%08X).", static_cast<unsigned>(winrt::to_hresult()));
                return nullptr;
            }
        }

        // Pulsante principale di un riquadro (Salva): lo stile d'accento di WinUI, se c'è.
        muxc::Button AccentButton(std::wstring const& text)
        {
            try
            {
                auto button = mux::Markup::XamlReader::Load(LR"(<Button xmlns="http://schemas.microsoft.com/winfx/2006/xaml/presentation" )"
                                                            LR"(Style="{StaticResource AccentButtonStyle}"/>)")
                                  .as<muxc::Button>();
                button.Content(winrt::box_value(text));
                return button;
            }
            catch (...)
            {
                muxc::Button button;
                button.Content(winrt::box_value(text));
                return button;
            }
        }

        muxc::Image ImageOf(muxm::ImageSource const& source)
        {
            muxc::Image image;
            image.Width(kIconDip);
            image.Height(kIconDip);
            image.VerticalAlignment(mux::VerticalAlignment::Center);
            if (source)
            {
                image.Source(source);
            }
            return image;
        }

        // Icona e nome di una voce (nella barra e negli elenchi).
        muxc::StackPanel ItemContent(std::wstring const& name, mux::UIElement const& icon, double fontSize, double spacing, double maxText)
        {
            muxc::StackPanel content;
            content.Orientation(muxc::Orientation::Horizontal);
            content.Spacing(spacing);
            if (icon)
            {
                content.Children().Append(icon);
            }
            muxc::TextBlock text;
            text.Text(name);
            text.FontSize(fontSize);
            text.MaxWidth(maxText);
            text.TextTrimming(mux::TextTrimming::CharacterEllipsis);
            text.VerticalAlignment(mux::VerticalAlignment::Center);
            content.Children().Append(text);
            return content;
        }

        // Riga di un gruppo in un elenco: icona e nome, e la freccia del sottomenu a destra.
        muxc::Grid GroupRowContent(std::wstring const& name, mux::UIElement const& icon)
        {
            muxc::Grid grid;
            muxc::ColumnDefinition nameColumn;
            nameColumn.Width(mux::GridLengthHelper::FromValueAndType(1, mux::GridUnitType::Star));
            muxc::ColumnDefinition arrowColumn;
            arrowColumn.Width(mux::GridLengthHelper::Auto());
            grid.ColumnDefinitions().Append(nameColumn);
            grid.ColumnDefinitions().Append(arrowColumn);
            const auto content = ItemContent(name, icon, kTextSize, 12, kListMaxTextWidth);
            grid.Children().Append(content);
            muxc::FontIcon arrow;
            arrow.Glyph(L"");
            arrow.FontSize(12);
            arrow.Margin(mux::Thickness{ 12, 0, 0, 0 });
            arrow.VerticalAlignment(mux::VerticalAlignment::Center);
            muxc::Grid::SetColumn(arrow, 1);
            grid.Children().Append(arrow);
            return grid;
        }

        // Come i segnalibri: il nome, e per le cartelle aggiunte anche dove si trovano.
        std::wstring TooltipOf(NavItem const& item)
        {
            return item.pinned && item.parsing.rfind(L"::", 0) != 0 ? item.name + L"\n" + item.parsing : item.name;
        }

        bool ControlDown()
        {
            return GetKeyState(VK_CONTROL) < 0;
        }

        bool MiddleReleased(IInspectable const& sender, muxi::PointerRoutedEventArgs const& args)
        {
            const auto point = args.GetCurrentPoint(sender.as<mux::UIElement>());
            return point.Properties().PointerUpdateKind() == winrt::Microsoft::UI::Input::PointerUpdateKind::MiddleButtonReleased;
        }

        // "&" nei nomi dei menu Win32 è il tasto di scelta rapida: va raddoppiato.
        std::wstring MenuText(std::wstring const& text)
        {
            std::wstring result;
            for (const wchar_t c : text)
            {
                result.push_back(c);
                if (c == L'&')
                {
                    result.push_back(L'&');
                }
            }
            return result;
        }

        std::wstring CountText(size_t count, const wchar_t* one, const wchar_t* many)
        {
            return std::to_wstring(count) + L" " + (count == 1 ? one : many);
        }

        std::vector<std::wstring> Split(std::wstring const& text, wchar_t separator)
        {
            std::vector<std::wstring> parts;
            size_t start = 0;
            for (;;)
            {
                const size_t at = text.find(separator, start);
                parts.push_back(text.substr(start, at == std::wstring::npos ? std::wstring::npos : at - start));
                if (at == std::wstring::npos)
                {
                    return parts;
                }
                start = at + 1;
            }
        }
    }

    NavBar::NavBar(HWND main, WindowLayout& layout) : m_main(main), m_layout(layout)
    {
    }

    NavBar::~NavBar() = default;

    bool NavBar::Create(mux::ElementTheme theme)
    {
        m_theme = theme;
        try
        {
            m_root = muxc::Grid();
            m_root.Background(Transparent());
            m_root.RequestedTheme(theme);
            muxc::ColumnDefinition itemsColumn;
            itemsColumn.Width(mux::GridLengthHelper::FromValueAndType(1, mux::GridUnitType::Star));
            muxc::ColumnDefinition moreColumn;
            moreColumn.Width(mux::GridLengthHelper::Auto());
            m_root.ColumnDefinitions().Append(itemsColumn);
            m_root.ColumnDefinitions().Append(moreColumn);

            m_items = muxc::StackPanel();
            m_items.Orientation(muxc::Orientation::Horizontal);
            m_items.Spacing(kItemSpacing);
            m_items.VerticalAlignment(mux::VerticalAlignment::Center);
            m_items.Margin(mux::Thickness{ kSideMargin, 0, 0, 0 });
            mux::Automation::AutomationProperties::SetName(m_items, Tr(L"Folders bar", L"Barra delle cartelle"));
            m_root.Children().Append(m_items);

            // "»": le voci che non ci stanno (come la barra dei segnalibri di Chrome).
            m_overflow = muxc::Button();
            m_overflow.Background(Transparent());
            m_overflow.BorderThickness(mux::Thickness{ 0, 0, 0, 0 });
            m_overflow.Padding(mux::Thickness{ 0, 0, 0, 0 });
            m_overflow.Width(kOverflowWidth - kSideMargin);
            m_overflow.Height(kItemHeight);
            m_overflow.CornerRadius(mux::CornerRadius{ 4, 4, 4, 4 });
            m_overflow.VerticalAlignment(mux::VerticalAlignment::Center);
            m_overflow.Margin(mux::Thickness{ 0, 0, kSideMargin, 0 });
            muxc::TextBlock more;
            more.Text(L"»");
            more.FontSize(16);
            more.Margin(mux::Thickness{ 0, -3, 0, 0 });
            m_overflow.Content(more);
            mux::Automation::AutomationProperties::SetName(m_overflow, Tr(L"More folders", L"Altre cartelle"));
            muxc::ToolTipService::SetToolTip(m_overflow, winrt::box_value(Tr(L"More folders", L"Altre cartelle")));
            m_overflow.Visibility(mux::Visibility::Collapsed);
            muxc::Grid::SetColumn(m_overflow, 1);
            m_root.Children().Append(m_overflow);

            // Sopra a tutto (senza intercettare il mouse): la linea d'inserimento del trascinamento.
            m_barOverlay = muxc::Canvas();
            m_barOverlay.IsHitTestVisible(false);
            muxc::Grid::SetColumnSpan(m_barOverlay, 2);
            m_barMarker = LoadMarker(false);
            if (m_barMarker)
            {
                m_barOverlay.Children().Append(m_barMarker);
            }
            m_root.Children().Append(m_barOverlay);

            m_listStyle = ListPresenterStyle();
            // Elenco "»": i nostri pulsanti in un flyout (in una finestra a parte: l'isola è alta 34 DIP).
            m_overflowLevel = MakeLevel(L"", 0);
            m_overflow.Flyout(m_overflowLevel->flyout);

            m_source = winrt::Microsoft::UI::Xaml::Hosting::DesktopWindowXamlSource();
            m_source.Initialize(interop::WindowIdFromHwnd(m_main));
            m_source.Content(m_root);
            try
            {
                // Lo stesso materiale della barra del titolo (come il pannello delle schede verticali).
                muxm::MicaBackdrop mica;
                mica.Kind(winrt::Microsoft::UI::Composition::SystemBackdrops::MicaKind::BaseAlt);
                m_source.SystemBackdrop(mica);
            }
            catch (...)
            {
                XPX_MLOG(L"NavBar: Mica non disponibile (0x%08X).", static_cast<unsigned>(winrt::to_hresult()));
            }
            const auto bridge = m_source.SiteBridge();
            m_hwnd = interop::HwndFromWindowId(bridge.WindowId());
            bridge.Hide();
            SetPropW(m_hwnd, kNavBarProp, reinterpret_cast<HANDLE>(1));

            const auto queue = m_root.DispatcherQueue();
            m_hoverTimer = queue.CreateTimer();
            m_hoverTimer.IsRepeating(false);
            m_hoverTimer.Interval(kHoverDelay);
            m_dragTimer = queue.CreateTimer();
            m_dragTimer.IsRepeating(true);
            // Un fotogramma (gira solo durante un trascinamento): staccato, segue da qui il cursore.
            m_dragTimer.Interval(std::chrono::milliseconds(16));
            m_switchTimer = queue.CreateTimer();
            m_switchTimer.IsRepeating(true);
            // Un fotogramma: il gruppo sotto il puntatore si apre subito (gira solo con un elenco della barra aperto, e con
            // il puntatore fuori dalla barra esce al primo controllo).
            m_switchTimer.Interval(std::chrono::milliseconds(16));
        }
        catch (...)
        {
            XPX_MLOG(L"NavBar: impossibile creare l'isola XAML (0x%08X).", static_cast<unsigned>(winrt::to_hresult()));
            Destroy();
            return false;
        }

        const std::weak_ptr<NavBar> weak = weak_from_this();
        m_root.SizeChanged([weak](auto&&, auto&&) {
            if (const auto self = weak.lock())
            {
                Guard(L"SizeChanged", [&] { self->UpdateOverflow(); });
            }
        });
        // Clic destro sullo sfondo della barra (le voci lo gestiscono da sé): "Nuovo gruppo".
        m_root.RightTapped([weak](IInspectable const&, muxi::RightTappedRoutedEventArgs const& args) {
            const auto self = weak.lock();
            if (!self || args.Handled())
            {
                return;
            }
            args.Handled(true);
            const Point position = args.GetPosition(self->m_root);
            Guard(L"menu della barra", [&] { self->ShowBarMenu(position); });
        });
        m_hoverTimer.Tick([weak](auto&&, auto&&) {
            if (const auto self = weak.lock(); self && self->m_hoverAction)
            {
                const auto action = std::move(self->m_hoverAction);
                self->m_hoverAction = nullptr;
                Guard(L"puntatore fermo", [&] { action(); });
            }
        });
        m_dragTimer.Tick([weak](auto&&, auto&&) {
            if (const auto self = weak.lock())
            {
                Guard(L"trascinamento fermo", [&] { self->OnDragTimer(); });
            }
        });
        m_switchTimer.Tick([weak](auto&&, auto&&) {
            if (const auto self = weak.lock())
            {
                Guard(L"cambio di elenco", [&] { self->OnSwitchTimer(); });
            }
        });
        // Durante un trascinamento il puntatore è catturato dalla radice della barra o dell'elenco.
        AttachDragHandlers(m_root);

        // Cartelle trascinate da fuori (Esplora file, il desktop...): bersaglio OLE dell'isola. Se il ponte dell'isola ne ha già
        // uno (quello di WinUI per il trascinamento XAML, che la barra non usa) lo si sostituisce.
        m_dropTarget = MakeNavDropTarget(weak, m_hwnd);
        HRESULT dropResult = m_dropTarget ? RegisterDragDrop(m_hwnd, m_dropTarget.get()) : E_OUTOFMEMORY;
        bool replaced = false;
        if (dropResult == DRAGDROP_E_ALREADYREGISTERED)
        {
            RevokeDragDrop(m_hwnd);
            dropResult = RegisterDragDrop(m_hwnd, m_dropTarget.get());
            replaced = true;
        }
        if (FAILED(dropResult))
        {
            m_dropTarget = nullptr;
        }
        XPX_MLOG(L"NavBar: cartelle trascinabili da fuori sulla barra: 0x%08X%s.", static_cast<unsigned>(dropResult),
                 replaced ? L" (sostituito il bersaglio che il ponte dell'isola aveva già)" : L"");

        m_iconPixels = IconPixels();
        m_groups = NavGroups::Instance().Latest();
        NavItems::Instance().UseIconSize(m_iconPixels);
        NavItems::Instance().Start();
        OnTick();
        XPX_MLOG(L"NavBar: barra creata (finestra %llx, barra %llx).", reinterpret_cast<uintptr_t>(m_main), reinterpret_cast<uintptr_t>(m_hwnd));
        return true;
    }

    void NavBar::Destroy()
    {
        // La barra va via e il riquadro di sistema torna: con l'ordine di adesso.
        Guard(L"riquadro di sistema", [&] { SyncNativePanes(); });
        m_drag = {};
        if (m_hoverTimer)
        {
            m_hoverTimer.Stop();
        }
        if (m_dragTimer)
        {
            m_dragTimer.Stop();
        }
        if (m_switchTimer)
        {
            m_switchTimer.Stop();
        }
        if (m_openTimer)
        {
            m_openTimer.Stop();
            m_openTimer = nullptr;
        }
        m_openQueue.clear();
        m_hoverAction = nullptr;
        // Prima il menu nuovo (chiuso e scollegato: il presenter non richiamerà più la barra).
        if (m_modernMenu)
        {
            m_modernMenu.reset();
        }
        m_modernMenuTab = nullptr;
        if (m_menuContext)
        {
            IUnknown_SetSite(m_menuContext.get(), nullptr);
            m_menuContext = nullptr;
        }
        Guard(L"chiusura degli elenchi", [&] {
            CloseLevels(0);
            if (m_nameEditor)
            {
                m_nameEditor.Hide();
            }
        });
        if (m_dropTarget)
        {
            if (m_hwnd && IsWindow(m_hwnd))
            {
                RevokeDragDrop(m_hwnd);
            }
            m_dropTarget = nullptr;
        }
        m_external = {};
        m_pendingDrops.clear();
        if (m_hwnd && IsWindow(m_hwnd))
        {
            RemovePropW(m_hwnd, kNavBarProp);
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
        m_levels.clear();
        m_entries.clear();
        m_overflowLevel = nullptr;
        m_nameEditor = nullptr;
        m_nameBox = nullptr;
        m_root = nullptr;
        m_items = nullptr;
        m_overflow = nullptr;
        m_barOverlay = nullptr;
        m_barMarker = nullptr;
        m_listStyle = nullptr;
        m_hoverTimer = nullptr;
        m_dragTimer = nullptr;
        m_switchTimer = nullptr;
        m_snapshot = nullptr;
        m_itemByKey.clear();
        m_hwnd = nullptr;
    }

    int NavBar::IconPixels() const
    {
        const UINT dpi = m_main ? GetDpiForWindow(m_main) : 96;
        return MulDiv(kIconDip, static_cast<int>(dpi ? dpi : 96), 96);
    }

    void NavBar::OnTick()
    {
        if (!m_root)
        {
            return;
        }
        // Schermo con un'altra scala: icone della dimensione nuova (arrivano con una versione nuova delle voci).
        if (const int pixels = IconPixels(); pixels != m_iconPixels)
        {
            m_iconPixels = pixels;
            NavItems::Instance().UseIconSize(pixels);
        }
        // Voci o gruppi nuovi: non a metà di un trascinamento (la voce trascinata deve restare dov'è).
        const auto latest = NavItems::Instance().Latest();
        const auto groups = NavGroups::Instance().Latest();
        const bool newItems = latest && (!m_snapshot || latest->version != m_snapshot->version);
        if ((newItems || groups != m_groups) && !m_drag.armed)
        {
            if (newItems)
            {
                SetSnapshot(latest);
            }
            m_groups = groups;
            Rebuild();
            // Una volta per processo, con le voci lette e dei gruppi: l'ordine di Windows come la barra letta in profondità
            // (i gruppi creati con la versione di prima, o con l'ordine cambiato altrove, si sistemano subito). Il primo
            // livello resta com'è adesso (quello di Windows per le cartelle libere): si spostano solo quelle nei gruppi.
            if (m_snapshot && m_groups && !m_groups->groups.empty() && TakeStartupOrder())
            {
                const auto before = m_snapshot;
                SyncWindowsOrder(*m_groups, TopLevel(*m_groups, PinnedKeys()));
                if (m_snapshot != before)
                {
                    Rebuild();
                }
            }
        }
        // Cartelle appena aggiunte trascinandole da fuori: quando la lettura le porta, al loro posto.
        PlacePendingDrops();
        // Finestra appena attivata: si rilegge (una cartella può essere stata aggiunta o tolta altrove).
        const bool foreground = GetForegroundWindow() == m_main;
        const ULONGLONG now = GetTickCount64();
        if (foreground && !m_wasForeground && now - m_lastActivationRefresh > 30000)
        {
            m_lastActivationRefresh = now;
            NavItems::Instance().Refresh();
        }
        m_wasForeground = foreground;
    }

    void NavBar::SetTheme(mux::ElementTheme theme)
    {
        m_theme = theme;
        if (m_root)
        {
            m_root.RequestedTheme(theme);
        }
        if (m_overflowLevel && m_overflowLevel->root)
        {
            m_overflowLevel->root.RequestedTheme(theme);
        }
        for (auto const& level : m_levels)
        {
            if (level->root)
            {
                level->root.RequestedTheme(theme);
            }
        }
        if (!m_levels.empty() && m_levels.front()->open)
        {
            SetOpenHighlight(m_levels.front(), true); // il colore del tema nuovo
        }
    }

    void NavBar::SetVisible(bool shown)
    {
        if (m_source)
        {
            if (!shown)
            {
                EndDrag();
                CloseLevels(0);
                if (m_nameEditor)
                {
                    m_nameEditor.Hide();
                }
                if (m_modernMenu)
                {
                    m_modernMenu->Dismiss();
                }
            }
            // Solo con la finestra dell'isola ancora viva: Show/Hide su un ponte già chiuso (finestra in chiusura) è un
            // accesso non valido, che try/catch non intercetta (successo in VTabsNotepad).
            if (m_hwnd && IsWindow(m_hwnd))
            {
                const auto bridge = m_source.SiteBridge();
                shown ? bridge.Show() : bridge.Hide();
            }
        }
    }

    // --- Voci ------------------------------------------------------------------------------------------------

    void NavBar::SetSnapshot(std::shared_ptr<const NavSnapshot> snapshot)
    {
        if (snapshot && DemoRoot())
        {
            // Modalità dimostrativa: una copia con le sole voci ammesse (stessa versione, quindi non si rifiltra).
            auto filtered = std::make_shared<NavSnapshot>(*snapshot);
            std::erase_if(filtered->items, [](NavItem const& item) { return !IsDemoItem(item, *DemoRoot()); });
            snapshot = std::move(filtered);
        }
        m_snapshot = std::move(snapshot);
        m_itemByKey.clear();
        if (m_snapshot)
        {
            for (size_t i = 0; i < m_snapshot->items.size(); ++i)
            {
                m_itemByKey.emplace(FolderKey(m_snapshot->items[i].parsing), i);
            }
        }
    }

    std::optional<size_t> NavBar::ItemOf(std::wstring const& key) const
    {
        const auto found = m_itemByKey.find(key);
        if (found == m_itemByKey.end() || !m_snapshot || found->second >= m_snapshot->items.size())
        {
            return std::nullopt;
        }
        return found->second;
    }

    std::vector<std::wstring> NavBar::PinnedKeys() const
    {
        std::vector<std::wstring> keys;
        if (m_snapshot)
        {
            for (auto const& item : m_snapshot->items)
            {
                if (item.pinned)
                {
                    keys.push_back(FolderKey(item.parsing));
                }
            }
        }
        return keys;
    }

    std::unordered_set<std::wstring> NavBar::PresentKeys() const
    {
        const auto keys = PinnedKeys();
        return { keys.begin(), keys.end() };
    }

    bool NavBar::IsDraggable(std::wstring const& key) const
    {
        if (IsGroupKey(key))
        {
            return true;
        }
        const auto item = ItemOf(key);
        return item && m_snapshot->items[*item].pinned;
    }

    std::wstring NavBar::NameOf(std::wstring const& key) const
    {
        if (IsGroupKey(key))
        {
            const NavGroup* group = m_groups ? m_groups->Find(GroupIdOf(key)) : nullptr;
            return group ? group->name : std::wstring{};
        }
        const auto item = ItemOf(key);
        return item ? m_snapshot->items[*item].name : std::wstring{};
    }

    std::wstring NavBar::TooltipOfKey(std::wstring const& key) const
    {
        if (!IsGroupKey(key))
        {
            const auto item = ItemOf(key);
            return item ? TooltipOf(m_snapshot->items[*item]) : std::wstring{};
        }
        // Nome e contenuto (le cartelle che si vedono e i sottogruppi).
        size_t folders = 0;
        size_t groups = 0;
        if (m_groups)
        {
            for (auto const& child : VisibleChildren(*m_groups, GroupIdOf(key), PresentKeys()))
            {
                ++(IsGroupKey(child) ? groups : folders);
            }
        }
        std::wstring text = NameOf(key) + L"\n";
        if (folders == 0 && groups == 0)
        {
            return text + Tr(L"Empty group", L"Gruppo vuoto");
        }
        if (folders > 0)
        {
            text += CountText(folders, Tr(L"folder", L"cartella"), Tr(L"folders", L"cartelle"));
        }
        if (groups > 0)
        {
            text += (folders > 0 ? L", " : L"") + CountText(groups, Tr(L"group", L"gruppo"), Tr(L"groups", L"gruppi"));
        }
        return text;
    }

    void NavBar::RefreshGroupTooltip(muxc::Button const& button, std::wstring const& key) const
    {
        if (!button || !IsGroupKey(key))
        {
            return;
        }
        const winrt::hstring text{ TooltipOfKey(key) };
        const auto current = muxc::ToolTipService::GetToolTip(button);
        if (!current || winrt::unbox_value_or<winrt::hstring>(current, L"") != text)
        {
            muxc::ToolTipService::SetToolTip(button, winrt::box_value(text));
        }
    }

    std::wstring NavBar::LookOf(std::wstring const& key) const
    {
        if (IsGroupKey(key))
        {
            // Solo il nome: il contenuto (nel suggerimento) cambia senza rifare il pulsante, che altrimenti chiuderebbe
            // l'elenco aperto da lui (una cartella lasciata nell'elenco di un gruppo deve restarci, a vista).
            return L"g|" + NameOf(key);
        }
        const auto item = ItemOf(key);
        if (!item)
        {
            return L"?";
        }
        NavItem const& navItem = m_snapshot->items[*item];
        size_t hash = 0;
        for (auto const& icon : navItem.icons)
        {
            if (icon.size == m_iconPixels)
            {
                hash = std::hash<std::string_view>{}(std::string_view(reinterpret_cast<const char*>(icon.bgra.data()), icon.bgra.size()));
            }
        }
        return L"i|" + navItem.name + L"|" + std::to_wstring(hash) + (navItem.pinned ? L"|p" : L"");
    }

    muxm::ImageSource NavBar::MakeIcon(NavItem const& item) const
    {
        for (auto const& icon : item.icons)
        {
            if (icon.size == m_iconPixels && icon.bgra.size() == static_cast<size_t>(icon.size) * icon.size * 4)
            {
                muxmi::WriteableBitmap bitmap(icon.size, icon.size);
                const auto buffer = bitmap.PixelBuffer();
                memcpy(buffer.data(), icon.bgra.data(), icon.bgra.size());
                bitmap.Invalidate();
                return bitmap;
            }
        }
        return nullptr;
    }

    mux::UIElement NavBar::IconElement(std::wstring const& key) const
    {
        if (IsGroupKey(key))
        {
            return GroupIcon();
        }
        const auto item = ItemOf(key);
        return ImageOf(item ? MakeIcon(m_snapshot->items[*item]) : nullptr);
    }

    // --- Barra -----------------------------------------------------------------------------------------------

    void NavBar::Rebuild()
    {
        EndDrag();
        if (!m_snapshot || !m_groups)
        {
            return;
        }
        auto const& items = m_snapshot->items;
        // Il blocco delle cartelle aggiunte (con i gruppi) sta dove le mette il riquadro, anche se non ce n'è nessuna.
        size_t blockAt = items.size();
        for (size_t i = 0; i < items.size(); ++i)
        {
            if (items[i].pinned || items[i].sortIndex > kPinnedSortIndex)
            {
                blockAt = i;
                break;
            }
        }
        std::vector<std::pair<std::wstring, std::wstring>> wanted; // chiave, aspetto
        for (size_t i = 0; i < blockAt; ++i)
        {
            if (!items[i].pinned)
            {
                const auto key = FolderKey(items[i].parsing);
                wanted.emplace_back(key, LookOf(key));
            }
        }
        const size_t blockFirst = wanted.size();
        for (auto const& key : TopLevel(*m_groups, PinnedKeys()))
        {
            wanted.emplace_back(key, LookOf(key));
        }
        const size_t blockEnd = wanted.size();
        for (size_t i = blockAt; i < items.size(); ++i)
        {
            if (!items[i].pinned)
            {
                const auto key = FolderKey(items[i].parsing);
                wanted.emplace_back(key, LookOf(key));
            }
        }

        // Stesse voci con lo stesso aspetto: i pulsanti restano (e gli elenchi aperti da loro).
        bool same = wanted.size() == m_entries.size() && blockFirst == m_blockFirst && blockEnd == m_blockEnd;
        for (size_t e = 0; same && e < wanted.size(); ++e)
        {
            same = m_entries[e].key == wanted[e].first && m_entries[e].look == wanted[e].second;
        }
        if (!same)
        {
            m_entries.clear();
            m_items.Children().Clear();
            m_blockFirst = blockFirst;
            m_blockEnd = blockEnd;
            const bool anyInBlock = blockEnd > blockFirst;
            for (size_t e = 0; e < wanted.size(); ++e)
            {
                Entry entry = MakeEntry(wanted[e].first, wanted[e].second);
                // I divisori del riquadro di sistema: prima della prima voce del blocco delle cartelle aggiunte e prima
                // della prima voce dopo (verificato sul riquadro: le due voci hanno la riga doppia con la linea).
                if (anyInBlock && e > 0 && (e == blockFirst || e == blockEnd))
                {
                    entry.divider = LoadDivider(true);
                    if (entry.divider)
                    {
                        m_items.Children().Append(entry.divider);
                    }
                }
                m_items.Children().Append(entry.button);
                m_entries.push_back(std::move(entry));
            }
            const winrt::Windows::Foundation::Size unlimited{ std::numeric_limits<float>::infinity(), std::numeric_limits<float>::infinity() };
            for (auto& entry : m_entries)
            {
                entry.button.Measure(unlimited);
                entry.width = entry.button.DesiredSize().Width + kItemSpacing;
                if (entry.divider)
                {
                    entry.divider.Measure(unlimited); // margini compresi
                    entry.width += entry.divider.DesiredSize().Width + kItemSpacing;
                }
            }
            UpdateOverflow();
            size_t groups = 0;
            for (auto const& entry : m_entries)
            {
                groups += IsGroupKey(entry.key);
            }
            XPX_MLOG(L"NavBar: %zu voci (versione %llu, gruppi %llu), %zu nella barra, %zu gruppi al primo livello.", m_entries.size(), m_snapshot->version,
                     m_groups->version, m_visibleCount, groups);
        }
        else
        {
            for (auto const& entry : m_entries)
            {
                RefreshGroupTooltip(entry.button, entry.key);
            }
        }
        RefreshLevels(!same);
        // Il riquadro di sistema (nascosto dalla barra) nell'ordine di Windows: spegnendo la barra è già giusto.
        SyncNativePanes();
    }

    NavBar::Entry NavBar::MakeEntry(std::wstring const& key, std::wstring const& look)
    {
        Entry entry;
        entry.key = key;
        entry.look = look;
        const bool group = IsGroupKey(key);
        muxc::Button button;
        button.Background(Transparent());
        button.BorderThickness(mux::Thickness{ 0, 0, 0, 0 });
        button.Padding(mux::Thickness{ 8, 0, 8, 0 });
        button.Height(kItemHeight);
        button.MinWidth(0);
        button.VerticalAlignment(mux::VerticalAlignment::Center);
        button.CornerRadius(mux::CornerRadius{ 4, 4, 4, 4 });
        const std::wstring name = NameOf(key);
        button.Content(ItemContent(name, IconElement(key), kTextSize, 6, kMaxTextWidth));
        muxc::ToolTipService::SetToolTip(button, winrt::box_value(TooltipOfKey(key)));
        mux::Automation::AutomationProperties::SetName(button, name);

        const std::weak_ptr<NavBar> weak = weak_from_this();
        if (group)
        {
            // Clic: il suo elenco (il flyout del pulsante). Con l'elenco di un altro gruppo aperto basta passarci sopra
            // (come la barra dei segnalibri di Chrome): lo fa OnSwitchTimer, perché con un elenco aperto i pulsanti della
            // barra non ricevono il puntatore.
            mux::Automation::AutomationProperties::SetAutomationId(button, L"NavGroup");
            entry.level = MakeLevel(std::wstring{ GroupIdOf(key) }, 0);
            button.Flyout(entry.level->flyout);
        }
        else
        {
            button.Click([weak, key](auto&&, auto&&) {
                if (const auto self = weak.lock())
                {
                    Guard(L"clic", [&] { self->OnItemClick(key, ControlDown()); });
                }
            });
        }
        button.RightTapped([weak, key](IInspectable const& sender, muxi::RightTappedRoutedEventArgs const& args) {
            args.Handled(true);
            if (const auto self = weak.lock())
            {
                Guard(L"clic destro", [&] { self->OnRightTapped(key, sender.as<mux::FrameworkElement>(), args); });
            }
        });
        button.AddHandler(mux::UIElement::PointerReleasedEvent(),
                          winrt::box_value(muxi::PointerEventHandler([weak, key](IInspectable const& sender, muxi::PointerRoutedEventArgs const& args) {
                              const auto self = weak.lock();
                              if (!self)
                              {
                                  return;
                              }
                              Guard(L"clic centrale", [&] {
                                  if (MiddleReleased(sender, args))
                                  {
                                      args.Handled(true);
                                      self->OnItemClick(key, true);
                                  }
                              });
                          })),
                          true);
        // Le cartelle aggiunte e i gruppi si spostano trascinandoli (le voci della shell no: per loro la pressione
        // conta solo nel registro dettagliato).
        button.AddHandler(mux::UIElement::PointerPressedEvent(),
                          winrt::box_value(muxi::PointerEventHandler([weak, key](IInspectable const& sender, muxi::PointerRoutedEventArgs const& args) {
                              const auto self = weak.lock();
                              if (!self)
                              {
                                  return;
                              }
                              if (self->IsDraggable(key))
                              {
                                  Guard(L"pressione", [&] { self->ArmDrag(key, sender.as<muxc::Button>(), self->m_root, -1, args); });
                              }
                              else if (g_dragDebug)
                              {
                                  XPX_MLOG(L"NavBar[trascinamento]: pressione su una voce non trascinabile.");
                              }
                          })),
                          true);
        entry.button = button;
        return entry;
    }

    void NavBar::UpdateOverflow()
    {
        if (!m_root)
        {
            return;
        }
        const double total = m_root.ActualWidth();
        if (total <= 0)
        {
            return; // non ancora disposta: arriva SizeChanged
        }
        const double room = total - 2 * kSideMargin;
        double all = 0;
        for (auto const& entry : m_entries)
        {
            all += entry.width;
        }
        size_t visible = m_entries.size();
        if (all > room)
        {
            const double available = room - kOverflowWidth;
            double used = 0;
            visible = 0;
            for (auto const& entry : m_entries)
            {
                if (used + entry.width > available)
                {
                    break;
                }
                used += entry.width;
                ++visible;
            }
        }
        const bool changed = visible != m_visibleCount;
        for (size_t i = 0; i < m_entries.size(); ++i)
        {
            const auto shown = i < visible ? mux::Visibility::Visible : mux::Visibility::Collapsed;
            m_entries[i].button.Visibility(shown);
            if (m_entries[i].divider)
            {
                m_entries[i].divider.Visibility(shown); // il divisore va e viene con la sua voce
            }
        }
        m_overflow.Visibility(visible < m_entries.size() ? mux::Visibility::Visible : mux::Visibility::Collapsed);
        m_visibleCount = visible;
        // Elenchi aperti da voci che ora sono nascoste (o nell'elenco "»" che è cambiato): si chiudono o si aggiornano.
        if (changed && !m_levels.empty() && !m_drag.armed)
        {
            RefreshLevels(true);
        }
    }

    // --- Elenchi ("»" e gruppi) ------------------------------------------------------------------------------

    std::shared_ptr<NavBar::Level> NavBar::MakeLevel(std::wstring const& group, size_t depth)
    {
        auto level = std::make_shared<Level>();
        level->group = group;
        level->depth = depth;
        level->flyout = muxc::Flyout();
        level->flyout.Placement(group.empty() ? muxcp::FlyoutPlacementMode::BottomEdgeAlignedRight
                                : depth == 0  ? muxcp::FlyoutPlacementMode::BottomEdgeAlignedLeft
                                              : muxcp::FlyoutPlacementMode::RightEdgeAlignedTop);
        level->flyout.ShouldConstrainToRootBounds(false);
        if (m_listStyle)
        {
            level->flyout.FlyoutPresenterStyle(m_listStyle);
        }
        const std::weak_ptr<NavBar> weak = weak_from_this();
        const std::weak_ptr<Level> weakLevel = level;
        level->flyout.Opening([weak, weakLevel](auto&&, auto&&) {
            const auto self = weak.lock();
            const auto level = weakLevel.lock();
            if (self && level)
            {
                Guard(L"elenco in apertura", [&] { self->OnLevelOpening(level); });
            }
        });
        level->flyout.Opened([weakLevel](auto&&, auto&&) {
            if (const auto level = weakLevel.lock())
            {
                level->open = true;
            }
        });
        level->flyout.Closed([weak, weakLevel](auto&&, auto&&) {
            const auto self = weak.lock();
            const auto level = weakLevel.lock();
            if (self && level)
            {
                Guard(L"elenco chiuso", [&] { self->OnLevelClosed(level); });
            }
        });
        return level;
    }

    void NavBar::BuildLevel(Level& level)
    {
        level.root = muxc::Grid();
        level.root.Background(Transparent());
        level.root.RequestedTheme(m_theme);
        level.list = muxc::StackPanel();
        level.list.Orientation(muxc::Orientation::Vertical);
        level.list.XYFocusKeyboardNavigation(mux::Input::XYFocusKeyboardNavigationMode::Enabled);
        level.root.Children().Append(level.list);
        muxc::Canvas overlay;
        overlay.IsHitTestVisible(false);
        level.marker = LoadMarker(true);
        if (level.marker)
        {
            overlay.Children().Append(level.marker);
        }
        level.root.Children().Append(overlay);
        // Con molte voci l'elenco scorre invece di uscire dallo schermo (come il menu di Chrome sotto "»").
        level.scroll = muxc::ScrollViewer();
        level.scroll.VerticalScrollBarVisibility(muxc::ScrollBarVisibility::Auto);
        level.scroll.HorizontalScrollBarVisibility(muxc::ScrollBarVisibility::Disabled);
        level.scroll.Content(level.root);
        level.flyout.Content(level.scroll);
        AttachDragHandlers(level.root);
        // Freccia sinistra in un sottogruppo: si torna alla riga da cui si è aperto (come nei menu).
        if (level.depth > 0)
        {
            const winrt::weak_ref<muxc::Flyout> flyout = level.flyout;
            level.root.KeyDown([flyout](IInspectable const&, muxi::KeyRoutedEventArgs const& args) {
                if (args.Key() == winrt::Windows::System::VirtualKey::Left)
                {
                    if (const auto strong = flyout.get())
                    {
                        args.Handled(true);
                        strong.Hide();
                    }
                }
            });
        }
    }

    void NavBar::OnLevelOpening(std::shared_ptr<Level> const& level)
    {
        if (!level->root)
        {
            BuildLevel(*level);
        }
        level->root.RequestedTheme(m_theme);
        // Al posto di quelli della stessa profondità (e più interni): un gruppo alla volta per livello.
        while (m_levels.size() > level->depth)
        {
            const auto last = m_levels.back();
            m_levels.pop_back();
            if (last != level && last->open)
            {
                last->flyout.Hide();
            }
        }
        m_levels.push_back(level);
        FillLevel(level);
        if (level->depth == 0)
        {
            SetOpenHighlight(level, true);
            // Aperto dalla barra: da qui si può passare agli altri gruppi della barra solo col puntatore.
            if (m_switchTimer && !m_switchTimer.IsRunning())
            {
                m_switchTimer.Start();
            }
        }
    }

    void NavBar::OnSwitchTimer()
    {
        if (m_levels.empty() || m_drag.armed || !m_hwnd)
        {
            if (m_levels.empty() && m_switchTimer)
            {
                m_switchTimer.Stop();
                m_testCursor.reset(); // (comando di prova: vale finché resta aperto un elenco)
            }
            return;
        }
        POINT cursor{};
        if (m_testCursor)
        {
            cursor = *m_testCursor; // comando di prova
        }
        else
        {
            GetCursorPos(&cursor);
        }
        std::shared_ptr<Level> target;
        mux::FrameworkElement anchor{ nullptr };
        std::wstring key;
        if (const auto entry = EntryAt(cursor))
        {
            Entry const& under = m_entries[*entry];
            if (under.level)
            {
                target = under.level;
                anchor = under.button;
                key = under.key;
            }
        }
        else if (m_overflow && m_overflow.Visibility() == mux::Visibility::Visible && IsUnder(m_overflow, cursor))
        {
            target = m_overflowLevel;
            anchor = m_overflow;
            key = L"»";
        }
        if (!target || target == m_levels.front() || target->open)
        {
            return;
        }
        // Subito e senza animazioni, come Chrome: l'elenco aperto sparisce e quello nuovo compare nello stesso momento.
        // (Le animazioni tornano quando l'elenco si chiude: aperto col clic resta animato come prima.)
        try
        {
            m_levels.front()->flyout.AreOpenCloseAnimationsEnabled(false);
            target->flyout.AreOpenCloseAnimationsEnabled(false);
        }
        catch (...)
        {
        }
        if (m_testCursor)
        {
            XPX_MLOG(L"NavBar: elenco cambiato passandoci sopra (prova), %llu ms dopo il comando.", GetTickCount64() - m_testCursorSince);
        }
        else
        {
            XPX_MVLOG(L"NavBar: elenco cambiato passandoci sopra (%s).", key == L"»" ? L"»" : L"gruppo");
        }
        ShowLevel(target, anchor, false);
    }

    bool NavBar::IsUnder(mux::FrameworkElement const& element, POINT screen) const
    {
        if (!element || !m_hwnd || !m_main)
        {
            return false;
        }
        RECT island{};
        if (!GetWindowRect(m_hwnd, &island) || !PtInRect(&island, screen))
        {
            return false;
        }
        const double scale = GetDpiForWindow(m_main) / 96.0;
        const double x = (screen.x - island.left) / scale;
        const double y = (screen.y - island.top) / scale;
        const auto origin = element.TransformToVisual(nullptr).TransformPoint({ 0, 0 });
        return x >= origin.X && x < origin.X + element.ActualWidth() && y >= origin.Y && y < origin.Y + element.ActualHeight();
    }

    void NavBar::SetOpenHighlight(std::shared_ptr<Level> const& level, bool on)
    {
        if (!level || level->depth != 0)
        {
            return;
        }
        muxc::Button button{ nullptr };
        if (level == m_overflowLevel)
        {
            button = m_overflow;
        }
        else
        {
            for (auto const& entry : m_entries)
            {
                if (entry.level == level)
                {
                    button = entry.button;
                    break;
                }
            }
        }
        if (!button)
        {
            return;
        }
        if (on)
        {
            button.Background(OpenHighlightBrush());
            return;
        }
        button.Background(Transparent());
        // Con un elenco aperto i pulsanti della barra non ricevono il puntatore: quello lasciato per un altro gruppo può
        // essere rimasto nello stato "puntatore sopra". Se il cursore non c'è, torna normale (resta evidenziato solo quello
        // dell'elenco aperto).
        POINT cursor{};
        if (GetCursorPos(&cursor) && !IsUnder(button, cursor))
        {
            mux::VisualStateManager::GoToState(button, L"Normal", false);
        }
    }

    muxm::SolidColorBrush NavBar::OpenHighlightBrush() const
    {
        bool dark = m_theme == mux::ElementTheme::Dark;
        if (m_theme == mux::ElementTheme::Default)
        {
            try
            {
                dark = mux::Application::Current().RequestedTheme() == mux::ApplicationTheme::Dark;
            }
            catch (...)
            {
            }
        }
        // Lo sfondo dei pulsanti al passaggio del mouse (ButtonBackgroundPointerOver = ControlFillColorSecondary di Fluent).
        return muxm::SolidColorBrush(dark ? winrt::Windows::UI::Color{ 0x15, 0xFF, 0xFF, 0xFF } : winrt::Windows::UI::Color{ 0x80, 0xF9, 0xF9, 0xF9 });
    }

    void NavBar::OnLevelClosed(std::shared_ptr<Level> const& level)
    {
        level->open = false;
        SetOpenHighlight(level, false);
        try
        {
            level->flyout.AreOpenCloseAnimationsEnabled(true); // (tolte se si era passati a lui o da lui passandoci sopra)
        }
        catch (...)
        {
        }
        const auto at = std::find(m_levels.begin(), m_levels.end(), level);
        if (at != m_levels.end())
        {
            const std::vector<std::shared_ptr<Level>> deeper(at + 1, m_levels.end());
            m_levels.erase(at, m_levels.end());
            for (auto const& inner : deeper)
            {
                if (inner->open)
                {
                    inner->flyout.Hide();
                }
            }
        }
        if (level->marker)
        {
            level->marker.Visibility(mux::Visibility::Collapsed);
        }
        // La voce trascinata era lì: non c'è più.
        if (m_drag.armed && level->root && m_drag.holder == level->root)
        {
            EndDrag();
        }
    }

    std::vector<std::pair<std::wstring, std::wstring>> NavBar::LevelContent(Level const& level) const
    {
        std::vector<std::pair<std::wstring, std::wstring>> content;
        if (level.group.empty())
        {
            for (size_t e = m_visibleCount; e < m_entries.size(); ++e)
            {
                // Il divisore conta: anche lui sta nell'elenco.
                content.emplace_back(m_entries[e].key, m_entries[e].look + (m_entries[e].divider && e > m_visibleCount ? L"|d" : L""));
            }
        }
        else if (m_groups)
        {
            for (auto const& key : VisibleChildren(*m_groups, level.group, PresentKeys()))
            {
                content.emplace_back(key, LookOf(key));
            }
        }
        return content;
    }

    void NavBar::FillLevel(std::shared_ptr<Level> const& level)
    {
        level->list.Children().Clear();
        level->rows.clear();
        // Al più fino al fondo dell'area di lavoro dello schermo (meno bordi e margini del flyout).
        RECT island{};
        MONITORINFO monitor{ sizeof(monitor) };
        if (m_hwnd && GetWindowRect(m_hwnd, &island) && GetMonitorInfoW(MonitorFromWindow(m_main, MONITOR_DEFAULTTONEAREST), &monitor))
        {
            const double scale = GetDpiForWindow(m_main) / 96.0;
            const double room = level->depth == 0 ? (monitor.rcWork.bottom - island.bottom) / scale - 24 : (monitor.rcWork.bottom - monitor.rcWork.top) / scale - 48;
            level->scroll.MaxHeight(std::max(160.0, room));
        }
        if (!m_snapshot || !m_groups)
        {
            return;
        }
        if (level->group.empty())
        {
            level->firstEntry = m_visibleCount;
            mux::Automation::AutomationProperties::SetName(level->list, Tr(L"List of more folders", L"Elenco delle altre cartelle"));
        }
        else
        {
            const NavGroup* group = m_groups->Find(level->group);
            mux::Automation::AutomationProperties::SetName(level->list, Tr(L"Group ", L"Gruppo ") + (group ? group->name : std::wstring{}));
        }
        const auto content = LevelContent(*level);
        for (size_t r = 0; r < content.size(); ++r)
        {
            // "»": lo stesso divisore della barra, orizzontale; non in cima all'elenco (non separerebbe nulla).
            if (level->group.empty() && content[r].second.ends_with(L"|d"))
            {
                if (const auto divider = LoadDivider(false))
                {
                    level->list.Children().Append(divider);
                }
            }
            Row row = MakeRow(level, content[r].first, content[r].second);
            level->list.Children().Append(row.button);
            level->rows.push_back(std::move(row));
        }
        if (!level->group.empty() && level->rows.empty())
        {
            // Come Chrome: "(vuoto)".
            muxc::TextBlock empty;
            empty.Text(Tr(L"(empty)", L"(vuoto)"));
            empty.FontSize(kTextSize);
            empty.Opacity(0.6);
            empty.Margin(mux::Thickness{ 16, 6, 16, 6 });
            mux::Automation::AutomationProperties::SetName(empty, Tr(L"Empty group", L"Gruppo vuoto"));
            level->list.Children().Append(empty);
        }
    }

    void NavBar::RefreshLevels(bool barRebuilt)
    {
        if (m_levels.empty())
        {
            return;
        }
        // I gruppi aperti dalla barra erano attaccati ai pulsanti vecchi: si chiudono. "»" resta (il suo pulsante non
        // cambia) se serve ancora.
        if (barRebuilt && (!m_levels.front()->group.empty() || m_overflow.Visibility() != mux::Visibility::Visible))
        {
            CloseLevels(0);
            return;
        }
        for (size_t i = 0; i < m_levels.size(); ++i)
        {
            const auto level = m_levels[i];
            if (!level->group.empty() && (!m_groups || !m_groups->Find(level->group)))
            {
                CloseLevels(i);
                return;
            }
            const auto content = LevelContent(*level);
            bool same = content.size() == level->rows.size();
            for (size_t r = 0; same && r < content.size(); ++r)
            {
                same = level->rows[r].key == content[r].first && level->rows[r].look == content[r].second;
            }
            if (!same)
            {
                // Righe nuove: i sottogruppi aperti dalle righe vecchie si chiudono.
                CloseLevels(i + 1);
                FillLevel(level);
                return;
            }
            for (auto const& row : level->rows)
            {
                RefreshGroupTooltip(row.button, row.key);
            }
        }
    }

    NavBar::Row NavBar::MakeRow(std::shared_ptr<Level> const& level, std::wstring const& key, std::wstring const& look)
    {
        Row row;
        row.key = key;
        row.look = look;
        const bool group = IsGroupKey(key);
        muxc::Button button;
        button.Background(Transparent());
        button.BorderThickness(mux::Thickness{ 0, 0, 0, 0 });
        button.HorizontalAlignment(mux::HorizontalAlignment::Stretch);
        button.HorizontalContentAlignment(group ? mux::HorizontalAlignment::Stretch : mux::HorizontalAlignment::Left);
        button.Padding(mux::Thickness{ 12, 0, 12, 0 });
        button.Margin(mux::Thickness{ 4, 0, 4, 0 });
        button.MinHeight(kListItemHeight);
        button.CornerRadius(mux::CornerRadius{ 4, 4, 4, 4 });
        const std::wstring name = NameOf(key);
        if (group)
        {
            button.Content(GroupRowContent(name, IconElement(key)));
        }
        else
        {
            button.Content(ItemContent(name, IconElement(key), kTextSize, 12, kListMaxTextWidth));
        }
        muxc::ToolTipService::SetToolTip(button, winrt::box_value(TooltipOfKey(key)));
        mux::Automation::AutomationProperties::SetName(button, name);

        // Come nella barra: clic, clic centrale (o Ctrl+clic) in una scheda nuova, clic destro col menu.
        const std::weak_ptr<NavBar> weak = weak_from_this();
        const std::weak_ptr<Level> weakLevel = level;
        if (group)
        {
            // Il sottogruppo si apre a lato: col clic (il flyout del pulsante), con la freccia destra o fermandosi sopra.
            mux::Automation::AutomationProperties::SetAutomationId(button, L"NavGroupRow");
            row.level = MakeLevel(std::wstring{ GroupIdOf(key) }, level->depth + 1);
            button.Flyout(row.level->flyout);
            const std::weak_ptr<Level> weakRowLevel = row.level;
            button.KeyDown([weak, weakRowLevel](IInspectable const& sender, muxi::KeyRoutedEventArgs const& args) {
                const auto self = weak.lock();
                const auto rowLevel = weakRowLevel.lock();
                if (self && rowLevel && args.Key() == winrt::Windows::System::VirtualKey::Right)
                {
                    args.Handled(true);
                    Guard(L"freccia destra", [&] { self->ShowLevel(rowLevel, sender.as<mux::FrameworkElement>(), false); });
                }
            });
            button.PointerEntered([weak, weakLevel, weakRowLevel](IInspectable const& sender, auto&&) {
                const auto self = weak.lock();
                const auto level = weakLevel.lock();
                if (self && level)
                {
                    Guard(L"puntatore sul gruppo", [&] { self->HoverRow(level, weakRowLevel.lock(), sender.as<muxc::Button>()); });
                }
            });
        }
        else
        {
            button.Click([weak, key](auto&&, auto&&) {
                if (const auto self = weak.lock())
                {
                    Guard(L"clic nell'elenco", [&] { self->OnItemClick(key, ControlDown()); });
                }
            });
            button.PointerEntered([weak, weakLevel](IInspectable const& sender, auto&&) {
                const auto self = weak.lock();
                const auto level = weakLevel.lock();
                if (self && level)
                {
                    Guard(L"puntatore sulla voce", [&] { self->HoverRow(level, nullptr, sender.as<muxc::Button>()); });
                }
            });
        }
        button.RightTapped([weak, key](IInspectable const& sender, muxi::RightTappedRoutedEventArgs const& args) {
            args.Handled(true);
            if (const auto self = weak.lock())
            {
                Guard(L"clic destro nell'elenco", [&] { self->OnRightTapped(key, sender.as<mux::FrameworkElement>(), args); });
            }
        });
        button.AddHandler(mux::UIElement::PointerReleasedEvent(),
                          winrt::box_value(muxi::PointerEventHandler([weak, key](IInspectable const& sender, muxi::PointerRoutedEventArgs const& args) {
                              const auto self = weak.lock();
                              if (!self)
                              {
                                  return;
                              }
                              Guard(L"clic centrale nell'elenco", [&] {
                                  if (MiddleReleased(sender, args))
                                  {
                                      args.Handled(true);
                                      self->OnItemClick(key, true);
                                  }
                              });
                          })),
                          true);
        button.AddHandler(mux::UIElement::PointerPressedEvent(),
                          winrt::box_value(muxi::PointerEventHandler([weak, weakLevel, key](IInspectable const& sender, muxi::PointerRoutedEventArgs const& args) {
                              const auto self = weak.lock();
                              const auto level = weakLevel.lock();
                              if (self && level && level->root && self->IsDraggable(key))
                              {
                                  Guard(L"pressione nell'elenco", [&] {
                                      self->ArmDrag(key, sender.as<muxc::Button>(), level->root, static_cast<int>(level->depth), args);
                                  });
                              }
                          })),
                          true);
        row.button = button;
        return row;
    }

    void NavBar::CloseLevels(size_t fromDepth)
    {
        while (m_levels.size() > fromDepth)
        {
            const auto last = m_levels.back();
            m_levels.pop_back();
            if (last->open)
            {
                last->flyout.Hide();
            }
        }
    }

    void NavBar::ShowLevel(std::shared_ptr<Level> const& level, mux::FrameworkElement const& anchor, bool transient)
    {
        if (!level || !anchor)
        {
            return;
        }
        muxcp::FlyoutShowOptions options;
        options.Placement(level->flyout.Placement());
        // Aperto passandoci sopra: senza prendere il fuoco (come i sottomenu).
        options.ShowMode(transient ? muxcp::FlyoutShowMode::Transient : muxcp::FlyoutShowMode::Standard);
        level->flyout.ShowAt(anchor, options);
    }

    void NavBar::HoverRow(std::shared_ptr<Level> const& level, std::shared_ptr<Level> const& rowLevel, muxc::Button const& button)
    {
        if (m_drag.armed || !m_hoverTimer)
        {
            return;
        }
        // Fermi su una riga: dopo un attimo si apre il suo gruppo a lato, o si chiude quello aperto da un'altra riga.
        m_hoverTimer.Stop();
        const std::weak_ptr<NavBar> weak = weak_from_this();
        const std::weak_ptr<Level> weakLevel = level;
        const std::weak_ptr<Level> weakRowLevel = rowLevel;
        const winrt::weak_ref<muxc::Button> weakButton = button;
        m_hoverAction = [weak, weakLevel, weakRowLevel, weakButton] {
            const auto self = weak.lock();
            const auto level = weakLevel.lock();
            if (!self || !level || !level->open || self->m_drag.armed)
            {
                return;
            }
            if (const auto rowLevel = weakRowLevel.lock())
            {
                if (!rowLevel->open)
                {
                    if (const auto anchor = weakButton.get())
                    {
                        self->ShowLevel(rowLevel, anchor, true);
                    }
                }
            }
            else
            {
                self->CloseLevels(level->depth + 1);
            }
        };
        m_hoverTimer.Start();
    }

    mux::FrameworkElement NavBar::AnchorOf(std::wstring const& key) const
    {
        for (size_t e = 0; e < m_entries.size() && e < m_visibleCount; ++e)
        {
            if (m_entries[e].key == key)
            {
                return m_entries[e].button;
            }
        }
        for (auto const& level : m_levels)
        {
            if (!level->open)
            {
                continue;
            }
            for (auto const& row : level->rows)
            {
                if (row.key == key)
                {
                    return row.button;
                }
            }
        }
        return nullptr;
    }

    bool NavBar::RevealGroup(std::wstring const& id)
    {
        if (!m_groups || !m_groups->Find(id))
        {
            return false;
        }
        for (auto const& level : m_levels)
        {
            if (level->group == id && level->open)
            {
                return true;
            }
        }
        // Un passo alla volta: il gruppo se si vede, altrimenti prima chi lo contiene (o "»").
        const std::wstring key = GroupKey(id);
        for (size_t e = 0; e < m_entries.size() && e < m_visibleCount; ++e)
        {
            if (m_entries[e].key == key)
            {
                ShowLevel(m_entries[e].level, m_entries[e].button, false);
                return false;
            }
        }
        for (auto const& level : m_levels)
        {
            for (auto const& row : level->rows)
            {
                if (row.key == key && level->open)
                {
                    ShowLevel(row.level, row.button, false);
                    return false;
                }
            }
        }
        const std::wstring parent = m_groups->ParentOf(key);
        if (!parent.empty())
        {
            RevealGroup(parent);
        }
        else if (m_overflow.Visibility() == mux::Visibility::Visible && !m_overflowLevel->open)
        {
            ShowLevel(m_overflowLevel, m_overflow, false);
        }
        return false;
    }

    // --- Azioni ----------------------------------------------------------------------------------------------

    void NavBar::OnItemClick(std::wstring const& key, bool newTab)
    {
        if (IsGroupKey(key))
        {
            // Clic centrale su un gruppo: tutte le sue cartelle in schede nuove (dopo l'evento: può chiedere conferma).
            const std::weak_ptr<NavBar> weak = weak_from_this();
            const std::wstring id{ GroupIdOf(key) };
            m_root.DispatcherQueue().TryEnqueue([weak, id] {
                if (const auto self = weak.lock())
                {
                    Guard(L"apri tutte", [&] {
                        self->CloseLevels(0);
                        self->OpenAll(id);
                    });
                }
            });
            return;
        }
        CloseLevels(0);
        if (newTab && ItemOf(key))
        {
            QueueNewTab(key);
        }
        else if (const auto item = ItemOf(key))
        {
            Open(*item, false);
        }
    }

    void NavBar::OnRightTapped(std::wstring const& key, mux::FrameworkElement const& anchor, muxi::RightTappedRoutedEventArgs const& args)
    {
        POINT point{};
        GetCursorPos(&point);
        if (IsGroupKey(key))
        {
            ShowGroupMenu(std::wstring{ GroupIdOf(key) }, anchor, args.GetPosition(anchor));
            return;
        }
        const DWORD flags = MenuInputFlags(args);
        if (const auto item = ItemOf(key))
        {
            CloseLevels(0);
            ShowContextMenu(*item, point, flags);
        }
    }

    void NavBar::Open(size_t index, bool newTab, HWND fromTab)
    {
        if (!m_snapshot || index >= m_snapshot->items.size())
        {
            return;
        }
        NavItem const& item = m_snapshot->items[index];
        const HWND tab = fromTab && IsWindow(fromTab) ? fromTab : m_layout.ActiveContent();
        IShellBrowser* browser = BrowserOf(tab);
        if (!browser)
        {
            XPX_MLOG(L"NavBar: la scheda attiva non ha un browser: voce %zu non aperta.", index);
            return;
        }
        HRESULT result = E_FAIL;
        // La cartella VERA (dal nome di analisi), non l'elemento dentro Accesso rapido da cui la barra la conosce:
        // aperto quello, Esplora file registra la visita con la sua copia dell'elenco delle fissate, che può essere
        // vecchia, e riscrive in Windows l'ordine di prima (annullando uno spostamento fatto dalla barra).
        PIDLIST_ABSOLUTE target = nullptr;
        if (FAILED(SHParseDisplayName(item.parsing.c_str(), nullptr, &target, 0, nullptr)))
        {
            target = PidlOf(item.idList);
        }
        if (!newTab)
        {
            if (target)
            {
                result = browser->BrowseObject(target, SBSP_SAMEBROWSER | SBSP_ABSOLUTE);
            }
        }
        else if (const auto menu = ContextMenuOf([&] {
                     com_ptr<IShellItem> shellItem;
                     if (target)
                     {
                         SHCreateItemFromIDList(target, IID_PPV_ARGS(shellItem.put()));
                     }
                     return shellItem;
                 }().get()))
        {
            // Come "Apri in una nuova scheda" del menu della cartella: il verbo della shell, con il browser
            // della scheda come sito (la scheda nuova si apre in questa finestra).
            IUnknown_SetSite(menu.get(), browser);
            if (const HMENU popup = CreatePopupMenu())
            {
                if (SUCCEEDED(menu->QueryContextMenu(popup, 0, 1, 0x7FFF, CMF_NORMAL)))
                {
                    CMINVOKECOMMANDINFOEX info{ sizeof(info) };
                    info.fMask = CMIC_MASK_UNICODE;
                    info.hwnd = m_main;
                    info.lpVerb = "opennewtab";
                    info.lpVerbW = L"opennewtab";
                    info.nShow = SW_SHOWNORMAL;
                    result = menu->InvokeCommand(reinterpret_cast<CMINVOKECOMMANDINFO*>(&info));
                }
                DestroyMenu(popup);
            }
            IUnknown_SetSite(menu.get(), nullptr);
        }
        if (target)
        {
            CoTaskMemFree(target);
        }
        // Il fuoco della tastiera lo dà Esplora file da sé alla vista nuova a navigazione finita (~0,2 s); spostarlo
        // qui finirebbe sulla vista vecchia, che sta per essere distrutta.
        XPX_MLOG(L"NavBar: voce %zu aperta %s (dalla scheda %llx): 0x%08X.", index, newTab ? L"in una scheda nuova" : L"nella scheda attiva",
                 reinterpret_cast<uintptr_t>(tab), static_cast<unsigned>(result));
    }

    void NavBar::OpenAll(std::wstring const& id, UINT settleMs)
    {
        if (!m_groups || !m_snapshot || !m_root)
        {
            return;
        }
        // Come "Apri tutti" dei segnalibri: le cartelle del gruppo (non quelle dei sottogruppi), nel loro ordine.
        std::vector<std::wstring> keys;
        for (auto const& key : VisibleChildren(*m_groups, id, PresentKeys()))
        {
            if (IsFolderKey(key) && ItemOf(key))
            {
                keys.push_back(key);
            }
        }
        if (keys.empty())
        {
            return;
        }
        if (keys.size() > kOpenAllConfirm)
        {
            const std::wstring question = UiItalian() ? L"Aprire " + std::to_wstring(keys.size()) + L" cartelle in schede nuove?"
                                                      : L"Open " + std::to_wstring(keys.size()) + L" folders in new tabs?";
            if (MessageBoxW(m_main, question.c_str(), Tr(L"File Explorer", L"Esplora file"), MB_OKCANCEL | MB_ICONQUESTION) != IDOK)
            {
                return;
            }
        }
        XPX_MLOG(L"NavBar: %zu cartelle del gruppo da aprire in schede nuove.", keys.size());
        if (settleMs)
        {
            m_openSettle = settleMs; // solo per le prove
        }
        for (auto const& key : keys)
        {
            QueueNewTab(key);
        }
    }

    void NavBar::QueueNewTab(std::wstring const& key)
    {
        if (!m_root)
        {
            return;
        }
        // Tutte dalla scheda attiva quando la coda parte (non da quella appena creata).
        if (m_openQueue.empty() && !m_openWaiting)
        {
            m_openTab = m_layout.ActiveContent();
        }
        m_openQueue.push_back(key);
        EnsureOpenTimer();
        if (m_openTimer && !m_openTimer.IsRunning())
        {
            OpenNextQueued();
        }
    }

    void NavBar::EnsureOpenTimer()
    {
        if (m_openTimer || !m_root)
        {
            return;
        }
        m_openTimer = m_root.DispatcherQueue().CreateTimer();
        m_openTimer.IsRepeating(false);
        const std::weak_ptr<NavBar> weak = weak_from_this();
        m_openTimer.Tick([weak](auto&&, auto&&) {
            if (const auto self = weak.lock())
            {
                Guard(L"schede nuove in coda", [&] { self->OpenNextQueued(); });
            }
        });
    }

    void NavBar::OpenNextQueued()
    {
        // Una scheda nuova alla volta: la prossima solo quando quella di prima ha la sua vista dei file (più una breve
        // pausa), o dopo al più 4 s. Due schede nuove create una sopra l'altra fanno cadere Esplora file (accesso a un
        // CShellBrowser non più valido da CExplorerFrame::SelectTab, durante CShellBrowser::CreateViewWindow; vedi
        // README): succede anche con due clic centrali di fila.
        constexpr ULONGLONG kTimeoutMs = 4000;
        constexpr UINT kSettleMs = 300;
        const ULONGLONG now = GetTickCount64();
        if (m_openWaiting && (ReadyTabs() > m_openReadyBefore || now - m_openStarted >= kTimeoutMs))
        {
            m_openWaiting = false;
            m_openNextAt = now + (m_openSettle ? m_openSettle : kSettleMs);
            XPX_MLOG(L"NavBar: scheda nuova %s dopo %llu ms.", ReadyTabs() > m_openReadyBefore ? L"pronta" : L"non ancora pronta (si va avanti)", now - m_openStarted);
        }
        if (!m_openWaiting && now >= m_openNextAt && !m_openQueue.empty())
        {
            const std::wstring key = m_openQueue.front();
            m_openQueue.erase(m_openQueue.begin());
            if (const auto item = ItemOf(key))
            {
                m_openReadyBefore = ReadyTabs();
                m_openStarted = GetTickCount64();
                m_openWaiting = true;
                Open(*item, true, m_openTab);
            }
        }
        if ((m_openWaiting || !m_openQueue.empty()) && m_openTimer)
        {
            m_openTimer.Interval(std::chrono::milliseconds(100));
            m_openTimer.Start();
        }
        else
        {
            m_openSettle = 0;
        }
    }

    size_t NavBar::ReadyTabs() const
    {
        // Schede della finestra che hanno la vista dei file (SHELLDLL_DefView da qualche parte dentro).
        size_t ready = 0;
        for (HWND tab = FindWindowExW(m_main, nullptr, L"ShellTabWindowClass", nullptr); tab; tab = FindWindowExW(m_main, tab, L"ShellTabWindowClass", nullptr))
        {
            bool found = false;
            EnumChildWindows(
                tab,
                [](HWND child, LPARAM param) -> BOOL {
                    wchar_t name[32]{};
                    if (GetClassNameW(child, name, ARRAYSIZE(name)) && wcscmp(name, L"SHELLDLL_DefView") == 0)
                    {
                        *reinterpret_cast<bool*>(param) = true;
                        return FALSE;
                    }
                    return TRUE;
                },
                reinterpret_cast<LPARAM>(&found));
            ready += found;
        }
        return ready;
    }

    void NavBar::ShowContextMenu(size_t index, POINT screen, DWORD inputFlags, bool classic)
    {
        if (!m_snapshot || index >= m_snapshot->items.size())
        {
            return;
        }
        // Il menu si apre dopo che l'evento XAML è finito (la voce si ritrova dalla chiave: le voci possono cambiare).
        const std::wstring key = FolderKey(m_snapshot->items[index].parsing);
        const std::weak_ptr<NavBar> weak = weak_from_this();
        m_root.DispatcherQueue().TryEnqueue([weak, key, screen, inputFlags, classic] {
            const auto self = weak.lock();
            if (!self || self->m_menuOpen)
            {
                return;
            }
            const auto index = self->ItemOf(key);
            if (!index)
            {
                return;
            }
            // Come il riquadro: il menu nuovo, e con Maiusc (o se il menu nuovo non c'è) quello classico completo.
            if (!classic && GetKeyState(VK_SHIFT) >= 0)
            {
                auto result = ModernMenu::ShowResult::Unavailable;
                Guard(L"menu nuovo", [&] { result = self->ShowModernMenu(*index, screen, inputFlags); });
                if (result != ModernMenu::ShowResult::Unavailable)
                {
                    return;
                }
            }
            const auto idList = self->m_snapshot->items[*index].idList;
            Guard(L"menu contestuale", [&] { self->ShowClassicMenu(idList, key, screen); });
        });
    }

    ModernMenu::ShowResult NavBar::ShowModernMenu(size_t index, POINT screen, DWORD inputFlags)
    {
        const auto idList = m_snapshot->items[index].idList;
        const std::wstring key = FolderKey(m_snapshot->items[index].parsing);
        const auto item = ShellItemOf(idList);
        const auto menu = item ? ItemMenuOf(item.get(), m_main) : nullptr;
        if (!item || !menu)
        {
            return ModernMenu::ShowResult::Unavailable;
        }
        // Come i presenter di Windows (uno per scheda, con la scheda come finestra): la sua isola XAML è una figlia della
        // scheda. Sulla finestra principale confonderebbe chi cerca l'isola dell'intestazione (la più in alto).
        const HWND tab = m_layout.ActiveContent();
        if (!tab)
        {
            return ModernMenu::ShowResult::Unavailable;
        }
        if (m_modernMenu && (m_modernMenuTab != tab || !IsWindow(m_modernMenuTab)))
        {
            m_modernMenu.reset();
            m_modernMenuTab = nullptr;
        }
        if (!m_modernMenu)
        {
            const std::weak_ptr<NavBar> weak = weak_from_this();
            ModernMenu::Callbacks callbacks;
            callbacks.invoke = [weak](IContextMenu* chosen, UINT command, POINT point) {
                if (const auto self = weak.lock())
                {
                    self->InvokeMenuCommand(chosen, command, point, self->m_menuKey);
                }
            };
            callbacks.dismissed = [weak] {
                if (const auto self = weak.lock(); self && self->m_menuContext)
                {
                    IUnknown_SetSite(self->m_menuContext.get(), nullptr);
                    self->m_menuContext = nullptr;
                    NavItems::Instance().Refresh(); // aggiunta o tolta da Accesso rapido...
                }
            };
            callbacks.expanded = [weak](POINT point) {
                // "Mostra altre opzioni": il menu classico (modale), dopo che il presenter ha finito.
                const auto self = weak.lock();
                if (!self || self->m_menuItem.empty())
                {
                    return;
                }
                self->m_root.DispatcherQueue().TryEnqueue([weak, idList = self->m_menuItem, key = self->m_menuKey, point] {
                    if (const auto self = weak.lock(); self && !self->m_menuOpen)
                    {
                        Guard(L"menu classico", [&] { self->ShowClassicMenu(idList, key, point); });
                    }
                });
            };
            callbacks.focus = [weak] {
                if (const auto self = weak.lock())
                {
                    self->RestoreFocusAfterMenu();
                }
            };
            // Le voci dei gruppi ("Sposta nel gruppo", "Togli dal gruppo") stanno in "Mostra altre opzioni": il menu
            // nuovo mostra solo i verbi di Windows ammessi e le estensioni installate (vedi ModernMenu::Show).
            m_modernMenu = std::make_unique<ModernMenu>(tab, std::move(callbacks));
            m_modernMenuTab = tab;
        }
        // Il browser della scheda come sito, come per il menu classico: "Apri in una nuova scheda" e simili
        // aprono in questa finestra.
        IShellBrowser* browser = BrowserOf(m_layout.ActiveContent());
        if (browser)
        {
            IUnknown_SetSite(menu.get(), browser);
        }
        if (m_menuContext && m_menuContext != menu)
        {
            IUnknown_SetSite(m_menuContext.get(), nullptr);
        }
        m_menuContext = menu;
        m_menuItem = idList;
        m_menuKey = key;
        const auto result = m_modernMenu->Show(menu.get(), item.get(), browser, screen, inputFlags);
        m_menuRetryPending = result == ModernMenu::ShowResult::Deferred;
        if (result == ModernMenu::ShowResult::Shown)
        {
            XPX_MLOG(L"NavBar: menu nuovo della voce %zu.", index);
        }
        else if (result == ModernMenu::ShowResult::Unavailable)
        {
            IUnknown_SetSite(menu.get(), nullptr);
            m_menuContext = nullptr;
        }
        return result;
    }

    void NavBar::ShowClassicMenu(std::vector<uint8_t> const& idList, std::wstring const& key, POINT screen)
    {
        // Come il menu classico del riquadro ("Mostra altre opzioni", Maiusc+clic destro): voce e sfondo della
        // cartella insieme, senza "Crea collegamento". "Apri" resta: il riquadro la sostituisce con "Espandi".
        const auto item = ShellItemOf(idList);
        const auto menu = item ? ItemMenuOf(item.get(), m_main) : nullptr;
        if (!menu)
        {
            return;
        }
        if (IShellBrowser* browser = BrowserOf(m_layout.ActiveContent()))
        {
            IUnknown_SetSite(menu.get(), browser);
        }
        const HMENU popup = CreatePopupMenu();
        const UINT flags = CMF_EXPLORE | CMF_ASYNCVERBSTATE | (GetKeyState(VK_SHIFT) < 0 ? CMF_EXTENDEDVERBS : 0);
        if (popup && SUCCEEDED(menu->QueryContextMenu(popup, 0, 1, kExtraFirstId - 1, flags)))
        {
            CleanupSeparators(popup);
            DeleteVerb(menu.get(), popup, 1, L"link");
            CleanupSeparators(popup);
            AddGroupCommands(popup, menu.get(), 1, kExtraFirstId, kExtraLastId, key);
            // Sottomenu disegnati dalla shell (Invia a, Apri con...): i loro messaggi passano da lei.
            const auto menu3 = menu.try_as<IContextMenu3>();
            const auto menu2 = menu.try_as<IContextMenu2>();
            m_layout.onMenuMessage = [menu3, menu2](UINT message, WPARAM wParam, LPARAM lParam, LRESULT& result) {
                if (menu3)
                {
                    return SUCCEEDED(menu3->HandleMenuMsg2(message, wParam, lParam, &result));
                }
                if (menu2 && message != WM_MENUCHAR)
                {
                    result = 0;
                    return SUCCEEDED(menu2->HandleMenuMsg(message, wParam, lParam));
                }
                return false;
            };
            XPX_MLOG(L"NavBar: menu classico di una voce.");
            m_menuOpen = true;
            const UINT command = TrackPopupMenuEx(popup, TPM_RETURNCMD | TPM_RIGHTBUTTON, screen.x, screen.y, m_main, nullptr);
            m_menuOpen = false;
            m_layout.onMenuMessage = nullptr;
            if (command >= kExtraFirstId)
            {
                RunGroupCommand(command - kExtraFirstId, key);
            }
            else if (command)
            {
                InvokeMenuCommand(menu.get(), command - 1, screen, key);
            }
        }
        if (popup)
        {
            DestroyMenu(popup);
        }
        IUnknown_SetSite(menu.get(), nullptr);
        NavItems::Instance().Refresh(); // aggiunta o tolta da Accesso rapido, rinominata...
    }

    void NavBar::InvokeMenuCommand(IContextMenu* menu, UINT command, POINT point, std::wstring const& key)
    {
        wchar_t verb[80]{};
        if (FAILED(menu->GetCommandString(command, GCS_VERBW, nullptr, reinterpret_cast<LPSTR>(verb), ARRAYSIZE(verb))))
        {
            verb[0] = L'\0';
        }
        CMINVOKECOMMANDINFOEX info{ sizeof(info) };
        info.fMask = CMIC_MASK_UNICODE | CMIC_MASK_PTINVOKE;
        info.hwnd = m_main;
        info.lpVerb = MAKEINTRESOURCEA(command);
        info.lpVerbW = MAKEINTRESOURCEW(command);
        info.nShow = SW_SHOWNORMAL;
        info.ptInvoke = point;
        // "Apri in una nuova scheda": anche questa scheda conta per la coda delle schede nuove (QueueNewTab).
        const bool newTab = _wcsicmp(verb, L"opennewtab") == 0;
        const size_t readyBefore = newTab ? ReadyTabs() : 0;
        const ULONGLONG started = GetTickCount64();
        const HRESULT result = menu->InvokeCommand(reinterpret_cast<CMINVOKECOMMANDINFO*>(&info));
        const auto index = ItemOf(key);
        XPX_MLOG(L"NavBar: comando %u (%s) del menu della voce %zd: 0x%08X.", command, verb[0] ? verb : L"senza verbo",
                 index ? static_cast<ptrdiff_t>(*index) : static_cast<ptrdiff_t>(-1), static_cast<unsigned>(result));
        if (newTab && SUCCEEDED(result) && !m_openWaiting)
        {
            m_openWaiting = true;
            m_openReadyBefore = readyBefore;
            m_openStarted = started;
            EnsureOpenTimer();
            if (m_openTimer && !m_openTimer.IsRunning())
            {
                m_openTimer.Interval(std::chrono::milliseconds(100));
                m_openTimer.Start();
            }
        }
        // Tolta da Accesso rapido: la barra la dimentica anche nei gruppi (riaggiunta, tornerà al primo livello).
        if (SUCCEEDED(result) && _wcsicmp(verb, L"unpinfromhome") == 0 && !key.empty())
        {
            if (const auto state = NavGroups::Instance().Forget(key))
            {
                Apply(state);
            }
        }
    }

    void NavBar::RestoreFocusAfterMenu()
    {
        // Il presenter lo chiede chiudendosi o prima di eseguire la voce: il riquadro rimette il fuoco nel suo albero.
        // Qui, se il menu se l'era preso (fuoco fuori dalla finestra), torna alla vista dei file della scheda attiva;
        // mai se nel frattempo è in primo piano un'altra applicazione.
        const HWND foreground = GetForegroundWindow();
        if (!foreground || GetAncestor(foreground, GA_ROOTOWNER) != m_main)
        {
            return;
        }
        const HWND focus = GetFocus();
        if (focus && (focus == m_main || IsChild(m_main, focus)))
        {
            return;
        }
        if (const HWND target = m_layout.ContentFocusTarget())
        {
            ::SetFocus(target);
        }
    }

    // --- Gruppi ----------------------------------------------------------------------------------------------

    void NavBar::AddGroupCommands(HMENU menu, IContextMenu* shellMenu, UINT shellFirst, UINT firstId, UINT lastId, std::wstring const& key)
    {
        m_groupCommands.clear();
        const auto item = ItemOf(key);
        if (!menu || !m_groups || !item || !m_snapshot->items[*item].pinned || lastId < firstId + 3)
        {
            return;
        }
        // Dopo "Rimuovi da Accesso rapido" (le voci sono parenti); altrimenti prima dell'ultima (Proprietà).
        const int count = GetMenuItemCount(menu);
        int position = count > 0 ? count - 1 : 0;
        for (int i = 0; i < count; ++i)
        {
            const UINT id = GetMenuItemID(menu, i);
            wchar_t verb[64]{};
            if (id != static_cast<UINT>(-1) && id >= shellFirst && id < firstId && shellMenu &&
                SUCCEEDED(shellMenu->GetCommandString(id - shellFirst, GCS_VERBW, nullptr, reinterpret_cast<LPSTR>(verb), ARRAYSIZE(verb))) &&
                _wcsicmp(verb, L"unpinfromhome") == 0)
            {
                position = i + 1;
                break;
            }
        }
        // "Sposta nel gruppo": tutti i gruppi, quelli annidati col percorso ("A › B"); quello attuale spuntato.
        const HMENU groups = CreatePopupMenu();
        if (!groups)
        {
            return;
        }
        const std::wstring parent = m_groups->ParentOf(key);
        UINT next = firstId + 2;
        std::function<void(std::wstring const&, std::wstring const&)> add = [&](std::wstring const& id, std::wstring const& prefix) {
            const NavGroup* group = m_groups->Find(id);
            if (!group || next >= lastId)
            {
                return;
            }
            const std::wstring text = prefix.empty() ? group->name : prefix + L" › " + group->name;
            AppendMenuW(groups, MF_STRING | (id == parent ? MF_CHECKED : 0), next++, MenuText(text).c_str());
            m_groupCommands.push_back(id);
            for (auto const& child : group->children)
            {
                if (IsGroupKey(child))
                {
                    add(std::wstring{ GroupIdOf(child) }, text);
                }
            }
        };
        for (auto const& top : TopLevel(*m_groups, PinnedKeys()))
        {
            if (IsGroupKey(top))
            {
                add(std::wstring{ GroupIdOf(top) }, L"");
            }
        }
        if (!m_groupCommands.empty())
        {
            AppendMenuW(groups, MF_SEPARATOR, 0, nullptr);
        }
        AppendMenuW(groups, MF_STRING, firstId + 1, Tr(L"New group…", L"Nuovo gruppo…"));
        MENUITEMINFOW info{ sizeof(info) };
        info.fMask = MIIM_SUBMENU | MIIM_STRING | MIIM_ID;
        info.wID = lastId;
        info.hSubMenu = groups;
        std::wstring moveText{ Tr(L"Move to group", L"Sposta nel gruppo") };
        info.dwTypeData = moveText.data();
        InsertMenuItemW(menu, static_cast<UINT>(position), TRUE, &info);
        if (!parent.empty())
        {
            MENUITEMINFOW out{ sizeof(out) };
            out.fMask = MIIM_STRING | MIIM_ID;
            out.wID = firstId;
            std::wstring outText{ Tr(L"Remove from group", L"Togli dal gruppo") };
            out.dwTypeData = outText.data();
            InsertMenuItemW(menu, static_cast<UINT>(position + 1), TRUE, &out);
        }
    }

    void NavBar::RunGroupCommand(UINT offset, std::wstring const& key)
    {
        if (offset == 0)
        {
            TakeOutOfGroup(key);
        }
        else if (offset == 1)
        {
            NewGroup(L"", L"", key, true); // al posto della cartella, con dentro la cartella
        }
        else if (offset - 2 < m_groupCommands.size())
        {
            PlaceNode(key, m_groupCommands[offset - 2], L"");
        }
    }

    void NavBar::TakeOutOfGroup(std::wstring const& key)
    {
        if (!m_groups)
        {
            return;
        }
        // Al livello di sopra, subito dopo il gruppo.
        const std::wstring parent = m_groups->ParentOf(key);
        if (parent.empty())
        {
            return;
        }
        const std::wstring groupKey = GroupKey(parent);
        const std::wstring holder = m_groups->ParentOf(groupKey);
        std::vector<std::wstring> siblings;
        if (holder.empty())
        {
            siblings = TopLevel(*m_groups, PinnedKeys());
        }
        else if (const NavGroup* group = m_groups->Find(holder))
        {
            siblings = group->children;
        }
        std::wstring before;
        if (auto at = std::find(siblings.begin(), siblings.end(), groupKey); at != siblings.end() && ++at != siblings.end())
        {
            before = *at;
        }
        PlaceNode(key, holder, before);
    }

    void NavBar::ShowGroupMenu(std::wstring const& id, mux::FrameworkElement const& anchor, std::optional<Point> position)
    {
        const NavGroup* group = m_groups ? m_groups->Find(id) : nullptr;
        if (!group || !anchor)
        {
            return;
        }
        size_t folders = 0;
        for (auto const& key : VisibleChildren(*m_groups, id, PresentKeys()))
        {
            folders += IsFolderKey(key);
        }
        muxc::MenuFlyout menu;
        if (const auto style = MenuPresenterStyle())
        {
            menu.MenuFlyoutPresenterStyle(style);
        }
        const std::weak_ptr<NavBar> weak = weak_from_this();
        const auto add = [&](std::wstring const& text, const wchar_t* glyph, std::function<void(NavBar&)> action, bool enabled) {
            muxc::MenuFlyoutItem item;
            item.Text(text);
            muxc::FontIcon icon;
            icon.Glyph(glyph);
            item.Icon(icon);
            item.IsEnabled(enabled);
            item.Click([weak, action](auto&&, auto&&) {
                if (const auto self = weak.lock())
                {
                    Guard(L"menu del gruppo", [&] { action(*self); });
                }
            });
            menu.Items().Append(item);
            return item;
        };
        add(Tr(L"Open all (", L"Apri tutte (") + std::to_wstring(folders) + L")", L"", [id](NavBar& self) {
            self.CloseLevels(0);
            self.OpenAll(id);
        }, folders > 0);
        menu.Items().Append(muxc::MenuFlyoutSeparator());
        add(Tr(L"New group…", L"Nuovo gruppo…"), L"", [id](NavBar& self) { self.NewGroup(id, L"", L"", true); }, true);
        add(Tr(L"Rename…", L"Rinomina…"), L"", [id](NavBar& self) { self.EditGroupName(id, false); }, true);
        const auto remove = add(Tr(L"Delete group", L"Elimina gruppo"), L"", [id](NavBar& self) { self.DeleteGroup(id); }, true);
        muxc::ToolTipService::SetToolTip(remove, winrt::box_value(Tr(L"The contents move up one level; the folders stay in Quick Access.",
                                                                     L"Il contenuto passa al livello di sopra; le cartelle restano in Accesso rapido.")));
        muxcp::FlyoutShowOptions options;
        if (position)
        {
            options.Position(winrt::Windows::Foundation::IReference<Point>{ *position });
        }
        options.ShowMode(muxcp::FlyoutShowMode::Standard);
        menu.ShowAt(anchor, options);
        XPX_MLOG(L"NavBar: menu di un gruppo (%zu cartelle).", folders);
    }

    void NavBar::ShowBarMenu(Point position)
    {
        muxc::MenuFlyout menu;
        if (const auto style = MenuPresenterStyle())
        {
            menu.MenuFlyoutPresenterStyle(style);
        }
        muxc::MenuFlyoutItem item;
        item.Text(Tr(L"New group…", L"Nuovo gruppo…"));
        muxc::FontIcon icon;
        icon.Glyph(L"");
        item.Icon(icon);
        const std::weak_ptr<NavBar> weak = weak_from_this();
        item.Click([weak](auto&&, auto&&) {
            if (const auto self = weak.lock())
            {
                Guard(L"nuovo gruppo", [&] { self->NewGroup(L"", L"", L"", true); });
            }
        });
        menu.Items().Append(item);
        muxcp::FlyoutShowOptions options;
        options.Position(winrt::Windows::Foundation::IReference<Point>{ position });
        options.ShowMode(muxcp::FlyoutShowMode::Standard);
        menu.ShowAt(m_root, options);
    }

    mux::Style NavBar::MenuPresenterStyle() const
    {
        if (m_theme == mux::ElementTheme::Default)
        {
            return nullptr;
        }
        try
        {
            std::wstring xaml = LR"(<Style xmlns="http://schemas.microsoft.com/winfx/2006/xaml/presentation" TargetType="MenuFlyoutPresenter">)"
                                LR"(<Setter Property="RequestedTheme" Value=")";
            xaml += m_theme == mux::ElementTheme::Dark ? L"Dark" : L"Light";
            xaml += LR"("/></Style>)";
            return mux::Markup::XamlReader::Load(xaml).as<mux::Style>();
        }
        catch (...)
        {
            return nullptr;
        }
    }

    void NavBar::NewGroup(std::wstring const& container, std::wstring const& before, std::wstring const& first, bool edit)
    {
        std::wstring id;
        const auto state = NavGroups::Instance().Create(Tr(L"New group", L"Nuovo gruppo"), container, before, first, PinnedKeys(), &id);
        if (!state)
        {
            XPX_MLOG(L"NavBar: gruppo non creato.");
            return;
        }
        Apply(state);
        if (edit)
        {
            // Come una cartella nuova: nasce con un nome e si rinomina subito (Esc lo lascia così). Dopo la disposizione
            // della barra, per mettere il riquadro sotto il pulsante nuovo.
            const std::weak_ptr<NavBar> weak = weak_from_this();
            m_root.DispatcherQueue().TryEnqueue(winrt::Microsoft::UI::Dispatching::DispatcherQueuePriority::Low, [weak, id] {
                if (const auto self = weak.lock())
                {
                    Guard(L"nome del gruppo nuovo", [&] { self->EditGroupName(id, true); });
                }
            });
        }
    }

    void NavBar::EditGroupName(std::wstring const& id, bool created)
    {
        const NavGroup* group = m_groups ? m_groups->Find(id) : nullptr;
        if (!group)
        {
            return;
        }
        // Sotto il gruppo se si vede, altrimenti sotto il primo che lo contiene e si vede (o "»", o la barra).
        mux::FrameworkElement anchor = AnchorOf(GroupKey(id));
        for (std::wstring parent = m_groups->ParentOf(GroupKey(id)); !anchor && !parent.empty(); parent = m_groups->ParentOf(GroupKey(parent)))
        {
            anchor = AnchorOf(GroupKey(parent));
        }
        if (!anchor && m_overflow && m_overflow.Visibility() == mux::Visibility::Visible)
        {
            anchor = m_overflow;
        }
        if (!anchor)
        {
            anchor = m_root;
        }
        if (m_nameEditor)
        {
            m_nameEditor.Hide();
        }
        muxc::Flyout editor;
        editor.Placement(muxcp::FlyoutPlacementMode::Bottom);
        editor.ShouldConstrainToRootBounds(false);
        muxc::StackPanel panel;
        panel.Spacing(12);
        panel.Width(300);
        panel.RequestedTheme(m_theme);
        muxc::TextBlock title;
        title.Text(created ? Tr(L"New group", L"Nuovo gruppo") : Tr(L"Rename group", L"Rinomina gruppo"));
        title.FontSize(14);
        title.FontWeight(winrt::Windows::UI::Text::FontWeight{ 600 });
        muxc::TextBox box;
        box.Text(group->name);
        box.MaxLength(100);
        mux::Automation::AutomationProperties::SetName(box, Tr(L"Group name", L"Nome del gruppo"));
        muxc::StackPanel buttons;
        buttons.Orientation(muxc::Orientation::Horizontal);
        buttons.Spacing(8);
        buttons.HorizontalAlignment(mux::HorizontalAlignment::Right);
        auto save = AccentButton(Tr(L"Save", L"Salva"));
        save.MinWidth(80);
        muxc::Button cancel;
        cancel.Content(winrt::box_value(Tr(L"Cancel", L"Annulla")));
        cancel.MinWidth(80);
        buttons.Children().Append(save);
        buttons.Children().Append(cancel);
        panel.Children().Append(title);
        panel.Children().Append(box);
        panel.Children().Append(buttons);
        editor.Content(panel);

        const std::weak_ptr<NavBar> weak = weak_from_this();
        const winrt::weak_ref<muxc::Flyout> weakEditor = editor;
        const winrt::weak_ref<muxc::TextBox> weakBox = box;
        const auto commit = [weak, weakEditor, weakBox, id] {
            const auto self = weak.lock();
            const auto box = weakBox.get();
            if (!self || !box)
            {
                return;
            }
            if (const auto state = NavGroups::Instance().Rename(id, std::wstring{ box.Text() }))
            {
                self->Apply(state);
            }
            if (const auto editor = weakEditor.get())
            {
                editor.Hide();
            }
        };
        save.Click([commit](auto&&, auto&&) { Guard(L"salva il nome", [&] { commit(); }); });
        cancel.Click([weakEditor](auto&&, auto&&) {
            if (const auto editor = weakEditor.get())
            {
                editor.Hide();
            }
        });
        box.KeyDown([commit, weakEditor](IInspectable const&, muxi::KeyRoutedEventArgs const& args) {
            if (args.Key() == winrt::Windows::System::VirtualKey::Enter)
            {
                args.Handled(true);
                Guard(L"Invio sul nome", [&] { commit(); });
            }
            else if (args.Key() == winrt::Windows::System::VirtualKey::Escape)
            {
                args.Handled(true);
                if (const auto editor = weakEditor.get())
                {
                    editor.Hide();
                }
            }
        });
        editor.Opened([weakBox](auto&&, auto&&) {
            if (const auto box = weakBox.get())
            {
                box.Focus(mux::FocusState::Programmatic);
                box.SelectAll();
            }
        });
        // Finché la casella ha il fuoco i suoi tasti non passano dalle scorciatoie di Esplora file (Backspace = Indietro).
        const auto typing = std::make_shared<bool>(false);
        const HWND main = m_main;
        box.GotFocus([typing, main](auto&&, auto&&) {
            if (!*typing)
            {
                *typing = true;
                TextInput::Begin(main);
            }
        });
        box.LostFocus([typing, main](auto&&, auto&&) {
            if (*typing)
            {
                *typing = false;
                TextInput::End(main);
            }
        });
        editor.Closed([weak, weakEditor, typing, main](auto&&, auto&&) {
            if (*typing)
            {
                *typing = false;
                TextInput::End(main);
            }
            const auto self = weak.lock();
            const auto editor = weakEditor.get();
            if (self && editor && self->m_nameEditor == editor)
            {
                self->m_nameEditor = nullptr;
                self->m_nameBox = nullptr;
            }
        });
        m_nameEditor = editor;
        m_nameBox = box;
        editor.ShowAt(anchor);
        XPX_MLOG(L"NavBar: nome di un gruppo da %s.", created ? L"scegliere (gruppo nuovo)" : L"cambiare");
    }

    void NavBar::DeleteGroup(std::wstring const& id)
    {
        if (const auto state = NavGroups::Instance().Remove(id, PinnedKeys()))
        {
            Apply(state);
        }
    }

    bool NavBar::PlaceNode(std::wstring const& key, std::wstring const& container, std::wstring const& before)
    {
        const auto state = NavGroups::Instance().Place(key, container, before, PinnedKeys());
        if (!state)
        {
            XPX_MLOG(L"NavBar: spostamento senza effetto (o non possibile).");
            return false;
        }
        XPX_MLOG(L"NavBar: %s %s.", IsGroupKey(key) ? L"gruppo spostato" : L"cartella spostata", container.empty() ? L"al primo livello" : L"in un gruppo");
        Apply(state);
        return true;
    }

    void NavBar::Apply(std::shared_ptr<const NavGroupsState> const& state)
    {
        if (!state)
        {
            return; // nessun cambiamento
        }
        m_groups = state;
        SyncWindowsOrder(*state, state->top); // il primo livello voluto (una voce appena spostata non è ancora lì in Windows)
        Rebuild();
    }

    void NavBar::SyncNativePanes()
    {
        if (m_snapshot && m_main)
        {
            SortNativePanes(m_main, *m_snapshot);
        }
    }

    void NavBar::SyncWindowsOrder(NavGroupsState const& state, std::vector<std::wstring> const& top)
    {
        // Subito a vista; l'ordine vero (quello di Windows) arriva con la lettura dopo gli spostamenti (NavOrder.cpp).
        // In modalità dimostrativa mai: la barra non vede le cartelle aggiunte dell'utente, e il loro ordine resta com'è.
        if (m_snapshot && !DemoRoot())
        {
            if (auto local = SyncPinnedOrder(*m_snapshot, state, top))
            {
                SetSnapshot(local);
            }
        }
    }

    // --- Trascinamento ---------------------------------------------------------------------------------------
    // Premuto il tasto su una cartella aggiunta o su un gruppo la voce è "armata"; oltre la soglia di Windows
    // (SM_CXDRAG) il pulsante perde la cattura (quindi niente clic) e la prende la radice della barra o dell'elenco,
    // che segue il puntatore anche fuori dalla sua finestra: una linea segna il posto (al primo livello solo nel
    // blocco delle cartelle aggiunte), il gruppo sotto il puntatore si evidenzia se la voce ci andrà dentro, e
    // restando fermi su un gruppo (o su "»") si apre il suo elenco. Al rilascio la voce va lì (subito a vista, poi
    // nell'ordine di Windows se serve). Esc o la cattura persa annullano.

    void NavBar::AttachDragHandlers(mux::UIElement const& holder)
    {
        const std::weak_ptr<NavBar> weak = weak_from_this();
        holder.AddHandler(mux::UIElement::PointerPressedEvent(),
                          winrt::box_value(muxi::PointerEventHandler([](IInspectable const& sender, muxi::PointerRoutedEventArgs const& args) {
                              if (g_dragDebug)
                              {
                                  Guard(L"pressione (registro)", [&] {
                                      const auto point = args.GetCurrentPoint(sender.as<mux::UIElement>()).Position();
                                      XPX_MLOG(L"NavBar[trascinamento]: pressione arrivata alla radice (%.0f,%.0f), già gestita %d.", point.X, point.Y,
                                               args.Handled() ? 1 : 0);
                                  });
                              }
                          })),
                          true);
        holder.AddHandler(mux::UIElement::PointerMovedEvent(),
                          winrt::box_value(muxi::PointerEventHandler([weak](IInspectable const&, muxi::PointerRoutedEventArgs const& args) {
                              if (const auto self = weak.lock())
                              {
                                  Guard(L"trascinamento", [&] { self->OnDragMoved(args); });
                              }
                          })),
                          true);
        holder.AddHandler(mux::UIElement::PointerReleasedEvent(),
                          winrt::box_value(muxi::PointerEventHandler([weak](IInspectable const&, muxi::PointerRoutedEventArgs const& args) {
                              if (const auto self = weak.lock())
                              {
                                  Guard(L"rilascio", [&] { self->OnDragReleased(args); });
                              }
                          })),
                          true);
        holder.AddHandler(mux::UIElement::PointerCaptureLostEvent(),
                          winrt::box_value(muxi::PointerEventHandler([weak](IInspectable const&, muxi::PointerRoutedEventArgs const&) {
                              // Staccato il trascinamento non dipende dalla cattura (quella dell'elenco di partenza si perde
                              // apposta).
                              if (const auto self = weak.lock(); self && self->m_drag.active && !self->m_drag.polled)
                              {
                                  Guard(L"cattura persa", [&] {
                                      XPX_MLOG(L"NavBar: trascinamento annullato (cattura del puntatore persa).");
                                      self->EndDrag();
                                  });
                              }
                          })),
                          true);
        holder.AddHandler(mux::UIElement::KeyDownEvent(),
                          winrt::box_value(muxi::KeyEventHandler([weak](IInspectable const&, muxi::KeyRoutedEventArgs const& args) {
                              if (const auto self = weak.lock(); self && self->m_drag.active && args.Key() == winrt::Windows::System::VirtualKey::Escape)
                              {
                                  args.Handled(true);
                                  Guard(L"Esc", [&] {
                                      XPX_MLOG(L"NavBar: trascinamento annullato (Esc).");
                                      self->EndDrag();
                                  });
                              }
                          })),
                          true);
    }

    void NavBar::ArmDrag(std::wstring const& key, muxc::Button const& source, mux::UIElement const& holder, int fromLevel,
                         muxi::PointerRoutedEventArgs const& args)
    {
        if (g_dragDebug)
        {
            XPX_MLOG(L"NavBar[trascinamento]: pressione su una voce (%s), sinistro %d, già armato %d, attivo %d.",
                     fromLevel < 0 ? L"barra" : L"elenco", args.GetCurrentPoint(nullptr).Properties().IsLeftButtonPressed() ? 1 : 0,
                     m_drag.armed ? 1 : 0, m_drag.active ? 1 : 0);
        }
        if (m_drag.active || !args.GetCurrentPoint(nullptr).Properties().IsLeftButtonPressed())
        {
            return;
        }
        m_drag = {};
        m_drag.armed = true;
        m_drag.key = key;
        m_drag.source = source;
        m_drag.holder = holder;
        m_drag.fromLevel = fromLevel;
        m_drag.pointer = args.Pointer().PointerId();
        GetCursorPos(&m_drag.start);
    }

    void NavBar::OnDragMoved(muxi::PointerRoutedEventArgs const& args)
    {
        if (!m_drag.armed || m_drag.test || args.Pointer().PointerId() != m_drag.pointer)
        {
            return;
        }
        if (!args.GetCurrentPoint(nullptr).Properties().IsLeftButtonPressed())
        {
            if (m_drag.polled)
            {
                FinishDrag(); // staccato: rilasciato altrove, si lascia dove indicava l'ultimo punto
                return;
            }
            if (g_dragDebug)
            {
                XPX_MLOG(L"NavBar[trascinamento]: movimento senza tasto premuto: disarmato.");
            }
            EndDrag(); // rilascio perso
            return;
        }
        if (!m_drag.active)
        {
            POINT now{};
            GetCursorPos(&now);
            if (g_dragDebug)
            {
                XPX_MLOG(L"NavBar[trascinamento]: movimento armato, spostamento %d,%d px.", now.x - m_drag.start.x, now.y - m_drag.start.y);
            }
            if (std::abs(now.x - m_drag.start.x) < GetSystemMetrics(SM_CXDRAG) && std::abs(now.y - m_drag.start.y) < GetSystemMetrics(SM_CYDRAG))
            {
                return;
            }
            if (!BeginDrag(args))
            {
                m_drag = {};
                return;
            }
        }
        if (m_drag.polled)
        {
            GetCursorPos(&m_drag.lastCursor); // staccato: il timer non rifà lo stesso punto
        }
        ShowTarget(HitTest([&](mux::UIElement const& element) { return args.GetCurrentPoint(element).Position(); }));
        args.Handled(true);
    }

    bool NavBar::BeginDrag(muxi::PointerRoutedEventArgs const& args)
    {
        if (!m_drag.source || !m_drag.holder)
        {
            return false;
        }
        // Il pulsante non si considera più premuto (niente clic al rilascio); la cattura passa alla radice.
        m_drag.source.ReleasePointerCapture(args.Pointer());
        if (!m_drag.holder.CapturePointer(args.Pointer()))
        {
            XPX_MLOG(L"NavBar: trascinamento non iniziato (cattura del puntatore rifiutata).");
            return false;
        }
        m_drag.active = true;
        m_drag.source.Opacity(0.4);
        m_hoverAction = nullptr;
        if (m_hoverTimer)
        {
            m_hoverTimer.Stop();
        }
        if (m_dragTimer)
        {
            m_dragTimer.Start();
        }
        XPX_MLOG(L"NavBar: trascinamento di %s (%s).", IsGroupKey(m_drag.key) ? L"un gruppo" : L"una cartella",
                 m_drag.fromLevel < 0 ? L"dalla barra" : L"da un elenco");
        return true;
    }

    void NavBar::OnDragTimer()
    {
        if (!m_drag.active)
        {
            return;
        }
        if (m_drag.polled)
        {
            // Staccato: gli eventi XAML possono non arrivare più (la finestra dell'elenco chiuso può tenersi il puntatore, e
            // fuori dalla barra e dagli elenchi non ce ne sono), quindi cursore e tasto si leggono da qui.
            std::optional<POINT> cursor;
            bool down = false;
            if (m_drag.test)
            {
                cursor = m_testDragCursor;
                down = m_testDragDown;
            }
            else
            {
                POINT now{};
                if (GetCursorPos(&now))
                {
                    cursor = now;
                }
                // Lo stato fisico del tasto: con i tasti scambiati il principale è il destro.
                down = (GetAsyncKeyState(GetSystemMetrics(SM_SWAPBUTTON) ? VK_RBUTTON : VK_LBUTTON) & 0x8000) != 0;
            }
            if (cursor && (cursor->x != m_drag.lastCursor.x || cursor->y != m_drag.lastCursor.y))
            {
                m_drag.lastCursor = *cursor;
                ShowTarget(HitTestScreen(*cursor));
            }
            if (!down)
            {
                FinishDrag();
                return;
            }
            if (!m_drag.test && (GetAsyncKeyState(VK_ESCAPE) & 0x8000) != 0 && GetForegroundWindow() == m_main)
            {
                XPX_MLOG(L"NavBar: trascinamento annullato (Esc).");
                EndDrag();
                return;
            }
        }
        // Fermi su un gruppo (o su "»"): si apre il suo elenco. Se è della profondità dell'elenco da cui parte la voce, o
        // meno profondo, quell'elenco si chiude (uno per profondità): il trascinamento prima si stacca da lui (come
        // Chrome: dall'elenco di un gruppo a un altro gruppo, per lasciarla dove si vuole nel suo elenco).
        if (!m_drag.target.hover || !m_drag.hoverSince || GetTickCount64() - m_drag.hoverSince < kOpenListAfterMs)
        {
            return;
        }
        m_drag.hoverSince = 0;
        const auto level = m_drag.target.hover;
        if (level->open)
        {
            return;
        }
        mux::FrameworkElement anchor{ nullptr };
        if (level == m_overflowLevel)
        {
            anchor = m_overflow;
        }
        else if (m_drag.target.hoverAnchor)
        {
            anchor = m_drag.target.hoverAnchor;
        }
        if (!anchor)
        {
            return;
        }
        if (m_drag.fromLevel >= 0 && level->depth <= static_cast<size_t>(m_drag.fromLevel))
        {
            DetachDrag();
        }
        ShowLevel(level, anchor, true);
    }

    void NavBar::DetachDrag()
    {
        XPX_MLOG(L"NavBar: si apre un elenco al posto di quello da cui parte la voce: il trascinamento continua staccato.");
        m_drag.fromLevel = -1;
        if (!m_drag.polled)
        {
            m_drag.polled = true;
            m_drag.lastCursor = POINT{ LONG_MIN, LONG_MIN };
        }
        // La cattura della radice dell'elenco che si chiude si toglie ora, finché l'elenco c'è (non resta a un elemento
        // fuori dall'albero); la sua perdita non annulla più il trascinamento.
        const auto holder = m_drag.holder;
        m_drag.holder = m_root;
        if (holder && holder != m_root)
        {
            try
            {
                holder.ReleasePointerCaptures();
            }
            catch (...)
            {
            }
        }
    }

    void NavBar::FinishDrag()
    {
        const Drag drag = m_drag;
        EndDrag();
        if (drag.active)
        {
            DropDragged(drag);
        }
    }

    void NavBar::DropDragged(Drag const& drag)
    {
        if (!drag.target.valid)
        {
            XPX_MLOG(L"NavBar: voce lasciata fuori posto: nessuno spostamento.");
            return;
        }
        // Lasciata nella barra: gli elenchi si chiudono; lasciata in un elenco restano aperti, aggiornati.
        if (drag.target.level < 0 && !drag.target.into)
        {
            CloseLevels(0);
        }
        PlaceNode(drag.key, drag.target.container, drag.target.into ? std::wstring{} : drag.target.before);
    }

    bool NavBar::StartTestDrag(std::wstring const& key)
    {
        if (m_drag.armed)
        {
            EndDrag();
        }
        if (key.empty() || !IsDraggable(key))
        {
            return false;
        }
        // La voce nell'elenco aperto più interno che la mostra, altrimenti nella barra (se si vede).
        muxc::Button source{ nullptr };
        mux::UIElement holder{ nullptr };
        int fromLevel = -1;
        for (size_t k = m_levels.size(); k-- > 0 && !source;)
        {
            Level const& level = *m_levels[k];
            if (!level.open || !level.root)
            {
                continue;
            }
            for (auto const& row : level.rows)
            {
                if (row.key == key)
                {
                    source = row.button;
                    holder = level.root;
                    fromLevel = static_cast<int>(level.depth);
                    break;
                }
            }
        }
        for (size_t e = 0; !source && e < m_entries.size() && e < m_visibleCount; ++e)
        {
            if (m_entries[e].key == key)
            {
                source = m_entries[e].button;
                holder = m_root;
            }
        }
        if (!source)
        {
            return false;
        }
        m_drag = {};
        m_drag.armed = true;
        m_drag.active = true;
        m_drag.polled = true;
        m_drag.test = true;
        m_drag.key = key;
        m_drag.source = source;
        m_drag.holder = holder;
        m_drag.fromLevel = fromLevel;
        m_testDragCursor.reset();
        m_testDragDown = true;
        source.Opacity(0.4);
        m_hoverAction = nullptr;
        if (m_hoverTimer)
        {
            m_hoverTimer.Stop();
        }
        if (m_dragTimer)
        {
            m_dragTimer.Start();
        }
        XPX_MLOG(L"NavBar: trascinamento di prova di %s (%s).", IsGroupKey(key) ? L"un gruppo" : L"una cartella",
                 fromLevel < 0 ? L"dalla barra" : L"da un elenco");
        return true;
    }

    NavBar::DropTarget NavBar::HitTestScreen(POINT screen)
    {
        RECT island{};
        if (!m_hwnd || !m_root || !m_main || !GetWindowRect(m_hwnd, &island))
        {
            return {};
        }
        const double scale = GetDpiForWindow(m_main) / 96.0;
        const Point point{ static_cast<float>((screen.x - island.left) / scale), static_cast<float>((screen.y - island.top) / scale) };
        winrt::hresult failure{ S_OK };
        const DropTarget target = HitTest([&](mux::UIElement const& element) {
            if (element == m_root)
            {
                return point;
            }
            try
            {
                return m_root.TransformToVisual(element).TransformPoint(point);
            }
            catch (...)
            {
                failure = winrt::to_hresult(); // (solo dentro catch)
                return Point{ -100000, -100000 };
            }
        });
        if (failure != S_OK && g_dragDebug)
        {
            XPX_MLOG(L"NavBar[trascinamento]: posizione in un elenco non calcolabile (0x%08X).", static_cast<unsigned>(failure.value));
        }
        return target;
    }

    NavBar::DropTarget NavBar::HitTest(std::function<Point(mux::UIElement const&)> const& pointIn) const
    {
        // Prima gli elenchi aperti, dal più interno (stanno sopra).
        for (size_t k = m_levels.size(); k-- > 0;)
        {
            Level const& level = *m_levels[k];
            if (!level.open || !level.scroll)
            {
                continue;
            }
            const auto view = pointIn(level.scroll);
            if (view.X >= 0 && view.X <= level.scroll.ActualWidth() && view.Y >= 0 && view.Y <= level.scroll.ActualHeight())
            {
                return HitLevel(k, pointIn(level.root).Y);
            }
        }
        const auto bar = pointIn(m_root);
        if (bar.X >= 0 && bar.X <= m_root.ActualWidth() && bar.Y >= -kDropTolerance && bar.Y <= m_root.ActualHeight() + kDropTolerance)
        {
            return HitBar(bar.X);
        }
        return {};
    }

    NavBar::DropTarget NavBar::HitBar(double x) const
    {
        if (m_overflow.Visibility() == mux::Visibility::Visible && x >= m_overflow.TransformToVisual(m_root).TransformPoint({ 0, 0 }).X)
        {
            // Su "»": in fondo alla barra, e restando fermi si apre l'elenco (per lasciarla lì dentro).
            auto target = TopSlot(m_visibleCount, -1, m_visibleCount);
            target.hover = m_overflowLevel;
            return target;
        }
        size_t slot = m_visibleCount;
        for (size_t j = 0; j < m_visibleCount && j < m_entries.size(); ++j)
        {
            Entry const& entry = m_entries[j];
            const double left = entry.button.TransformToVisual(m_root).TransformPoint({ 0, 0 }).X;
            const double width = entry.button.ActualWidth();
            if (x < left + width)
            {
                // Al centro di un gruppo: dentro (in fondo); restando fermi si apre il suo elenco.
                if (entry.level && x >= left + width * 0.25 && x < left + width * 0.75 && CanDropInto(entry.level->group))
                {
                    DropTarget target;
                    target.valid = true;
                    target.container = entry.level->group;
                    target.into = entry.button;
                    target.hover = entry.level;
                    target.hoverAnchor = entry.button;
                    return target;
                }
                slot = x < left + width / 2 ? j : j + 1;
                break;
            }
        }
        return TopSlot(slot, -1, slot);
    }

    NavBar::DropTarget NavBar::HitLevel(size_t index, double y) const
    {
        Level const& level = *m_levels[index];
        size_t row = level.rows.size();
        for (size_t r = 0; r < level.rows.size(); ++r)
        {
            Row const& current = level.rows[r];
            const double top = current.button.TransformToVisual(level.root).TransformPoint({ 0, 0 }).Y;
            const double height = current.button.ActualHeight();
            if (y < top + height)
            {
                if (current.level && y >= top + height * 0.25 && y < top + height * 0.75 && CanDropInto(current.level->group))
                {
                    DropTarget target;
                    target.valid = true;
                    target.container = current.level->group;
                    target.into = current.button;
                    target.level = static_cast<int>(index);
                    target.hover = current.level;
                    target.hoverAnchor = current.button;
                    return target;
                }
                row = y < top + height / 2 ? r : r + 1;
                break;
            }
        }
        if (level.group.empty())
        {
            return TopSlot(level.firstEntry + row, static_cast<int>(index), row);
        }
        DropTarget target;
        // Prima o dopo sé stessa: nessuno spostamento; un gruppo mai dentro sé stesso.
        for (size_t s = 0; s < level.rows.size(); ++s)
        {
            if (level.rows[s].key == m_drag.key)
            {
                if (row == s || row == s + 1)
                {
                    return target;
                }
                break;
            }
        }
        if (!CanDropInto(level.group))
        {
            return target;
        }
        target.valid = true;
        target.container = level.group;
        target.before = row < level.rows.size() ? level.rows[row].key : std::wstring{};
        target.level = static_cast<int>(index);
        target.slot = row;
        return target;
    }

    NavBar::DropTarget NavBar::TopSlot(size_t entry, int level, size_t row) const
    {
        DropTarget target;
        // Al primo livello solo nel blocco delle cartelle aggiunte; prima o dopo sé stessa: nessuno spostamento.
        const size_t slot = std::clamp(entry, m_blockFirst, m_blockEnd);
        for (size_t s = m_blockFirst; s < m_blockEnd && s < m_entries.size(); ++s)
        {
            if (m_entries[s].key == m_drag.key)
            {
                if (slot == s || slot == s + 1)
                {
                    return target;
                }
                break;
            }
        }
        target.valid = true;
        target.before = slot < m_blockEnd && slot < m_entries.size() ? m_entries[slot].key : std::wstring{};
        // La linea: dove punta il puntatore, o (posto spostato nel blocco) nella barra o nell'elenco "»".
        if (slot == entry)
        {
            target.level = level;
            target.slot = row;
        }
        else if (level >= 0 && slot >= m_levels[static_cast<size_t>(level)]->firstEntry)
        {
            target.level = level;
            target.slot = slot - m_levels[static_cast<size_t>(level)]->firstEntry;
        }
        else if (slot <= m_visibleCount)
        {
            target.level = -1;
            target.slot = slot;
        }
        return target;
    }

    bool NavBar::CanDropInto(std::wstring const& id) const
    {
        if (!m_groups || !m_groups->Find(id))
        {
            return false;
        }
        if (IsGroupKey(m_drag.key))
        {
            const std::wstring source{ GroupIdOf(m_drag.key) };
            return id != source && !m_groups->IsInside(GroupKey(id), source);
        }
        return true;
    }

    void NavBar::ShowTarget(DropTarget const& target)
    {
        const bool barLine = target.valid && !target.into && target.level < 0 && target.slot != SIZE_MAX && target.slot <= m_visibleCount;
        if (m_barMarker)
        {
            if (barLine)
            {
                double x = kSideMargin;
                if (target.slot < m_visibleCount)
                {
                    x = m_entries[target.slot].button.TransformToVisual(m_root).TransformPoint({ 0, 0 }).X - kItemSpacing / 2;
                }
                else if (m_visibleCount > 0)
                {
                    const auto last = m_entries[m_visibleCount - 1].button;
                    x = last.TransformToVisual(m_root).TransformPoint({ 0, 0 }).X + last.ActualWidth() + kItemSpacing / 2;
                }
                muxc::Canvas::SetLeft(m_barMarker, x - 1);
                muxc::Canvas::SetTop(m_barMarker, (m_root.ActualHeight() - kMarkerLength) / 2);
            }
            m_barMarker.Visibility(barLine ? mux::Visibility::Visible : mux::Visibility::Collapsed);
        }
        for (size_t k = 0; k < m_levels.size(); ++k)
        {
            Level& level = *m_levels[k];
            if (!level.marker)
            {
                continue;
            }
            const bool line = target.valid && !target.into && target.level == static_cast<int>(k) && target.slot <= level.rows.size();
            if (line)
            {
                double y = 0;
                if (target.slot < level.rows.size())
                {
                    y = level.rows[target.slot].button.TransformToVisual(level.root).TransformPoint({ 0, 0 }).Y;
                }
                else if (!level.rows.empty())
                {
                    const auto last = level.rows.back().button;
                    y = last.TransformToVisual(level.root).TransformPoint({ 0, 0 }).Y + last.ActualHeight();
                }
                level.marker.Width(std::max(0.0, level.root.ActualWidth() - 16));
                muxc::Canvas::SetLeft(level.marker, 8);
                muxc::Canvas::SetTop(level.marker, std::max(0.0, y - 1));
            }
            level.marker.Visibility(line ? mux::Visibility::Visible : mux::Visibility::Collapsed);
        }
        // Il gruppo che riceverà la voce: evidenziato come al passaggio del puntatore.
        if (m_drag.target.into != target.into)
        {
            if (m_drag.target.into)
            {
                mux::VisualStateManager::GoToState(m_drag.target.into, L"Normal", true);
            }
            if (target.into)
            {
                mux::VisualStateManager::GoToState(target.into, L"PointerOver", true);
            }
        }
        if (target.hover != m_drag.target.hover)
        {
            m_drag.hoverSince = target.hover ? GetTickCount64() : 0;
        }
        m_drag.target = target;
    }

    void NavBar::OnDragReleased(muxi::PointerRoutedEventArgs const& args)
    {
        if (g_dragDebug)
        {
            XPX_MLOG(L"NavBar[trascinamento]: rilascio, armato %d, attivo %d, bersaglio %d.", m_drag.armed ? 1 : 0, m_drag.active ? 1 : 0,
                     m_drag.target.valid ? 1 : 0);
        }
        if (!m_drag.armed || m_drag.test || args.Pointer().PointerId() != m_drag.pointer)
        {
            return;
        }
        if (m_drag.polled)
        {
            // Staccato: rilasciata sulla barra o su un elenco (le finestre da cui arriva l'evento), dove è il puntatore ora.
            args.Handled(true);
            GetCursorPos(&m_drag.lastCursor);
            ShowTarget(HitTest([&](mux::UIElement const& element) { return args.GetCurrentPoint(element).Position(); }));
            FinishDrag();
            return;
        }
        const Drag drag = m_drag;
        EndDrag();
        if (!drag.active)
        {
            return; // un clic: lo gestisce il pulsante
        }
        args.Handled(true);
        DropDragged(drag);
    }

    void NavBar::EndDrag()
    {
        const Drag drag = m_drag;
        m_drag = {};
        m_testDragCursor.reset();
        m_testDragDown = false;
        if (m_dragTimer)
        {
            m_dragTimer.Stop();
        }
        if (!drag.active)
        {
            return;
        }
        if (drag.source)
        {
            drag.source.Opacity(1.0);
        }
        ClearTargetVisuals(drag.target);
        if (drag.holder)
        {
            drag.holder.ReleasePointerCaptures();
        }
    }

    void NavBar::ClearTargetVisuals(DropTarget const& target)
    {
        if (target.into)
        {
            mux::VisualStateManager::GoToState(target.into, L"Normal", true);
        }
        if (m_barMarker)
        {
            m_barMarker.Visibility(mux::Visibility::Collapsed);
        }
        for (auto const& level : m_levels)
        {
            if (level->marker)
            {
                level->marker.Visibility(mux::Visibility::Collapsed);
            }
        }
    }

    // --- Cartelle trascinate da fuori ------------------------------------------------------------------------
    // Da Esplora file, dal desktop o da un altro programma, come un link sulla barra dei segnalibri di Chrome: la linea
    // segna il posto (al primo livello solo nel blocco delle cartelle aggiunte), il gruppo sotto il puntatore si evidenzia
    // se la cartella ci andrà dentro, e accanto all'immagine del trascinamento compare "Aggiungi ad Accesso rapido" (o
    // "Aggiungi a <gruppo>"). Al rilascio la cartella si aggiunge ad Accesso rapido in quel posto; una già aggiunta si
    // sposta lì. Gli elenchi non si aprono: sono finestre a parte, dove il trascinamento non arriva alla barra.

    DWORD NavBar::OnExternalDragEnter(IDataObject* data, POINT screen, DWORD allowed)
    {
        OnExternalDragLeave();
        if (!m_root || m_drag.armed || m_menuOpen)
        {
            return DROPEFFECT_NONE; // un trascinamento della barra, o un menu modale, in corso
        }
        m_external.active = true;
        m_external.folders = FoldersOf(data);
        m_external.data.copy_from(data);
        CloseLevels(0);
        m_drag = {};
        // Una sola cartella già nella barra: lasciata accanto a sé stessa non si sposta (come trascinandola dalla barra).
        if (m_external.folders.size() == 1)
        {
            m_drag.key = m_external.folders.front().key;
        }
        XPX_MLOG(L"NavBar: trascinamento da fuori sopra la barra (%zu %s).", m_external.folders.size(),
                 m_external.folders.size() == 1 ? L"cartella" : L"cartelle");
        return OnExternalDragOver(screen, allowed);
    }

    DWORD NavBar::OnExternalDragOver(POINT screen, DWORD allowed)
    {
        if (!m_external.active || m_external.folders.empty() || !m_root)
        {
            return DROPEFFECT_NONE;
        }
        DropTarget target = HitTestScreen(screen);
        if (target.level >= 0)
        {
            target = {}; // (con gli elenchi chiusi non succede: solo la barra)
        }
        target.hover = nullptr; // da fuori gli elenchi non si aprono
        target.hoverAnchor = nullptr;
        ShowTarget(target);
        const DWORD effect = ExternalEffect(target, allowed);
        DescribeExternal(target, effect);
        return effect;
    }

    void NavBar::OnExternalDragLeave()
    {
        if (!m_external.active)
        {
            return;
        }
        if (!m_external.described.empty())
        {
            SetDropDescription(m_external.data.get(), DROPIMAGE_INVALID, {}, {});
        }
        ClearTargetVisuals(m_drag.target);
        m_drag = {};
        m_external = {};
    }

    DWORD NavBar::OnExternalDrop(IDataObject* data, POINT screen, DWORD allowed)
    {
        if (!m_external.active)
        {
            OnExternalDragEnter(data, screen, allowed); // (rilascio senza ingresso: per sicurezza si rifà tutto)
        }
        const DWORD effect = OnExternalDragOver(screen, allowed);
        const DropTarget target = m_drag.target;
        const std::vector<DroppedFolder> folders = m_external.folders;
        OnExternalDragLeave(); // linea, evidenziazione e descrizione via
        if (effect == DROPEFFECT_NONE || !target.valid || folders.empty())
        {
            XPX_MLOG(L"NavBar: trascinamento da fuori lasciato fuori posto, o senza cartelle: nulla aggiunto.");
            return DROPEFFECT_NONE;
        }
        DropFolders(folders, target);
        return effect;
    }

    DWORD NavBar::ExternalEffect(DropTarget const& target, DWORD allowed) const
    {
        if (!target.valid)
        {
            return DROPEFFECT_NONE;
        }
        // Un collegamento (come "Aggiungi ad Accesso rapido"); copia solo se la sorgente non lo ammette. Mai spostamento.
        if (allowed & DROPEFFECT_LINK)
        {
            return DROPEFFECT_LINK;
        }
        return (allowed & DROPEFFECT_COPY) ? DROPEFFECT_COPY : DROPEFFECT_NONE;
    }

    void NavBar::DescribeExternal(DropTarget const& target, DWORD effect)
    {
        std::wstring wanted;
        std::wstring group;
        if (effect != DROPEFFECT_NONE)
        {
            if (target.into)
            {
                const NavGroup* found = m_groups ? m_groups->Find(target.container) : nullptr;
                group = found ? found->name : std::wstring{};
                wanted = L"g:" + target.container + L"|" + group;
            }
            else
            {
                wanted = L"top";
            }
            wanted += effect == DROPEFFECT_LINK ? L"|link" : L"|copy";
        }
        if (wanted == m_external.described)
        {
            return;
        }
        m_external.described = wanted;
        const DROPIMAGETYPE image = effect == DROPEFFECT_LINK ? DROPIMAGE_LINK : DROPIMAGE_COPY;
        if (wanted.empty())
        {
            SetDropDescription(m_external.data.get(), DROPIMAGE_INVALID, {}, {});
        }
        else if (target.into)
        {
            SetDropDescription(m_external.data.get(), image, Tr(L"Add to %1", L"Aggiungi a %1"), group);
        }
        else
        {
            SetDropDescription(m_external.data.get(), image, Tr(L"Pin to Quick access", L"Aggiungi ad Accesso rapido"), {});
        }
    }

    void NavBar::DropFolders(std::vector<DroppedFolder> const& folders, DropTarget const& target)
    {
        const std::wstring container = target.container; // "" = primo livello
        const std::wstring before = target.into ? std::wstring{} : target.before;
        NavPin pin;
        if (container.empty())
        {
            pin.before = WindowsAnchorFor(before);
        }
        size_t moved = 0;
        const ULONGLONG until = GetTickCount64() + kPendingDropMs;
        for (auto const& folder : folders)
        {
            if (IsDraggable(folder.key))
            {
                moved += PlaceNode(folder.key, container, before) ? 1 : 0; // già aggiunta: si sposta lì
                continue;
            }
            pin.items.push_back(folder.idList);
            m_pendingDrops.push_back(PendingDrop{ folder.key, container, before, until });
        }
        const size_t fresh = pin.items.size();
        NavItems::Instance().PinFolders(std::move(pin));
        XPX_MLOG(L"NavBar: %zu %s da fuori %s: %zu da aggiungere ad Accesso rapido, %zu già aggiunte spostate.", folders.size(),
                 folders.size() == 1 ? L"cartella lasciata" : L"cartelle lasciate", container.empty() ? L"nella barra" : L"in un gruppo", fresh, moved);
    }

    std::vector<uint8_t> NavBar::WindowsAnchorFor(std::wstring const& before) const
    {
        if (before.empty() || !m_snapshot)
        {
            return {};
        }
        bool reached = false;
        for (size_t e = m_blockFirst; e < m_blockEnd && e < m_entries.size(); ++e)
        {
            reached = reached || m_entries[e].key == before;
            if (reached && IsFolderKey(m_entries[e].key))
            {
                if (const auto item = ItemOf(m_entries[e].key); item && m_snapshot->items[*item].pinned)
                {
                    return m_snapshot->items[*item].idList;
                }
            }
        }
        return {};
    }

    void NavBar::PlacePendingDrops()
    {
        if (m_pendingDrops.empty() || m_drag.armed || m_external.active)
        {
            return;
        }
        const ULONGLONG now = GetTickCount64();
        std::vector<PendingDrop> pending;
        pending.swap(m_pendingDrops);
        for (auto& drop : pending)
        {
            if (IsDraggable(drop.key))
            {
                // Nel gruppo, o accanto a un gruppo, e l'ordine di Windows come la barra: come trascinandola dalla barra.
                const bool placed = PlaceNode(drop.key, drop.container, drop.before);
                XPX_MLOG(L"NavBar: cartella trascinata da fuori ora nella barra%s.", placed ? L", messa al suo posto" : L" (già al suo posto)");
            }
            else if (now < drop.until)
            {
                m_pendingDrops.push_back(std::move(drop));
            }
            else
            {
                XPX_MLOG(L"NavBar: cartella trascinata da fuori non comparsa in Accesso rapido entro %llu s.", kPendingDropMs / 1000);
            }
        }
    }

    // --- Menu e geometria ------------------------------------------------------------------------------------

    std::optional<size_t> NavBar::EntryAt(POINT screen) const
    {
        if (!m_hwnd || !m_root || !IsWindowVisible(m_hwnd))
        {
            return std::nullopt;
        }
        RECT island{};
        GetWindowRect(m_hwnd, &island);
        if (!PtInRect(&island, screen))
        {
            return std::nullopt;
        }
        const double scale = GetDpiForWindow(m_main) / 96.0;
        const double x = (screen.x - island.left) / scale;
        const double y = (screen.y - island.top) / scale;
        for (size_t i = 0; i < m_entries.size(); ++i)
        {
            const auto button = m_entries[i].button;
            if (!button || button.Visibility() != mux::Visibility::Visible)
            {
                continue;
            }
            const auto origin = button.TransformToVisual(nullptr).TransformPoint({ 0, 0 });
            if (x >= origin.X && x < origin.X + button.ActualWidth() && y >= origin.Y && y < origin.Y + button.ActualHeight())
            {
                return i;
            }
        }
        return std::nullopt;
    }

    std::optional<POINT> NavBar::CenterOf(size_t index) const
    {
        if (index >= m_entries.size())
        {
            return std::nullopt;
        }
        return CenterOfElement(m_entries[index].button);
    }

    std::optional<POINT> NavBar::CenterOfElement(mux::FrameworkElement const& element) const
    {
        // Solo per gli elementi della barra (gli elenchi sono in finestre a parte).
        if (!element || !m_hwnd || element.Visibility() != mux::Visibility::Visible)
        {
            return std::nullopt;
        }
        const auto origin = element.TransformToVisual(nullptr).TransformPoint({ 0, 0 });
        RECT island{};
        GetWindowRect(m_hwnd, &island);
        const double scale = GetDpiForWindow(m_main) / 96.0;
        return POINT{ island.left + static_cast<LONG>((origin.X + element.ActualWidth() / 2) * scale),
                      island.top + static_cast<LONG>((origin.Y + element.ActualHeight() / 2) * scale) };
    }

    void NavBar::OnTabDestroying(HWND tab)
    {
        if (m_modernMenu && tab == m_modernMenuTab)
        {
            m_modernMenu.reset();
            m_modernMenuTab = nullptr;
            m_menuRetryPending = false;
        }
    }

    bool NavBar::OnContextMenuAt(POINT screen)
    {
        // Solo la riapertura chiesta dal presenter (menu rimandato perché quello di prima si chiudeva): un
        // WM_CONTEXTMENU qualunque arrivato alla finestra non apre menu (il clic destro passa da RightTapped).
        if (!m_menuRetryPending || !m_root || !m_snapshot)
        {
            return false;
        }
        const auto entry = EntryAt(screen);
        const auto item = entry ? ItemOf(m_entries[*entry].key) : std::nullopt;
        if (!item)
        {
            return false;
        }
        m_menuRetryPending = false;
        ShowContextMenu(*item, screen, ModernMenu::kFromRightClick);
        return true;
    }

    // --- Comandi di prova ------------------------------------------------------------------------------------

    void NavBar::DevCommand(std::wstring const& command)
    {
        int index = -1;
        int slot = -1;
        const auto entryAt = [&](int i) -> Entry const* {
            return i >= 0 && static_cast<size_t>(i) < m_entries.size() ? &m_entries[static_cast<size_t>(i)] : nullptr;
        };
        if (swscanf_s(command.c_str(), L"navbar open %d", &index) == 1 && entryAt(index))
        {
            Entry const& entry = *entryAt(index);
            if (entry.level)
            {
                ShowLevel(entry.level, entry.button, false);
            }
            else if (const auto item = ItemOf(entry.key))
            {
                Open(*item, false);
            }
        }
        else if (swscanf_s(command.c_str(), L"navbar newtab %d", &index) == 1 && entryAt(index))
        {
            OnItemClick(entryAt(index)->key, true);
        }
        else if (swscanf_s(command.c_str(), L"navbar move %d %d", &index, &slot) == 2 && index >= 0 && slot >= 0)
        {
            // Come un trascinamento lasciato prima della voce "slot" (con gli stessi limiti).
            const size_t from = static_cast<size_t>(index);
            const size_t clamped = std::clamp(static_cast<size_t>(slot), m_blockFirst, m_blockEnd);
            if (from >= m_blockFirst && from < m_blockEnd && clamped != from && clamped != from + 1)
            {
                PlaceNode(m_entries[from].key, L"", clamped < m_blockEnd ? m_entries[clamped].key : std::wstring{});
            }
            else
            {
                XPX_MLOG(L"NavBar: spostamento di prova %d -> %d non valido o senza effetto.", index, slot);
            }
        }
        else if ((swscanf_s(command.c_str(), L"navbar context %d", &index) == 1 || swscanf_s(command.c_str(), L"navbar classic %d", &index) == 1) &&
                 entryAt(index))
        {
            // Al centro del pulsante (se è nella barra), altrimenti dove si trova il cursore.
            Entry const& entry = *entryAt(index);
            POINT point{};
            GetCursorPos(&point);
            if (const auto center = CenterOf(static_cast<size_t>(index)))
            {
                point = *center;
            }
            if (entry.level)
            {
                ShowGroupMenu(entry.level->group, entry.button, std::nullopt);
            }
            else if (const auto item = ItemOf(entry.key))
            {
                ShowContextMenu(*item, point, ModernMenu::kFromRightClick, command.rfind(L"navbar classic", 0) == 0);
            }
        }
        else if (command == L"navbar dragdebug on" || command == L"navbar dragdebug off")
        {
            g_dragDebug = command == L"navbar dragdebug on";
        }
        else if (command == L"navbar more")
        {
            if (m_overflow && m_overflow.Visibility() == mux::Visibility::Visible)
            {
                ShowLevel(m_overflowLevel, m_overflow, false);
            }
        }
        else if (command == L"navbar closemenu")
        {
            CloseLevels(0);
            if (m_nameEditor)
            {
                m_nameEditor.Hide();
            }
            if (m_modernMenu)
            {
                m_modernMenu->Dismiss();
            }
        }
        else if (command.rfind(L"navbar group ", 0) == 0)
        {
            DevGroupCommand(command.substr(13));
        }
        else if (command == L"navbar info" && m_snapshot)
        {
            // Diagnosi senza nomi né percorsi: tipo, lunghezza del nome, icona.
            for (size_t i = 0; i < m_snapshot->items.size(); ++i)
            {
                NavItem const& item = m_snapshot->items[i];
                const wchar_t* kind = item.parsing.rfind(L"::", 0) == 0     ? L"shell"
                                      : item.parsing.rfind(L"\\\\", 0) == 0 ? L"rete"
                                      : item.parsing.size() <= 3            ? L"unità"
                                                                            : L"percorso";
                size_t opaque = 0;
                for (auto const& icon : item.icons)
                {
                    if (icon.size == m_iconPixels)
                    {
                        for (size_t p = 3; p < icon.bgra.size(); p += 4)
                        {
                            opaque += icon.bgra[p] != 0;
                        }
                    }
                }
                XPX_MLOG(L"NavBar: voce %zu: %s%s, nome di %zu caratteri, icona %zu pixel visibili.", i, kind, item.pinned ? L" (aggiunta)" : L"",
                         item.name.size(), opaque);
            }
            std::wstring dividers;
            std::wstring groups;
            for (size_t i = 0; i < m_entries.size(); ++i)
            {
                if (m_entries[i].divider)
                {
                    dividers += (dividers.empty() ? L"" : L", ") + std::to_wstring(i);
                }
                if (IsGroupKey(m_entries[i].key))
                {
                    groups += (groups.empty() ? L"" : L", ") + std::to_wstring(i);
                }
            }
            XPX_MLOG(L"NavBar: divisori prima delle voci: %s.", dividers.empty() ? L"nessuna" : dividers.c_str());
            XPX_MLOG(L"NavBar: nella barra si vedono %zu voci.", m_visibleCount);
            XPX_MLOG(L"NavBar: gruppi nella barra alle voci: %s (blocco %zu-%zu, %zu voci).", groups.empty() ? L"nessuna" : groups.c_str(), m_blockFirst,
                     m_blockEnd, m_entries.size());
            XPX_MLOG(L"NavBar: riquadro di spostamento della scheda attiva %s.", m_layout.NavPaneOff() ? L"spento (barra nascosta)" : L"acceso");
        }
        XPX_MLOG(L"NavBar: comando di prova \"%s\".", command.c_str());
    }

    std::wstring NavBar::KeyByName(std::wstring const& reference) const
    {
        // "g:<nome>" un gruppo, "f:<nome>" una cartella aggiunta (per nome mostrato); senza prefisso prima i gruppi.
        const bool groupOnly = reference.rfind(L"g:", 0) == 0;
        const bool folderOnly = reference.rfind(L"f:", 0) == 0;
        const std::wstring name = groupOnly || folderOnly ? reference.substr(2) : reference;
        if (!folderOnly && m_groups)
        {
            for (auto const& group : m_groups->groups)
            {
                if (_wcsicmp(group.name.c_str(), name.c_str()) == 0)
                {
                    return GroupKey(group.id);
                }
            }
        }
        if (!groupOnly && m_snapshot)
        {
            for (auto const& item : m_snapshot->items)
            {
                if (item.pinned && _wcsicmp(item.name.c_str(), name.c_str()) == 0)
                {
                    return FolderKey(item.parsing);
                }
            }
        }
        return {};
    }

    // "navbar group <comando>", con i riferimenti per nome (KeyByName) e gli argomenti separati da "|":
    //   new <nome> | newin <gruppo>|<nome> | newwith <voce>|<nome>   gruppo nuovo (in fondo al primo livello, in fondo
    //                                                                al gruppo, al posto della voce che conterrà)
    //   rename <gruppo>|<nome> | delete <gruppo> | put <voce>|<gruppo> (dentro, in fondo) | out <cartella>
    //   place <voce>|<gruppo o ->|<prima di questa voce, - o ^>     come un trascinamento lasciato lì (- = in fondo,
    //                                                                ^ = in testa)
    //   context <cartella> | classic <cartella>                      il suo menu (nuovo o classico), anche se è in un gruppo
    //   show <gruppo> (un passo: apre il suo elenco, o quello che lo contiene) | openall <gruppo> | menu <gruppo>
    //   edit <gruppo> (riquadro del nome) | barmenu (menu dello sfondo) | dump (struttura nel registro)
    //   drop <voce>|bar|<voce della barra>|<frazione> o drop <voce>|list|<profondità>|<riga>|<frazione>: il
    //   bersaglio del trascinamento in quel punto (frazione della larghezza o dell'altezza), poi il rilascio lì
    //   drag <voce> | dragto <x>|<y> | dragup | dragesc: un trascinamento vero (timer, elenchi che si aprono restando
    //   fermi, stacco dall'elenco di partenza) guidato da punto e tasto di prova: la pressione sulla voce (nell'elenco
    //   aperto più interno che la mostra, o nella barra), il cursore in pixel dello schermo, il rilascio, Esc
    //   hover <gruppo>|>>|off: il cursore che vede il cambio d'elenco (OnSwitchTimer)
    void NavBar::DevGroupCommand(std::wstring const& command)
    {
        if (!m_groups || !m_snapshot)
        {
            return;
        }
        const size_t space = command.find(L' ');
        const std::wstring verb = command.substr(0, space);
        const std::wstring rest = space == std::wstring::npos ? std::wstring{} : command.substr(space + 1);
        const auto args = Split(rest, L'|');
        const auto arg = [&](size_t i) { return i < args.size() ? args[i] : std::wstring{}; };
        const auto groupId = [&](std::wstring const& reference) {
            const std::wstring key = KeyByName(reference.rfind(L"g:", 0) == 0 ? reference : L"g:" + reference);
            return std::wstring{ GroupIdOf(key) };
        };
        bool done = true;
        if (verb == L"new")
        {
            Apply(NavGroups::Instance().Create(rest, L"", L"", L"", PinnedKeys(), nullptr));
        }
        else if (verb == L"newin" && !groupId(arg(0)).empty())
        {
            Apply(NavGroups::Instance().Create(arg(1), groupId(arg(0)), L"", L"", PinnedKeys(), nullptr));
        }
        else if (verb == L"newwith" && !KeyByName(arg(0)).empty())
        {
            Apply(NavGroups::Instance().Create(arg(1), L"", L"", KeyByName(arg(0)), PinnedKeys(), nullptr));
        }
        else if (verb == L"rename" && !groupId(arg(0)).empty())
        {
            if (const auto state = NavGroups::Instance().Rename(groupId(arg(0)), arg(1)))
            {
                Apply(state);
            }
        }
        else if (verb == L"delete" && !groupId(rest).empty())
        {
            DeleteGroup(groupId(rest));
        }
        else if (verb == L"hover")
        {
            // Il controllo del cambio d'elenco (OnSwitchTimer) vede il cursore sul pulsante del gruppo nella barra (">>" = sul
            // pulsante »); "off" = di nuovo il cursore vero.
            m_testCursor.reset();
            if (rest != L"off")
            {
                std::optional<POINT> center;
                if (rest == L">>")
                {
                    center = CenterOfElement(m_overflow);
                }
                else
                {
                    const std::wstring key = KeyByName(rest.rfind(L"g:", 0) == 0 ? rest : L"g:" + rest);
                    for (size_t i = 0; i < m_entries.size() && i < m_visibleCount; ++i)
                    {
                        if (m_entries[i].key == key)
                        {
                            center = CenterOf(i);
                            break;
                        }
                    }
                }
                if (center)
                {
                    m_testCursor = *center;
                    m_testCursorSince = GetTickCount64();
                }
                else
                {
                    XPX_MLOG(L"NavBar: hover, pulsante non visibile nella barra.");
                }
            }
        }
        else if (verb == L"drag" && !KeyByName(rest).empty())
        {
            if (!StartTestDrag(KeyByName(rest)))
            {
                XPX_MLOG(L"NavBar: trascinamento di prova non iniziato (voce non visibile o non trascinabile).");
            }
        }
        else if (verb == L"dragto" && m_drag.test && args.size() == 2)
        {
            m_testDragCursor = POINT{ _wtoi(arg(0).c_str()), _wtoi(arg(1).c_str()) };
        }
        else if (verb == L"dragup" && m_drag.test)
        {
            m_testDragDown = false; // il timer lascia la voce sul bersaglio dell'ultimo punto
        }
        else if (verb == L"dragesc" && m_drag.test)
        {
            XPX_MLOG(L"NavBar: trascinamento annullato (Esc).");
            EndDrag();
        }
        else if (verb == L"put" && !KeyByName(arg(0)).empty() && !groupId(arg(1)).empty())
        {
            PlaceNode(KeyByName(arg(0)), groupId(arg(1)), L"");
        }
        else if (verb == L"out" && !KeyByName(rest).empty())
        {
            TakeOutOfGroup(KeyByName(rest));
        }
        else if (verb == L"place" && !KeyByName(arg(0)).empty())
        {
            // "^" = prima della prima voce del contenitore (in testa al blocco delle cartelle aggiunte o al gruppo).
            const std::wstring container = arg(1) == L"-" ? std::wstring{} : groupId(arg(1));
            std::wstring before = arg(2) == L"-" ? std::wstring{} : KeyByName(arg(2));
            if (arg(2) == L"^")
            {
                const auto keys = container.empty() ? TopLevel(*m_groups, PinnedKeys()) : VisibleChildren(*m_groups, container, PresentKeys());
                before = keys.empty() ? std::wstring{} : keys.front();
            }
            PlaceNode(KeyByName(arg(0)), container, before);
        }
        else if ((verb == L"context" || verb == L"classic") && ItemOf(KeyByName(rest)))
        {
            // Il menu di una cartella anche se è in un gruppo: al centro del suo pulsante se è nella barra, altrimenti al
            // centro della barra (mai dove si trova il puntatore: potrebbe essere su un'altra finestra).
            const std::wstring key = KeyByName(rest);
            POINT point{};
            RECT island{};
            if (m_hwnd && GetWindowRect(m_hwnd, &island))
            {
                point = POINT{ (island.left + island.right) / 2, (island.top + island.bottom) / 2 };
            }
            for (size_t e = 0; e < m_entries.size() && e < m_visibleCount; ++e)
            {
                if (m_entries[e].key == key)
                {
                    if (const auto center = CenterOf(e))
                    {
                        point = *center;
                    }
                }
            }
            ShowContextMenu(*ItemOf(key), point, ModernMenu::kFromRightClick, verb == L"classic");
        }
        else if (verb == L"show" && !groupId(rest).empty())
        {
            RevealGroup(groupId(rest));
        }
        else if (verb == L"openall" && !groupId(arg(0)).empty())
        {
            // "openall <gruppo>|<ms>": la pausa fra una scheda e l'altra (per le prove).
            OpenAll(groupId(arg(0)), arg(1).empty() ? 0 : static_cast<UINT>(_wtoi(arg(1).c_str())));
        }
        else if (verb == L"menu" && !groupId(rest).empty())
        {
            const std::wstring id = groupId(rest);
            ShowGroupMenu(id, AnchorOf(GroupKey(id)), std::nullopt);
        }
        else if (verb == L"edit" && !groupId(rest).empty())
        {
            EditGroupName(groupId(rest), false);
        }
        else if (verb == L"barmenu")
        {
            ShowBarMenu(Point{ static_cast<float>(std::max(0.0, m_root.ActualWidth() - kOverflowWidth - 40)), static_cast<float>(m_root.ActualHeight() / 2) });
        }
        else if (verb == L"drop" && !KeyByName(arg(0)).empty())
        {
            // Il bersaglio come lo calcola il trascinamento vero, con il punto dato rispetto a un elemento.
            const std::wstring key = KeyByName(arg(0));
            mux::UIElement element{ nullptr };
            Point point{};
            if (arg(1) == L"bar")
            {
                // Solo le voci che si vedono nella barra (quelle in "»" non hanno posizione).
                const size_t entry = static_cast<size_t>(_wtoi(arg(2).c_str()));
                if (entry < m_entries.size() && entry < m_visibleCount)
                {
                    const auto button = m_entries[entry].button;
                    const double fraction = _wtof(arg(3).c_str());
                    const auto origin = button.TransformToVisual(m_root).TransformPoint({ 0, 0 });
                    element = m_root;
                    point = Point{ static_cast<float>(origin.X + button.ActualWidth() * fraction), static_cast<float>(m_root.ActualHeight() / 2) };
                }
            }
            else if (arg(1) == L"list")
            {
                const size_t depth = static_cast<size_t>(_wtoi(arg(2).c_str()));
                const size_t row = static_cast<size_t>(_wtoi(arg(3).c_str()));
                const double fraction = _wtof(arg(4).c_str());
                if (depth < m_levels.size() && m_levels[depth]->open)
                {
                    Level const& level = *m_levels[depth];
                    double y = 0;
                    if (row < level.rows.size())
                    {
                        const auto button = level.rows[row].button;
                        y = button.TransformToVisual(level.root).TransformPoint({ 0, 0 }).Y + button.ActualHeight() * fraction;
                    }
                    else if (!level.rows.empty())
                    {
                        const auto last = level.rows.back().button;
                        y = last.TransformToVisual(level.root).TransformPoint({ 0, 0 }).Y + last.ActualHeight() + 1;
                    }
                    element = level.root;
                    point = Point{ 20, static_cast<float>(y) };
                }
            }
            if (!element)
            {
                XPX_MLOG(L"NavBar: punto di prova non valido.");
                return;
            }
            // Come se si trascinasse quella voce: il punto vale per l'elemento scelto, gli altri sono lontani.
            const std::wstring saved = m_drag.key;
            m_drag.key = key;
            const DropTarget target = HitTest([&](mux::UIElement const& other) {
                if (other == element)
                {
                    return point;
                }
                // Stesso albero (un elenco e la sua radice): si converte; altrimenti lontanissimo.
                try
                {
                    return element.TransformToVisual(other).TransformPoint(point);
                }
                catch (...)
                {
                    return Point{ -100000, -100000 };
                }
            });
            // Le stesse indicazioni del trascinamento vero (linea, gruppo illuminato), lette e poi tolte.
            ShowTarget(target);
            const bool barLine = m_barMarker && m_barMarker.Visibility() == mux::Visibility::Visible;
            int listLine = -1;
            for (size_t k = 0; k < m_levels.size(); ++k)
            {
                if (m_levels[k]->marker && m_levels[k]->marker.Visibility() == mux::Visibility::Visible)
                {
                    listLine = static_cast<int>(k);
                }
            }
            const DropTarget shown = m_drag.target;
            m_drag.target = {};
            m_drag.hoverSince = 0;
            ClearTargetVisuals(shown);
            m_drag.key = saved;
            XPX_MLOG(L"NavBar: bersaglio di prova: %s, %s, linea %d/%zd, dentro %d; a vista: linea nella barra %d, nell'elenco %d.",
                     target.valid ? L"valido" : L"nessuno", target.container.empty() ? L"primo livello" : L"gruppo", target.level,
                     target.slot == SIZE_MAX ? -1 : static_cast<ptrdiff_t>(target.slot), target.into ? 1 : 0, barLine ? 1 : 0, listLine);
            if (target.valid)
            {
                if (target.level < 0 && !target.into)
                {
                    CloseLevels(0);
                }
                PlaceNode(key, target.container, target.into ? std::wstring{} : target.before);
            }
        }
        else if (verb == L"dump")
        {
            // Nomi solo con il file di prova (i gruppi dell'utente restano privati); cartelle solo se di prova (xpx-).
            const bool test = NavGroups::Instance().FilePath().ends_with(L"-test.json");
            const auto label = [&](std::wstring const& key) -> std::wstring {
                if (IsGroupKey(key))
                {
                    return test ? L"g:" + NameOf(key) : L"g:#" + std::wstring{ GroupIdOf(key) };
                }
                const std::wstring name = NameOf(key);
                return name.rfind(L"xpx-", 0) == 0 ? L"f:" + name : L"f:*";
            };
            std::wstring text;
            for (auto const& key : TopLevel(*m_groups, PinnedKeys()))
            {
                text += (text.empty() ? L"" : L", ") + label(key);
            }
            XPX_MLOG(L"NavBar[gruppi]: primo livello: %s.", text.c_str());
            const auto present = PresentKeys();
            for (auto const& group : m_groups->groups)
            {
                std::wstring children;
                for (auto const& key : VisibleChildren(*m_groups, group.id, present))
                {
                    children += (children.empty() ? L"" : L", ") + label(key);
                }
                XPX_MLOG(L"NavBar[gruppi]: %s = [%s].", label(GroupKey(group.id)).c_str(), children.c_str());
            }
            std::wstring open;
            for (auto const& level : m_levels)
            {
                open += (open.empty() ? L"" : L" > ") + (level->group.empty() ? std::wstring{ L"»" } : label(GroupKey(level->group)));
            }
            XPX_MLOG(L"NavBar[gruppi]: elenchi aperti: %s.", open.empty() ? L"nessuno" : open.c_str());
            // Pulsanti della barra con lo sfondo dell'elenco aperto (SetOpenHighlight), e le animazioni dell'elenco aperto
            const auto highlighted = [](muxc::Button const& button) {
                const auto brush = button ? button.Background().try_as<muxm::SolidColorBrush>() : nullptr;
                return brush && brush.Color().A != 0;
            };
            std::wstring lit;
            for (auto const& entry : m_entries)
            {
                if (highlighted(entry.button))
                {
                    lit += (lit.empty() ? L"" : L", ") + label(entry.key);
                }
            }
            if (highlighted(m_overflow))
            {
                lit += (lit.empty() ? L"" : L", ") + std::wstring{ L"»" };
            }
            XPX_MLOG(L"NavBar[gruppi]: evidenziati: %s; animazioni dell'elenco aperto: %s.", lit.empty() ? L"nessuno" : lit.c_str(),
                     m_levels.empty() ? L"-" : (m_levels.front()->flyout.AreOpenCloseAnimationsEnabled() ? L"sì" : L"no"));
            // Suggerimenti dei gruppi della barra come sono sui pulsanti (UI Automation non li espone): solo il contenuto,
            // la riga dopo il nome.
            std::wstring tips;
            for (auto const& entry : m_entries)
            {
                if (!IsGroupKey(entry.key) || !entry.button)
                {
                    continue;
                }
                std::wstring tip{ winrt::unbox_value_or<winrt::hstring>(muxc::ToolTipService::GetToolTip(entry.button), L"") };
                const size_t newline = tip.find(L'\n');
                tip = newline == std::wstring::npos ? std::wstring{ L"?" } : tip.substr(newline + 1);
                tips += (tips.empty() ? L"" : L"; ") + label(entry.key) + L" = " + tip;
            }
            XPX_MLOG(L"NavBar[gruppi]: suggerimenti: %s.", tips.empty() ? L"nessuno" : tips.c_str());
        }
        else
        {
            done = false;
        }
        if (!done)
        {
            XPX_MLOG(L"NavBar: comando dei gruppi non valido.");
        }
    }
}
