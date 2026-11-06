#include "pch.h"
#include "NavGroups.h"

#include "ModLog.h"
#include "../ModShared.h"

#include <winrt/Windows.Data.Json.h>

namespace xpx::mod
{
    namespace
    {
        namespace json = winrt::Windows::Data::Json;

        constexpr size_t kMaxNameLength = 100;

        std::wstring Lower(std::wstring_view text)
        {
            std::wstring result{ text };
            if (!result.empty())
            {
                const int size = LCMapStringEx(LOCALE_NAME_INVARIANT, LCMAP_LOWERCASE, text.data(), static_cast<int>(text.size()), result.data(),
                                               static_cast<int>(result.size()), nullptr, nullptr, 0);
                result.resize(size > 0 ? static_cast<size_t>(size) : 0);
            }
            return result;
        }

        // Nome di un gruppo: senza spazi ai lati né caratteri di controllo, al più 100 caratteri.
        std::wstring Trimmed(std::wstring_view name)
        {
            std::wstring result;
            for (const wchar_t c : name)
            {
                result.push_back(c < 0x20 ? L' ' : c);
            }
            const size_t first = result.find_first_not_of(L" \t 　");
            if (first == std::wstring::npos)
            {
                return {};
            }
            const size_t last = result.find_last_not_of(L" \t 　");
            result = result.substr(first, last - first + 1);
            if (result.size() > kMaxNameLength)
            {
                result.resize(kMaxNameLength);
            }
            return result;
        }

        NavGroup* FindMutable(NavGroupsState& state, std::wstring_view id)
        {
            for (auto& group : state.groups)
            {
                if (group.id == id)
                {
                    return &group;
                }
            }
            return nullptr;
        }

        // Inserisce la chiave prima di "before" ("" o non trovata: in fondo).
        void Insert(std::vector<std::wstring>& keys, std::wstring const& key, std::wstring const& before)
        {
            auto position = before.empty() ? keys.end() : std::find(keys.begin(), keys.end(), before);
            keys.insert(position, key);
        }

        void Visit(NavGroup& group, std::unordered_map<std::wstring, NavGroup*> const& byId, std::unordered_set<std::wstring>& visited,
                   std::vector<std::wstring>& path)
        {
            visited.insert(group.id);
            path.push_back(group.id);
            std::vector<std::wstring> kept;
            for (auto const& key : group.children)
            {
                if (IsGroupKey(key))
                {
                    const std::wstring id{ GroupIdOf(key) };
                    const auto found = byId.find(id);
                    // Un gruppo che contiene un suo antenato (o uno già visto): si toglie il collegamento.
                    if (found == byId.end() || visited.count(id) || std::find(path.begin(), path.end(), id) != path.end())
                    {
                        continue;
                    }
                    kept.push_back(key);
                    Visit(*found->second, byId, visited, path);
                }
                else
                {
                    kept.push_back(key);
                }
            }
            group.children = std::move(kept);
            path.pop_back();
        }

