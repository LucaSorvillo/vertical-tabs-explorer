#pragma once
// Percorsi, log e piccole utilità di VTabsExplorer.

#include <windows.h>

#include <string>
#include <string_view>

namespace xpx
{
    inline constexpr wchar_t kAppName[] = L"VTabsExplorer";

    // %LOCALAPPDATA%\VTabsExplorer (creata se manca).
    std::wstring DataDirectory();
    // %LOCALAPPDATA%\VTabsExplorer\logs (creata se manca).
    std::wstring LogDirectory();
    // %LOCALAPPDATA%\VTabsExplorer\settings.ini
    std::wstring SettingsPath();

    // Modalità prova: se esiste %LOCALAPPDATA%\VTabsExplorer\dev-filter.txt, la sua prima riga è una
    // cartella e VTabsExplorer considera solo le finestre con tutte le schede dentro quella cartella
    // (le finestre dell'utente vengono ignorate) e usa un file di sessione separato.
    std::wstring DevFilterPath();
    // Cartella della modalità prova ("" se la modalità prova è spenta).
    std::wstring DevFilterRoot();
    // In modalità prova le finestre si velano solo mentre esiste questo file (dev-arm.txt): la prova lo
    // crea prima di aprire la finestra che fa scattare il ripristino.
    std::wstring DevArmPath();
    // Cartelle in una scheda (vedi Veil) in modalità prova: solo mentre esiste dev-merge.txt, perché le altre prove
    // aprono finestre di prova che devono restare finestre. Se il file contiene "external", ogni finestra nuova conta
    // come aperta da un altro programma: la prova non può scegliere chi è in primo piano.
    enum class DevMerge
    {
        Off,
        On,
        External,
    };
    DevMerge DevMergeMode();

    // Sessione salvata: session.txt (session-dev.txt in modalità prova).
    std::wstring SessionPath(bool devMode);
    // Pila di Ctrl+Maiusc+T su disco (closed.txt; in modalità prova closed-dev.txt).
    std::wstring ClosedStackPath(bool devMode);

    bool EqualsIgnoreCase(std::wstring_view a, std::wstring_view b);
    // true se path è root o sta dentro root (confronto senza maiuscole/minuscole).
    bool IsInsideFolder(std::wstring_view path, std::wstring_view root);
    // true se il percorso è di file system (unità o percorso di rete), non una cartella virtuale.
    bool IsFileSystemPath(std::wstring_view path);

    // Log in %LOCALAPPDATA%\VTabsExplorer\logs\vtabsexplorer.log (thread-safe).
    void Log(const wchar_t* format, ...);
    void SetVerbose(bool verbose);
    bool IsVerbose();
}

#define XPX_VLOG(...)                 \
    do                                \
    {                                 \
        if (::xpx::IsVerbose())       \
        {                             \
            ::xpx::Log(__VA_ARGS__);  \
        }                             \
    } while (0)
