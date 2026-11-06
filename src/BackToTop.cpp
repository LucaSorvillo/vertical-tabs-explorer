#include "BackToTop.h"

#include "Common.h"

#include <commctrl.h>
#include <objidl.h>
#include <gdiplus.h>
#include <uxtheme.h>
#include <windowsx.h>

#include <algorithm>
#include <cmath>
#include <string>

using Microsoft::WRL::ComPtr;

namespace xpx
{
    namespace
    {
        constexpr int kSizeDip = 40;          // diametro del pulsante
        constexpr int kSideMarginDip = 12;    // distanza dalla barra di scorrimento verticale
        constexpr int kBottomMarginDip = 16;  // distanza dal bordo inferiore della vista
        constexpr UINT_PTR kTimerWatch = 1;   // controllo locale mentre una finestra di Esplora file è in primo piano
        constexpr UINT_PTR kTimerFade = 2;
        constexpr UINT_PTR kTimerScroll = 3;
        constexpr UINT_PTR kTimerPending = 4; // posizione da rileggere dopo una raffica di scorrimenti
        constexpr UINT kWatchMs = 200;
        constexpr UINT kRestoreWatchMs = 3000; // controllo locale dopo il ripristino da icona di una finestra di Esplora file
        constexpr UINT kFadeStepMs = 15;
        constexpr int kFadeStep = 255 * static_cast<int>(kFadeStepMs) / 150; // dissolvenza in circa 150 ms
        constexpr UINT kScrollStepMs = 15;
        constexpr double kScrollMs = 260.0;
        constexpr UINT kPendingMs = 40;
        constexpr UINT kRefreshEveryTicks = 2; // da visibile si rilegge la posizione ogni 400 ms (cambi di cartella)
        constexpr DWORD kEventContentScrolled = 0x8015; // EVENT_OBJECT_CONTENTSCROLLED
        constexpr UINT WM_APP_REFRESH = WM_APP + 1;
        constexpr wchar_t kGlyphUp[] = L"\xE74A"; // freccia in su (Segoe Fluent Icons)

        BackToTop* g_instance = nullptr; // gli hook e la procedura di finestra non hanno un parametro di contesto

        std::wstring ClassOf(HWND hwnd)
        {
            wchar_t name[64]{};
            GetClassNameW(hwnd, name, ARRAYSIZE(name));
            return name;
        }

        // Scheda attiva: la prima ShellTabWindowClass nell'ordine Z dei figli della finestra.
        HWND ActiveTab(HWND window)
        {
            for (HWND child = GetWindow(window, GW_CHILD); child; child = GetWindow(child, GW_HWNDNEXT))
            {
                if (ClassOf(child) == L"ShellTabWindowClass")
                {
                    return child;
                }
            }
            return nullptr;
        }

        HWND FindDescendant(HWND root, wchar_t const* className)
        {
            struct Context
            {
                wchar_t const* className;
                HWND found;
            } context{ className, nullptr };
            EnumChildWindows(
                root,
                [](HWND hwnd, LPARAM param) -> BOOL {
                    auto& ctx = *reinterpret_cast<Context*>(param);
                    if (ClassOf(hwnd) == ctx.className)
                    {
                        ctx.found = hwnd;
                        return FALSE;
                    }
                    return TRUE;
                },
                reinterpret_cast<LPARAM>(&context));
            return context.found;
        }

        bool IsLightTheme()
        {
            DWORD value = 1;
            DWORD size = sizeof(value);
            RegGetValueW(HKEY_CURRENT_USER, L"Software\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize", L"AppsUseLightTheme",
                         RRF_RT_REG_DWORD, nullptr, &value, &size);
            return value != 0;
        }

        int Scale(int dip, UINT dpi)
        {
            return MulDiv(dip, static_cast<int>(dpi), USER_DEFAULT_SCREEN_DPI);
        }

