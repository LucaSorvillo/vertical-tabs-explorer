#include "pch.h"
#include "Glass.h"

#include "ModLog.h"

#include <dwmapi.h>
#include <unordered_set>

namespace xpx::mod::glass
{
    namespace
    {
        using ExtendFrameFn = HRESULT(WINAPI*)(HWND, const MARGINS*);

        struct WindowGlass
        {
            std::optional<MARGINS> requested; // ultima zona chiesta da Esplora file
            std::optional<int> limit;         // nostro limite al margine superiore
            int fallbackTop = 0;
        };

        std::mutex g_lock;
        std::unordered_map<HWND, WindowGlass> g_windows;
        std::unordered_set<HMODULE> g_hookedModules;
        std::atomic<bool> g_hooked{ false };
        // Voci delle importazioni sostituite, col valore di prima (anche il thunk del caricamento ritardato), per rimetterle
        // allo scaricamento; chiamate in corso nell'aggancio.
        struct HookedSlot
        {
            void** slot = nullptr;
            void* original = nullptr;
        };
        std::vector<HookedSlot> g_slots;
        std::atomic<int> g_inFlight{ 0 };

        // La funzione vera (attraverso le importazioni di questa DLL): mai il thunk del caricamento ritardato del modulo
        // agganciato, che risolverebbe la funzione riscrivendo la voce e togliendo l'aggancio.
        HRESULT CallOriginal(HWND hwnd, const MARGINS* margins)
        {
            return DwmExtendFrameIntoClientArea(hwnd, margins);
        }

        bool IsSheet(MARGINS const& m)
        {
            return m.cxLeftWidth < 0 || m.cxRightWidth < 0 || m.cyTopHeight < 0 || m.cyBottomHeight < 0;
        }

        MARGINS Limited(MARGINS m, int limit)
        {
            if (IsSheet(m))
            {
                return MARGINS{ 0, 0, limit, 0 }; // tutta la finestra di vetro: resta solo la fascia in cima
            }
            m.cyTopHeight = std::min(m.cyTopHeight, limit);
            return m;
        }

        MARGINS Effective(WindowGlass const& glass)
        {
            const MARGINS base = glass.requested.value_or(MARGINS{ 0, 0, glass.fallbackTop, 0 });
            return glass.limit ? Limited(base, *glass.limit) : base;
        }

        HRESULT WINAPI ExtendFrameHook(HWND hwnd, const MARGINS* margins)
        {
            ++g_inFlight;
            struct Leave
            {
                ~Leave()
                {
                    --g_inFlight;
                }
            } const leave;
            if (!margins)
            {
                return CallOriginal(hwnd, margins);
            }
            MARGINS applied = *margins;
            {
                std::scoped_lock lock(g_lock);
                std::erase_if(g_windows, [](auto const& entry) { return !IsWindow(entry.first); });
                WindowGlass& glass = g_windows[hwnd];
                glass.requested = *margins;
                if (glass.limit)
                {
                    applied = Limited(*margins, *glass.limit);
                }
            }
            if (applied.cyTopHeight != margins->cyTopHeight)
            {
                XPX_MVLOG(L"Zona vetro: Esplora file chiede %d px in cima, ridotti a %d (contenuto salito).", margins->cyTopHeight,
                          applied.cyTopHeight);
            }
            return CallOriginal(hwnd, &applied);
        }

        const IMAGE_NT_HEADERS* Headers(BYTE* base)
        {
            const auto* dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(base);
            if (dos->e_magic != IMAGE_DOS_SIGNATURE)
            {
                return nullptr;
            }
            const auto* nt = reinterpret_cast<const IMAGE_NT_HEADERS*>(base + dos->e_lfanew);
            return nt->Signature == IMAGE_NT_SIGNATURE ? nt : nullptr;
        }

        // Voce di 'function' in una tabella degli indirizzi (IAT) data la tabella dei nomi corrispondente.
        void** FindInThunks(BYTE* base, DWORD addressTableRva, DWORD nameTableRva, const char* function)
        {
            if (!addressTableRva || !nameTableRva)
            {
                return nullptr;
            }
            auto* slot = reinterpret_cast<IMAGE_THUNK_DATA*>(base + addressTableRva);
            for (const auto* name = reinterpret_cast<const IMAGE_THUNK_DATA*>(base + nameTableRva); name->u1.AddressOfData; ++name, ++slot)
            {
                if (IMAGE_SNAP_BY_ORDINAL(name->u1.Ordinal))
                {
                    continue;
                }
                const auto* byName = reinterpret_cast<const IMAGE_IMPORT_BY_NAME*>(base + name->u1.AddressOfData);
                if (strcmp(reinterpret_cast<const char*>(byName->Name), function) == 0)
                {
                    return reinterpret_cast<void**>(&slot->u1.Function);
                }
            }
            return nullptr;
        }

