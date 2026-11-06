#include "pch.h"
#include "TabColors.h"

namespace xpx::mod::tabcolors
{
    namespace
    {
        constexpr winrt::Windows::UI::Color Rgb(uint32_t rgb)
        {
            return { 0xFF, static_cast<uint8_t>(rgb >> 16), static_cast<uint8_t>(rgb >> 8), static_cast<uint8_t>(rgb) };
        }

        // Stessi indici di kTabColorKeys (ModShared.h): grey, blue, red, yellow, green, pink, purple, cyan, orange.
        constexpr Swatch kPalette[] = {
            { 1, L"Grigio", Rgb(0x5F6368), Rgb(0xBDC1C6) },
            { 2, L"Blu", Rgb(0x1A73E8), Rgb(0x8AB4F8) },
            { 3, L"Rosso", Rgb(0xD93025), Rgb(0xF28B82) },
            { 4, L"Giallo", Rgb(0xF9AB00), Rgb(0xFDD663) },
            { 5, L"Verde", Rgb(0x188038), Rgb(0x81C995) },
            { 6, L"Rosa", Rgb(0xD01884), Rgb(0xFF8BCB) },
            { 7, L"Viola", Rgb(0x9334E6), Rgb(0xC58AF9) },
            { 8, L"Azzurro", Rgb(0x007B83), Rgb(0x78D9EC) },
            { 9, L"Arancione", Rgb(0xFA903E), Rgb(0xFCAD70) },
        };
    }

    std::span<const Swatch> Palette()
    {
        return kPalette;
    }

    Swatch const* Find(int color)
    {
        for (auto const& swatch : kPalette)
        {
            if (swatch.color == color)
            {
                return &swatch;
            }
        }
        return nullptr;
    }
}
