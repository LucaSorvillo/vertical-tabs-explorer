#pragma once

#include <shobjidl.h>

namespace xpx::mod
{
    // Più menu contestuali della shell come uno solo, senza verbi doppi: come
    // CContextMenuOnContextMenuArrayNoDuplicateVerbs di ExplorerFrame.dll, con cui il riquadro di spostamento
    // unisce il menu della voce e quello "di sfondo" della cartella (da cui vengono "Nuovo" e simili).
    //  - QueryContextMenu: ogni menu, in ordine, mette le sue voci dove ha finito il precedente, con la sua fascia
    //    di id (uno di distanza fra le fasce); poi si tolgono le voci con un verbo già dato da un menu precedente;
    //  - comandi, verbi (GetCommandString) e messaggi dei sottomenu vanno al menu che ha quell'id; un verbo per
    //    nome si prova menu per menu;
    //  - sito (IObjectWithSite) e restrizioni (IDefaultFolderMenuInitialize) vanno a tutti.
    winrt::com_ptr<IContextMenu> MakeMenuArray(std::vector<winrt::com_ptr<IContextMenu>> menus);

    // Menu "di sfondo" della cartella item (come il riquadro: IShellFolder::CreateViewObject), se è una cartella.
    winrt::com_ptr<IContextMenu> BackgroundMenuOf(IShellItem* item, HWND owner);

    // Toglie i separatori in testa, in coda e doppi (come SHCleanupSeparators).
    void CleanupSeparators(HMENU menu);

    // Toglie dal menu (riempito da menu con i comandi da firstId) il comando con quel verbo (come
    // ContextMenu_DeleteCommandByName del riquadro).
    void DeleteVerb(IContextMenu* menu, HMENU popup, UINT firstId, const wchar_t* verb);
}
