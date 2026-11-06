#include "TabColorFiles.h"

#include "Common.h"

#include <cwchar>

namespace xpx
{
    namespace
    {
        // "live-1a2b3c.txt" -> (L"live", HWND 0x1a2b3c). false se il nome non è di quella forma.
        bool ParseName(std::wstring const& name, std::wstring& kind, HWND& window)
        {
            const size_t dash = name.find(L'-');
            const size_t dot = name.rfind(L".txt");
            if (dash == std::wstring::npos || dot == std::wstring::npos || dot <= dash + 1 || dot + 4 != name.size())
            {
                return false;
            }
            kind = name.substr(0, dash);
            const std::wstring hex = name.substr(dash + 1, dot - dash - 1);
            if (hex.find_first_not_of(L"0123456789abcdefABCDEF") != std::wstring::npos)
            {
                return false;
            }
            window = reinterpret_cast<HWND>(static_cast<uintptr_t>(wcstoull(hex.c_str(), nullptr, 16)));
            return window != nullptr;
        }

        template <typename F>
        void ForEachColorFile(F&& action)
        {
            const std::wstring directory = ColorsDirectory();
            WIN32_FIND_DATAW find{};
            const HANDLE search = FindFirstFileW((directory + L"\\*.txt").c_str(), &find);
            if (search == INVALID_HANDLE_VALUE)
            {
                return;
            }
            do
            {
                if (find.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)
                {
                    continue;
                }
                std::wstring kind;
                HWND window = nullptr;
                if (ParseName(find.cFileName, kind, window))
                {
                    action(directory + L"\\" + find.cFileName, kind, window);
                }
            } while (FindNextFileW(search, &find));
            FindClose(search);
        }
    }

    std::wstring ColorsDirectory()
    {
        // Creata una volta (la si guarda a ogni controllo, ogni mezzo secondo).
        static const std::wstring directory = [] {
            std::wstring path = DataDirectory() + L"\\colors";
            CreateDirectoryW(path.c_str(), nullptr);
            return path;
        }();
        return directory;
    }

    ULONGLONG ColorsStamp()
    {
        WIN32_FILE_ATTRIBUTE_DATA data{};
        if (!GetFileAttributesExW(ColorsDirectory().c_str(), GetFileExInfoStandard, &data))
        {
            return 0;
        }
        return (static_cast<ULONGLONG>(data.ftLastWriteTime.dwHighDateTime) << 32) | data.ftLastWriteTime.dwLowDateTime;
    }

    void ReadColorFiles(std::unordered_map<HWND, TabColorList>& live, std::unordered_map<HWND, TabColorList>& pending)
    {
        live.clear();
        pending.clear();
        ForEachColorFile([&](std::wstring const& path, std::wstring const& kind, HWND window) {
            TabColorList list;
            if (!IsWindow(window) || !ReadTabColors(path, list))
            {
                return;
            }
            if (kind == L"live")
            {
                live[window] = std::move(list);
            }
            else if (kind == L"pending")
            {
                pending[window] = std::move(list);
            }
        });
    }

    void WritePendingColors(HWND window, TabColorList const& list)
    {
        if (!window || list.colored.empty())
        {
            return;
        }
        // Come per la sessione: il file può essere aperto per un attimo da un altro programma.
        const std::wstring path = ColorsDirectory() + L"\\" + ColorFileName(L"pending", window);
        for (int attempt = 0; attempt < 5; ++attempt)
        {
            if (attempt)
            {
                Sleep(40);
            }
            if (WriteTabColors(path, list))
            {
                return;
            }
        }
        Log(L"Colori delle schede da rimettere non scritti (errore %lu).", GetLastError());
    }

    void CleanupColorFiles()
    {
        ForEachColorFile([](std::wstring const& path, std::wstring const&, HWND window) {
            if (!IsWindow(window))
            {
                DeleteFileW(path.c_str());
            }
        });
    }
}
