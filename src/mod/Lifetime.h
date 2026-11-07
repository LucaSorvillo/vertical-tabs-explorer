#pragma once

#include <atomic>

// Vita della mod dentro Esplora file. Con "Esci" della tray la mod si toglie del tutto (ModCore::TryUnload): stacca ogni
// finestra sul suo thread, rimette gli agganci originali, ferma i suoi thread e scarica la DLL. Prima deve essere sicuro che
// nessun thread di Esplora file abbia ancora il nostro codice sullo stack.
namespace xpx::mod::lifetime
{
    // Cicli modali aperti dal nostro codice (menu classico, domanda): finché uno è aperto il nostro codice è sullo stack di
    // quel thread (e i callback del dispatcher arrivano dentro il ciclo), quindi la DLL non si scarica.
    std::atomic<int>& ModalDepth();

    struct ModalScope
    {
        ModalScope() noexcept
        {
            ++ModalDepth();
        }
        ~ModalScope()
        {
            --ModalDepth();
        }
        ModalScope(ModalScope const&) = delete;
        ModalScope& operator=(ModalScope const&) = delete;
    };

    // La mod si sta togliendo (o si è tolta) da Esplora file: niente finestre nuove né lavoro nuovo.
    bool Unloading();
}
