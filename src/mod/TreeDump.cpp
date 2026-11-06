#include "pch.h"
#include "TreeDump.h"

#include "ModLog.h"

namespace xpx::mod::TreeDump
{
    namespace
    {
        namespace mux = winrt::Microsoft::UI::Xaml;
        namespace muxc = winrt::Microsoft::UI::Xaml::Controls;
        namespace muxm = winrt::Microsoft::UI::Xaml::Media;
        using winrt::Windows::Foundation::IInspectable;

        std::wstring Num(double value)
        {
            if (std::isnan(value))
            {
                return L"NaN";
            }
            if (std::isinf(value))
            {
                return L"Inf";
            }
            wchar_t buffer[32];
            swprintf_s(buffer, L"%g", value);
            return buffer;
        }

        std::wstring Quote(winrt::hstring const& text)
        {
            std::wstring value{ text };
            for (auto& ch : value)
            {
                if (ch == L'\r' || ch == L'\n' || ch == L'\t')
                {
                    ch = L' ';
                }
            }
            if (value.size() > 60)
            {
                value = value.substr(0, 60) + L"…";
            }
            return L"\"" + value + L"\"";
        }

        std::wstring ClassName(IInspectable const& object)
        {
            try
            {
                return object ? std::wstring{ winrt::get_class_name(object) } : std::wstring{};
            }
            catch (...)
            {
                return L"?";
            }
        }

        // Pennello: colore #AARRGGBB per quelli a tinta unita, altrimenti il tipo (acrilico, gradiente...).
        std::wstring BrushText(muxm::Brush const& brush)
        {
            if (!brush)
            {
                return L"null";
            }
            std::wstring text;
            if (const auto solid = brush.try_as<muxm::SolidColorBrush>())
            {
                const auto c = solid.Color();
                wchar_t buffer[16];
                swprintf_s(buffer, L"#%02X%02X%02X%02X", c.A, c.R, c.G, c.B);
                text = buffer;
            }
            else
            {
                text = ClassName(brush);
                const auto dot = text.find_last_of(L'.');
                text = dot == std::wstring::npos ? text : text.substr(dot + 1);
            }
            if (brush.Opacity() != 1.0)
            {
                text += L"*" + Num(brush.Opacity());
            }
            return text;
        }

        std::wstring ThicknessText(mux::Thickness const& t)
        {
            if (t.Left == t.Top && t.Top == t.Right && t.Right == t.Bottom)
            {
                return Num(t.Left);
            }
            return Num(t.Left) + L"," + Num(t.Top) + L"," + Num(t.Right) + L"," + Num(t.Bottom);
        }

        std::wstring CornerText(mux::CornerRadius const& r)
        {
            if (r.TopLeft == r.TopRight && r.TopRight == r.BottomRight && r.BottomRight == r.BottomLeft)
            {
                return Num(r.TopLeft);
            }
            return Num(r.TopLeft) + L"," + Num(r.TopRight) + L"," + Num(r.BottomRight) + L"," + Num(r.BottomLeft);
        }

