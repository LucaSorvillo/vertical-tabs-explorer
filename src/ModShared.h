#pragma once
// Costanti condivise tra la tray (VTabsExplorer.exe, che inietta) e la mod dentro Esplora file
// (VTabsExplorerMod.dll, schede verticali).

#include <windows.h>

#include <cwchar>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace xpx
{
    inline constexpr wchar_t kModDllName[] = L"VTabsExplorerMod.dll";

    // Lingua dei testi dell'app (menu e avvisi della tray, menu e suggerimenti della mod): l'italiano se la lingua
    // di visualizzazione di Windows dell'utente è l'italiano, altrimenti l'inglese. Si legge una volta per
    // processo: cambiare la lingua di Windows richiede di disconnettersi.
    inline bool UiItalian()
    {
        static const bool italian = PRIMARYLANGID(GetUserDefaultUILanguage()) == LANG_ITALIAN;
        return italian;
    }
    // Il testo nella lingua dell'app.
    inline const wchar_t* Tr(const wchar_t* english, const wchar_t* italian)
    {
        return UiItalian() ? italian : english;
    }


    // Processo della barra delle applicazioni: è l'explorer.exe che possiede Shell_TrayWnd (con
    // l'impostazione predefinita di Windows è anche quello di tutte le finestre di Esplora file).
    inline DWORD ShellProcessId()
    {
        DWORD pid = 0;
        if (const HWND tray = FindWindowW(L"Shell_TrayWnd", nullptr))
        {
            GetWindowThreadProcessId(tray, &pid);
        }
        return pid;
    }

    // ---------------------------------------------------------------------------------------------
    // Colori delle schede (schede verticali): il colore vive nel pannello, legato alla scheda; la tray
    // lo salva nella sessione e lo rimette quando riapre la scheda. Si scambiano file in
    // %LOCALAPPDATA%\VTabsExplorer\colors:
    //   live-<hwnd>.txt     (scritto dalla mod) colori della finestra <hwnd>, per posizione della scheda
    //   pending-<hwnd>.txt  (scritto dalla tray) colori da dare alle schede di una finestra appena
    //                       riaperta; la mod li applica quando la finestra ha tutte le schede e lo cancella
    // Formato (UTF-8): "count=<schede>" e poi una riga "<indice>=<colore>" per ogni scheda colorata; nei
    // file della mod "<indice>=<colore>|<titolo>": la tray abbina un colore alla sua scheda solo se nella
    // posizione indicata c'è una scheda con quel titolo (il suo ordine può essere ancora quello di prima di
    // un riordino). <hwnd> in esadecimale, senza prefisso.
    // ---------------------------------------------------------------------------------------------

    // Chiavi dei colori (indice 0 = nessun colore), le stesse di VTabsNotepad; l'ordine è quello del menu.
    inline constexpr const wchar_t* kTabColorKeys[] = { L"", L"grey", L"blue", L"red", L"yellow", L"green", L"pink", L"purple", L"cyan", L"orange" };
    inline constexpr int kTabColorCount = static_cast<int>(sizeof(kTabColorKeys) / sizeof(kTabColorKeys[0]));

    inline int TabColorFromKey(std::wstring_view key)
    {
        for (int i = 1; i < kTabColorCount; ++i)
        {
            if (key == kTabColorKeys[i])
            {
                return i;
            }
        }
        return 0;
    }

    inline std::string Utf8Of(std::wstring_view text)
    {
        if (text.empty())
        {
            return {};
        }
        const int bytes = WideCharToMultiByte(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), nullptr, 0, nullptr, nullptr);
        std::string result(static_cast<size_t>(bytes > 0 ? bytes : 0), '\0');
        if (bytes > 0)
        {
            WideCharToMultiByte(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), result.data(), bytes, nullptr, nullptr);
        }
        return result;
    }

    inline std::wstring WideOf(std::string_view text)
    {
        if (text.empty())
        {
            return {};
        }
        const int length = MultiByteToWideChar(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), nullptr, 0);
        std::wstring result(static_cast<size_t>(length > 0 ? length : 0), L'\0');
        if (length > 0)
        {
            MultiByteToWideChar(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), result.data(), length);
        }
        return result;
    }

    inline std::wstring ColorFileName(const wchar_t* kind, HWND window)
    {
        wchar_t name[64]{};
        swprintf_s(name, L"%s-%llx.txt", kind, static_cast<unsigned long long>(reinterpret_cast<uintptr_t>(window)));
        return name;
    }

    struct TabColorEntry
    {
        size_t index = 0;  // posizione della scheda nella barra
        int color = 0;     // indice in kTabColorKeys
        std::string title; // titolo della scheda in UTF-8 (solo nei file della mod; vuoto = non controllato)
    };

    struct TabColorList
    {
        size_t count = 0;                   // schede della finestra quando è stato scritto
        std::vector<TabColorEntry> colored; // solo le schede colorate
    };

    inline std::string FormatTabColors(TabColorList const& list)
    {
        std::string text = "count=" + std::to_string(list.count) + "\r\n";
        for (auto const& entry : list.colored)
        {
            if (entry.color > 0 && entry.color < kTabColorCount)
            {
                text += std::to_string(entry.index) + "=";
                for (const wchar_t c : std::wstring_view{ kTabColorKeys[entry.color] })
                {
                    text += static_cast<char>(c); // chiavi ASCII
                }
                if (!entry.title.empty())
                {
                    std::string title = entry.title;
                    for (char& c : title)
                    {
                        c = (c == '\r' || c == '\n') ? ' ' : c;
                    }
                    text += "|" + title;
                }
                text += "\r\n";
            }
        }
        return text;
    }

    inline TabColorList ParseTabColors(std::string_view text)
    {
        TabColorList list;
        size_t position = 0;
        while (position < text.size())
        {
            size_t end = text.find_first_of("\r\n", position);
            if (end == std::string_view::npos)
            {
                end = text.size();
            }
            const std::string_view line = text.substr(position, end - position);
            position = end + 1;
            const size_t equals = line.find('=');
            if (equals == std::string_view::npos)
            {
                continue;
            }
            const std::string name{ line.substr(0, equals) };
            const std::string value{ line.substr(equals + 1) };
            if (name == "count")
            {
                list.count = static_cast<size_t>(strtoull(value.c_str(), nullptr, 10));
                continue;
            }
            const size_t bar = value.find('|');
            const std::string key = value.substr(0, bar);
            const int color = TabColorFromKey(std::wstring(key.begin(), key.end()));
            if (color > 0 && !name.empty() && name.find_first_not_of("0123456789") == std::string::npos)
            {
                list.colored.push_back(TabColorEntry{ static_cast<size_t>(strtoull(name.c_str(), nullptr, 10)), color,
                                                      bar == std::string::npos ? std::string{} : value.substr(bar + 1) });
            }
        }
        return list;
    }

    // Lettura e scrittura (atomica: file temporaneo e poi sostituzione) di un file dei colori.
    inline bool WriteTabColors(std::wstring const& path, TabColorList const& list)
    {
        const std::string text = FormatTabColors(list);
        const std::wstring temp = path + L".tmp";
        const HANDLE file = CreateFileW(temp.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (file == INVALID_HANDLE_VALUE)
        {
            return false;
        }
        DWORD written = 0;
        const bool ok = WriteFile(file, text.data(), static_cast<DWORD>(text.size()), &written, nullptr) && written == text.size();
        CloseHandle(file);
        if (!ok)
        {
            DeleteFileW(temp.c_str());
            return false;
        }
        return MoveFileExW(temp.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING) != FALSE;
    }

    inline bool ReadTabColors(std::wstring const& path, TabColorList& list)
    {
        const HANDLE file = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr,
                                        OPEN_EXISTING, 0, nullptr);
        if (file == INVALID_HANDLE_VALUE)
        {
            return false;
        }
        std::string text(16 * 1024, '\0');
        DWORD read = 0;
        const bool ok = ReadFile(file, text.data(), static_cast<DWORD>(text.size()), &read, nullptr) != FALSE;
        CloseHandle(file);
        if (!ok)
        {
            return false;
        }
        text.resize(read);
        list = ParseTabColors(text);
        return true;
    }
}
