#pragma once

#include <windows.h>

namespace xpx
{
    // Icona della tray: una cartella a contorno con la barretta piena in basso al centro (richiama Esplora file
    // senza essere la sua icona), monocroma come le icone di sistema: bianca sulla barra delle applicazioni scura,
    // nera su quella chiara. Disegnata con GDI+ alla misura chiesta; null se GDI+ non parte.
    HICON CreateFolderOutlineIcon(int size, bool lightTaskbar);

    // Crea l'icona della tray disegnando un glifo di "Segoe Fluent Icons" (o "Segoe MDL2 Assets"),
    // bianco o nero a seconda del tema della barra delle applicazioni (ripiego di CreateFolderOutlineIcon).
    HICON CreateGlyphIcon(wchar_t glyph, int size, bool lightTaskbar);

    // true se la barra delle applicazioni usa il tema chiaro.
    bool IsTaskbarLight();
}