        // Rimette in ordine uno stato letto dal file o appena cambiato: id unici, ogni voce in un posto solo (il primo),
        // niente cicli, al primo livello solo le voci che non sono in un gruppo, e ci sono tutti i gruppi di primo livello.
        void Normalize(NavGroupsState& state)
        {
            std::unordered_set<std::wstring> ids;
            std::vector<NavGroup> groups;
            for (auto& group : state.groups)
            {
                if (group.id.empty() || !ids.insert(group.id).second)
                {
                    continue;
                }
                group.name = Trimmed(group.name);
                if (group.name.empty())
                {
                    group.name = Tr(L"Group", L"Gruppo");
                }
                groups.push_back(std::move(group));
            }
            state.groups = std::move(groups);

            std::unordered_set<std::wstring> claimed;
            for (auto& group : state.groups)
            {
                std::vector<std::wstring> kept;
                for (auto& key : group.children)
                {
                    if (IsGroupKey(key))
                    {
                        const std::wstring id{ GroupIdOf(key) };
                        if (id == group.id || !ids.count(id))
                        {
                            continue;
                        }
                    }
                    else if (!IsFolderKey(key) || key.size() <= 2)
                    {
                        continue;
                    }
                    if (claimed.insert(key).second)
                    {
                        kept.push_back(std::move(key));
                    }
                }
                group.children = std::move(kept);
            }

            // Cicli: si visita dai gruppi di primo livello; quelli rimasti (solo in cicli) si staccano uno alla volta.
            std::unordered_map<std::wstring, NavGroup*> byId;
            for (auto& group : state.groups)
            {
                byId[group.id] = &group;
            }
            std::unordered_set<std::wstring> visited;
            std::vector<std::wstring> path;
            for (auto& group : state.groups)
            {
                if (!claimed.count(GroupKey(group.id)))
                {
                    Visit(group, byId, visited, path);
                }
            }
            for (auto& group : state.groups)
            {
                if (visited.count(group.id))
                {
                    continue;
                }
                const std::wstring key = GroupKey(group.id);
                for (auto& other : state.groups)
                {
                    std::erase(other.children, key);
                }
                Visit(group, byId, visited, path);
            }

            claimed.clear();
            for (auto const& group : state.groups)
            {
                claimed.insert(group.children.begin(), group.children.end());
            }
            std::unordered_set<std::wstring> seen;
            std::vector<std::wstring> top;
            for (auto& key : state.top)
            {
                const bool valid = IsGroupKey(key) ? ids.count(std::wstring{ GroupIdOf(key) }) > 0 : IsFolderKey(key) && key.size() > 2;
                if (valid && !claimed.count(key) && seen.insert(key).second)
                {
                    top.push_back(std::move(key));
                }
            }
            for (auto const& group : state.groups)
            {
                const std::wstring key = GroupKey(group.id);
                if (!claimed.count(key) && !seen.count(key))
                {
                    top.push_back(key);
                }
            }
            state.top = std::move(top);
        }

        bool SameLayout(NavGroupsState const& a, NavGroupsState const& b)
        {
            if (a.top != b.top || a.groups.size() != b.groups.size())
            {
                return false;
            }
            for (size_t i = 0; i < a.groups.size(); ++i)
            {
                if (a.groups[i].id != b.groups[i].id || a.groups[i].name != b.groups[i].name || a.groups[i].children != b.groups[i].children)
                {
                    return false;
                }
            }
            return true;
        }

        std::wstring StringOf(json::IJsonValue const& value)
        {
            return value && value.ValueType() == json::JsonValueType::String ? std::wstring{ value.GetString() } : std::wstring{};
        }

        std::vector<std::wstring> KeysOf(json::JsonObject const& object, const wchar_t* name)
        {
            std::vector<std::wstring> keys;
            if (object.HasKey(name) && object.GetNamedValue(name).ValueType() == json::JsonValueType::Array)
            {
                for (auto const& value : object.GetNamedArray(name))
                {
                    if (auto key = StringOf(value); !key.empty())
                    {
                        keys.push_back(IsGroupKey(key) ? std::move(key) : Lower(key));
                    }
                }
            }
            return keys;
        }

        json::JsonArray ArrayOf(std::vector<std::wstring> const& keys)
        {
            json::JsonArray array;
            for (auto const& key : keys)
            {
                array.Append(json::JsonValue::CreateStringValue(key));
            }
            return array;
        }
    }

    std::wstring FolderKey(std::wstring_view parsing)
    {
        return L"f:" + Lower(parsing);
    }

    std::wstring GroupKey(std::wstring_view id)
    {
        return L"g:" + std::wstring{ id };
    }

    bool IsGroupKey(std::wstring_view key)
    {
        return key.size() > 2 && key[0] == L'g' && key[1] == L':';
    }

    bool IsFolderKey(std::wstring_view key)
    {
        return key.size() > 2 && key[0] == L'f' && key[1] == L':';
    }

    std::wstring_view GroupIdOf(std::wstring_view key)
    {
        return IsGroupKey(key) ? key.substr(2) : std::wstring_view{};
    }

