#include "pch.h"
#include "TextInput.h"

#include "ModLog.h"

extern HMODULE g_module;

namespace xpx::mod
{
    namespace
    {
        // Per thread (ogni finestra di Esplora file ha il suo): l'hook, quante caselle lo vogliono, la finestra.
        thread_local HHOOK t_hook = nullptr;
        thread_local int t_users = 0;
        thread_local HWND t_main = nullptr;

        // Il tasto va alla finestra col fuoco, che è d'ingresso di un'isola XAML della nostra finestra principale.
        bool IsOurs(HWND target)
        {
            if (!target || target != GetFocus() || !t_main)
            {
                return false;
            }
            wchar_t name[64]{};
            if (!GetClassNameW(target, name, ARRAYSIZE(name)) || wcscmp(name, L"InputSiteWindowClass") != 0)
            {
                return false;
            }
            const HWND root = GetAncestor(target, GA_ROOTOWNER);
            return root == t_main;
        }

        LRESULT CALLBACK MessageHook(int code, WPARAM wParam, LPARAM lParam)
        {
            if (code == HC_ACTION && wParam == PM_REMOVE && lParam)
            {
                MSG* message = reinterpret_cast<MSG*>(lParam);
                const UINT kind = message->message;
                if ((kind == WM_KEYDOWN || kind == WM_KEYUP || kind == WM_CHAR || kind == WM_DEADCHAR) && IsOurs(message->hwnd))
                {
                    // Consegnato qui (WM_CHAR compreso, che TranslateMessage mette in coda e ripassa da qui), e al ciclo
                    // di Esplora file un messaggio vuoto: niente scorciatoie.
                    MSG copy = *message;
                    TranslateMessage(&copy);
                    DispatchMessageW(&copy);
                    message->message = WM_NULL;
                    message->wParam = 0;
                    message->lParam = 0;
                }
            }
            return CallNextHookEx(nullptr, code, wParam, lParam);
        }
    }

    void TextInput::Begin(HWND main)
    {
        if (t_users++ > 0)
        {
            return;
        }
        t_main = main;
        // hMod NULL: thread di questo processo (con il modulo, user32 ne terrebbe un riferimento in più: vedi Discovery.cpp).
        t_hook = SetWindowsHookExW(WH_GETMESSAGE, MessageHook, nullptr, GetCurrentThreadId());
        if (!t_hook)
        {
            XPX_MLOG(L"TextInput: hook dei tasti non installato (%lu): Backspace e simili restano a Esplora file.", GetLastError());
        }
    }

    void TextInput::End(HWND main)
    {
        if (t_users == 0 || main != t_main)
        {
            return;
        }
        if (--t_users > 0)
        {
            return;
        }
        if (t_hook)
        {
            UnhookWindowsHookEx(t_hook);
            t_hook = nullptr;
        }
        t_main = nullptr;
    }

    void TextInput::Reset()
    {
        if (t_hook)
        {
            UnhookWindowsHookEx(t_hook);
            t_hook = nullptr;
        }
        t_users = 0;
        t_main = nullptr;
    }
}
