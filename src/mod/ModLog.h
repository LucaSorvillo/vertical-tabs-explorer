#pragma once
// Log della mod: %LOCALAPPDATA%\VTabsExplorer\logs\mod.log (thread-safe). Mai percorsi o nomi delle
// cartelle dell'utente: solo conteggi, tipi e stati.

namespace xpx::mod
{
    // %LOCALAPPDATA%\VTabsExplorer (e \logs), create se mancano.
    std::wstring DataDirectory();
    std::wstring LogDirectory();

    void LogInit();
    void Log(const wchar_t* format, ...);
    void SetVerbose(bool verbose);
    bool IsVerbose();
}

#define XPX_MLOG(...) ::xpx::mod::Log(__VA_ARGS__)
#define XPX_MVLOG(...)                 \
    do                                 \
    {                                  \
        if (::xpx::mod::IsVerbose())   \
        {                              \
            ::xpx::mod::Log(__VA_ARGS__); \
        }                              \
    } while (0)