        // Voce della funzione nelle importazioni di 'module': prima quelle ritardate (ExplorerFrame.dll), poi quelle normali.
        void** FindImport(HMODULE module, const char* dll, const char* function)
        {
            auto* base = reinterpret_cast<BYTE*>(module);
            const IMAGE_NT_HEADERS* nt = Headers(base);
            if (!nt)
            {
                return nullptr;
            }
            const IMAGE_DATA_DIRECTORY& delay = nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_DELAY_IMPORT];
            if (delay.VirtualAddress && delay.Size)
            {
                for (auto* descriptor = reinterpret_cast<const IMAGE_DELAYLOAD_DESCRIPTOR*>(base + delay.VirtualAddress); descriptor->DllNameRVA;
                     ++descriptor)
                {
                    if (!descriptor->Attributes.RvaBased ||
                        _stricmp(reinterpret_cast<const char*>(base + descriptor->DllNameRVA), dll) != 0)
                    {
                        continue;
                    }
                    if (void** slot = FindInThunks(base, descriptor->ImportAddressTableRVA, descriptor->ImportNameTableRVA, function))
                    {
                        return slot;
                    }
                }
            }
            const IMAGE_DATA_DIRECTORY& normal = nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT];
            if (normal.VirtualAddress && normal.Size)
            {
                for (auto* descriptor = reinterpret_cast<const IMAGE_IMPORT_DESCRIPTOR*>(base + normal.VirtualAddress); descriptor->Name; ++descriptor)
                {
                    if (_stricmp(reinterpret_cast<const char*>(base + descriptor->Name), dll) != 0)
                    {
                        continue;
                    }
                    const DWORD names = descriptor->OriginalFirstThunk ? descriptor->OriginalFirstThunk : descriptor->FirstThunk;
                    if (void** slot = FindInThunks(base, descriptor->FirstThunk, names, function))
                    {
                        return slot;
                    }
                }
            }
            return nullptr;
        }

        bool HookModule(const wchar_t* name)
        {
            const HMODULE module = GetModuleHandleW(name);
            if (!module || g_hookedModules.contains(module))
            {
                return false;
            }
            void** slot = FindImport(module, "dwmapi.dll", "DwmExtendFrameIntoClientArea");
            if (!slot)
            {
                g_hookedModules.insert(module); // niente da agganciare: non si riprova
                XPX_MLOG(L"Zona vetro: %s non importa DwmExtendFrameIntoClientArea.", name);
                return false;
            }
            DWORD protect = 0;
            if (!VirtualProtect(slot, sizeof(void*), PAGE_READWRITE, &protect))
            {
                XPX_MLOG(L"Zona vetro: importazioni di %s non modificabili (%lu).", name, GetLastError());
                return false;
            }
            void* const original = InterlockedExchangePointer(slot, reinterpret_cast<void*>(&ExtendFrameHook));
            VirtualProtect(slot, sizeof(void*), protect, &protect);
            g_slots.push_back(HookedSlot{ slot, original });
            g_hookedModules.insert(module);
            XPX_MLOG(L"Zona vetro: aggancio di DwmExtendFrameIntoClientArea installato in %s.", name);
            return true;
        }
    }

    std::mutex& InstallLock()
    {
        static std::mutex installLock;
        return installLock;
    }

    bool Uninstall()
    {
        std::scoped_lock lock(InstallLock());
        bool ok = true;
        std::vector<HookedSlot> kept;
        for (HookedSlot const& hooked : g_slots)
        {
            DWORD protect = 0;
            if (!VirtualProtect(hooked.slot, sizeof(void*), PAGE_READWRITE, &protect))
            {
                ok = false;
                kept.push_back(hooked);
                continue;
            }
            const bool ours = InterlockedCompareExchangePointer(hooked.slot, hooked.original, reinterpret_cast<void*>(&ExtendFrameHook)) ==
                              reinterpret_cast<void*>(&ExtendFrameHook);
            VirtualProtect(hooked.slot, sizeof(void*), protect, &protect);
            if (!ours)
            {
                ok = false; // un altro aggancio sopra il nostro: la DLL non si scarica
                kept.push_back(hooked);
            }
        }
        g_slots = std::move(kept);
        if (ok)
        {
            g_hookedModules.clear();
            g_hooked = false;
            std::scoped_lock windows(g_lock);
            g_windows.clear();
        }
        XPX_MLOG(L"Zona vetro: importazioni originali rimesse (%s).", ok ? L"tutte" : L"NON tutte: la mod non si scarica");
        return ok;
    }

    int InFlight()
    {
        return g_inFlight.load();
    }

    bool Install()
    {
        std::scoped_lock lock(InstallLock());
        for (const wchar_t* name : { L"ExplorerFrame.dll", L"ieframe.dll" })
        {
            if (HookModule(name))
            {
                g_hooked = true;
            }
        }
        return g_hooked;
    }

    bool Hooked()
    {
        return g_hooked;
    }

    void SetTopLimit(HWND hwnd, std::optional<int> limitPx, int fallbackTopPx)
    {
        if (!hwnd)
        {
            return;
        }
        MARGINS target{};
        {
            std::scoped_lock lock(g_lock);
            WindowGlass& glass = g_windows[hwnd];
            glass.fallbackTop = fallbackTopPx;
            if (glass.limit == limitPx)
            {
                return;
            }
            glass.limit = limitPx;
            target = Effective(glass);
        }
        const HRESULT hr = CallOriginal(hwnd, &target);
        XPX_MLOG(L"Zona vetro: margine superiore %d px (%s, 0x%08X).", target.cyTopHeight,
                 limitPx ? L"fino al contenuto salito" : L"quello di Esplora file", static_cast<unsigned>(hr));
    }

    void Forget(HWND hwnd)
    {
        std::scoped_lock lock(g_lock);
        g_windows.erase(hwnd);
    }
}
