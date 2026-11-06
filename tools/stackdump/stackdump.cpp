// stackdump: stack dei thread di un processo con i simboli pubblici Microsoft, senza debugger installato.
// Compilazione (prompt x64 di Visual Studio): cl /nologo /EHsc /O2 /std:c++17 stackdump.cpp
// Accanto a stackdump.exe vanno copiati dbghelp.dll, symsrv.dll, dbgcore.dll e srcsrv.dll da
// "C:\Program Files (x86)\Windows Kits\10\Debuggers\x64" (quella di System32 non scarica i simboli).
// Uso: set STACKDUMP_SYMBOLS=srv*<cache>*https://msdl.microsoft.com/download/symbols (serve la rete), poi
//      stackdump <pid> [tid...]; con STACKDUMP_FRAMESCAN=1 cerca anche CExplorerFrame sullo stack.
// Solo su processi di prova: ogni thread viene sospeso per un istante.
// Stack dei thread di un processo (simboli pubblici Microsoft via symsrv): stackdump <pid> [tid...]
// Senza tid: tutti i thread. Ogni thread viene sospeso solo per leggerne il contesto.
#include <windows.h>
#include <tlhelp32.h>
#include <dbghelp.h>

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <vector>

#pragma comment(lib, "dbghelp.lib")

static void DumpThread(HANDLE process, DWORD tid, int maxFrames)
{
    HANDLE thread = OpenThread(THREAD_SUSPEND_RESUME | THREAD_GET_CONTEXT | THREAD_QUERY_INFORMATION, FALSE, tid);
    if (!thread)
    {
        printf("=== thread %lu: OpenThread %lu\n", tid, GetLastError());
        return;
    }
    if (SuspendThread(thread) == static_cast<DWORD>(-1))
    {
        printf("=== thread %lu: SuspendThread %lu\n", tid, GetLastError());
        CloseHandle(thread);
        return;
    }
    CONTEXT context{};
    context.ContextFlags = CONTEXT_FULL;
    std::vector<DWORD64> pcs;
    std::vector<DWORD64> rsis; // rsi nel frame (registro non volatile, ripristinato dallo srotolamento)
    DWORD64 stackPointer = 0;
    if (GetThreadContext(thread, &context))
    {
        stackPointer = context.Rsp;
        STACKFRAME64 frame{};
        frame.AddrPC.Offset = context.Rip;
        frame.AddrPC.Mode = AddrModeFlat;
        frame.AddrFrame.Offset = context.Rbp;
        frame.AddrFrame.Mode = AddrModeFlat;
        frame.AddrStack.Offset = context.Rsp;
        frame.AddrStack.Mode = AddrModeFlat;
        for (int i = 0; i < maxFrames; ++i)
        {
            if (!StackWalk64(IMAGE_FILE_MACHINE_AMD64, process, thread, &frame, &context, nullptr, SymFunctionTableAccess64, SymGetModuleBase64,
                             nullptr) ||
                !frame.AddrPC.Offset)
            {
                break;
            }
            pcs.push_back(frame.AddrPC.Offset);
            rsis.push_back(context.Rsi);
        }
    }
    ResumeThread(thread);
    CloseHandle(thread);

    // Ricerca di CExplorerFrame sullo stack: un valore p con [p+0x130] = oggetto la cui vtable è un "thread ref" della shell.
    if (stackPointer && getenv("STACKDUMP_FRAMESCAN"))
    {
        std::vector<DWORD64> words(0x4000);
        SIZE_T got = 0;
        ReadProcessMemory(process, reinterpret_cast<void*>(stackPointer), words.data(), words.size() * sizeof(DWORD64), &got);
        std::vector<DWORD64> seen;
        for (size_t i = 0; i < got / sizeof(DWORD64); ++i)
        {
            const DWORD64 candidate = words[i];
            if (candidate < 0x10000 || (candidate & 7) || std::find(seen.begin(), seen.end(), candidate) != seen.end())
            {
                continue;
            }
            seen.push_back(candidate);
            DWORD64 ref = 0, vtable = 0;
            LONG count = 0;
            SIZE_T r = 0;
            if (!ReadProcessMemory(process, reinterpret_cast<void*>(candidate + 0x130), &ref, sizeof(ref), &r) || !ref ||
                !ReadProcessMemory(process, reinterpret_cast<void*>(ref), &vtable, sizeof(vtable), &r) ||
                !ReadProcessMemory(process, reinterpret_cast<void*>(candidate + 0x128), &count, sizeof(count), &r))
            {
                continue;
            }
            alignas(SYMBOL_INFO) char buffer[sizeof(SYMBOL_INFO) + 512]{};
            auto* symbol = reinterpret_cast<SYMBOL_INFO*>(buffer);
            symbol->SizeOfStruct = sizeof(SYMBOL_INFO);
            symbol->MaxNameLen = 500;
            DWORD64 displacement = 0;
            if (SymFromAddr(process, vtable, &displacement, symbol) && strstr(symbol->Name, "Ref"))
            {
                DWORD64 frameVtable = 0;
                ReadProcessMemory(process, reinterpret_cast<void*>(candidate), &frameVtable, sizeof(frameVtable), &r);
                alignas(SYMBOL_INFO) char buffer2[sizeof(SYMBOL_INFO) + 512]{};
                auto* symbol2 = reinterpret_cast<SYMBOL_INFO*>(buffer2);
                symbol2->SizeOfStruct = sizeof(SYMBOL_INFO);
                symbol2->MaxNameLen = 500;
                DWORD64 d2 = 0;
                const bool named = SymFromAddr(process, frameVtable, &d2, symbol2) != FALSE;
                printf("  candidato this %llx (vtable %s): thread ref %ld, oggetto %llx (%s+0x%llx)\n", candidate, named ? symbol2->Name : "?",
                       count, ref, symbol->Name, displacement);
            }
        }
    }

    printf("=== thread %lu\n", tid);
    for (size_t index = 0; index < pcs.size(); ++index)
    {
        const DWORD64 pc = pcs[index];
        alignas(SYMBOL_INFO) char buffer[sizeof(SYMBOL_INFO) + 1024]{};
        auto* symbol = reinterpret_cast<SYMBOL_INFO*>(buffer);
        symbol->SizeOfStruct = sizeof(SYMBOL_INFO);
        symbol->MaxNameLen = 1000;
        IMAGEHLP_MODULE64 module{};
        module.SizeOfStruct = sizeof(module);
        const bool haveModule = SymGetModuleInfo64(process, pc, &module) != FALSE;
        DWORD64 displacement = 0;
        if (SymFromAddr(process, pc, &displacement, symbol))
        {
            printf("  %s!%s+0x%llx\n", haveModule ? module.ModuleName : "?", symbol->Name, displacement);
            // Il ciclo della finestra di Esplora file: this (rsi) e il contatore dei thread ref (+0x128)
            if (strstr(symbol->Name, "FrameMessagePump"))
            {
                const DWORD64 self = index + 1 < rsis.size() ? rsis[index] : 0;
                LONG count = 0;
                void* ref = nullptr;
                SIZE_T read = 0;
                ReadProcessMemory(process, reinterpret_cast<void*>(self + 0x128), &count, sizeof(count), &read);
                ReadProcessMemory(process, reinterpret_cast<void*>(self + 0x130), &ref, sizeof(ref), &read);
                void* vtable = nullptr;
                ReadProcessMemory(process, ref, &vtable, sizeof(vtable), &read);
                DWORD64 vtDisplacement = 0;
                alignas(SYMBOL_INFO) char vtBuffer[sizeof(SYMBOL_INFO) + 512]{};
                auto* vtSymbol = reinterpret_cast<SYMBOL_INFO*>(vtBuffer);
                vtSymbol->SizeOfStruct = sizeof(SYMBOL_INFO);
                vtSymbol->MaxNameLen = 500;
                const bool named = SymFromAddr(process, reinterpret_cast<DWORD64>(vtable), &vtDisplacement, vtSymbol) != FALSE;
                printf("      this %llx, thread ref %ld, oggetto %p (vtable %s)\n", self, count, ref, named ? vtSymbol->Name : "?");
            }
        }
        else
        {
            printf("  %s+0x%llx\n", haveModule ? module.ModuleName : "?", haveModule ? pc - module.BaseOfImage : pc);
        }
    }
}

