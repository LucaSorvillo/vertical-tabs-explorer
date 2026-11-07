#include "pch.h"
#include "NavDrop.h"

#include "ModLog.h"
#include "NavBar.h"
#include "NavGroups.h"

#include <shobjidl.h>

namespace xpx::mod
{
    namespace
    {
        using winrt::com_ptr;

        // Oltre queste voci di una selezione non si guarda (ognuna chiede i suoi attributi alla shell): una selezione così
        // grande trascinata sulla barra è quasi sempre per sbaglio.
        constexpr DWORD kMaxExamined = 500;

        std::wstring DisplayName(IShellItem* item, SIGDN form)
        {
            PWSTR text = nullptr;
            if (FAILED(item->GetDisplayName(form, &text)) || !text)
            {
                return {};
            }
            std::wstring result{ text };
            CoTaskMemFree(text);
            return result;
        }

        struct NavDropTarget : winrt::implements<NavDropTarget, ::IDropTarget>
        {
            NavDropTarget(std::weak_ptr<NavBar> bar, HWND hwnd) : m_bar(std::move(bar)), m_hwnd(hwnd)
            {
                // L'immagine del trascinamento (quella di Esplora file) resta visibile anche sopra la barra.
                CoCreateInstance(CLSID_DragDropHelper, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(m_helper.put()));
            }

            HRESULT __stdcall DragEnter(IDataObject* data, DWORD, POINTL point, DWORD* effect) noexcept override
            {
                if (!effect)
                {
                    return E_INVALIDARG;
                }
                const DWORD allowed = *effect;
                *effect = DROPEFFECT_NONE;
                Call(L"ingresso", [&](NavBar& bar) { *effect = bar.OnExternalDragEnter(data, POINT{ point.x, point.y }, allowed); });
                if (m_helper)
                {
                    POINT screen{ point.x, point.y };
                    m_helper->DragEnter(m_hwnd, data, &screen, *effect);
                }
                return S_OK;
            }

            HRESULT __stdcall DragOver(DWORD, POINTL point, DWORD* effect) noexcept override
            {
                if (!effect)
                {
                    return E_INVALIDARG;
                }
                const DWORD allowed = *effect;
                *effect = DROPEFFECT_NONE;
                Call(L"movimento", [&](NavBar& bar) { *effect = bar.OnExternalDragOver(POINT{ point.x, point.y }, allowed); });
                if (m_helper)
                {
                    POINT screen{ point.x, point.y };
                    m_helper->DragOver(&screen, *effect);
                }
                return S_OK;
            }

            HRESULT __stdcall DragLeave() noexcept override
            {
                Call(L"uscita", [&](NavBar& bar) { bar.OnExternalDragLeave(); });
                if (m_helper)
                {
                    m_helper->DragLeave();
                }
                return S_OK;
            }

            HRESULT __stdcall Drop(IDataObject* data, DWORD, POINTL point, DWORD* effect) noexcept override
            {
                if (!effect)
                {
                    return E_INVALIDARG;
                }
                const DWORD allowed = *effect;
                *effect = DROPEFFECT_NONE;
                Call(L"rilascio", [&](NavBar& bar) { *effect = bar.OnExternalDrop(data, POINT{ point.x, point.y }, allowed); });
                if (m_helper)
                {
                    POINT screen{ point.x, point.y };
                    m_helper->Drop(data, &screen, *effect);
                }
                return S_OK;
            }

        private:
            template <typename F>
            void Call(const wchar_t* where, F&& action) noexcept
            {
                try
                {
                    if (const auto bar = m_bar.lock())
                    {
                        action(*bar);
                    }
                }
                catch (...)
                {
                    XPX_MLOG(L"NavBar: errore nel trascinamento da fuori (%s, 0x%08X).", where, static_cast<unsigned>(winrt::to_hresult()));
                }
            }

            std::weak_ptr<NavBar> m_bar;
            HWND m_hwnd = nullptr;
            com_ptr<IDropTargetHelper> m_helper;
        };
    }

    std::vector<DroppedFolder> FoldersOf(IDataObject* data)
    {
        std::vector<DroppedFolder> result;
        com_ptr<IShellItemArray> items;
        if (!data || FAILED(SHCreateShellItemArrayFromDataObject(data, IID_PPV_ARGS(items.put()))) || !items)
        {
            return result;
        }
        DWORD count = 0;
        if (FAILED(items->GetCount(&count)))
        {
            return result;
        }
        for (DWORD i = 0; i < count && i < kMaxExamined; ++i)
        {
            com_ptr<IShellItem> item;
            SFGAOF attributes = 0;
            if (FAILED(items->GetItemAt(i, item.put())) || !item || FAILED(item->GetAttributes(SFGAO_FOLDER | SFGAO_STREAM, &attributes)) ||
                !(attributes & SFGAO_FOLDER) || (attributes & SFGAO_STREAM))
            {
                continue;
            }
            DroppedFolder folder;
            PIDLIST_ABSOLUTE pidl = nullptr;
            if (FAILED(SHGetIDListFromObject(item.get(), &pidl)) || !pidl)
            {
                continue;
            }
            folder.idList.assign(reinterpret_cast<const uint8_t*>(pidl), reinterpret_cast<const uint8_t*>(pidl) + ILGetSize(pidl));
            CoTaskMemFree(pidl);
            const std::wstring parsing = DisplayName(item.get(), SIGDN_DESKTOPABSOLUTEPARSING);
            if (parsing.empty())
            {
                continue;
            }
            folder.key = FolderKey(parsing);
            folder.name = DisplayName(item.get(), SIGDN_NORMALDISPLAY);
            result.push_back(std::move(folder));
        }
        return result;
    }

    void SetDropDescription(IDataObject* data, DROPIMAGETYPE type, std::wstring const& message, std::wstring const& insert)
    {
        if (!data)
        {
            return;
        }
        static const CLIPFORMAT kFormat = static_cast<CLIPFORMAT>(RegisterClipboardFormatW(CFSTR_DROPDESCRIPTION));
        const HGLOBAL memory = GlobalAlloc(GMEM_MOVEABLE | GMEM_ZEROINIT, sizeof(DROPDESCRIPTION));
        if (!memory)
        {
            return;
        }
        if (auto* description = static_cast<DROPDESCRIPTION*>(GlobalLock(memory)))
        {
            description->type = type;
            wcsncpy_s(description->szMessage, message.c_str(), _TRUNCATE);
            wcsncpy_s(description->szInsert, insert.c_str(), _TRUNCATE);
            GlobalUnlock(memory);
        }
        FORMATETC format{ kFormat, nullptr, DVASPECT_CONTENT, -1, TYMED_HGLOBAL };
        STGMEDIUM medium{};
        medium.tymed = TYMED_HGLOBAL;
        medium.hGlobal = memory;
        if (FAILED(data->SetData(&format, &medium, TRUE)))
        {
            GlobalFree(memory); // (con TRUE il blocco passa all'oggetto solo se lo accetta)
        }
    }

    winrt::com_ptr<IDropTarget> MakeNavDropTarget(std::weak_ptr<NavBar> bar, HWND hwnd)
    {
        return winrt::make_self<NavDropTarget>(std::move(bar), hwnd).as<IDropTarget>();
    }
}
