#include "pch.h"
#include "DevDiagnostics.h"

#include "ModLog.h"

namespace xpx::mod
{
    namespace
    {
        constexpr DWORD kCppException = 0xE06D7363;
        std::atomic<int> g_logged{ 0 };
        thread_local bool t_inside = false;

        // Nome (decorato) del tipo di un'eccezione C++ dal suo record; vuoto se non si legge. Senza oggetti C++
        // (serve __try).
        void CppTypeName(EXCEPTION_RECORD const* record, char* out, size_t size)
        {
            out[0] = '\0';
            __try
            {
                if (record->NumberParameters < 4 || record->ExceptionInformation[0] != 0x19930520)
                {
                    return;
                }
                const auto base = static_cast<uintptr_t>(record->ExceptionInformation[3]);
                const auto* throwInfo = reinterpret_cast<const int*>(record->ExceptionInformation[2]);
                if (!base || !throwInfo)
                {
                    return;
                }
                const auto* types = reinterpret_cast<const int*>(base + static_cast<uintptr_t>(throwInfo[3]));
                if (types[0] <= 0)
                {
                    return;
                }
                const auto* catchable = reinterpret_cast<const int*>(base + static_cast<uintptr_t>(types[1]));
                const char* name = reinterpret_cast<const char*>(base + static_cast<uintptr_t>(catchable[1])) + 2 * sizeof(void*);
                strncpy_s(out, size, name, _TRUNCATE);
            }
            __except (EXCEPTION_EXECUTE_HANDLER)
            {
                out[0] = '\0';
            }
        }

        std::wstring Frame(void* address)
        {
            HMODULE module = nullptr;
            if (!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT, static_cast<LPCWSTR>(address),
                                    &module))
            {
                wchar_t text[32];
                swprintf_s(text, L"%p", address);
                return text;
            }
            wchar_t path[MAX_PATH]{};
            GetModuleFileNameW(module, path, ARRAYSIZE(path));
            const wchar_t* name = wcsrchr(path, L'\\');
            wchar_t text[MAX_PATH + 32];
            swprintf_s(text, L"%s+0x%llX", name ? name + 1 : path,
                       static_cast<unsigned long long>(reinterpret_cast<uintptr_t>(address) - reinterpret_cast<uintptr_t>(module)));
            return text;
        }

        LONG CALLBACK Handler(EXCEPTION_POINTERS* info)
        {
            const DWORD code = info->ExceptionRecord->ExceptionCode;
            if (code != kCppException && code != EXCEPTION_ACCESS_VIOLATION && code != STATUS_STACK_BUFFER_OVERRUN && code != STATUS_HEAP_CORRUPTION)
            {
                return EXCEPTION_CONTINUE_SEARCH;
            }
            if (t_inside || g_logged.fetch_add(1) >= 400)
            {
                return EXCEPTION_CONTINUE_SEARCH;
            }
            t_inside = true;
            char type[160];
            CppTypeName(info->ExceptionRecord, type, sizeof(type));
            void* frames[48]{};
            const USHORT count = RtlCaptureStackBackTrace(0, ARRAYSIZE(frames), frames, nullptr);
            std::wstring stack;
            for (USHORT i = 0; i < count; ++i)
            {
                stack += (i ? L" < " : L"") + Frame(frames[i]);
            }
            XPX_MLOG(L"DIAGNOSI eccezione 0x%08X %S in %s (thread %lu): %s", code, type, Frame(info->ExceptionRecord->ExceptionAddress).c_str(),
                     GetCurrentThreadId(), stack.c_str());
            t_inside = false;
            return EXCEPTION_CONTINUE_SEARCH;
        }
    }

    void InstallDevExceptionLog(bool shellProcess)
    {
        // Mai nel processo della barra delle applicazioni; solo se lo chiede una prova.
        if (shellProcess || GetFileAttributesW((DataDirectory() + L"\\dev-exceptions.txt").c_str()) == INVALID_FILE_ATTRIBUTES)
        {
            return;
        }
        AddVectoredExceptionHandler(1, Handler);
        XPX_MLOG(L"DIAGNOSI: eccezioni C++ e gravi registrate con lo stack (dev-exceptions.txt).");
    }
}
