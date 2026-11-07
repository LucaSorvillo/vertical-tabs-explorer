#pragma once

#include <oleidl.h>
#include <shlobj.h>

namespace xpx::mod
{
    class NavBar;

    // Cartelle trascinate da fuori (Esplora file, il desktop, un altro programma) sulla barra delle cartelle, come i link
    // trascinati sulla barra dei segnalibri di Chrome: si aggiungono ad Accesso rapido dove le si lascia (o in un gruppo).
    struct DroppedFolder
    {
        std::vector<uint8_t> idList; // PIDL assoluto
        std::wstring key;            // chiave della barra (FolderKey del nome di analisi)
        std::wstring name;           // nome mostrato
    };

    // Le cartelle di un oggetto trascinato, nell'ordine: non i file, né gli archivi (.zip: cartelle che sono anche file).
    std::vector<DroppedFolder> FoldersOf(IDataObject* data);

    // Testo accanto all'immagine del trascinamento ("Aggiungi ad Accesso rapido"; %1 diventa insert). DROPIMAGE_INVALID
    // lo toglie: lo si fa uscendo dalla barra e al rilascio, come chiede Windows a chi lo mette.
    void SetDropDescription(IDataObject* data, DROPIMAGETYPE type, std::wstring const& message, std::wstring const& insert);

    // Bersaglio OLE (RegisterDragDrop) dell'isola della barra, e della finestra di ogni elenco aperto durante un trascinamento
    // da fuori: passa tutto alla barra, che può non esserci più, e tiene l'immagine del trascinamento di Windows anche sopra
    // di lei (IDropTargetHelper, per la finestra hwnd). Sul thread UI della finestra.
    winrt::com_ptr<IDropTarget> MakeNavDropTarget(std::weak_ptr<NavBar> bar, HWND hwnd);
}