        // Colori del pulsante: gli stessi del pulsante "Torna su" di VTabsNotepad (palette Fluent, pieni).
        struct Palette
        {
            Gdiplus::Color fill, hover, pressed, stroke, glyph;
        };
        Palette PaletteFor(bool light)
        {
            if (light)
            {
                return { Gdiplus::Color(255, 0xFF, 0xFF, 0xFF), Gdiplus::Color(255, 0xF5, 0xF5, 0xF5), Gdiplus::Color(255, 0xEB, 0xEB, 0xEB),
                         Gdiplus::Color(255, 0xD6, 0xD6, 0xD6), Gdiplus::Color(255, 0x1A, 0x1A, 0x1A) };
            }
            return { Gdiplus::Color(255, 0x3C, 0x3C, 0x3C), Gdiplus::Color(255, 0x47, 0x47, 0x47), Gdiplus::Color(255, 0x33, 0x33, 0x33),
                     Gdiplus::Color(255, 0x55, 0x55, 0x55), Gdiplus::Color(255, 0xFF, 0xFF, 0xFF) };
        }
    }

    BackToTop::~BackToTop()
    {
        Stop();
    }

    void BackToTop::Start(std::function<bool(HWND)> accepts)
    {
        m_accepts = std::move(accepts);
        g_instance = this;
        const HANDLE ready = CreateEventW(nullptr, TRUE, FALSE, nullptr);
        m_thread = std::thread([this, ready] { Run(ready); });
        WaitForSingleObject(ready, 5000);
        CloseHandle(ready);
    }

    void BackToTop::Stop()
    {
        if (m_thread.joinable())
        {
            PostThreadMessageW(m_threadId, WM_QUIT, 0, 0);
            m_thread.join();
        }
        if (g_instance == this)
        {
            g_instance = nullptr;
        }
    }

    void BackToTop::SetEnabled(bool enabled)
    {
        if (m_enabled.exchange(enabled) != enabled && m_hwnd)
        {
            PostMessageW(m_hwnd, WM_APP_REFRESH, 0, 0);
        }
    }

    void BackToTop::Run(HANDLE ready)
    {
        const HRESULT init = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);
        MSG message;
        PeekMessageW(&message, nullptr, WM_USER, WM_USER, PM_NOREMOVE); // crea la coda dei messaggi
        m_threadId = GetCurrentThreadId();

