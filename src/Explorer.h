#pragma once
// Accesso alle finestre di Esplora file dall'esterno, con le API della shell (nessuna iniezione):
//   - IShellWindows elenca un elemento per ogni scheda (con l'HWND della sua finestra); da ognuno si
//     arriva alla cartella mostrata (IShellBrowser -> IShellView -> IFolderView -> PIDL);
//   - l'ordine visivo delle schede passa dalla barra delle schede (XAML) con UI Automation; la scheda
//     attiva è la prima ShellTabWindowClass nell'ordine Z dei figli;
//   - una scheda nuova si crea con il comando "nuova scheda" della finestra (come Ctrl+T) e poi si
//     porta sulla cartella con IShellBrowser::BrowseObject; la scheda attiva si cambia con i comandi
//     di Ctrl+Tab e Ctrl+Maiusc+Tab; una finestra nuova si apre con il verbo "opennewwindow" (sempre
//     una finestra a sé, anche se ce ne sono altre aperte).
// Da usare su un solo thread con COM inizializzato (STA).

#include "Session.h"

#include <ole2.h> // escluso da WIN32_LEAN_AND_MEAN, serve alle interfacce COM della shell
#include <exdisp.h>
#include <shobjidl.h>
#include <uiautomation.h>
#include <wrl/client.h>

#include <unordered_map>

namespace xpx
{
    class Explorer
    {
    public:
        // Tutte le finestre di Esplora file visibili, dalla più in alto nell'ordine Z. Le schede di cui non
        // si conosce ancora la cartella (vista non pronta) mancano: una finestra può avere zero schede, e
        // questo non vuol dire che sia stata chiusa.
        std::vector<Window> Snapshot();

        // Apre una nuova finestra sulla cartella della scheda; restituisce la finestra (nullptr se non compare).
        // Si può chiamare da più thread insieme (ognuno con il suo Explorer): ogni chiamata riconosce la
        // propria finestra dalla cartella e la "prenota", così due aperture parallele non si confondono.
        HWND OpenWindow(Tab const& tab);
        // Tabs di una finestra, con la cartella (vuota se la vista non è pronta).
        std::vector<Tab> TabsIn(HWND window);
        // true appena si può leggere la cartella di una scheda della finestra (finestra appena aperta).
        bool HasReadableTab(HWND window);
        // Schede di una finestra appena aperta (con la sua scheda): con navigateFirst la scheda esistente
        // va su tabs[0] e le altre si aggiungono dopo, altrimenti si aggiungono tutte dopo quella esistente.
        // Poi rende attiva la scheda active (indice nell'ordine finale) e aspetta che tutte mostrino la loro
        // cartella. Le schede nuove si chiedono una dopo l'altra senza aspettare che carichino (vedi
        // NewTab). false se non è stato possibile crearle tutte; in missing (se c'è) gli indici in tabs
        // di quelle che non sono aperte sulla loro cartella (dopo la prima mancata non se ne creano altre).
        bool BuildTabs(HWND window, std::vector<Tab> const& tabs, bool navigateFirst, int active, std::vector<size_t>* missing = nullptr);
        // Rende attiva la scheda (indice visivo) con UI Automation.
        bool SelectTab(HWND window, int index);
        // Posizione e stato della finestra (se lo schermo di allora esiste ancora).
        void Place(HWND window, WINDOWPLACEMENT placement);
        // Porta la finestra in primo piano.
        void Activate(HWND window);
        // Elementi selezionati nella scheda attiva (PIDL relativi alla sua cartella), al massimo maxItems: ad esempio il file
        // che un programma ha chiesto di mostrare nella cartella (explorer /select).
        std::vector<std::vector<BYTE>> SelectedItems(HWND window, size_t maxItems = 64);
        // Seleziona gli elementi (vedi SelectedItems) nella scheda attiva, appena mostra la cartella folder (il primo con il
        // fuoco e visibile); false se non ci riesce entro timeoutMs.
        bool SelectItems(HWND window, std::wstring const& folder, std::vector<std::vector<BYTE>> const& items, DWORD timeoutMs = 3000);
        // La finestra sta nel desktop virtuale attuale (in caso di dubbio sì).
        static bool OnCurrentDesktop(HWND window);

