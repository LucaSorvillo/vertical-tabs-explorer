# Architecture

A technical overview of VTabsExplorer. The complete development notes, with the reasons behind each choice,
are in [DESIGN-NOTES.it.md](DESIGN-NOTES.it.md) (Italian). Code comments are in Italian too.

## Overview

```
VTabsExplorer.exe (tray app)                          explorer.exe
├─ Worker thread: session, restore, Ctrl+Shift+T,     ├─ File Explorer windows (CabinetWClass)
│  folders from other apps → tabs                     │  ├─ WinUI 3 header: tabs, address bar,
├─ Veil: hides windows while they are rebuilt         │  │  command bar (XAML islands)
├─ Back to top: own layered window over the view      │  └─ tab content: DirectUI view, nav pane
├─ Hotkey: Ctrl+Shift+T while Explorer is active      │
└─ ModManager ── CreateRemoteThread + LoadLibraryW ─► └─ VTabsExplorerMod.dll (optional)
   (only with vertical tabs / folders bar on)            ├─ Discovery: vtable hooks → each TabView
                                                         ├─ VerticalTabs + WindowLayout + header
                                                         └─ NavBar: folders bar and groups
```

The tray app works from the outside with documented shell APIs. Only the vertical tabs and the folders bar
need code inside `explorer.exe`. With the default Windows settings, that is the same process that hosts the
taskbar and the desktop, so the module has extra safety measures.

## Tray app (`src`)

