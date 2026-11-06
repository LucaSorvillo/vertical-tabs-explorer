#pragma once

namespace xpx::mod::interop
{
    // Proprietà di finestra sull'isola XAML del nostro pannello (la legge anche la tray: vedi Explorer.cpp).
    inline constexpr wchar_t kPanelProp[] = L"VTabsExplorer.VerticalTabsPanel";
    // Lo stesso sull'isola della barra delle cartelle (per chi la cerca da fuori, es. i test).
    inline constexpr wchar_t kNavBarProp[] = L"VTabsExplorer.NavBar";

    // Conversioni HWND <-> WindowId del Windows App SDK. Usano le funzioni esportate da
    // Microsoft.Internal.FrameworkUdk.dll (le stesse di Microsoft.UI.Interop.h); se non sono
    // disponibili ripiegano sull'equivalenza WindowId.Value == HWND valida su desktop.
    winrt::Microsoft::UI::WindowId WindowIdFromHwnd(HWND hwnd);
    HWND HwndFromWindowId(winrt::Microsoft::UI::WindowId const& id);

    // Finestra top-level (CabinetWClass) che ospita l'isola XAML dell'elemento; null se non si trova.
    HWND MainWindowOf(winrt::Microsoft::UI::Xaml::UIElement const& element);

    // Rettangolo di 'child' nelle coordinate client di 'reference'.
    RECT RectInClientOf(HWND child, HWND reference);

    // Nome della classe di finestra.
    std::wstring ClassOf(HWND hwnd);

    // Figli diretti di 'parent' (in ordine Z, dal primo).
    std::vector<HWND> DirectChildren(HWND parent);

    // Primo discendente nell'albero visuale XAML con il nome indicato (x:Name), o null.
    winrt::Microsoft::UI::Xaml::FrameworkElement FindByName(winrt::Microsoft::UI::Xaml::DependencyObject const& root, std::wstring_view name);
}
