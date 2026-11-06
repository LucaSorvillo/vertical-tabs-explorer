#include "Session.h"

#include "Common.h"
#include "ModShared.h"

#include <wincrypt.h>

#include <algorithm>
#include <cstdlib>
#include <sstream>

namespace xpx
{
    namespace
    {
        constexpr char kHeader[] = "VTabsExplorer-session 1";

        std::string ToUtf8(std::wstring_view text)
        {
            if (text.empty())
            {
                return {};
            }
            const int bytes = WideCharToMultiByte(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), nullptr, 0, nullptr, nullptr);
            std::string result(static_cast<size_t>(bytes), '\0');
            WideCharToMultiByte(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), result.data(), bytes, nullptr, nullptr);
            return result;
        }

        std::wstring FromUtf8(std::string_view text)
        {
            if (text.empty())
            {
                return {};
            }
            const int length = MultiByteToWideChar(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), nullptr, 0);
            std::wstring result(static_cast<size_t>(length), L'\0');
            MultiByteToWideChar(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), result.data(), length);
            return result;
        }

        std::string ToBase64(std::vector<BYTE> const& bytes)
        {
            if (bytes.empty())
            {
                return {};
            }
            DWORD length = 0;
            const DWORD flags = CRYPT_STRING_BASE64 | CRYPT_STRING_NOCRLF;
            CryptBinaryToStringA(bytes.data(), static_cast<DWORD>(bytes.size()), flags, nullptr, &length);
            std::string text(length, '\0');
            CryptBinaryToStringA(bytes.data(), static_cast<DWORD>(bytes.size()), flags, text.data(), &length);
            text.resize(length);
            return text;
        }

        std::vector<BYTE> FromBase64(std::string const& text)
        {
            DWORD length = 0;
            if (!CryptStringToBinaryA(text.c_str(), static_cast<DWORD>(text.size()), CRYPT_STRING_BASE64, nullptr, &length, nullptr, nullptr))
            {
                return {};
            }
            std::vector<BYTE> bytes(length);
            CryptStringToBinaryA(text.c_str(), static_cast<DWORD>(text.size()), CRYPT_STRING_BASE64, bytes.data(), &length, nullptr, nullptr);
            bytes.resize(length);
            return bytes;
        }

        bool SamePlacement(WINDOWPLACEMENT const& a, WINDOWPLACEMENT const& b)
        {
            return a.length == b.length && a.showCmd == b.showCmd && EqualRect(&a.rcNormalPosition, &b.rcNormalPosition);
        }
    }

    size_t Session::TabCount() const
    {
        size_t count = 0;
        for (auto const& window : windows)
        {
            count += window.tabs.size();
        }
        return count;
    }

    bool SameSession(Session const& a, Session const& b)
    {
        if (a.windows.size() != b.windows.size())
        {
            return false;
        }
        for (size_t i = 0; i < a.windows.size(); ++i)
        {
            auto const& wa = a.windows[i];
            auto const& wb = b.windows[i];
            if (wa.active != wb.active || wa.tabs.size() != wb.tabs.size() || !SamePlacement(wa.placement, wb.placement))
            {
                return false;
            }
            for (size_t t = 0; t < wa.tabs.size(); ++t)
            {
                if (wa.tabs[t].path != wb.tabs[t].path || wa.tabs[t].color != wb.tabs[t].color)
                {
                    return false;
                }
            }
        }
        return true;
    }

    namespace
    {
        constexpr char kClosedHeader[] = "VTabsExplorer-closed 1";

        // Una finestra nel formato del file (dopo la sua riga "window").
        void WriteWindow(std::ostringstream& out, Window const& window)
        {
            if (window.placement.length == sizeof(WINDOWPLACEMENT))
            {
                auto const& p = window.placement;
                out << "placement " << p.flags << ' ' << p.showCmd << ' ' << p.ptMinPosition.x << ' ' << p.ptMinPosition.y << ' '
                    << p.ptMaxPosition.x << ' ' << p.ptMaxPosition.y << ' ' << p.rcNormalPosition.left << ' ' << p.rcNormalPosition.top << ' '
                    << p.rcNormalPosition.right << ' ' << p.rcNormalPosition.bottom << "\n";
            }
            out << "active " << window.active << "\n";
            for (auto const& tab : window.tabs)
            {
                out << "tab " << ToUtf8(tab.path) << "\n";
                if (!tab.idList.empty())
                {
                    out << "idlist " << ToBase64(tab.idList) << "\n";
                }
                if (tab.color > 0 && tab.color < kTabColorCount)
                {
                    out << "color " << ToUtf8(kTabColorKeys[tab.color]) << "\n";
                }
            }
        }

        // Scrittura atomica: file temporaneo e poi sostituzione.
        bool WriteAtomically(std::wstring const& file, std::string const& text)
        {
            const std::wstring temp = file + L".tmp";
            const HANDLE handle = CreateFileW(temp.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
            if (handle == INVALID_HANDLE_VALUE)
            {
                return false;
            }
            DWORD written = 0;
            const bool ok = WriteFile(handle, text.data(), static_cast<DWORD>(text.size()), &written, nullptr) && written == text.size();
            FlushFileBuffers(handle);
            CloseHandle(handle);
            if (!ok)
            {
                DeleteFileW(temp.c_str());
                return false;
            }
            return MoveFileExW(temp.c_str(), file.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH) != FALSE;
        }

        // Finestre di un file (sessione o pila): ogni "window" ne comincia una; kinds riceve il resto della riga "window"
        // (vuoto nella sessione, "whole"/"tabs" nella pila). false se il file c'è ma non comincia con header.
        bool ReadWindows(std::wstring const& file, const char* header, std::vector<Window>& windows, std::vector<std::string>* kinds)
        {
            const HANDLE handle = CreateFileW(file.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_DELETE, nullptr, OPEN_EXISTING,
                                              FILE_ATTRIBUTE_NORMAL, nullptr);
            if (handle == INVALID_HANDLE_VALUE)
            {
                return true; // nessun file: niente da leggere
            }
            LARGE_INTEGER size{};
            GetFileSizeEx(handle, &size);
            std::string text(static_cast<size_t>(std::min<LONGLONG>(size.QuadPart, 16 * 1024 * 1024)), '\0');
            DWORD read = 0;
            ReadFile(handle, text.data(), static_cast<DWORD>(text.size()), &read, nullptr);
            CloseHandle(handle);
            text.resize(read);

            std::istringstream in(text);
            std::string line;
            if (!std::getline(in, line) || line.rfind(header, 0) != 0)
            {
                return false;
            }
            std::vector<std::string> foundKinds;
            while (std::getline(in, line))
            {
                if (!line.empty() && line.back() == '\r')
                {
                    line.pop_back();
                }
                const size_t space = line.find(' ');
                const std::string key = line.substr(0, space);
                const std::string value = space == std::string::npos ? std::string{} : line.substr(space + 1);
                if (key == "window")
                {
                    windows.emplace_back();
                    foundKinds.push_back(value);
                }
                else if (windows.empty())
                {
                    continue;
                }
                else if (key == "placement")
                {
                    WINDOWPLACEMENT p{ sizeof(WINDOWPLACEMENT) };
                    std::istringstream fields(value);
                    fields >> p.flags >> p.showCmd >> p.ptMinPosition.x >> p.ptMinPosition.y >> p.ptMaxPosition.x >> p.ptMaxPosition.y >>
                        p.rcNormalPosition.left >> p.rcNormalPosition.top >> p.rcNormalPosition.right >> p.rcNormalPosition.bottom;
                    if (fields)
                    {
                        windows.back().placement = p;
                    }
                }
                else if (key == "active")
                {
                    windows.back().active = std::atoi(value.c_str());
                }
                else if (key == "tab")
                {
                    windows.back().tabs.push_back(Tab{ FromUtf8(value) });
                }
                else if (key == "idlist" && !windows.back().tabs.empty())
                {
                    windows.back().tabs.back().idList = FromBase64(value);
                }
                else if (key == "color" && !windows.back().tabs.empty())
                {
                    windows.back().tabs.back().color = TabColorFromKey(FromUtf8(value));
                }
            }
            // Finestre senza schede: via (con il loro tipo).
            for (size_t i = windows.size(); i-- > 0;)
            {
                if (windows[i].tabs.empty())
                {
                    windows.erase(windows.begin() + static_cast<std::ptrdiff_t>(i));
                    foundKinds.erase(foundKinds.begin() + static_cast<std::ptrdiff_t>(i));
                }
            }
            for (auto& window : windows)
            {
                if (window.active < 0 || window.active >= static_cast<int>(window.tabs.size()))
                {
                    window.active = 0;
                }
            }
            if (kinds)
            {
                *kinds = std::move(foundKinds);
            }
            return true;
        }
    }

    bool SaveSession(std::wstring const& file, Session const& session)
    {
        std::ostringstream out;
        out << kHeader << "\n";
        for (auto const& window : session.windows)
        {
            out << "window\n";
            WriteWindow(out, window);
        }
        return WriteAtomically(file, out.str());
    }

    Session LoadSession(std::wstring const& file)
    {
        Session session;
        if (!ReadWindows(file, kHeader, session.windows, nullptr))
        {
            Log(L"Sessione non valida in %s: ignorata.", file.c_str());
            session.windows.clear();
        }
        return session;
    }

    bool SaveClosedStack(std::wstring const& file, std::vector<ClosedEntry> const& entries)
    {
        if (entries.empty())
        {
            return DeleteFileW(file.c_str()) || GetLastError() == ERROR_FILE_NOT_FOUND;
        }
        std::ostringstream out;
        out << kClosedHeader << "\n";
        for (auto const& entry : entries)
        {
            // "window tabs at 3": il posto della scheda nella sua finestra (i file senza "at" restano validi)
            out << "window " << (entry.wholeWindow ? "whole" : "tabs");
            if (!entry.wholeWindow && entry.index >= 0)
            {
                out << " at " << entry.index;
            }
            out << "\n";
            WriteWindow(out, entry.window);
        }
        return WriteAtomically(file, out.str());
    }

    std::vector<ClosedEntry> LoadClosedStack(std::wstring const& file)
    {
        std::vector<Window> windows;
        std::vector<std::string> kinds;
        std::vector<ClosedEntry> entries;
        if (!ReadWindows(file, kClosedHeader, windows, &kinds))
        {
            Log(L"Schede chiuse non valide in %s: ignorate.", file.c_str());
            return entries;
        }
        for (size_t i = 0; i < windows.size(); ++i)
        {
            const std::string kind = i < kinds.size() ? kinds[i] : std::string{};
            const size_t at = kind.find(" at ");
            entries.push_back(ClosedEntry{ kind == "whole", std::move(windows[i]), at == std::string::npos ? -1 : std::atoi(kind.c_str() + at + 4) });
        }
        return entries;
    }

    std::wstring Describe(Session const& session, bool withPaths)
    {
        std::wstring text = std::to_wstring(session.windows.size()) + (session.windows.size() == 1 ? L" finestra, " : L" finestre, ") +
                            std::to_wstring(session.TabCount()) + (session.TabCount() == 1 ? L" scheda" : L" schede");
        if (withPaths)
        {
            for (auto const& window : session.windows)
            {
                text += L" [";
                for (size_t i = 0; i < window.tabs.size(); ++i)
                {
                    text += (i ? L" | " : L"") + std::wstring(static_cast<int>(i) == window.active ? L"*" : L"") + window.tabs[i].path;
                    if (window.tabs[i].color > 0 && window.tabs[i].color < kTabColorCount)
                    {
                        text += std::wstring(L" (") + kTabColorKeys[window.tabs[i].color] + L")";
                    }
                }
                text += L"]";
            }
        }
        return text;
    }
}
