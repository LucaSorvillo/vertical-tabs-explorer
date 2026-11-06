#include "Common.h"

#include <shlobj.h>

#include <atomic>
#include <cstdarg>
#include <cstdio>
#include <mutex>

namespace xpx
{
    namespace
    {
        std::mutex g_logLock;
        std::atomic<bool> g_verbose{ false };
        constexpr LONGLONG kMaxLogBytes = 1024 * 1024;
    }

    std::wstring DataDirectory()
    {
        std::wstring result;
        PWSTR localAppData = nullptr;
        if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_LocalAppData, KF_FLAG_DEFAULT, nullptr, &localAppData)))
        {
            result = localAppData;
            result += L"\\";
            result += kAppName;
            CreateDirectoryW(result.c_str(), nullptr);
        }
        CoTaskMemFree(localAppData);
        return result;
    }

    std::wstring LogDirectory()
    {
        std::wstring dir = DataDirectory() + L"\\logs";
        CreateDirectoryW(dir.c_str(), nullptr);
        return dir;
    }

    std::wstring SettingsPath()
    {
        return DataDirectory() + L"\\settings.ini";
    }

    std::wstring DevFilterPath()
    {
        return DataDirectory() + L"\\dev-filter.txt";
    }

    std::wstring DevArmPath()
    {
        return DataDirectory() + L"\\dev-arm.txt";
    }

    DevMerge DevMergeMode()
    {
        const HANDLE file = CreateFileW((DataDirectory() + L"\\dev-merge.txt").c_str(), GENERIC_READ,
                                        FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (file == INVALID_HANDLE_VALUE)
        {
            return DevMerge::Off;
        }
        char buffer[64]{};
        DWORD read = 0;
        ReadFile(file, buffer, sizeof(buffer) - 1, &read, nullptr);
        CloseHandle(file);
        return std::string_view(buffer, read).find("external") != std::string_view::npos ? DevMerge::External : DevMerge::On;
    }

    std::wstring DevFilterRoot()
    {
        const HANDLE file = CreateFileW(DevFilterPath().c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr,
                                        OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (file == INVALID_HANDLE_VALUE)
        {
            return {};
        }
        char buffer[2048]{};
        DWORD read = 0;
        ReadFile(file, buffer, sizeof(buffer) - 1, &read, nullptr);
        CloseHandle(file);

        std::string text(buffer, read);
        if (text.size() >= 3 && static_cast<unsigned char>(text[0]) == 0xEF && static_cast<unsigned char>(text[1]) == 0xBB &&
            static_cast<unsigned char>(text[2]) == 0xBF)
        {
            text.erase(0, 3); // BOM UTF-8
        }
        text = text.substr(0, text.find_first_of("\r\n"));
        while (!text.empty() && (text.back() == ' ' || text.back() == '\\'))
        {
            text.pop_back();
        }
        if (text.empty())
        {
            return {};
        }
        const int length = MultiByteToWideChar(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), nullptr, 0);
        std::wstring root(static_cast<size_t>(length), L'\0');
        MultiByteToWideChar(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), root.data(), length);
        return root;
    }

    std::wstring SessionPath(bool devMode)
    {
        return DataDirectory() + (devMode ? L"\\session-dev.txt" : L"\\session.txt");
    }

    std::wstring ClosedStackPath(bool devMode)
    {
        return DataDirectory() + (devMode ? L"\\closed-dev.txt" : L"\\closed.txt");
    }

    bool EqualsIgnoreCase(std::wstring_view a, std::wstring_view b)
    {
        return CompareStringOrdinal(a.data(), static_cast<int>(a.size()), b.data(), static_cast<int>(b.size()), TRUE) == CSTR_EQUAL;
    }

    bool IsInsideFolder(std::wstring_view path, std::wstring_view root)
    {
        if (root.empty() || path.size() < root.size() || !EqualsIgnoreCase(path.substr(0, root.size()), root))
        {
            return false;
        }
        return path.size() == root.size() || path[root.size()] == L'\\';
    }

    bool IsFileSystemPath(std::wstring_view path)
    {
        const bool drive = path.size() >= 3 && path[1] == L':' && path[2] == L'\\';
        const bool unc = path.size() >= 3 && path[0] == L'\\' && path[1] == L'\\' && path[2] != L'?';
        return drive || unc;
    }

    void Log(const wchar_t* format, ...)
    {
        wchar_t message[2048];
        va_list args;
        va_start(args, format);
        _vsnwprintf_s(message, _TRUNCATE, format, args);
        va_end(args);

        SYSTEMTIME now;
        GetLocalTime(&now);
        wchar_t line[2200];
        _snwprintf_s(line, _TRUNCATE, L"%04u-%02u-%02u %02u:%02u:%02u.%03u [%5lu] %s\r\n", now.wYear, now.wMonth, now.wDay, now.wHour,
                     now.wMinute, now.wSecond, now.wMilliseconds, GetCurrentThreadId(), message);
        OutputDebugStringW(line);

        std::scoped_lock lock(g_logLock);
        const std::wstring path = LogDirectory() + L"\\vtabsexplorer.log";
        WIN32_FILE_ATTRIBUTE_DATA info{};
        if (GetFileAttributesExW(path.c_str(), GetFileExInfoStandard, &info) &&
            ((static_cast<LONGLONG>(info.nFileSizeHigh) << 32) | info.nFileSizeLow) > kMaxLogBytes)
        {
            const std::wstring old = path + L".old";
            MoveFileExW(path.c_str(), old.c_str(), MOVEFILE_REPLACE_EXISTING);
        }
        const HANDLE file = CreateFileW(path.c_str(), FILE_APPEND_DATA, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr,
                                        OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (file != INVALID_HANDLE_VALUE)
        {
            char utf8[8192];
            const int bytes = WideCharToMultiByte(CP_UTF8, 0, line, -1, utf8, sizeof(utf8), nullptr, nullptr);
            DWORD written = 0;
            if (bytes > 1)
            {
                WriteFile(file, utf8, static_cast<DWORD>(bytes - 1), &written, nullptr);
            }
            CloseHandle(file);
        }
    }

    void SetVerbose(bool verbose)
    {
        g_verbose = verbose;
    }

    bool IsVerbose()
    {
        return g_verbose;
    }
}