    NavGroup const* NavGroupsState::Find(std::wstring_view id) const
    {
        for (auto const& group : groups)
        {
            if (group.id == id)
            {
                return &group;
            }
        }
        return nullptr;
    }

    std::wstring NavGroupsState::ParentOf(std::wstring_view key) const
    {
        for (auto const& group : groups)
        {
            if (std::find(group.children.begin(), group.children.end(), key) != group.children.end())
            {
                return group.id;
            }
        }
        return {};
    }

    bool NavGroupsState::IsInside(std::wstring_view key, std::wstring_view id) const
    {
        std::wstring parent = ParentOf(key);
        for (size_t guard = 0; !parent.empty() && guard <= groups.size(); ++guard)
        {
            if (parent == id)
            {
                return true;
            }
            parent = ParentOf(GroupKey(parent));
        }
        return false;
    }

    std::vector<std::wstring> NavGroupsState::PathOf(std::wstring_view id) const
    {
        std::vector<std::wstring> path;
        std::wstring current{ id };
        for (size_t guard = 0; !current.empty() && guard <= groups.size(); ++guard)
        {
            path.insert(path.begin(), current);
            current = ParentOf(GroupKey(current));
        }
        return path;
    }

    std::vector<std::wstring> TopLevel(NavGroupsState const& state, std::vector<std::wstring> const& pinned)
    {
        std::unordered_set<std::wstring> grouped;
        for (auto const& group : state.groups)
        {
            grouped.insert(group.children.begin(), group.children.end());
        }
        std::vector<std::wstring> free;
        std::unordered_set<std::wstring> freeSet;
        for (auto const& key : pinned)
        {
            if (!grouped.count(key) && freeSet.insert(key).second)
            {
                free.push_back(key);
            }
        }
        // Ogni gruppo di primo livello prima della prima cartella libera che lo seguiva ("" = in fondo).
        std::vector<std::pair<std::wstring, std::wstring>> anchored;
        for (size_t i = 0; i < state.top.size(); ++i)
        {
            std::wstring const& key = state.top[i];
            if (!IsGroupKey(key) || grouped.count(key) || !state.Find(GroupIdOf(key)))
            {
                continue;
            }
            std::wstring anchor;
            for (size_t j = i + 1; j < state.top.size(); ++j)
            {
                if (IsFolderKey(state.top[j]) && freeSet.count(state.top[j]))
                {
                    anchor = state.top[j];
                    break;
                }
            }
            anchored.emplace_back(key, std::move(anchor));
        }
        std::vector<std::wstring> result;
        result.reserve(free.size() + anchored.size());
        for (auto const& folder : free)
        {
            for (auto const& [group, anchor] : anchored)
            {
                if (anchor == folder)
                {
                    result.push_back(group);
                }
            }
            result.push_back(folder);
        }
        for (auto const& [group, anchor] : anchored)
        {
            if (anchor.empty())
            {
                result.push_back(group);
            }
        }
        return result;
    }

    std::vector<std::wstring> VisibleChildren(NavGroupsState const& state, std::wstring_view id, std::unordered_set<std::wstring> const& present)
    {
        std::vector<std::wstring> result;
        if (const NavGroup* group = state.Find(id))
        {
            for (auto const& key : group->children)
            {
                if (IsGroupKey(key) ? state.Find(GroupIdOf(key)) != nullptr : present.count(key) > 0)
                {
                    result.push_back(key);
                }
            }
        }
        return result;
    }

    NavGroups& NavGroups::Instance()
    {
        static NavGroups* instance = new NavGroups(); // mai distrutto: la mod resta caricata fino alla fine del processo
        return *instance;
    }

    std::shared_ptr<const NavGroupsState> NavGroups::Latest()
    {
        std::scoped_lock lock(m_lock);
        EnsureLoaded();
        return m_state;
    }

    std::wstring NavGroups::FilePath()
    {
        std::scoped_lock lock(m_lock);
        EnsureLoaded();
        return m_path;
    }

