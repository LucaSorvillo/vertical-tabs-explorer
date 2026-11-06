#pragma once

#include "NavGroups.h"
#include "NavItems.h"

namespace xpx::mod
{
    // L'ordine delle cartelle aggiunte in Windows (quello del riquadro di spostamento di sistema, della Home e del menu
    // dell'icona di Esplora file) secondo i gruppi della barra, con la barra accesa o spenta:
    //   - le cartelle stanno come nella barra letta in profondità: quelle di primo livello al loro posto e, al posto di
    //     ogni gruppo, le sue cartelle nel suo ordine, con quelle dei sottogruppi prima di passare alla voce successiva;
    //   - il riquadro di sistema delle schede già aperte non rilegge mai l'ordine (lo prendono solo quelle aperte dopo):
    //     lo si rimette in pari riordinando le voci di primo livello del suo albero come le dà Windows.

    // Chiavi (FolderKey) delle cartelle aggiunte della lettura, nell'ordine di Windows.
    std::vector<std::wstring> PinnedKeysOf(NavSnapshot const& snapshot);

    // Gli spostamenti che mettono le cartelle nell'ordine dei gruppi (top: il primo livello voluto), passati a
    // NavItems::MovePinned. Si spostano solo quelle fuori posto: tutte tranne la sottosequenza crescente più lunga,
    // dall'ultima alla prima, ognuna prima della successiva (già al suo posto). Restituisce la lettura con gli
    // spostamenti già fatti (per mostrarli subito), o null se non ne serve nessuno.
    std::shared_ptr<NavSnapshot> SyncPinnedOrder(NavSnapshot const& snapshot, NavGroupsState const& state, std::vector<std::wstring> const& top);

    // Il riquadro di sistema di ogni scheda della finestra nell'ordine delle voci della lettura (quello di un riquadro
    // appena creato). Va chiamata dal thread della finestra. Quante schede ha riordinato.
    size_t SortNativePanes(HWND main, NavSnapshot const& snapshot);

    // Sistemazione dell'ordine all'avvio, una volta per processo: true solo la prima volta che la si chiede.
    bool TakeStartupOrder();
}
