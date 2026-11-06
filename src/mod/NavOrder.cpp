#include "pch.h"
#include "NavOrder.h"

#include "ModLog.h"

namespace xpx::mod
{
    namespace
    {
        std::atomic<bool> g_startupOrderDone{ false };

        // Albero del riquadro di spostamento di sistema di una scheda (SysTreeView32 dentro NamespaceTreeControl), se c'è.
        HWND NativeTreeOf(HWND tab)
        {
            HWND found = nullptr;
            EnumChildWindows(
                tab,
                [](HWND child, LPARAM param) -> BOOL {
                    wchar_t name[32]{};
                    wchar_t parent[32]{};
                    if (GetClassNameW(child, name, ARRAYSIZE(name)) && wcscmp(name, L"SysTreeView32") == 0 &&
                        GetClassNameW(GetParent(child), parent, ARRAYSIZE(parent)) && wcscmp(parent, L"NamespaceTreeControl") == 0)
                    {
                        *reinterpret_cast<HWND*>(param) = child;
                        return FALSE;
                    }
                    return TRUE;
                },
                reinterpret_cast<LPARAM>(&found));
            return found;
        }

        int CALLBACK CompareRank(LPARAM a, LPARAM b, LPARAM ranks)
        {
            auto const& map = *reinterpret_cast<std::unordered_map<LPARAM, double> const*>(ranks);
            const auto first = map.find(a);
            const auto second = map.find(b);
            const double x = first == map.end() ? 1e9 : first->second;
            const double y = second == map.end() ? 1e9 : second->second;
            return x < y ? -1 : x > y ? 1 : 0;
        }

        // Mette le voci di primo livello dell'albero (figlie della radice nascosta) nell'ordine dei nomi dati; i nomi doppi
        // (es. le cartelle col nome invisibile) nell'ordine in cui compaiono, una voce sconosciuta resta dopo quella che la
        // precede. true se le ha riordinate.
        bool SortNativeTree(HWND tree, std::vector<std::wstring> const& desired)
        {
            const HTREEITEM root = TreeView_GetRoot(tree);
            if (!root)
            {
                return false;
            }
            std::vector<std::pair<LPARAM, std::wstring>> children;
            for (HTREEITEM child = TreeView_GetChild(tree, root); child; child = TreeView_GetNextSibling(tree, child))
            {
                wchar_t text[300]{};
                TVITEMW item{};
                item.mask = TVIF_TEXT | TVIF_PARAM;
                item.hItem = child;
                item.pszText = text;
                item.cchTextMax = ARRAYSIZE(text);
                if (!SendMessageW(tree, TVM_GETITEMW, 0, reinterpret_cast<LPARAM>(&item)))
                {
                    return false;
                }
                children.emplace_back(item.lParam, text);
            }
            std::unordered_map<std::wstring, std::vector<size_t>> positions;
            for (size_t i = 0; i < desired.size(); ++i)
            {
                positions[desired[i]].push_back(i);
            }
            std::unordered_map<std::wstring, size_t> used;
            std::unordered_map<LPARAM, double> ranks;
            double last = -1;
            double previous = -2;
            bool sorted = true;
            for (size_t k = 0; k < children.size(); ++k)
            {
                auto const& [param, name] = children[k];
                auto const& list = positions[name];
                size_t& next = used[name];
                double rank = 0;
                if (next < list.size())
                {
                    rank = static_cast<double>(list[next++]);
                    last = rank;
                }
                else
                {
                    rank = last + 0.001 * static_cast<double>(k + 1);
                }
                if (!ranks.emplace(param, rank).second)
                {
                    return false; // due voci con lo stesso dato: non si tocca nulla
                }
                sorted = sorted && rank > previous;
                previous = rank;
            }
            if (sorted)
            {
                return false;
            }
            TVSORTCB sort{ root, CompareRank, reinterpret_cast<LPARAM>(&ranks) };
            return SendMessageW(tree, TVM_SORTCHILDRENCB, FALSE, reinterpret_cast<LPARAM>(&sort)) != FALSE;
        }
    }

    std::vector<std::wstring> PinnedKeysOf(NavSnapshot const& snapshot)
    {
        std::vector<std::wstring> keys;
        for (auto const& item : snapshot.items)
        {
            if (item.pinned)
            {
                keys.push_back(FolderKey(item.parsing));
            }
        }
        return keys;
    }

