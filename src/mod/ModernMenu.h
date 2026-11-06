#pragma once

#include <shobjidl.h>

namespace xpx::mod
{
    struct ModernMenuHost;

    // Il menu contestuale nuovo di Windows 11 per un elemento della shell, aperto come lo apre il riquadro di
    // spostamento di sistema (ExplorerFrame.dll, CNscTree::DoCuratedContextMenu): il "presenter" di
    // Windows.UI.FileExplorer.dll (classe COM "File Explorer Context Menu") riceve il menu classico della
    // shell e ne mostra la versione nuova (icone in alto, voci scelte, "Mostra altre opzioni").
    //
    // Interfacce NON documentate, ricostruite dai simboli pubblici di Microsoft (build 26300): ogni passo
    // controlla il risultato e, se qualcosa non va (classe o interfaccia assenti, errori), Show restituisce
    // false e chi chiama mostra il menu classico. Il menu è asincrono: le scelte arrivano dai callback, sul
    // thread della finestra. Va creato e usato sul thread UI della finestra.
    class ModernMenu
    {
    public:
        struct Callbacks
        {
            // Voce scelta: indice del comando per IContextMenu::InvokeCommand (id del menu meno il primo id).
            std::function<void(IContextMenu* menu, UINT command, POINT point)> invoke;
            // Menu chiuso (dopo una scelta o senza).
            std::function<void()> dismissed;
            // "Mostra altre opzioni": il menu classico, in quel punto.
            std::function<void(POINT point)> expanded;
            // Il menu chiede di rimettere il fuoco della tastiera (prima di eseguire la voce o chiudendosi).
            std::function<void()> focus;
        };

        // Bit di DoContextMenu (come li passa il riquadro): aperto con il clic destro; con il tocco; con la penna.
        static constexpr DWORD kFromRightClick = 0x8;
        static constexpr DWORD kFromTouch = 0x2;
        static constexpr DWORD kFromPen = 0x4;

        ModernMenu(HWND owner, Callbacks callbacks);
        ~ModernMenu();
        ModernMenu(ModernMenu const&) = delete;
        ModernMenu& operator=(ModernMenu const&) = delete;

        enum class ShowResult
        {
            Shown,       // aperto
            Deferred,    // il menu di prima si sta chiudendo: il presenter manderà WM_CONTEXTMENU al proprietario nel punto
            Unavailable, // non disponibile: usare il menu classico (niente è stato mostrato)
        };
        // Apre il menu di item (menu = il suo IContextMenu, site = sito per i verbi, es. il browser della scheda)
        // nel punto dello schermo.
        ShowResult Show(IContextMenu* menu, IShellItem* item, IUnknown* site, POINT screen, DWORD inputFlags);
        // Chiude il menu, se è aperto.
        void Dismiss();
        bool IsOpen() const;

    private:
        friend struct ModernMenuHost;

        bool Ensure();
        void OnInvoke(IContextMenu* menu, UINT id, POINT point);
        void OnDismiss();
        void OnExpand(POINT point);
        void OnFocus();
        static LRESULT CALLBACK MessageProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam);

        HWND m_owner = nullptr;
        Callbacks m_callbacks;
        winrt::com_ptr<IUnknown> m_presenter;   // IContextMenuPresenter (vedi ModernMenu.cpp)
        winrt::com_ptr<ModernMenuHost> m_host;  // IInvokeContextMenuCommand + IOleWindow, nostro
        HWND m_messageWindow = nullptr;         // riceve dal presenter l'ordine di eseguire la voce scelta
        bool m_failed = false;                  // presenter non disponibile: niente altri tentativi
    };
}
