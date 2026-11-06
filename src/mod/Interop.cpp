#include "pch.h"
#include "Interop.h"

namespace xpx::mod::interop
{
    namespace
    {
        // Firme di Microsoft.UI.Interop.h (Windows App SDK). WindowId è una struct { UINT64 Value; }
        // con lo stesso layout della proiezione C++/WinRT.
        using PFN_GetWindowIdFromWindow = HRESULT(WINAPI*)(HWND, winrt::Microsoft::UI::WindowId*);
        using PFN_GetWindowFromWindowId = HRESULT(WINAPI*)(winrt::Microsoft::UI::WindowId, HWND*);

        template <typename T>
        T UdkExport(const char* name)
        {
            const HMODULE udk = GetModuleHandleW(L"Microsoft.Internal.FrameworkUdk.dll");
            return udk ? reinterpret_cast<T>(GetProcAddress(udk, name)) : nullptr;
        }
    }

    winrt::Microsoft::UI::WindowId WindowIdFromHwnd(HWND hwnd)
    {
        static const auto fn = UdkExport<PFN_GetWindowIdFromWindow>("Windowing_GetWindowIdFromWindow");
        winrt::Microsoft::UI::WindowId id{};
        if (fn && SUCCEEDED(fn(hwnd, &id)))
        {
            return id;
        }
        return winrt::Microsoft::UI::WindowId{ static_cast<uint64_t>(reinterpret_cast<uintptr_t>(hwnd)) };
    }

    HWND HwndFromWindowId(winrt::Microsoft::UI::WindowId const& id)
    {
        static const auto fn = UdkExport<PFN_GetWindowFromWindowId>("Windowing_GetWindowFromWindowId");
        HWND hwnd = nullptr;
        if (fn && SUCCEEDED(fn(id, &hwnd)) && hwnd)
        {
            return hwnd;
        }
        return reinterpret_cast<HWND>(static_cast<uintptr_t>(id.Value));
    }

    HWND MainWindowOf(winrt::Microsoft::UI::Xaml::UIElement const& element)
    {
        HWND main = nullptr;
        try
        {
            if (const auto xamlRoot = element.XamlRoot())
            {
                if (const auto environment = xamlRoot.ContentIslandEnvironment())
                {
                    main = HwndFromWindowId(environment.AppWindowId());
                }
            }
        }
        catch (...)
        {
        }
        if (main)
        {
            main = GetAncestor(main, GA_ROOT);
        }
        return main && IsWindow(main) ? main : nullptr;
    }

    RECT RectInClientOf(HWND child, HWND reference)
    {
        RECT rect{};
        GetWindowRect(child, &rect);
        MapWindowPoints(HWND_DESKTOP, reference, reinterpret_cast<POINT*>(&rect), 2);
        return rect;
    }

    std::wstring ClassOf(HWND hwnd)
    {
        wchar_t name[256]{};
        GetClassNameW(hwnd, name, ARRAYSIZE(name));
        return name;
    }

    std::vector<HWND> DirectChildren(HWND parent)
    {
        std::vector<HWND> children;
        for (HWND child = GetWindow(parent, GW_CHILD); child; child = GetWindow(child, GW_HWNDNEXT))
        {
            children.push_back(child);
        }
        return children;
    }

    winrt::Microsoft::UI::Xaml::FrameworkElement FindByName(winrt::Microsoft::UI::Xaml::DependencyObject const& root, std::wstring_view name)
    {
        using winrt::Microsoft::UI::Xaml::Media::VisualTreeHelper;
        if (!root)
        {
            return nullptr;
        }
        std::vector<winrt::Microsoft::UI::Xaml::DependencyObject> pending{ root };
        while (!pending.empty())
        {
            const auto node = pending.back();
            pending.pop_back();
            if (const auto element = node.try_as<winrt::Microsoft::UI::Xaml::FrameworkElement>(); element && element.Name() == name)
            {
                return element;
            }
            const int count = VisualTreeHelper::GetChildrenCount(node);
            for (int i = count - 1; i >= 0; --i)
            {
                pending.push_back(VisualTreeHelper::GetChild(node, i));
            }
        }
        return nullptr;
    }
}