    std::shared_ptr<NavSnapshot> SyncPinnedOrder(NavSnapshot const& snapshot, NavGroupsState const& state, std::vector<std::wstring> const& top)
    {
        auto local = std::make_shared<NavSnapshot>(snapshot);
        std::unordered_map<std::wstring, size_t> position;
        for (auto const& item : local->items)
        {
            if (item.pinned)
            {
                position.emplace(FolderKey(item.parsing), position.size());
            }
        }
        // L'ordine voluto: il primo livello, e al posto di ogni gruppo il suo contenuto, in profondità.
        std::vector<std::wstring> desired;
        std::vector<size_t> order;
        std::unordered_set<std::wstring> seen;
        std::function<void(std::wstring const&, size_t)> add = [&](std::wstring const& key, size_t depth) {
            if (IsGroupKey(key))
            {
                const NavGroup* group = state.Find(GroupIdOf(key));
                if (group && depth <= state.groups.size())
                {
                    for (auto const& child : group->children)
                    {
                        add(child, depth + 1);
                    }
                }
            }
            else if (const auto found = position.find(key); IsFolderKey(key) && found != position.end() && seen.insert(key).second)
            {
                desired.push_back(key);
                order.push_back(found->second);
            }
        };
        for (auto const& key : top)
        {
            add(key, 0);
        }
        // Sottosequenza crescente più lunga (queste restano dove sono).
        const size_t count = desired.size();
        std::vector<size_t> tails;
        std::vector<size_t> previous(count, SIZE_MAX);
        for (size_t i = 0; i < count; ++i)
        {
            const auto at = std::lower_bound(tails.begin(), tails.end(), order[i], [&](size_t index, size_t value) { return order[index] < value; });
            if (at != tails.begin())
            {
                previous[i] = *(at - 1);
            }
            if (at == tails.end())
            {
                tails.push_back(i);
            }
            else
            {
                *at = i;
            }
        }
        std::vector<bool> keep(count, false);
        for (size_t i = tails.empty() ? SIZE_MAX : tails.back(); i != SIZE_MAX; i = previous[i])
        {
            keep[i] = true;
        }
        const auto find = [&](std::wstring const& key) {
            return std::find_if(local->items.begin(), local->items.end(), [&](NavItem const& item) { return item.pinned && FolderKey(item.parsing) == key; });
        };
        std::vector<NavMove> moves;
        for (size_t i = count; i-- > 0;)
        {
            if (keep[i])
            {
                continue;
            }
            const auto from = find(desired[i]);
            if (from == local->items.end())
            {
                continue;
            }
            // Dove va: prima della successiva voluta, o in fondo (dopo l'ultima cartella aggiunta).
            std::vector<NavItem>::iterator to = local->items.end();
            if (i + 1 < count)
            {
                to = find(desired[i + 1]);
                if (to == local->items.end() || to == from)
                {
                    continue;
                }
            }
            NavMove move;
            move.item = from->idList;
            if (i + 1 < count)
            {
                move.before = to->idList;
            }
            NavItem moved = std::move(*from);
            const size_t fromIndex = static_cast<size_t>(from - local->items.begin());
            local->items.erase(from);
            if (i + 1 < count)
            {
                local->items.insert(find(desired[i + 1]), std::move(moved));
            }
            else
            {
                auto last = local->items.end();
                for (auto it = local->items.begin(); it != local->items.end(); ++it)
                {
                    if (it->pinned)
                    {
                        last = it;
                    }
                }
                if (last == local->items.end())
                {
                    local->items.insert(local->items.begin() + static_cast<ptrdiff_t>(fromIndex), std::move(moved)); // era l'unica: resta lì
                    continue;
                }
                move.after = last->idList;
                local->items.insert(last + 1, std::move(moved));
            }
            moves.push_back(std::move(move));
        }
        if (moves.empty())
        {
            return nullptr;
        }
        for (auto& move : moves)
        {
            NavItems::Instance().MovePinned(std::move(move));
        }
        XPX_MLOG(L"NavOrder: %zu %s nell'ordine dei gruppi (in Windows).", moves.size(), moves.size() == 1 ? L"cartella spostata" : L"cartelle spostate");
        return local;
    }

    size_t SortNativePanes(HWND main, NavSnapshot const& snapshot)
    {
        if (!main)
        {
            return 0;
        }
        std::vector<std::wstring> names;
        names.reserve(snapshot.items.size());
        for (auto const& item : snapshot.items)
        {
            names.push_back(item.name);
        }
        size_t sorted = 0;
        for (HWND tab = FindWindowExW(main, nullptr, L"ShellTabWindowClass", nullptr); tab; tab = FindWindowExW(main, tab, L"ShellTabWindowClass", nullptr))
        {
            if (const HWND tree = NativeTreeOf(tab))
            {
                sorted += SortNativeTree(tree, names);
            }
        }
        if (sorted)
        {
            XPX_MLOG(L"NavOrder: riquadro di spostamento di sistema rimesso nell'ordine di Windows in %zu %s.", sorted, sorted == 1 ? L"scheda" : L"schede");
        }
        return sorted;
    }

    bool TakeStartupOrder()
    {
        return !g_startupOrderDone.exchange(true);
    }
}
