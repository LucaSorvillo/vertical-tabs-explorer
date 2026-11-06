#pragma once
// Colori delle schede (schede verticali), lato tray: lettura dei colori pubblicati dalla mod per ogni
// finestra e scrittura di quelli da rimettere nelle finestre riaperte (formato in ModShared.h).

#include "ModShared.h"

#include <windows.h>

#include <string>
#include <unordered_map>

namespace xpx
{
    // %LOCALAPPDATA%\VTabsExplorer\colors (creata se manca).
    std::wstring ColorsDirectory();
    // Momento dell'ultima modifica della cartella (i file vi si scrivono sostituendoli: cambia a ogni scrittura).
    ULONGLONG ColorsStamp();
    // Colori pubblicati dalla mod (live) e colori da rimettere non ancora presi dalla mod (pending), per
    // finestra (solo finestre che esistono ancora).
    void ReadColorFiles(std::unordered_map<HWND, TabColorList>& live, std::unordered_map<HWND, TabColorList>& pending);
    // Colori da dare alle schede di una finestra appena riaperta (niente file se nessuna è colorata).
    void WritePendingColors(HWND window, TabColorList const& list);
    // Via i file di finestre che non esistono più.
    void CleanupColorFiles();
}