    void NavGroups::EnsureLoaded()
    {
        if (m_state)
        {
            return;
        }
        // Il processo della barra delle applicazioni ha i gruppi dell'utente; ogni altro explorer.exe (le finestre di
        // prova isolate) un file a parte. Senza barra delle applicazioni (Esplora file che riparte) è quello della shell.
        const DWORD shell = ShellProcessId();
        const bool shellProcess = shell == 0 || shell == GetCurrentProcessId();
        m_path = DataDirectory() + (shellProcess ? L"\\navbar-groups.json" : L"\\navbar-groups-test.json");

        auto state = std::make_shared<NavGroupsState>();
        state->version = 1;
        const HANDLE file = CreateFileW(m_path.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_DELETE, nullptr, OPEN_EXISTING, 0, nullptr);
        if (file != INVALID_HANDLE_VALUE)
        {
            std::string bytes;
            LARGE_INTEGER size{};
            if (GetFileSizeEx(file, &size) && size.QuadPart > 0 && size.QuadPart < 16 * 1024 * 1024)
            {
                bytes.resize(static_cast<size_t>(size.QuadPart));
                DWORD read = 0;
                if (!ReadFile(file, bytes.data(), static_cast<DWORD>(bytes.size()), &read, nullptr))
                {
                    read = 0;
                }
                bytes.resize(read);
            }
            CloseHandle(file);
            if (bytes.size() >= 3 && static_cast<uint8_t>(bytes[0]) == 0xEF && static_cast<uint8_t>(bytes[1]) == 0xBB && static_cast<uint8_t>(bytes[2]) == 0xBF)
            {
                bytes.erase(0, 3);
            }
            std::wstring text(bytes.size(), L'\0');
            const int length = MultiByteToWideChar(CP_UTF8, 0, bytes.data(), static_cast<int>(bytes.size()), text.data(), static_cast<int>(text.size()));
            text.resize(length > 0 ? static_cast<size_t>(length) : 0);
            json::JsonObject root{ nullptr };
            bool parsed = false;
            try
            {
                parsed = !text.empty() && json::JsonObject::TryParse(text, root) && root;
                if (parsed)
                {
                    state->top = KeysOf(root, L"top");
                    if (root.HasKey(L"groups") && root.GetNamedValue(L"groups").ValueType() == json::JsonValueType::Array)
                    {
                        for (auto const& value : root.GetNamedArray(L"groups"))
                        {
                            if (value.ValueType() != json::JsonValueType::Object)
                            {
                                continue;
                            }
                            const auto object = value.GetObject();
                            NavGroup group;
                            group.id = object.HasKey(L"id") ? StringOf(object.GetNamedValue(L"id")) : std::wstring{};
                            group.name = object.HasKey(L"name") ? StringOf(object.GetNamedValue(L"name")) : std::wstring{};
                            group.children = KeysOf(object, L"children");
                            state->groups.push_back(std::move(group));
                        }
                    }
                }
            }
            catch (...)
            {
                parsed = false;
            }
            if (!parsed)
            {
                // Illeggibile: si mette da parte (mai sovrascritto) e si riparte senza gruppi.
                const std::wstring aside = m_path + L".illeggibile-" + std::to_wstring(GetTickCount64());
                MoveFileExW(m_path.c_str(), aside.c_str(), MOVEFILE_REPLACE_EXISTING);
                XPX_MLOG(L"NavGroups: file dei gruppi illeggibile, messo da parte; si riparte senza gruppi.");
                state->top.clear();
                state->groups.clear();
            }
        }
        Normalize(*state);
        m_state = state;
        XPX_MLOG(L"NavGroups: %zu gruppi (%s%s).", state->groups.size(), shellProcess ? L"navbar-groups.json" : L"navbar-groups-test.json",
                 file == INVALID_HANDLE_VALUE ? L", file ancora assente" : L"");
    }