        // Sfondo, bordo e angoli (per capire chi dipinge cosa nell'intestazione): "!" = valore locale
        // (impostato sull'elemento, non dallo stile o dal modello).
        std::wstring PaintText(mux::DependencyObject const& node)
        {
            std::wstring text;
            const auto local = [&](mux::DependencyProperty const& property) {
                return node.ReadLocalValue(property) != mux::DependencyProperty::UnsetValue() ? L"!" : L"";
            };
            const auto add = [&](wchar_t const* name, muxm::Brush const& brush, mux::DependencyProperty const& property) {
                if (brush)
                {
                    text += std::wstring(L" ") + name + L"=" + BrushText(brush) + local(property);
                }
            };
            const auto addFrame = [&](mux::Thickness const& thickness, mux::CornerRadius const& corner) {
                if (thickness.Left || thickness.Top || thickness.Right || thickness.Bottom)
                {
                    text += L" bt=" + ThicknessText(thickness);
                }
                if (corner.TopLeft || corner.TopRight || corner.BottomRight || corner.BottomLeft)
                {
                    text += L" cr=" + CornerText(corner);
                }
            };
            try
            {
                if (const auto border = node.try_as<muxc::Border>())
                {
                    add(L"bg", border.Background(), muxc::Border::BackgroundProperty());
                    add(L"bb", border.BorderBrush(), muxc::Border::BorderBrushProperty());
                    addFrame(border.BorderThickness(), border.CornerRadius());
                }
                else if (const auto panel = node.try_as<muxc::Panel>())
                {
                    add(L"bg", panel.Background(), muxc::Panel::BackgroundProperty());
                    if (const auto grid = node.try_as<muxc::Grid>())
                    {
                        add(L"bb", grid.BorderBrush(), muxc::Grid::BorderBrushProperty());
                        addFrame(grid.BorderThickness(), grid.CornerRadius());
                    }
                    else if (const auto stack = node.try_as<muxc::StackPanel>())
                    {
                        add(L"bb", stack.BorderBrush(), muxc::StackPanel::BorderBrushProperty());
                        addFrame(stack.BorderThickness(), stack.CornerRadius());
                    }
                }
                else if (const auto control = node.try_as<muxc::Control>())
                {
                    add(L"bg", control.Background(), muxc::Control::BackgroundProperty());
                    add(L"bb", control.BorderBrush(), muxc::Control::BorderBrushProperty());
                    addFrame(control.BorderThickness(), control.CornerRadius());
                }
                else if (const auto presenter = node.try_as<muxc::ContentPresenter>())
                {
                    add(L"bg", presenter.Background(), muxc::ContentPresenter::BackgroundProperty());
                    add(L"bb", presenter.BorderBrush(), muxc::ContentPresenter::BorderBrushProperty());
                    addFrame(presenter.BorderThickness(), presenter.CornerRadius());
                }
                else if (const auto shape = node.try_as<winrt::Microsoft::UI::Xaml::Shapes::Shape>())
                {
                    add(L"fill", shape.Fill(), winrt::Microsoft::UI::Xaml::Shapes::Shape::FillProperty());
                    add(L"stroke", shape.Stroke(), winrt::Microsoft::UI::Xaml::Shapes::Shape::StrokeProperty());
                }
            }
            catch (...)
            {
                text += L" paint=?";
            }
            return text;
        }

        void DumpNode(std::wstring& out, mux::DependencyObject const& node, mux::UIElement const& root, IInspectable const& parentContext, int depth)
        {
            std::wstring line(static_cast<size_t>(depth) * 2, L' ');
            line += ClassName(node);
            IInspectable context = parentContext;

            if (const auto fe = node.try_as<mux::FrameworkElement>())
            {
                if (!fe.Name().empty())
                {
                    line += L" #" + std::wstring{ fe.Name() };
                }
                line += L" [" + Num(fe.ActualWidth()) + L"x" + Num(fe.ActualHeight()) + L"]";
                try
                {
                    const auto origin = fe.TransformToVisual(root).TransformPoint({ 0, 0 });
                    line += L" @" + Num(std::round(origin.X)) + L"," + Num(std::round(origin.Y));
                }
                catch (...)
                {
                }
                if (fe.Visibility() == mux::Visibility::Collapsed)
                {
                    line += L" COLLAPSED";
                }
                if (fe.Opacity() != 1.0)
                {
                    line += L" opacity=" + Num(fe.Opacity());
                }
                const int row = muxc::Grid::GetRow(fe);
                const int column = muxc::Grid::GetColumn(fe);
                if (row || column)
                {
                    line += L" grid(r" + std::to_wstring(row) + L",c" + std::to_wstring(column) + L")";
                }
                // Il DataContext si eredita: si scrive solo dove cambia (lì comincia un modello nuovo).
                try
                {
                    context = fe.DataContext();
                    if (context && context != parentContext)
                    {
                        line += L" dc=" + ClassName(context);
                    }
                }
                catch (...)
                {
                }
            }
            if (const auto grid = node.try_as<muxc::Grid>())
            {
                if (grid.RowDefinitions().Size() > 0)
                {
                    line += L" rows=" + std::to_wstring(grid.RowDefinitions().Size());
                }
                if (grid.ColumnDefinitions().Size() > 0)
                {
                    line += L" cols=" + std::to_wstring(grid.ColumnDefinitions().Size());
                }
            }
            if (const auto textBlock = node.try_as<muxc::TextBlock>())
            {
                line += L" text=" + Quote(textBlock.Text());
            }
            if (const auto tabView = node.try_as<muxc::TabView>())
            {
                line += L" TABVIEW selected=" + std::to_wstring(tabView.SelectedIndex()) + L" items=" + std::to_wstring(tabView.TabItems().Size());
                if (const auto source = tabView.TabItemsSource())
                {
                    line += L" itemsSource=" + ClassName(source);
                }
            }
            else if (const auto items = node.try_as<muxc::ItemsControl>())
            {
                line += L" items=" + std::to_wstring(items.Items().Size());
                if (const auto source = items.ItemsSource())
                {
                    line += L" itemsSource=" + ClassName(source);
                }
            }
            if (const auto tabItem = node.try_as<muxc::TabViewItem>())
            {
                const auto header = tabItem.Header();
                if (const auto text = winrt::unbox_value_or<winrt::hstring>(header, L""); !text.empty())
                {
                    line += L" header=" + Quote(text);
                }
                else if (header)
                {
                    line += L" header=" + ClassName(header);
                }
                if (const auto content = tabItem.Content())
                {
                    line += L" content=" + ClassName(content);
                }
                if (tabItem.IconSource())
                {
                    line += L" icon=" + ClassName(tabItem.IconSource());
                }
            }
            else if (const auto contentControl = node.try_as<muxc::ContentControl>())
            {
                const auto content = contentControl.Content();
                if (const auto text = winrt::unbox_value_or<winrt::hstring>(content, L""); !text.empty())
                {
                    line += L" content=" + Quote(text);
                }
                else if (content && !content.try_as<mux::UIElement>())
                {
                    line += L" content:" + ClassName(content);
                }
            }
            if (const auto image = node.try_as<muxc::Image>())
            {
                if (const auto source = image.Source())
                {
                    line += L" source=" + ClassName(source);
                }
            }
            if (node.try_as<mux::FrameworkElement>())
            {
                const auto automationId = mux::Automation::AutomationProperties::GetAutomationId(node);
                if (!automationId.empty())
                {
                    line += L" aid=" + Quote(automationId);
                }
            }
            line += PaintText(node);

            out += line;
            out += L"\n";

            const int count = muxm::VisualTreeHelper::GetChildrenCount(node);
            for (int i = 0; i < count; ++i)
            {
                DumpNode(out, muxm::VisualTreeHelper::GetChild(node, i), root, context, depth + 1);
            }
        }
    }