        Gdiplus::GdiplusStartupInput gdiplus;
        Gdiplus::GdiplusStartup(&m_gdiplusToken, &gdiplus, nullptr);
        CoCreateInstance(__uuidof(CUIAutomation), nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&m_automation));

        const HINSTANCE instance = GetModuleHandleW(nullptr);
        WNDCLASSEXW windowClass{ sizeof(windowClass) };
        windowClass.lpfnWndProc = WindowProc;
        windowClass.hInstance = instance;
        windowClass.lpszClassName = kWindowClass;
        windowClass.hCursor = LoadCursorW(nullptr, IDC_ARROW);
        RegisterClassExW(&windowClass);
        // Sempre sopra, mai attiva (il clic lascia il focus a Esplora file), fuori da barra delle
        // applicazioni e Alt+Tab; trasparente intorno al cerchio.
        m_hwnd = CreateWindowExW(WS_EX_LAYERED | WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE | WS_EX_TOPMOST, kWindowClass, L"Torna su", WS_POPUP, 0, 0, 1,
                                 1, nullptr, nullptr, instance, nullptr);

        INITCOMMONCONTROLSEX controls{ sizeof(controls), ICC_WIN95_CLASSES };
        InitCommonControlsEx(&controls);
        m_tooltip = CreateWindowExW(WS_EX_TOPMOST, TOOLTIPS_CLASSW, nullptr, WS_POPUP | TTS_NOPREFIX | TTS_ALWAYSTIP, CW_USEDEFAULT, CW_USEDEFAULT,
                                    CW_USEDEFAULT, CW_USEDEFAULT, m_hwnd, nullptr, instance, nullptr);
        if (m_tooltip)
        {
            TTTOOLINFOW tool{ sizeof(tool) };
            tool.uFlags = TTF_SUBCLASS | TTF_IDISHWND;
            tool.hwnd = m_hwnd;
            tool.uId = reinterpret_cast<UINT_PTR>(m_hwnd);
            tool.lpszText = const_cast<LPWSTR>(L"Torna su");
            SendMessageW(m_tooltip, TTM_ADDTOOLW, 0, reinterpret_cast<LPARAM>(&tool));
            if (!IsLightTheme())
            {
                SetWindowTheme(m_tooltip, L"DarkMode_Explorer", nullptr); // descrizione scura come il tema
            }
        }

        // Eventi di sistema poco frequenti (cambi di primo piano, spostamenti, riduzioni a icona) di tutti i
        // processi: servono a seguire la finestra in primo piano.
        const DWORD flags = WINEVENT_OUTOFCONTEXT | WINEVENT_SKIPOWNPROCESS;
        m_hookForeground = SetWinEventHook(EVENT_SYSTEM_FOREGROUND, EVENT_SYSTEM_FOREGROUND, nullptr, OnEvent, 0, 0, flags);
        m_hookMoveSize = SetWinEventHook(EVENT_SYSTEM_MOVESIZESTART, EVENT_SYSTEM_MOVESIZEEND, nullptr, OnEvent, 0, 0, flags);
        m_hookMinimize = SetWinEventHook(EVENT_SYSTEM_MINIMIZESTART, EVENT_SYSTEM_MINIMIZEEND, nullptr, OnEvent, 0, 0, flags);
        SetEvent(ready);
        OnForeground(GetForegroundWindow());

        while (GetMessageW(&message, nullptr, 0, 0) > 0)
        {
            TranslateMessage(&message);
            DispatchMessageW(&message);
        }

        for (HWINEVENTHOOK* hook : { &m_hookForeground, &m_hookMoveSize, &m_hookMinimize, &m_hookScroll })
        {
            if (*hook)
            {
                UnhookWinEvent(*hook);
                *hook = nullptr;
            }
        }
        if (m_tooltip)
        {
            DestroyWindow(m_tooltip);
            m_tooltip = nullptr;
        }
        if (m_hwnd)
        {
            DestroyWindow(m_hwnd);
            m_hwnd = nullptr;
        }
        m_scroll.Reset();
        m_automation.Reset();
        Gdiplus::GdiplusShutdown(m_gdiplusToken);
        if (SUCCEEDED(init))
        {
            CoUninitialize();
        }
    }

    void CALLBACK BackToTop::OnEvent(HWINEVENTHOOK, DWORD event, HWND hwnd, LONG object, LONG, DWORD, DWORD)
    {
        BackToTop* self = g_instance;
        if (!self || !hwnd)
        {
            return;
        }
        switch (event)
        {
        case EVENT_SYSTEM_FOREGROUND:
        {
            // Vale la finestra principale: Esplora file segnala il primo piano anche sulla scheda attiva
            // (ShellTabWindowClass, una finestra figlia).
            const HWND root = GetAncestor(hwnd, GA_ROOT);
            self->OnForeground(root ? root : GetForegroundWindow());
            break;
        }
        case EVENT_SYSTEM_MOVESIZESTART:
            if (hwnd == self->m_window)
            {
                self->m_moving = true;
                self->Hide(true);
            }
            break;
        case EVENT_SYSTEM_MOVESIZEEND:
            if (hwnd == self->m_window)
            {
                self->m_moving = false;
                self->Refresh();
            }
            break;
        case EVENT_SYSTEM_MINIMIZESTART:
            if (hwnd == self->m_window)
            {
                self->Hide(true);
            }
            break;
        case EVENT_SYSTEM_MINIMIZEEND:
            if (hwnd == self->m_window)
            {
                self->Refresh();
            }
            else if (ClassOf(hwnd) == L"CabinetWClass")
            {
                // Finestra di Esplora file ripristinata da icona mentre era in primo piano un'altra finestra:
                // il primo piano può arrivarle poco dopo senza un evento utile, lo si ricontrolla per un po'.
                XPX_VLOG(L"Torna su: finestra di Esplora file %p ripristinata da icona.", hwnd);
                self->WatchFor(kRestoreWatchMs);
            }
            break;
        case kEventContentScrolled:
            // La vista dei file della finestra seguita è scorsa: si rilegge la posizione a raffica finita.
            if (object != OBJID_CURSOR && hwnd == self->m_view && self->m_hwnd)
            {
                SetTimer(self->m_hwnd, kTimerPending, kPendingMs, nullptr);
            }
            break;
        default:
            break;
        }
    }

    void BackToTop::HookProcess(DWORD process)
    {
        if (!process || process == m_scrollProcess)
        {
            return;
        }
        if (m_hookScroll)
        {
            UnhookWinEvent(m_hookScroll);
        }
        // Solo gli scorrimenti del processo della finestra seguita (a riposo non ne emette nessuno).
        m_hookScroll = SetWinEventHook(kEventContentScrolled, kEventContentScrolled, nullptr, OnEvent, process, 0, WINEVENT_OUTOFCONTEXT);
        m_scrollProcess = m_hookScroll ? process : 0;
    }

    void BackToTop::OnForeground(HWND window)
    {
        const bool frame = window && IsWindow(window) && ClassOf(window) == L"CabinetWClass";
        const bool accepted = frame && (!m_accepts || m_accepts(window));
        if (window != m_lastForegroundSeen)
        {
            m_lastForegroundSeen = window;
            XPX_VLOG(L"Torna su: in primo piano %p (%s%s).", window, window ? ClassOf(window).c_str() : L"nessuna",
                     frame && !accepted ? L", non considerata" : L"");
        }
        if (!accepted)
        {
            if (m_window)
            {
                m_window = nullptr;
                m_view = nullptr;
                m_scroll.Reset();
            }
            Hide(false);
            // Una finestra di Esplora file non ancora considerata (in modalità prova la tray la legge poco
            // dopo l'apertura) si ricontrolla con il timer, come per qualche istante dopo un ripristino da
            // icona (vedi WatchFor); con altre finestre in primo piano il timer si ferma.
            if (frame || GetTickCount64() < m_watchUntil)
            {
                SetTimer(m_hwnd, kTimerWatch, kWatchMs, nullptr);
            }
            else
            {
                KillTimer(m_hwnd, kTimerWatch);
            }
            return;
        }
        if (window != m_window)
        {
            XPX_VLOG(L"Torna su: segue la finestra di Esplora file %p.", window);
            m_window = window;
            m_view = nullptr;
            m_scroll.Reset();
            m_moving = false;
        }
        DWORD process = 0;
        GetWindowThreadProcessId(window, &process);
        HookProcess(process);
        SetTimer(m_hwnd, kTimerWatch, kWatchMs, nullptr);
        ResolveView();
        Refresh();
    }

    void BackToTop::WatchFor(UINT milliseconds)
    {
        m_watchUntil = GetTickCount64() + milliseconds;
        SetTimer(m_hwnd, kTimerWatch, kWatchMs, nullptr);
    }

    void BackToTop::ResolveView()
    {
        HWND view = nullptr;
        if (m_window && IsWindow(m_window))
        {
            if (const HWND tab = ActiveTab(m_window))
            {
                if (const HWND defView = FindDescendant(tab, L"SHELLDLL_DefView"))
                {
                    view = FindWindowExW(defView, nullptr, L"DirectUIHWND", nullptr);
                }
            }
        }
        if (view == m_view && (m_scroll || !view))
        {
            return;
        }
        m_view = view;
        m_scroll.Reset();
        m_vbarWidth = 0;
        m_hbarHeight = 0;
        SetRectEmpty(&m_viewRect);
        ComPtr<IUIAutomationElement> element;
        if (view && m_automation && SUCCEEDED(m_automation->ElementFromHandle(view, &element)) && element)
        {
            // La vista dei file (lista UIItemsView) è l'elemento radice della DirectUIHWND.
            element->GetCurrentPatternAs(UIA_ScrollPatternId, IID_PPV_ARGS(&m_scroll));
        }
        XPX_VLOG(L"Torna su: vista dei file %p%s.", view, m_scroll ? L"" : L" (senza ScrollPattern)");
    }

    void BackToTop::Refresh()
    {
        bool wanted = false;
        wchar_t const* reason = L"";
        double percent = -1;
        if (!m_enabled)
        {
            reason = L"spento";
        }
        else if (!m_window || !IsWindow(m_window))
        {
            reason = L"nessuna finestra seguita";
        }
        else if (m_moving || IsIconic(m_window))
        {
            reason = m_moving ? L"finestra in movimento" : L"finestra ridotta a icona";
        }
        else if (GetForegroundWindow() != m_window)
        {
            reason = L"finestra non in primo piano";
        }
        else if (!m_scroll)
        {
            reason = L"vista senza ScrollPattern";
        }
        else
        {
            BOOL scrollable = FALSE;
            wanted = SUCCEEDED(m_scroll->get_CurrentVerticallyScrollable(&scrollable)) && scrollable &&
                     SUCCEEDED(m_scroll->get_CurrentVerticalScrollPercent(&percent)) && percent > 0.0;
            reason = wanted ? L"vista scorsa" : L"vista in cima o non scorrevole";
        }
        if (wanted != m_wanted)
        {
            XPX_VLOG(L"Torna su: %s (%.1f%%).", reason, percent);
        }
        wanted ? Show() : Hide(false);
    }

    bool BackToTop::Place()
    {
        RECT view{};
        if (!m_view || !m_window || !GetWindowRect(m_view, &view) || IsRectEmpty(&view))
        {
            return false;
        }
        m_dpi = GetDpiForWindow(m_window);
        if (m_dpi == 0)
        {
            m_dpi = USER_DEFAULT_SCREEN_DPI;
        }
        // Barre di scorrimento della vista (elementi della lista): si rimisurano quando la vista cambia forma.
        if (!EqualRect(&view, &m_viewRect))
        {
            m_viewRect = view;
            m_vbarWidth = 0;
            m_hbarHeight = 0;
            ComPtr<IUIAutomationElement> element;
            if (m_automation && SUCCEEDED(m_automation->ElementFromHandle(m_view, &element)) && element)
            {
                for (const bool vertical : { true, false })
                {
                    VARIANT name{};
                    name.vt = VT_BSTR;
                    name.bstrVal = SysAllocString(vertical ? L"LineVScrollBar" : L"LineHScrollBar");
                    ComPtr<IUIAutomationCondition> condition;
                    ComPtr<IUIAutomationElement> bar;
                    RECT r{};
                    if (SUCCEEDED(m_automation->CreatePropertyCondition(UIA_ClassNamePropertyId, name, &condition)) &&
                        SUCCEEDED(element->FindFirst(TreeScope_Children, condition.Get(), &bar)) && bar &&
                        SUCCEEDED(bar->get_CurrentBoundingRectangle(&r)) && !IsRectEmpty(&r))
                    {
                        (vertical ? m_vbarWidth : m_hbarHeight) = vertical ? r.right - r.left : r.bottom - r.top;
                    }
                    VariantClear(&name);
                }
            }
        }
        const int size = Scale(kSizeDip, m_dpi);
        const int right = view.right - m_vbarWidth - Scale(kSideMarginDip, m_dpi);
        const int bottom = view.bottom - m_hbarHeight - Scale(kBottomMarginDip, m_dpi);
        if (right - size < view.left || bottom - size < view.top)
        {
            return false; // vista troppo piccola
        }
        if (size != m_size)
        {
            m_size = size;
            Render();
        }
        RECT current{};
        GetWindowRect(m_hwnd, &current);
        if (current.left != right - size || current.top != bottom - size || current.right - current.left != size)
        {
            SetWindowPos(m_hwnd, HWND_TOPMOST, right - size, bottom - size, size, size, SWP_NOACTIVATE | SWP_NOOWNERZORDER);
        }
        return true;
    }

    void BackToTop::Show()
    {
        if (!Place())
        {
            Hide(false);
            return;
        }
        if (!m_wanted)
        {
            XPX_VLOG(L"Torna su: pulsante visibile.");
        }
        m_wanted = true;
        if (!IsWindowVisible(m_hwnd))
        {
            Render();
            ShowWindow(m_hwnd, SW_SHOWNOACTIVATE);
        }
        if (m_alpha < 255)
        {
            SetTimer(m_hwnd, kTimerFade, kFadeStepMs, nullptr);
        }
    }

    void BackToTop::Hide(bool immediately)
    {
        if (m_wanted)
        {
            XPX_VLOG(L"Torna su: pulsante nascosto.");
        }
        m_wanted = false;
        if (m_tooltip)
        {
            SendMessageW(m_tooltip, TTM_POP, 0, 0);
        }
        if (immediately || m_alpha == 0)
        {
            KillTimer(m_hwnd, kTimerFade);
            m_alpha = 0;
            m_hover = false;
            m_pressed = false;
            if (IsWindowVisible(m_hwnd))
            {
                ShowWindow(m_hwnd, SW_HIDE);
            }
            return;
        }
        SetTimer(m_hwnd, kTimerFade, kFadeStepMs, nullptr);
    }

    void BackToTop::Render()
    {
        if (!m_hwnd || m_size <= 0)
        {
            return;
        }
        const int size = m_size;
        BITMAPINFO info{};
        info.bmiHeader.biSize = sizeof(info.bmiHeader);
        info.bmiHeader.biWidth = size;
        info.bmiHeader.biHeight = -size; // dall'alto in basso
        info.bmiHeader.biPlanes = 1;
        info.bmiHeader.biBitCount = 32;
        info.bmiHeader.biCompression = BI_RGB;
        void* bits = nullptr;
        const HDC screen = GetDC(nullptr);
        const HDC memory = CreateCompatibleDC(screen);
        const HBITMAP dib = CreateDIBSection(screen, &info, DIB_RGB_COLORS, &bits, nullptr, 0);
        if (!dib || !bits)
        {
            DeleteDC(memory);
            ReleaseDC(nullptr, screen);
            return;
        }
        const HGDIOBJ previous = SelectObject(memory, dib);
        {
            // Cerchio con il bordo e la freccia, con l'antialiasing (pixel premoltiplicati per UpdateLayeredWindow)
            Gdiplus::Bitmap canvas(size, size, size * 4, PixelFormat32bppPARGB, static_cast<BYTE*>(bits));
            Gdiplus::Graphics graphics(&canvas);
            graphics.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
            graphics.SetPixelOffsetMode(Gdiplus::PixelOffsetModeHalf);
            graphics.SetTextRenderingHint(Gdiplus::TextRenderingHintAntiAliasGridFit);
            graphics.Clear(Gdiplus::Color(0, 0, 0, 0));
            const Palette palette = PaletteFor(IsLightTheme());
            const float edge = static_cast<float>(size) - 1.0f;
            Gdiplus::SolidBrush fill(m_pressed ? palette.pressed : m_hover ? palette.hover : palette.fill);
            graphics.FillEllipse(&fill, 0.5f, 0.5f, edge, edge);
            Gdiplus::Pen stroke(palette.stroke, 1.0f);
            graphics.DrawEllipse(&stroke, 0.5f, 0.5f, edge, edge);

            Gdiplus::FontFamily fluent(L"Segoe Fluent Icons");
            Gdiplus::FontFamily mdl2(L"Segoe MDL2 Assets");
            Gdiplus::FontFamily const& family = fluent.GetLastStatus() == Gdiplus::Ok ? fluent : mdl2;
            Gdiplus::Font font(&family, static_cast<float>(Scale(16, m_dpi)), Gdiplus::FontStyleRegular, Gdiplus::UnitPixel);
            Gdiplus::StringFormat format(Gdiplus::StringFormat::GenericTypographic());
            format.SetAlignment(Gdiplus::StringAlignmentCenter);
            format.SetLineAlignment(Gdiplus::StringAlignmentCenter);
            Gdiplus::SolidBrush glyph(palette.glyph);
            graphics.DrawString(kGlyphUp, 1, &font, Gdiplus::RectF(0.0f, 0.0f, static_cast<float>(size), static_cast<float>(size)), &format, &glyph);
        }
        POINT source{ 0, 0 };
        SIZE extent{ size, size };
        BLENDFUNCTION blend{ AC_SRC_OVER, 0, static_cast<BYTE>(m_alpha), AC_SRC_ALPHA };
        UpdateLayeredWindow(m_hwnd, screen, nullptr, &extent, memory, &source, 0, &blend, ULW_ALPHA);
        SelectObject(memory, previous);
        DeleteObject(dib);
        DeleteDC(memory);
        ReleaseDC(nullptr, screen);
    }

    void BackToTop::ScrollToTop()
    {
        double percent = 0;
        if (!m_scroll || FAILED(m_scroll->get_CurrentVerticalScrollPercent(&percent)) || percent <= 0.0)
        {
            return;
        }
        XPX_VLOG(L"Torna su: dal %.1f%% della vista.", percent);
        m_scrollFrom = percent;
        m_scrollStart = GetTickCount64();
        SetTimer(m_hwnd, kTimerScroll, kScrollStepMs, nullptr);
        ScrollStep();
    }

    void BackToTop::ScrollStep()
    {
        // Animazione a piccoli salti, rallentando verso la fine (ogni salto è uno scorrimento della vista).
        const double t = std::min(1.0, static_cast<double>(GetTickCount64() - m_scrollStart) / kScrollMs);
        const double eased = 1.0 - std::pow(1.0 - t, 3.0);
        const double percent = t >= 1.0 ? 0.0 : m_scrollFrom * (1.0 - eased);
        if (!m_scroll || FAILED(m_scroll->SetScrollPercent(UIA_ScrollPatternNoScroll, percent)) || t >= 1.0)
        {
            KillTimer(m_hwnd, kTimerScroll);
        }
    }

    LRESULT CALLBACK BackToTop::WindowProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam)
    {
        BackToTop* self = g_instance;
        if (self && self->m_hwnd == hwnd)
        {
            return self->Handle(message, wParam, lParam);
        }
        return DefWindowProcW(hwnd, message, wParam, lParam);
    }

    LRESULT BackToTop::Handle(UINT message, WPARAM wParam, LPARAM lParam)
    {
        switch (message)
        {
        case WM_MOUSEACTIVATE:
            return MA_NOACTIVATE; // il focus resta a Esplora file

        case WM_MOUSEMOVE:
            if (!m_hover)
            {
                m_hover = true;
                TRACKMOUSEEVENT track{ sizeof(track), TME_LEAVE, m_hwnd, 0 };
                TrackMouseEvent(&track);
                Render();
            }
            return 0;

        case WM_MOUSELEAVE:
            m_hover = false;
            Render();
            return 0;

        case WM_LBUTTONDOWN:
            m_pressed = true;
            SetCapture(m_hwnd);
            Render();
            return 0;

        case WM_LBUTTONUP:
            if (m_pressed)
            {
                m_pressed = false;
                ReleaseCapture();
                // Vale il rilascio dentro il cerchio (come un pulsante: trascinando fuori si annulla).
                const double radius = m_size / 2.0;
                const double dx = GET_X_LPARAM(lParam) - radius;
                const double dy = GET_Y_LPARAM(lParam) - radius;
                Render();
                if (dx * dx + dy * dy <= radius * radius)
                {
                    ScrollToTop();
                }
            }
            return 0;

        case WM_CAPTURECHANGED:
            if (m_pressed)
            {
                m_pressed = false;
                Render();
            }
            return 0;

        case WM_TIMER:
            switch (wParam)
            {
            case kTimerWatch:
                // Finestra seguita: scheda o cartella cambiate, posizione da seguire mentre il pulsante è visibile.
                // Senza finestra seguita: quella in primo piano è di Esplora file ma non ancora considerata, o una
                // finestra di Esplora file è appena stata ripristinata da icona (vedi WatchFor).
                if (!m_window || !IsWindow(m_window))
                {
                    OnForeground(GetForegroundWindow());
                    break;
                }
                {
                    // Primo piano passato a un'altra finestra senza un evento utile: si segue quella.
                    const HWND current = GetForegroundWindow();
                    if (current && current != m_window)
                    {
                        OnForeground(current);
                        break;
                    }
                    // Ripristino da icona o ritorno in primo piano senza eventi utili (es. finestra rimasta
                    // "in primo piano" mentre era ridotta: ripristinandola Windows non segnala nulla, e
                    // l'evento di fine riduzione arriva quando risulta ancora ridotta): ci pensa questo
                    // confronto locale.
                    const bool iconic = IsIconic(m_window) != FALSE;
                    const bool foreground = current == m_window;
                    const bool stateChanged = iconic != m_lastIconic || foreground != m_lastForeground;
                    if (stateChanged)
                    {
                        XPX_VLOG(L"Torna su: finestra %s, %s.", iconic ? L"ridotta a icona" : L"aperta", foreground ? L"in primo piano" : L"non in primo piano");
                    }
                    m_lastIconic = iconic;
                    m_lastForeground = foreground;
                    const HWND before = m_view;
                    ResolveView();
                    if (m_view != before || stateChanged)
                    {
                        Refresh();
                    }
                    else if (m_wanted || m_alpha > 0)
                    {
                        if (!Place())
                        {
                            Hide(false);
                        }
                        else if (++m_refreshTicks % kRefreshEveryTicks == 0)
                        {
                            Refresh();
                        }
                    }
                }
                break;
            case kTimerPending:
                KillTimer(m_hwnd, kTimerPending);
                Refresh();
                break;
            case kTimerScroll:
                ScrollStep();
                break;
            case kTimerFade:
                m_alpha = m_wanted ? std::min(255, m_alpha + kFadeStep) : std::max(0, m_alpha - kFadeStep);
                if (m_alpha == 0 && !m_wanted)
                {
                    Hide(true);
                    break;
                }
                Render();
                if (m_alpha == 255 || m_alpha == 0)
                {
                    KillTimer(m_hwnd, kTimerFade);
                }
                break;
            default:
                break;
            }
            return 0;

        case WM_APP_REFRESH:
            Refresh();
            return 0;

        default:
            break;
        }
        return DefWindowProcW(m_hwnd, message, wParam, lParam);
    }
}
