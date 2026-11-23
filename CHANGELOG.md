# Changelog

## 1.0.0 - 2026-11-23

First release.

- **Two files, zero setup:** `simplyui.h` + `simplyui.dll` (`simplyui32.dll` for 32-bit programs). The header loads the DLL by itself, so there is nothing to link and nothing to configure, and it shows a clear message if the DLL is missing or doesn't match.
- Works with any C++11 (or newer) compiler - Visual Studio, MinGW-w64, clang - because the DLL talks to your program through plain C functions only.
- **Hard to crash:** a missing `End...()`/`Pop...()` is closed automatically and an extra one is ignored; NaN and absurd values are clamped; printf-style number formats are validated; UI calls from worker threads are ignored instead of corrupting memory. Each mistake is reported once in the debugger output. Tested with thousands of frames of random, hostile API calls.
- **Thread-friendly:** `Toast`, `SetTitle`, `Quit`, `Wake`, `RequestRedraw`, `RedrawIn`, `SaveSettings`, `BringToFront`, `HideWindow`, dialogs, message boxes, clipboard and `Log` work from any thread.
- **Made for background tools:** `Config::tray` (icon in the notification area; the X button hides the window there), `Config::startHidden`, `Config::singleInstance` (a second start brings the running window to the front and hands it its files), `HideWindow()`, `ToggleWindow()` for hotkeys, `TaskbarProgress()` on the taskbar button, and `Toast()` becomes a Windows notification while the window is hidden. Global hotkeys, `RedrawIn()` timers and `Wake()` keep working while the window is minimized or hidden (it just draws nothing), and toasts wait until the window can be seen.
- Immediate-mode GUI rendered with Direct2D + DirectWrite (part of Windows; the DLL needs nothing else).
- Event-driven redraw (0% CPU when idle), per-monitor DPI v2, dark/light/system themes, custom or system accent color, dark title bar.
- Widgets: buttons (normal, primary, danger, icon), checkbox, toggle, radio, selectable, slider, drag, number inputs with steppers, single/multi-line text input, password field, search box, combo, list box, tabs, segmented control, collapsing header, tree, progress bar, spinner, color picker, badges, links, drop zone.
- Data: tables (striped, sortable, resizable, scrollable), line and bar plots, thread-safe log view, images via WIC.
- Layout: automatic label column, rows, columns, cards, scroll areas, horizontal groups with alignment, footer, menu bar, flexible widths, disabled sections.
- Overlays: menus and sub-menus, context menus, popups, modal dialogs, tooltips, toast notifications.
- Text editing: selection, clipboard, undo/redo, word navigation, IME positioning.
- System: native file/folder dialogs, message boxes, clipboard, drag & drop, shortcuts, global hotkeys, settings persisted to `%APPDATA%`, window position memory.
- Canvas for custom drawing and 55 built-in vector icons. `AddFont()` can be called every frame (same font = same id).
- `FocusNext()` puts the keyboard focus on the next widget, `CommandLine()` returns the program's arguments as UTF-8, `Beep()` plays the Windows message sounds.
- The DLL carries version information (Properties > Details).
- Examples: hello, showcase, file_hasher, system_monitor, todo, timer - with an app icon. None of them needs anything but `simplyui.h` and the DLL.
- `build.bat`: one double-click builds both DLLs and the examples with Visual Studio and packs the release zip.