    std::wstring DumpWindow(mux::UIElement const& element, std::wstring_view reason)
    {
        static std::atomic<int> counter{ 0 };
        std::wstring path;
        try
        {
            mux::DependencyObject top = element;
            while (const auto parent = muxm::VisualTreeHelper::GetParent(top))
            {
                top = parent;
            }
            const auto root = top.as<mux::UIElement>();

            std::wstring out = L"VTabsExplorer - albero visuale (" + std::wstring{ reason } + L")\n\n";
            // Antenati dell'elemento: dove sta la barra delle schede nell'intestazione.
            out += L"Antenati:\n";
            for (mux::DependencyObject node = element; node; node = muxm::VisualTreeHelper::GetParent(node))
            {
                std::wstring name;
                if (const auto fe = node.try_as<mux::FrameworkElement>())
                {
                    name = fe.Name();
                }
                out += L"  " + ClassName(node) + (name.empty() ? L"" : L" #" + name) + L"\n";
            }
            out += L"\nAlbero:\n";
            DumpNode(out, top, root, nullptr, 0);

            if (const auto xamlRoot = element.XamlRoot())
            {
                const auto popups = muxm::VisualTreeHelper::GetOpenPopupsForXamlRoot(xamlRoot);
                out += L"\nPopup aperti: " + std::to_wstring(popups.Size()) + L"\n";
            }

            path = LogDirectory() + L"\\tree-" + std::to_wstring(GetCurrentProcessId()) + L"-" + std::to_wstring(++counter) + L".txt";
            const HANDLE file = CreateFileW(path.c_str(), GENERIC_WRITE, FILE_SHARE_READ, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
            if (file != INVALID_HANDLE_VALUE)
            {
                const int bytes = WideCharToMultiByte(CP_UTF8, 0, out.c_str(), static_cast<int>(out.size()), nullptr, 0, nullptr, nullptr);
                std::string utf8(static_cast<size_t>(bytes), '\0');
                WideCharToMultiByte(CP_UTF8, 0, out.c_str(), static_cast<int>(out.size()), utf8.data(), bytes, nullptr, nullptr);
                DWORD written = 0;
                WriteFile(file, utf8.data(), static_cast<DWORD>(utf8.size()), &written, nullptr);
                CloseHandle(file);
            }
            XPX_MLOG(L"Albero visuale esportato (%s): %s", std::wstring{ reason }.c_str(), path.c_str());
        }
        catch (...)
        {
            XPX_MLOG(L"Esportazione dell'albero non riuscita: 0x%08X", static_cast<unsigned>(winrt::to_hresult()));
        }
        return path;
    }
}