    void NavGroups::Save(NavGroupsState const& state)
    {
        try
        {
            json::JsonObject root;
            root.SetNamedValue(L"format", json::JsonValue::CreateNumberValue(1));
            root.SetNamedValue(L"top", ArrayOf(state.top));
            json::JsonArray groups;
            for (auto const& group : state.groups)
            {
                json::JsonObject object;
                object.SetNamedValue(L"id", json::JsonValue::CreateStringValue(group.id));
                object.SetNamedValue(L"name", json::JsonValue::CreateStringValue(group.name));
                object.SetNamedValue(L"children", ArrayOf(group.children));
                groups.Append(object);
            }
            root.SetNamedValue(L"groups", groups);
            const winrt::hstring text = root.Stringify();
            std::string bytes;
            const int size = WideCharToMultiByte(CP_UTF8, 0, text.c_str(), static_cast<int>(text.size()), nullptr, 0, nullptr, nullptr);
            bytes.resize(size > 0 ? static_cast<size_t>(size) : 0);
            if (size > 0)
            {
                WideCharToMultiByte(CP_UTF8, 0, text.c_str(), static_cast<int>(text.size()), bytes.data(), size, nullptr, nullptr);
            }
            // Prima un file a parte, poi al posto di quello vecchio: mai un file a metà.
            const std::wstring temporary = m_path + L".tmp";
            const HANDLE file = CreateFileW(temporary.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
            if (file == INVALID_HANDLE_VALUE)
            {
                XPX_MLOG(L"NavGroups: gruppi non salvati (errore %lu).", GetLastError());
                return;
            }
            DWORD written = 0;
            const bool ok = WriteFile(file, bytes.data(), static_cast<DWORD>(bytes.size()), &written, nullptr) && written == bytes.size();
            FlushFileBuffers(file);
            CloseHandle(file);
            if (!ok || !MoveFileExW(temporary.c_str(), m_path.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
            {
                XPX_MLOG(L"NavGroups: gruppi non salvati (errore %lu).", GetLastError());
                DeleteFileW(temporary.c_str());
            }
        }
        catch (...)
        {
            XPX_MLOG(L"NavGroups: gruppi non salvati (0x%08X).", static_cast<unsigned>(winrt::to_hresult()));
        }
    }

    template <typename F>
    std::shared_ptr<const NavGroupsState> NavGroups::Mutate(const wchar_t* what, F&& change)
    {
        std::scoped_lock lock(m_lock);
        EnsureLoaded();
        auto next = std::make_shared<NavGroupsState>(*m_state);
        if (!change(*next))
        {
            return nullptr;
        }
        Normalize(*next);
        if (SameLayout(*next, *m_state))
        {
            return nullptr;
        }
        next->version = m_state->version + 1;
        m_state = next;
        Save(*next);
        XPX_MLOG(L"NavGroups: %s (versione %llu, %zu gruppi).", what, next->version, next->groups.size());
        return next;
    }

    std::shared_ptr<const NavGroupsState> NavGroups::Place(std::wstring const& key, std::wstring const& container, std::wstring const& before,
                                                           std::vector<std::wstring> const& pinned)
    {
        return Mutate(L"voce spostata", [&](NavGroupsState& state) {
            if (key.empty() || key == before)
            {
                return false;
            }
            if (IsGroupKey(key))
            {
                const std::wstring id{ GroupIdOf(key) };
                // Mai dentro sé stesso o un suo discendente.
                if (!state.Find(id) || (!container.empty() && (container == id || state.IsInside(GroupKey(container), id))))
                {
                    return false;
                }
            }
            else if (!IsFolderKey(key))
            {
                return false;
            }
            NavGroup* target = container.empty() ? nullptr : FindMutable(state, container);
            if (!container.empty() && !target)
            {
                return false;
            }
            auto top = TopLevel(state, pinned);
            std::erase(top, key);
            for (auto& group : state.groups)
            {
                std::erase(group.children, key);
            }
            Insert(target ? target->children : top, key, before);
            state.top = std::move(top);
            return true;
        });
    }

    std::shared_ptr<const NavGroupsState> NavGroups::Create(std::wstring const& name, std::wstring const& container, std::wstring const& before,
                                                            std::wstring const& first, std::vector<std::wstring> const& pinned, std::wstring* created)
    {
        std::wstring id;
        auto result = Mutate(L"gruppo creato", [&](NavGroupsState& state) {
            unsigned long long highest = 0;
            for (auto const& group : state.groups)
            {
                highest = std::max(highest, wcstoull(group.id.c_str(), nullptr, 10));
            }
            id = std::to_wstring(highest + 1);
            const std::wstring key = GroupKey(id);
            NavGroup group;
            group.id = id;
            group.name = Trimmed(name);
            if (group.name.empty())
            {
                group.name = Tr(L"New group", L"Nuovo gruppo");
            }
            auto top = TopLevel(state, pinned);
            if (!first.empty())
            {
                // Il gruppo prende il posto della voce e la contiene.
                if (const auto at = std::find(top.begin(), top.end(), first); at != top.end())
                {
                    *at = key;
                }
                else
                {
                    NavGroup* parent = FindMutable(state, state.ParentOf(first));
                    if (!parent)
                    {
                        return false;
                    }
                    std::replace(parent->children.begin(), parent->children.end(), first, key);
                }
                group.children.push_back(first);
            }
            else if (container.empty())
            {
                Insert(top, key, before);
            }
            else if (NavGroup* parent = FindMutable(state, container))
            {
                Insert(parent->children, key, before);
            }
            else
            {
                return false;
            }
            state.groups.push_back(std::move(group));
            state.top = std::move(top);
            return true;
        });
        if (result && created)
        {
            *created = id;
        }
        return result;
    }

    std::shared_ptr<const NavGroupsState> NavGroups::Rename(std::wstring const& id, std::wstring const& name)
    {
        return Mutate(L"gruppo rinominato", [&](NavGroupsState& state) {
            NavGroup* group = FindMutable(state, id);
            const std::wstring trimmed = Trimmed(name);
            if (!group || trimmed.empty() || trimmed == group->name)
            {
                return false;
            }
            group->name = trimmed;
            return true;
        });
    }

    std::shared_ptr<const NavGroupsState> NavGroups::Remove(std::wstring const& id, std::vector<std::wstring> const& pinned)
    {
        return Mutate(L"gruppo eliminato", [&](NavGroupsState& state) {
            size_t index = 0;
            while (index < state.groups.size() && state.groups[index].id != id)
            {
                ++index;
            }
            if (index == state.groups.size())
            {
                return false;
            }
            const std::wstring key = GroupKey(id);
            const std::vector<std::wstring> children = state.groups[index].children;
            const std::wstring parent = state.ParentOf(key);
            auto top = TopLevel(state, pinned);
            if (parent.empty())
            {
                // Al primo livello vanno le cartelle presenti e i sottogruppi (le altre non si vedrebbero comunque).
                const std::unordered_set<std::wstring> present(pinned.begin(), pinned.end());
                std::vector<std::wstring> moved;
                for (auto const& child : children)
                {
                    if (IsGroupKey(child) || present.count(child))
                    {
                        moved.push_back(child);
                    }
                }
                auto at = std::find(top.begin(), top.end(), key);
                at = at == top.end() ? top.end() : top.erase(at);
                top.insert(at, moved.begin(), moved.end());
            }
            else if (NavGroup* holder = FindMutable(state, parent))
            {
                auto at = std::find(holder->children.begin(), holder->children.end(), key);
                at = at == holder->children.end() ? holder->children.end() : holder->children.erase(at);
                holder->children.insert(at, children.begin(), children.end());
            }
            state.groups.erase(state.groups.begin() + static_cast<ptrdiff_t>(index));
            state.top = std::move(top);
            return true;
        });
    }

    std::shared_ptr<const NavGroupsState> NavGroups::Forget(std::wstring const& key)
    {
        return Mutate(L"cartella tolta dai gruppi", [&](NavGroupsState& state) {
            bool found = std::erase(state.top, key) > 0;
            for (auto& group : state.groups)
            {
                found = std::erase(group.children, key) > 0 || found;
            }
            return found;
        });
    }
}
