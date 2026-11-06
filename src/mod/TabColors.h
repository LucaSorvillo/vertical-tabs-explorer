#pragma once

namespace xpx::mod::tabcolors
{
    // Colori assegnabili alle schede, gli stessi di VTabsNotepad (quelli dei gruppi di schede di Chrome).
    // color è l'indice in kTabColorKeys (ModShared.h): 0 = nessun colore.
    struct Swatch
    {
        int color;
        const wchar_t* displayName;          // nome nel menu
        winrt::Windows::UI::Color fillLight; // tono saturo per il tema chiaro
        winrt::Windows::UI::Color fillDark;  // tono chiaro per il tema scuro
    };

    // Colori disponibili (senza "nessuno"), nell'ordine del menu.
    std::span<const Swatch> Palette();
    Swatch const* Find(int color);
}