        // true se la cartella è quella in cui si apre Esplora file (Home, Questo PC o l'impostazione
        // "Apri Esplora file in").
        static bool IsStartLocation(std::wstring const& path);
        // Cartella della scheda ancora utilizzabile (le cartelle di file system che non esistono più no).
        static bool IsRestorable(Tab const& tab);

        // Dimentica i collegamenti con Esplora file (dopo un suo riavvio).
        void Reset();
        // Dimentica l'ordine delle schede tenuto da parte: alla prossima lettura si rilegge dalla barra
        // (un riordino non cambia né le finestre delle schede né i loro titoli).
        void ForgetOrder() { m_orderCache.clear(); }

    private:
        struct LiveTab
        {
            HWND top = nullptr;                               // finestra CabinetWClass
            Microsoft::WRL::ComPtr<IDispatch> dispatch;
            Microsoft::WRL::ComPtr<IUnknown> identity;        // identità COM della scheda
            Microsoft::WRL::ComPtr<IShellBrowser> browser;    // solo con i dettagli
            Tab tab;                                          // solo con i dettagli; path vuoto se la vista non è pronta
        };
        struct CachedOrder
        {
            std::vector<std::pair<HWND, std::wstring>> signature; // schede e titoli quando è stato letto
            std::vector<HWND> order;                              // finestre delle schede nell'ordine visivo
            ULONGLONG tick = 0;
        };
        // Una scheda della barra delle schede secondo UI Automation.
        struct TabItem
        {
            Microsoft::WRL::ComPtr<IUIAutomationElement> element;
            std::wstring name;
            int position = 0; // PositionInSet (da 1), 0 se non disponibile
        };
        // Scheda mandata su una cartella: il titolo che aveva e quello che avrà (nome della cartella).
        struct Pending
        {
            HWND tabWindow = nullptr;
            std::wstring before;
            std::wstring expected;
        };
        using Identities = std::vector<Microsoft::WRL::ComPtr<IUnknown>>;

        bool EnsureShellWindows();
        bool EnsureAutomation();
        // Schede di tutte le finestre o di una sola. Ogni chiamata verso Esplora file passa dal suo thread
        // (lento mentre crea una scheda): la finestra di appartenenza si chiede subito, e i dettagli
        // (cartella, finestra della scheda) solo se servono e solo per le schede richieste.
        std::vector<LiveTab> EnumerateTabs(HWND only = nullptr, bool details = true);
        std::vector<LiveTab> TabsOf(HWND window);
        static bool ReadDetails(LiveTab& live);
        // Schede della barra in ordine visivo. Con molte schede la barra scorre ed è virtualizzata: quelle
        // fuori vista possono mancare, ma ognuna di quelle presenti ha la sua posizione esatta.
        std::vector<TabItem> TabItems(HWND window);
        std::vector<LiveTab const*> OrderTabs(HWND window, std::vector<LiveTab const*> tabs);
        std::vector<LiveTab> OrderedTabsOf(HWND window);
        // Browser della scheda (finestra ShellTabWindowClass), saltando quelli già abbinati.
        Microsoft::WRL::ComPtr<IShellBrowser> BrowserOf(HWND window, HWND tabWindow, Identities& mapped);
        // Porta la scheda sulla cartella senza aspettare che la carichi.
        bool NavigateTab(HWND window, HWND tabWindow, Tab const& tab, Identities& mapped, std::vector<Pending>& pending);
        // Crea una scheda dopo quella attiva e la porta sulla cartella; nullptr se non compare.
        HWND NewTab(HWND window, Tab const& tab, Identities& mapped, std::vector<Pending>& pending, bool& navigated);
        // Rende attiva order[index] con i comandi Ctrl+Tab / Ctrl+Maiusc+Tab (order: schede in ordine visivo).
        bool ActivateTab(HWND window, std::vector<HWND> const& order, int index);

        Microsoft::WRL::ComPtr<IShellWindows> m_shellWindows;
        Microsoft::WRL::ComPtr<IUIAutomation> m_automation;
        std::unordered_map<HWND, CachedOrder> m_orderCache;
        std::unordered_map<HWND, Tab> m_lastKnown; // ultima cartella nota di ogni scheda (mentre ne carica un'altra)
        std::unordered_map<HWND, size_t> m_lastPosition; // ultima posizione nota di ogni scheda (per quelle fuori vista)
    };
}
