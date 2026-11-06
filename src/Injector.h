#pragma once
// Iniezione della mod (VTabsExplorerMod.dll) in un processo di Esplora file. Stesso metodo di VTabsNotepad:
// LoadLibraryW in un thread remoto, poi è la DLL stessa ad agganciarsi alla diagnostica XAML.
//
// Sicurezza: entra nel processo della barra delle applicazioni solo se chi chiama lo consente
// esplicitamente (la tray, con le schede verticali attive); lo strumento di sviluppo --inject-pid mai.

#include <windows.h>

#include <string>

namespace xpx
{
    // Copia la DLL in una cartella per ogni build (%LOCALAPPDATA%\VTabsExplorer\runtime\<stamp>): un
    // explorer.exe che la tiene caricata non blocca la compilazione successiva. "" se non riesce.
    std::wstring PrepareModDll(std::wstring const& sourceDll);
    // true se la mod è già caricata nel processo.
    bool IsModLoaded(DWORD pid);
    // Inietta la DLL; false con il motivo in error. allowShell: consentito anche il processo della barra
    // delle applicazioni (dove vivono le finestre di Esplora file con l'impostazione predefinita).
    bool InjectMod(DWORD pid, std::wstring const& dllPath, std::wstring& error, bool allowShell = false);
}
