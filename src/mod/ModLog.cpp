#include "pch.h"
#include "ModLog.h"

#include <cstdarg>
#include <cstdio>

namespace xpx::mod
{
    namespace
    {
        std::mutex g_lock;
        std::wstring g_path;    // mod.log (vuoto finché LogInit non è stata chiamata)
        std::wstring g_oldPath; // mod.old.log
        unsigned g_writes = 0;
        std::atomic<bool> g_verbose{ false };
        constexpr LONGLONG kMaxLogSize = 2 * 1024 * 1024;

        // Rotazione semplice: oltre 2 MB il file diventa mod.old.log. Sotto g_lock.
        void RotateIfLarge()
        {
            WIN32_FILE_ATTRIBUTE_DATA info{};
            if (GetFileAttributesExW(g_path.c_str(), GetFileExInfoStandard, &info))
            {
                const LONGLONG size = (static_cast<LONGLONG>(info.nFileSizeHigh) << 32) | info.nFileSizeLow;
                if (size > kMaxLogSize)
                {
                    MoveFileExW(g_path.c_str(), g_oldPath.c_str(), MOVEFILE_REPLACE_EXISTING);
                }
            }
        }
    }

    std::wstring DataDirectory()
    {
        wchar_t local[MAX_PATH]{};
        GetEnvironmentVariableW(L"LOCALAPPDATA", local, ARRAYSIZE(local));
        std::wstring dir = std::wstring{ local } + L"\\VTabsExplorer";
        CreateDirectoryW(dir.c_str(), nullptr);
        return dir;
    }

    std::wstring LogDirectory()
    {
        std::wstring dir = DataDirectory() + L"\\logs";
        CreateDirectoryW(dir.c_str(), nullptr);
        return dir;
    }

    void LogInit()
    {
        std::scoped_lock lock(g_lock);
        if (!g_path.empty())
        {
            return;
        }
        g_path = LogDirectory() + L"\\mod.log";
        g_oldPath = LogDirectory() + L"\\mod.old.log";
        RotateIfLarge();
    }

    void SetVerbose(bool verbose)
    {
        g_verbose = verbose;
    }

    bool IsVerbose()
    {
        return g_verbose;
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
        _snwprintf_s(line, _TRUNCATE, L"%04u-%02u-%02u %02u:%02u:%02u.%03u [%5lu:%5lu] %s\r\n", now.wYear, now.wMonth, now.wDay, now.wHour,
                     now.wMinute, now.wSecond, now.wMilliseconds, GetCurrentProcessId(), GetCurrentThreadId(), message);

        const int bytes = WideCharToMultiByte(CP_UTF8, 0, line, -1, nullptr, 0, nullptr, nullptr);
        if (bytes <= 1)
        {
            return;
        }
        std::string utf8(static_cast<size_t>(bytes - 1), '\0');
        WideCharToMultiByte(CP_UTF8, 0, line, -1, utf8.data(), bytes, nullptr, nullptr);

        std::scoped_lock lock(g_lock);
        if (g_path.empty())
        {
            return;
        }
        // Il file si apre a ogni riga: più processi (ogni explorer.exe con la mod) scrivono nello stesso log, e chi lo ruota
        // (rinominandolo) non deve lasciare gli altri a scrivere per sempre nel file vecchio, come succedeva tenendo aperto
        // un handle. Anche la dimensione si ricontrolla ogni tanto, non solo all'avvio.
        if (++g_writes % 256 == 0)
        {
            RotateIfLarge();
        }
        const HANDLE file = CreateFileW(g_path.c_str(), FILE_APPEND_DATA, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr, OPEN_ALWAYS,
                                        FILE_ATTRIBUTE_NORMAL, nullptr);
        if (file != INVALID_HANDLE_VALUE)
        {
            DWORD written = 0;
            WriteFile(file, utf8.data(), static_cast<DWORD>(utf8.size()), &written, nullptr);
            CloseHandle(file);
        }
    }
}
