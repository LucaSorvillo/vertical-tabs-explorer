#pragma once

#include <unordered_set>

namespace xpx::mod
{
    // Gruppi della barra delle cartelle (NavBar), come le cartelle dei segnalibri di Chrome: raccolgono cartelle
    // aggiunte ad Accesso rapido e altri gruppi (annidati a piacere). Vivono solo nella barra: le cartelle restano
    // aggiunte ad Accesso rapido (la Home e il riquadro di sistema non cambiano) e un gruppo non tocca i file.
    //
    // Le voci si riconoscono da chiavi: "f:<nome di analisi in minuscolo>" per le cartelle, "g:<id>" per i
    // gruppi. Una cartella sta al più in un gruppo; quelle in nessun gruppo restano al primo livello della barra,
    // nell'ordine di Windows (lo stesso del riquadro e della Home). I gruppi di primo livello stanno prima della
    // cartella che li seguiva l'ultima volta che la barra li ha disposti ("top"); se quella non c'è più, prima
    // della successiva ancora lì, e in fondo se non ne resta nessuna. Così, se l'ordine di Windows cambia
    // altrove, i gruppi seguono le cartelle vicine.
    //
    // Un archivio per processo (NavGroups::Instance), letto e scritto da tutte le finestre (thread UI diversi):
    // ogni modifica pubblica uno stato nuovo e immutabile, con una versione nuova, e lo salva subito in
    // %LOCALAPPDATA%\VTabsExplorer\navbar-groups.json (navbar-groups-test.json nei processi di prova, cioè in ogni
    // explorer.exe che non è quello della barra delle applicazioni: i test non toccano i gruppi dell'utente).
    struct NavGroup
    {
        std::wstring id;
        std::wstring name;
        std::vector<std::wstring> children; // chiavi, in ordine
    };

    struct NavGroupsState
    {
        uint64_t version = 0;
        std::vector<std::wstring> top;   // primo livello del blocco delle cartelle aggiunte, com'era (cartelle e gruppi)
        std::vector<NavGroup> groups;    // tutti i gruppi (anche quelli annidati)

        NavGroup const* Find(std::wstring_view id) const;
        // Gruppo che contiene la chiave ("" se non è in nessun gruppo).
        std::wstring ParentOf(std::wstring_view key) const;
        // La chiave sta dentro il gruppo id, a qualunque profondità.
        bool IsInside(std::wstring_view key, std::wstring_view id) const;
        // Il gruppo e i suoi antenati, dal primo livello in giù ("A", "B" per B dentro A).
        std::vector<std::wstring> PathOf(std::wstring_view id) const;
    };

    std::wstring FolderKey(std::wstring_view parsing);
    std::wstring GroupKey(std::wstring_view id);
    bool IsGroupKey(std::wstring_view key);
    bool IsFolderKey(std::wstring_view key);
    std::wstring_view GroupIdOf(std::wstring_view key);

    // Primo livello del blocco delle cartelle aggiunte, in ordine: le cartelle presenti e non raggruppate
    // (pinned: tutte le cartelle aggiunte presenti, nell'ordine di Windows) con i gruppi di primo livello al loro
    // posto (vedi sopra).
    std::vector<std::wstring> TopLevel(NavGroupsState const& state, std::vector<std::wstring> const& pinned);
    // Contenuto di un gruppo che si vede: le cartelle presenti e i sottogruppi, nell'ordine del gruppo.
    std::vector<std::wstring> VisibleChildren(NavGroupsState const& state, std::wstring_view id, std::unordered_set<std::wstring> const& present);

    class NavGroups
    {
    public:
        static NavGroups& Instance();

        // Stato attuale (letto dal file alla prima chiamata). Mai null.
        std::shared_ptr<const NavGroupsState> Latest();

        // Le operazioni restituiscono lo stato nuovo, o null se non cambia nulla (o se non si può: un gruppo dentro
        // sé stesso, una chiave che non c'è...). "pinned" sono le cartelle aggiunte presenti nell'ordine di Windows
        // (della barra che chiede), per rifare il primo livello come lo si vede.
        //
        // Sposta la voce nel contenitore ("" = primo livello, altrimenti l'id di un gruppo), prima della voce
        // "before" ("" = in fondo).
        std::shared_ptr<const NavGroupsState> Place(std::wstring const& key, std::wstring const& container, std::wstring const& before,
                                                    std::vector<std::wstring> const& pinned);
        // Gruppo nuovo nel contenitore, prima di "before"; con "first" (una cartella o un gruppo) il gruppo prende il
        // suo posto e lo contiene. L'id del gruppo nuovo in *created.
        std::shared_ptr<const NavGroupsState> Create(std::wstring const& name, std::wstring const& container, std::wstring const& before,
                                                     std::wstring const& first, std::vector<std::wstring> const& pinned, std::wstring* created);
        std::shared_ptr<const NavGroupsState> Rename(std::wstring const& id, std::wstring const& name);
        // Elimina il gruppo: il contenuto (cartelle e sottogruppi) passa al livello di sopra, al suo posto.
        std::shared_ptr<const NavGroupsState> Remove(std::wstring const& id, std::vector<std::wstring> const& pinned);
        // Toglie la cartella dal suo gruppo e dalla memoria della barra (es. tolta da Accesso rapido).
        std::shared_ptr<const NavGroupsState> Forget(std::wstring const& key);

        // Percorso del file (per il registro e i test).
        std::wstring FilePath();

    private:
        NavGroups() = default;
        void EnsureLoaded();
        template <typename F>
        std::shared_ptr<const NavGroupsState> Mutate(const wchar_t* what, F&& change);
        void Save(NavGroupsState const& state);

        std::mutex m_lock;
        std::shared_ptr<const NavGroupsState> m_state;
        std::wstring m_path;
    };
}
