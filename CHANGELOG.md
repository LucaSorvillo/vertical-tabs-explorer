# Changelog

## 1.4.0 — 2026-11-07

- Folders bar: while you drag a folder in from File Explorer (or the desktop, or another app), stop on a group
  (or on "»") for a moment to open its list, like Chrome's bookmark folders. Then drop the folder exactly where
  you want it in the list, in a subgroup too. The lists close when you drop the folder or drag it away.
- Folders dragged from apps that only offer file paths (another file manager, an editor) are accepted too.

## 1.3.0 — 2026-11-07

- Folders bar: drag a folder from File Explorer (or the desktop, or another app) onto the bar to pin it to Quick
  Access right there, like a link dropped on Chrome's bookmarks bar. Dropped on the middle of a group, it goes into
  the group. A folder already in Quick Access moves there instead. The bar shows a line where the folder will go,
  and "Pin to Quick access" (or "Add to <group>") next to the dragged image. Files aren't accepted, and the dragged
  folder is never moved or copied.

## 1.2.0 — 2026-11-06

- <kbd>Ctrl</kbd>+<kbd>Shift</kbd>+<kbd>T</kbd>: the reopened tab comes back in its place, like in VTabsNotepad (with the
  vertical tabs on).
- App icon: the tray icon on a dark tile, for the executable, the setup, the website and the README.
- The tray icon tooltip shows only the app name.

## 1.1.0 — 2026-11-06

- The app follows the Windows display language: Italian if Windows is in Italian, English otherwise. This covers
  the tray menu and notifications, the "Tab color" menu and its colors, the group menus, tooltips and
  accessibility names.
- The setup chooses its language the same way, without asking.
- Documentation and website with English screenshots. The website picks its language from the browser, with
  English as the fallback.

## 1.0.0 — 2026-11-06

First public release.

- Session restore: windows, tabs in order, active tab, positions and tab colors come back when File Explorer
  reopens, also after a restart of the PC. Windows appear complete.
- <kbd>Ctrl</kbd>+<kbd>Shift</kbd>+<kbd>T</kbd> reopens closed tabs and windows, up to 25, also after restarts.
- Folders opened by other apps go into a tab of the window used last, like links in Chrome.
- "Back to top" button in the file view.
- Vertical tabs in a column from the top of the window, with the title row hidden, the address bar next to
  the window buttons and the header in the title bar color. Drag to reorder, File Explorer's own tab menu,
  tab colors, collapse to icons, resizable width.
- Folders bar: the navigation pane as a bar like Chrome's bookmarks bar, with the Windows 11 context menu,
  drag reordering synced with Quick Access, and groups (nested, hover switching, drag into groups).
- Automatic suspension of the vertical tabs and the folders bar if File Explorer crashes with the module
  loaded.
- Per-user installer (English and Italian), no administrator rights needed. It offers to turn on the vertical
  tabs and the folders bar.
