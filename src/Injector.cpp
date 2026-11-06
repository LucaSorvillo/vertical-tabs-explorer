#include "Injector.h"

#include "Common.h"
#include "ModShared.h"

#include <tlhelp32.h>

#include <cwchar>

namespace xpx
{
    namespace
    {
        // Handle che si chiude da solo.
        struct UniqueHandle
        {
            HANDLE value = nullptr;
            explicit UniqueHandle(HANDLE h = nullptr) : value(h == INVALID_HANDLE_VALUE ? nullptr : h) {}
            ~UniqueHandle()
            {
                if (value)
                {
                    CloseHandle(value);
                }
            }
            UniqueHandle(UniqueHandle const&) = delete;
            UniqueHandle& operator=(UniqueHandle const&) = delete;
            explicit operator bool() const { return value != nullptr; }
        };

        std::wstring Win32Error(const wchar_t* what, DWORD code = GetLastError())
        {
            return std::wstring{ what } + L": errore " + std::to_wstring(code);
        }

        std::wstring ProcessImage(DWORD pid)
        {
            UniqueHandle process(OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid));
            wchar_t path[MAX_PATH * 2]{};
            DWORD size = ARRAYSIZE(path);
            if (!process || !QueryFullProcessImageNameW(process.value, 0, path, &size))
            {
                return {};
            }
            return path;
        }
    }

    bool IsModLoaded(DWORD pid)
    {
        UniqueHandle snapshot;
        // TH32CS_SNAPMODULE può fallire con ERROR_BAD_LENGTH mentre il processo carica moduli: si riprova.
        for (int attempt = 0; attempt < 5 && !snapshot; ++attempt)
        {
            snapshot.value = CreateToolhelp32Snapshot(TH32CS_SNAPMODULE, pid);
            if (snapshot.value == INVALID_HANDLE_VALUE)
            {
                snapshot.value = nullptr;
                if (GetLastError() != ERROR_BAD_LENGTH)
                {
                    break;
                }
            }
        }
        if (!snapshot)
        {
            return false;
        }
        MODULEENTRY32W module{ sizeof(module) };
        for (BOOL ok = Module32FirstW(snapshot.value, &module); ok; ok = Module32NextW(snapshot.value, &module))
        {
            if (EqualsIgnoreCase(module.szModule, kModDllName))
            {
                return true;
            }
        }
        return false;
    }

    std::wstring PrepareModDll(std::wstring const& sourceDll)
    {
        WIN32_FILE_ATTRIBUTE_DATA info{};
        if (!GetFileAttributesExW(sourceDll.c_str(), GetFileExInfoStandard, &info))
        {
            return {};
        }
        // Una cartella per ogni build (data di modifica + dimensione).
        wchar_t stamp[64];
        swprintf_s(stamp, L"%08lX%08lX-%lX", info.ftLastWriteTime.dwHighDateTime, info.ftLastWriteTime.dwLowDateTime, info.nFileSizeLow);
        const std::wstring runtimeRoot = DataDirectory() + L"\\runtime";
        CreateDirectoryW(runtimeRoot.c_str(), nullptr);
        const std::wstring targetDir = runtimeRoot + L"\\" + stamp;
        CreateDirectoryW(targetDir.c_str(), nullptr);
        const std::wstring target = targetDir + L"\\" + kModDllName;
        if (GetFileAttributesW(target.c_str()) == INVALID_FILE_ATTRIBUTES && !CopyFileW(sourceDll.c_str(), target.c_str(), TRUE))
        {
            return {};
        }
        // Pulizia delle copie vecchie: quelle ancora caricate in qualche processo non si possono cancellare.
        WIN32_FIND_DATAW find{};
        const HANDLE search = FindFirstFileW((runtimeRoot + L"\\*").c_str(), &find);
        if (search != INVALID_HANDLE_VALUE)
        {
            do
            {
                if (!(find.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) || find.cFileName[0] == L'.' || EqualsIgnoreCase(find.cFileName, stamp))
                {
                    continue;
                }
                const std::wstring oldDir = runtimeRoot + L"\\" + find.cFileName;
                if (DeleteFileW((oldDir + L"\\" + kModDllName).c_str()) || GetLastError() == ERROR_FILE_NOT_FOUND)
                {
                    RemoveDirectoryW(oldDir.c_str());
                }
            } while (FindNextFileW(search, &find));
            FindClose(search);
        }
        return target;
    }

    bool InjectMod(DWORD pid, std::wstring const& dllPath, std::wstring& error, bool allowShell)
    {
        // Solo dentro Esplora file; nel processo della barra delle applicazioni solo se consentito.
        const std::wstring image = ProcessImage(pid);
        const wchar_t* name = wcsrchr(image.c_str(), L'\\');
        if (image.empty() || !EqualsIgnoreCase(name ? name + 1 : image, L"explorer.exe"))
        {
            error = L"il processo non è un explorer.exe";
            return false;
        }
        if (!allowShell && pid == ShellProcessId())
        {
            error = L"è il processo della barra delle applicazioni: rifiutato";
            return false;
        }

        UniqueHandle process(OpenProcess(PROCESS_CREATE_THREAD | PROCESS_QUERY_INFORMATION | PROCESS_VM_OPERATION | PROCESS_VM_WRITE |
                                             PROCESS_VM_READ | SYNCHRONIZE,
                                         FALSE, pid));
        if (!process)
        {
            error = Win32Error(L"OpenProcess");
            return false;
        }
        const SIZE_T bytes = (dllPath.size() + 1) * sizeof(wchar_t);
        void* remotePath = VirtualAllocEx(process.value, nullptr, bytes, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
        if (!remotePath)
        {
            error = Win32Error(L"VirtualAllocEx");
            return false;
        }
        bool ok = false;
        if (!WriteProcessMemory(process.value, remotePath, dllPath.c_str(), bytes, nullptr))
        {
            error = Win32Error(L"WriteProcessMemory");
        }
        else
        {
            // kernel32 è mappata allo stesso indirizzo in tutti i processi della sessione.
            const auto loadLibrary = reinterpret_cast<LPTHREAD_START_ROUTINE>(
                reinterpret_cast<void*>(GetProcAddress(GetModuleHandleW(L"kernel32.dll"), "LoadLibraryW")));
            UniqueHandle thread(CreateRemoteThread(process.value, nullptr, 0, loadLibrary, remotePath, 0, nullptr));
            if (!thread)
            {
                error = Win32Error(L"CreateRemoteThread");
            }
            else if (WaitForSingleObject(thread.value, 15000) != WAIT_OBJECT_0)
            {
                error = L"LoadLibraryW non ha risposto entro 15 secondi";
            }
            else
            {
                DWORD exitCode = 0;
                GetExitCodeThread(thread.value, &exitCode);
                ok = exitCode != 0; // parte bassa dell'HMODULE restituito da LoadLibraryW
                if (!ok)
                {
                    error = L"LoadLibraryW ha restituito NULL (DLL non caricabile)";
                }
            }
        }
        VirtualFreeEx(process.value, remotePath, 0, MEM_RELEASE);
        return ok;
    }
}
