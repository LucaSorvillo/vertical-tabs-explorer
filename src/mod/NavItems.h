#pragma once

namespace xpx::mod
{
    // Voci di primo livello del riquadro di spostamento di Esplora file, composte come le compone lui
    // (verificato confrontandole con l'albero del riquadro, vedi tools\test\vertical-tabs\Test-NavBar.ps1):
    //   - gli elementi del Desktop "fissati nell'albero" (System.IsPinnedToNameSpaceTree: Home, Galleria,
    //     OneDrive, Questo PC, Rete, Linux...), ordinati con il loro SortOrderIndex (registro, chiave del
    //     CLSID; Questo PC, senza valore, viene dopo Galleria e OneDrive);
    //   - le cartelle aggiunte ad Accesso rapido (System.Home.IsPinned), nel loro ordine, tra quelli
    //     (indice 0x48: dopo Home 0x40, Galleria 0x41, OneDrive 0x42, prima di Questo PC 0x50, Rete 0x58).
    // Un thread a sé le legge: all'avvio, quando la shell segnala cambiamenti in Accesso rapido o sul Desktop,
    // quando una finestra lo chiede (Refresh) e comunque ogni 5 minuti; produce anche le icone alle dimensioni
    // in pixel chieste dalle finestre. Le finestre prendono l'ultima versione con Latest().
    struct NavIcon
    {
        int size = 0;               // lato in pixel
        std::vector<uint8_t> bgra;  // BGRA premoltiplicato, righe dall'alto
    };

    struct NavItem
    {
        std::wstring name;            // nome mostrato
        std::wstring parsing;         // nome di analisi assoluto (::{GUID} o percorso)
        std::vector<uint8_t> idList;  // PIDL assoluto (per aprirla)
        // Cartella aggiunta ad Accesso rapido. Si riporta così com'è, come nel riquadro di spostamento:
        // anche quelle con un nome invisibile (U+2800) e un'icona trasparente, usate per distanziare le voci.
        bool pinned = false;
        // Posto nel riquadro (SortOrderIndex; 0x48 le cartelle aggiunte): dice dove sta il blocco delle cartelle
        // aggiunte anche quando non ce n'è nessuna (la barra ci mette i gruppi).
        uint32_t sortIndex = 0;
        std::vector<NavIcon> icons;   // una per dimensione chiesta
    };

    struct NavSnapshot
    {
        uint64_t version = 0;
        std::vector<NavItem> items;
        ULONGLONG readMs = 0; // durata dell'ultima lettura (per il log)
    };

    // Spostamento di una cartella aggiunta (PIDL assoluti): prima di "before" oppure, se è vuoto, subito dopo
    // "after", come trascinandola nel riquadro di spostamento.
    struct NavMove
    {
        std::vector<uint8_t> item;
        std::vector<uint8_t> before;
        std::vector<uint8_t> after;
    };

    // Cartelle da aggiungere ad Accesso rapido (PIDL assoluti), nell'ordine, prima della cartella aggiunta "before"
    // (vuoto, o non più aggiunta: in fondo), come trascinate nella barra. Quelle già aggiunte restano dove sono.
    struct NavPin
    {
        std::vector<std::vector<uint8_t>> items;
        std::vector<uint8_t> before;
    };

    class NavItems
    {
    public:
        static NavItems& Instance();

        // Avvia il thread (alla prima chiamata).
        void Start();
        // Icone anche di questa dimensione (pixel); se è nuova si rilegge.
        void UseIconSize(int pixels);
        // Rilettura appena possibile (es. dopo un comando del menu contestuale, o finestra attivata).
        void Refresh();
        // Ultima lettura (null finché la prima non è finita).
        std::shared_ptr<const NavSnapshot> Latest() const;
        // Sposta una cartella aggiunta ad Accesso rapido, nell'ordine di Windows (lo stesso del riquadro, della
        // Home e del jump list di Esplora file). Lo fa il thread delle letture, poi rilegge e pubblica comunque
        // una versione nuova (le finestre che hanno già spostato la voce a vista si rimettono in pari).
        void MovePinned(NavMove move);
        // Aggiunge cartelle ad Accesso rapido (come "Aggiungi ad Accesso rapido", nel posto indicato). Come MovePinned: lo
        // fa il thread delle letture (prima degli spostamenti in attesa), poi rilegge e pubblica una versione nuova.
        void PinFolders(NavPin pin);

    private:
        NavItems() = default;
        void Run();
        void Load();
        void DoMove(NavMove const& move);
        void DoPin(NavPin const& pin);

        mutable std::mutex m_lock;
        std::shared_ptr<const NavSnapshot> m_latest;
        std::vector<int> m_sizes;
        std::vector<NavMove> m_moves; // in attesa del thread delle letture
        std::vector<NavPin> m_pins;   // in attesa del thread delle letture
        HANDLE m_wake = nullptr;
        bool m_started = false;
        bool m_forcePublish = false; // solo dal thread delle letture
        // Solo dal thread delle letture: icone già prodotte (per nome di analisi e dimensione) e dimensioni
        // dell'ultima lettura pubblicata.
        std::unordered_map<std::wstring, std::vector<NavIcon>> m_iconCache;
        std::vector<int> m_publishedSizes;
    };
}
