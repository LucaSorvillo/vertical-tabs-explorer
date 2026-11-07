#include "pch.h"
#include "NavDrop.h"

#include "ModLog.h"
#include "NavBar.h"
#include "NavGroups.h"

#include <shellapi.h>
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

    namespace
    {
        // Gli elementi come li dà la shell (Esplora file, il desktop: CFSTR_SHELLIDLIST).
        std::vector<com_ptr<IShellItem>> ShellItemsOf(IDataObject* data)
        {
            std::vector<com_ptr<IShellItem>> result;
            com_ptr<IShellItemArray> items;
            DWORD count = 0;
            if (FAILED(SHCreateShellItemArrayFromDataObject(data, IID_PPV_ARGS(items.put()))) || !items || FAILED(items->GetCount(&count)))
            {
                return result;
            }
            for (DWORD i = 0; i < count && i < kMaxExamined; ++i)
            {
                com_ptr<IShellItem> item;
                if (SUCCEEDED(items->GetItemAt(i, item.put())) && item)
                {
                    result.push_back(std::move(item));
                }
            }
            return result;
        }

        // Altri programmi (un altro gestore di file, un editor...) danno spesso solo i percorsi (CF_HDROP).
        std::vector<com_ptr<IShellItem>> PathItemsOf(IDataObject* data)
        {
            std::vector<com_ptr<IShellItem>> result;
            FORMATETC format{ CF_HDROP, nullptr, DVASPECT_CONTENT, -1, TYMED_HGLOBAL };
            STGMEDIUM medium{};
            if (FAILED(data->GetData(&format, &medium)))
            {
                return result;
            }
            if (medium.tymed == TYMED_HGLOBAL && medium.hGlobal)
            {
                const auto drop = static_cast<HDROP>(medium.hGlobal);
                const UINT count = DragQueryFileW(drop, 0xFFFFFFFF, nullptr, 0);
                for (UINT i = 0; i < count && i < kMaxExamined; ++i)
                {
                    const UINT length = DragQueryFileW(drop, i, nullptr, 0);
                    if (length == 0)
                    {
                        continue;
                    }
                    std::wstring path(length, L'\0');
                    if (DragQueryFileW(drop, i, path.data(), length + 1) == 0)
                    {
                        continue;
                    }
                    com_ptr<IShellItem> item;
                    if (SUCCEEDED(SHCreateItemFromParsingName(path.c_str(), nullptr, IID_PPV_ARGS(item.put()))) && item)
                    {
                        result.push_back(std::move(item));
                    }
                }
            }
            ReleaseStgMedium(&medium);
            return result;
        }
    }

    std::vector<DroppedFolder> FoldersOf(IDataObject* data)
    {
        std::vector<DroppedFolder> result;
        if (!data)
        {
            return result;
        }
        auto items = ShellItemsOf(data);
        if (items.empty())
        {
            items = PathItemsOf(data);
            if (!items.empty())
            {
                XPX_MLOG(L"NavBar: trascinamento da fuori con i soli percorsi (CF_HDROP): %zu elementi.", items.size());
            }
        }
        for (auto const& item : items)
        {
            SFGAOF attributes = 0;
            if (FAILED(item->GetAttributes(SFGAO_FOLDER | SFGAO_STREAM, &attributes)) || !(attributes & SFGAO_FOLDER) || (attributes & SFGAO_STREAM))
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
