#include "pch.h"
#include "ModSettings.h"

#include "ModLog.h"

namespace xpx::mod
{
    namespace
    {
        std::wstring SettingsFile()
        {
            return DataDirectory() + L"\\settings.ini";
        }

        std::wstring DevWindowsFile()
        {
            return DataDirectory() + L"\\dev-windows.txt";
        }

        ULONGLONG WriteTime(std::wstring const& path)
        {
            WIN32_FILE_ATTRIBUTE_DATA data{};
            if (!GetFileAttributesExW(path.c_str(), GetFileExInfoStandard, &data))
            {
                return 0;
            }
            return (static_cast<ULONGLONG>(data.ftLastWriteTime.dwHighDateTime) << 32) | data.ftLastWriteTime.dwLowDateTime;
        }

        // Elenco dei test: un HWND per riga (es. "0x220ff6" o "220ff6"). false se il file non c'è.
        bool ReadDevWindows(std::vector<uint64_t>& windows)
        {
            const HANDLE file = CreateFileW(DevWindowsFile().c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                                            nullptr, OPEN_EXISTING, 0, nullptr);
            if (file == INVALID_HANDLE_VALUE)
            {
                return false;
            }
            std::string text(4096, '\0');
            DWORD read = 0;
            ReadFile(file, text.data(), static_cast<DWORD>(text.size() - 1), &read, nullptr);
            CloseHandle(file);
            text.resize(read);
            size_t position = 0;
            while (position < text.size())
            {
                size_t end = text.find_first_of("\r\n", position);
                if (end == std::string::npos)
                {
                    end = text.size();
                }
                const std::string line = text.substr(position, end - position);
                if (!line.empty())
                {
                    if (const uint64_t value = strtoull(line.c_str(), nullptr, 16))
                    {
                        windows.push_back(value);
                    }
                }
                position = end + 1;
            }
            return true;
        }
    }

    ModSettings ModSettings::Load()
    {
        const std::wstring file = SettingsFile();
        ModSettings s;
        s.verticalTabs = GetPrivateProfileIntW(L"VerticalTabs", L"Enabled", s.verticalTabs ? 1 : 0, file.c_str()) != 0;
        s.width = std::clamp(static_cast<int>(GetPrivateProfileIntW(L"VerticalTabs", L"Width", s.width, file.c_str())), kMinWidth, kMaxWidth);
        s.collapsed = GetPrivateProfileIntW(L"VerticalTabs", L"Collapsed", s.collapsed ? 1 : 0, file.c_str()) != 0;
        s.navBar = GetPrivateProfileIntW(L"NavBar", L"Enabled", s.navBar ? 1 : 0, file.c_str()) != 0;
        s.verboseLog = GetPrivateProfileIntW(L"Debug", L"VerboseLog", s.verboseLog ? 1 : 0, file.c_str()) != 0;
        s.devAllowlist = ReadDevWindows(s.devWindows);
        return s;
    }

    bool ModSettings::SavePanelState() const
    {
        const std::wstring file = SettingsFile();
        const std::wstring widthText = std::to_wstring(width);
        bool ok = WritePrivateProfileStringW(L"VerticalTabs", L"Width", widthText.c_str(), file.c_str()) != FALSE;
        ok &= WritePrivateProfileStringW(L"VerticalTabs", L"Collapsed", collapsed ? L"1" : L"0", file.c_str()) != FALSE;
        return ok;
    }

    ULONGLONG ModSettings::FileStamp()
    {
        // Somma dei momenti di modifica: cambia se cambia uno dei due file (o se l'elenco compare/sparisce).
        return WriteTime(SettingsFile()) + WriteTime(DevWindowsFile()) * 3;
    }

    bool ModSettings::Allows(HWND window) const
    {
        if (!devAllowlist)
        {
            return true;
        }
        const auto value = static_cast<uint64_t>(reinterpret_cast<uintptr_t>(window));
        return std::find(devWindows.begin(), devWindows.end(), value) != devWindows.end();
    }
}
