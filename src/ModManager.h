#pragma once
// Schede verticali: la tray porta la mod (VTabsExplorerMod.dll) dentro i processi explorer.exe che hanno
// finestre di Esplora file aperte, solo se la funzione è attiva ([VerticalTabs] Enabled=1). Nessuna
// finestra aperta, nessuna iniezione: la mod entra quando serve, senza riavviare nulla.
//
// Sicurezza: con l'impostazione predefinita di Windows le finestre di Esplora file vivono nello stesso
// processo della barra delle applicazioni e del desktop. Ogni processo con la mod caricata viene
// sorvegliato: se termina in modo anomalo (codice di errore NTSTATUS, es. 0xC0000005 o un fail fast),
// la funzione viene sospesa subito (Enabled=0, Suspended=1) e l'utente avvisato; non si inietta più
// finché non la riattiva dal menu della tray. La mod non si può scaricare: una versione nuova entra in
// un processo solo dopo il suo riavvio.

#include <windows.h>

#include <functional>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

namespace xpx
{
    class ModManager
    {
    public:
        ModManager() = default;
        ~ModManager();
        ModManager(ModManager const&) = delete;
        ModManager& operator=(ModManager const&) = delete;

        // notify: messaggio per l'utente (chiamato dal thread di controllo).
        void Start(std::function<void(std::wstring const&)> notify);
        void Stop();
        // Ricontrolla subito (es. impostazione cambiata dal menu).
        void Wake();

    private:
        struct Tracked
        {
            DWORD pid = 0;
            HANDLE process = nullptr; // per accorgersi della sua fine e leggerne il codice d'uscita
            bool injectedByUs = false;
        };
        struct Attempt
        {
            DWORD pid = 0;
            ULONGLONG at = 0;
            int failures = 0;
            bool checkedWithoutMod = false; // controllato: la mod non c'è (con la funzione spenta non si ricontrolla subito)
        };

        // Processo con finestre di Esplora file visibili; settled = nessuna appena comparsa o velata.
        struct ProcessWindows
        {
            DWORD pid = 0;
            bool settled = true;
        };

        void Run();
        void Tick();
        std::vector<ProcessWindows> ScanWindows(ULONGLONG now);
        void Track(DWORD pid, bool injectedByUs);
        bool IsTracked(DWORD pid) const;
        void OnProcessExit(Tracked const& tracked);
        Attempt& AttemptFor(DWORD pid);

        HANDLE m_stop = nullptr;
        HANDLE m_wake = nullptr;
        std::thread m_thread;
        std::function<void(std::wstring const&)> m_notify;
        // Solo sul thread di controllo
        std::vector<Tracked> m_tracked;
        std::vector<Attempt> m_attempts;
        // Ultimo controllo che la mod sia ancora nei processi sorvegliati: con "Esci" la mod si toglie da sola, e se la tray
        // riparte mentre si sta togliendo la trova ancora lì; quando poi sparisce, la si rimette.
        ULONGLONG m_lastVerify = 0;
        std::unordered_map<HWND, ULONGLONG> m_firstSeen; // quando è comparsa ogni finestra di Esplora file
    };
}