| File | Role |
| --- | --- |
| `main.cpp` | Tray icon and menu, settings, "start with Windows", command line (`--restore-now`, `--reopen-tab`, `--exit`, `--inject-pid <pid>`). |
| `Worker.cpp` | The real work, on its own thread, because calls into File Explorer are cross-process and can be slow. Tracks live windows and saves the session on every change. Restores the session when File Explorer is reopened. Keeps the stack of closed tabs and windows. Moves folders opened by other apps into a tab. |
| `Explorer.cpp` | Shell COM wrapper: `IShellWindows` / `IWebBrowser2` to read windows and tabs, open a tab (the window's "new tab" command, then `Navigate2`), activate and place windows. |
| `Session.cpp` | `session.txt` and `closed.txt`: versioned text formats, written atomically. A closed tab keeps the position it had (`window tabs at N`). |
| `Veil.cpp` | Makes a window invisible without removing it from the taskbar (`WS_EX_LAYERED`, opacity 0) while it is rebuilt. A WinEvent hook on File Explorer's process catches new windows before they are seen. |
| `BackToTop.cpp` | The back-to-top button: a small layered window that never takes the focus. It reacts to `EVENT_OBJECT_CONTENTSCROLLED` from the file view and reads the position with the UI Automation `ScrollPattern`. |
| `Hotkey.cpp` | <kbd>Ctrl</kbd>+<kbd>Shift</kbd>+<kbd>T</kbd> is registered only while a File Explorer window is in the foreground. |
| `ModManager.cpp`, `Injector.cpp` | Load the module into the `explorer.exe` processes that have windows, only when the feature is on. Every process with the module is watched: if it ends with an NTSTATUS error code, the feature is suspended (`Enabled=0`, `Suspended=1`) and the user is told. |
| `TabColorFiles.cpp` | Exchanges tab colors with the module through files in `colors\`: the module publishes the live colors, and the tray app sends the colors to apply to reopened tabs. For a tab reopened with <kbd>Ctrl</kbd>+<kbd>Shift</kbd>+<kbd>T</kbd> the file also says where to move it (`move=from,to`): File Explorer can only add a tab at the end, so the module moves it back with the same reorder the column uses. |
| `Common.cpp`, `Settings.cpp`, `ModShared.h` | Paths, test mode, `settings.ini`, constants shared with the module (such as the color keys). |

The module DLL is copied to `%LOCALAPPDATA%\VTabsExplorer\runtime\<stamp>\` before being loaded, so the
installed file is never locked by File Explorer.

## Module (`src/mod`)

### Finding the tab bars without XAML diagnostics (`Discovery.cpp`)

The XAML diagnostics API (`InitializeXamlDiagnosticsEx`, used by VTabsNotepad) keeps strong references to the
roots of every XAML island. In File Explorer, that kept every closed window alive forever. Instead, the module
replaces two slots in vtables shared by all instances of a class (public WinRT interfaces, stable ABI):

- `IFrameworkElementOverrides::OnApplyTemplate` of `TabView`: every new tab bar announces itself on its own
  thread, before it appears;
- `IAutomationPeerOverrides::GetAutomationControlTypeCore` of `TabViewItemAutomationPeer`: for windows that
  were already open, a UI Automation search makes File Explorer call this method on the window's thread, and
  the module walks from the peer to the tab and its bar. Outside such a search, the hook does nothing.

### Window (`VerticalTabs.cpp`, `WindowLayout.cpp`, `UnifiedHeader.cpp`)

- **The column**: a `ListView` in the module's own `DesktopWindowXamlSource`, bound to the native
  `TabViewItem`s. It handles switching, closing, new tab, drag reordering on the real tabs, File Explorer's own
  tab menu with the "Tab color" submenu, collapse and width. The native strip stays alive but hidden.
- **Layout**: the column runs from the top of the window. The header island is raised by the height of the
  title row, so the address bar sits next to the window buttons. The content and the navigation pane are moved
  to the right of the column.
- **Header in the title bar color**: File Explorer's background brushes of the address and command bars are
  overridden by zero-length "hold" animations. The original values stay underneath and keep following the
  theme; stopping the animations restores them exactly.
- `CaptionButtons.cpp`: own minimize/maximize/close buttons, as tall as the address bar. The DWM buttons are
  hidden with the undocumented `0x10000` flag of `SetWindowThemeAttribute(WTA_NONCLIENT)`. The new buttons are
  a layered child window drawn with Direct2D, with `HTMAXBUTTON` hit-testing so that Snap layouts still work.
- `Glass.cpp`: File Explorer extends the frame into the client area down to the bottom of its header. With
  the title row hidden, that would make the top of the content lighter. A hook in the delay-load import table
  of `ExplorerFrame.dll` (and `ieframe.dll`) caps the top margin at the content.
- XAML islands ignore `WM_NCHITTEST`, so the empty caption areas are window-region holes that let the frame
  answer as a title bar.

### Folders bar (`NavBar.cpp`, `NavItems.cpp`, `NavOrder.cpp`, `NavGroups.cpp`, `ModernMenu.cpp`, `MenuArray.cpp`)

- `NavItems`: reads the top-level items of the navigation pane the way File Explorer composes them. Those are
  the Desktop items pinned to the namespace tree, sorted by `SortOrderIndex`, plus the pinned Quick Access
  folders. The reading runs on a worker thread, again when the shell reports changes, and produces icons at the
  pixel sizes each window needs.
- `NavBar`: the bar (WinUI buttons in the module's island), the **»** overflow list, the group lists, and
  dragging. A drag can move across lists and groups: it is polled with `GetCursorPos`/`GetAsyncKeyState`,
  because WinUI pointer capture belongs to the input site that got the press, and a closed popup drops its
  input.
- `NavOrder`: moving a pinned folder calls `IAutomaticDestinationList::PinItem`, so Windows' own order (the
  navigation pane, Home, the jump list) is the bar's order read depth-first through the groups.
- `NavDrop`: folders dragged from outside (File Explorer, the desktop, other apps). The bar's island registers an
  OLE drop target (`RegisterDragDrop`). Only folders count: `SFGAO_FOLDER` without `SFGAO_STREAM`, so no files and
  no `.zip` archives. The target reuses the bar's own drag logic (`HitTestScreen`, `ShowTarget`). The effect is
  always `DROPEFFECT_LINK` (or `COPY`), never `MOVE`, so the source never moves the folder. `IDropTargetHelper`
  keeps Windows' drag image, with a `CFSTR_DROPDESCRIPTION` text. New folders are pinned with `PinItem` before
  the next pinned folder (`NavItems::PinFolders`), then placed like a bar drag once the next reading includes them.
- `NavGroups`: one store per process, immutable versioned snapshots, saved to `navbar-groups.json`.
- `ModernMenu`: the Windows 11 context menu, opened the way the navigation pane opens it, through the
  "File Explorer Context Menu" presenter of `Windows.UI.FileExplorer.dll`. These interfaces are undocumented
  and were rebuilt from Microsoft's public symbols. If any step fails, the classic menu is shown instead.
- `MenuArray`: merges the item's menu with its folder background menu without duplicate verbs, like the
  navigation pane does. Used for the classic menu and its "Move to group" items.

### Robustness

- Every event handler toward the XAML runtime is wrapped: no exception leaves the module's code.
- The module is never unloaded, because its hooks stay installed. Turning a feature off restores File
  Explorer's interface.
- Test mode (`dev-filter.txt`, `dev-windows.txt` in the data folder) restricts the tray app and the module to
  the windows opened by the tests. `mod-command.txt` accepts test commands addressed to one window.

## Languages

The texts the user sees (tray menu and notifications, the "Tab color" menu and the color names, the group menus,
the rename box, tooltips, accessibility names) exist in English and Italian. `UiItalian()` in `ModShared.h` reads
`GetUserDefaultUILanguage()` once per process, in the tray app and in the module: Italian if the Windows display
language is Italian, English otherwise (changing it requires signing out). `Tr(english, italian)` picks the text.
The XAML markup is written in Italian; in English, the user-facing attribute values are replaced before
`XamlReader::Load`. Logs stay in Italian. The installer chooses its language the same way (Inno Setup detects the
UI language; `ShowLanguageDialog=no`, English first).

## Data files

All in `%LOCALAPPDATA%\VTabsExplorer`: `settings.ini`, `session.txt`, `closed.txt`, `navbar-groups.json`,
`colors\`, `logs\`, `runtime\`. The [README](README.md#privacy) lists what each one contains.

## Build and tests

- `build.ps1` loads the Visual Studio x64 environment, generates the projections and builds with CMake + Ninja
  into `build\bin`. It also restarts the tray app if it was running.
- `tools\Make-Icon.ps1` draws the app icon, the tray icon on a dark tile, into `src\VTabsExplorer.ico` (used by the
  executable and the setup) and into the PNGs of the website and the README.
- `tools\Generate-Projections.ps1` runs `cppwinrt.exe` on the Windows SDK and on the Windows App SDK runtime
  that `explorer.exe` loads (`%windir%\SystemApps\Microsoft.WindowsAppRuntime.CBS*`), so the interfaces match
  the ones in the process.
- `installer\VTabsExplorer.iss` is the Inno Setup script: per-user install, no admin rights, English and
  Italian. It offers to turn on the vertical tabs and the folders bar.
- `tools\test` contains the end-to-end tests:
  - `Run-Tests.ps1`: session, restore, <kbd>Ctrl</kbd>+<kbd>Shift</kbd>+<kbd>T</kbd>, back to top;
  - `Test-OpenInTabs.ps1`: folders opened by other apps;
  - `vertical-tabs\*.ps1`: the module, in a separate `explorer.exe`, with the user's windows isolated.
- `tools\stackdump`: prints live thread stacks of a process with symbols, for hangs.