int wmain(int argc, wchar_t** argv)
{
    if (argc < 2)
    {
        printf("uso: stackdump <pid> [tid...]\n");
        return 1;
    }
    const DWORD pid = wcstoul(argv[1], nullptr, 10);
    HANDLE process = OpenProcess(PROCESS_QUERY_INFORMATION | PROCESS_VM_READ, FALSE, pid);
    if (!process)
    {
        printf("OpenProcess %lu\n", GetLastError());
        return 1;
    }
    wchar_t symbols[MAX_PATH * 2]{};
    if (!GetEnvironmentVariableW(L"STACKDUMP_SYMBOLS", symbols, ARRAYSIZE(symbols)))
    {
        wcscpy_s(symbols, L"srv*C:\\symbols*https://msdl.microsoft.com/download/symbols");
    }
    SymSetOptions(SYMOPT_UNDNAME | SYMOPT_DEFERRED_LOADS | SYMOPT_FAIL_CRITICAL_ERRORS | SYMOPT_NO_PROMPTS);
    if (!SymInitializeW(process, symbols, TRUE))
    {
        printf("SymInitialize %lu\n", GetLastError());
        return 1;
    }
    std::vector<DWORD> threads;
    for (int i = 2; i < argc; ++i)
    {
        threads.push_back(wcstoul(argv[i], nullptr, 10));
    }
    if (threads.empty())
    {
        const HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD, 0);
        THREADENTRY32 entry{ sizeof(entry) };
        for (BOOL ok = Thread32First(snapshot, &entry); ok; ok = Thread32Next(snapshot, &entry))
        {
            if (entry.th32OwnerProcessID == pid)
            {
                threads.push_back(entry.th32ThreadID);
            }
        }
        CloseHandle(snapshot);
    }
    for (DWORD tid : threads)
    {
        DumpThread(process, tid, 48);
    }
    SymCleanup(process);
    CloseHandle(process);
    return 0;
}
