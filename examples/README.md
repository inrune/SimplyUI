# Examples

| Example | What it shows |
|---|---|
| [hello](hello/main.cpp) | The smallest useful program: a few widgets and a toast. |
| [showcase](showcase/main.cpp) | Every widget, layout tool and overlay in five tabs. The best place to look up how something is used. |
| [file_hasher](file_hasher/main.cpp) | A real tool: drop files to compute CRC32 and SHA-256 on a worker thread. Drag & drop, `Wake()`, progress on the window and the taskbar, tables, clipboard, save dialog, files from the command line and a single instance. |
| [system_monitor](system_monitor/main.cpp) | Live CPU, memory and disk usage. Plots, `RedrawIn()` timers, always-on-top, a global hotkey (Ctrl+Shift+M). |
| [todo](todo/main.cpp) | A to-do list saved between runs with `Persist()`. Enter-to-submit, `FocusNext()`, context menus, filters and a confirmation modal. |
| [timer](timer/main.cpp) | A focus timer that lives in the tray. `Config::tray`, single instance, `GlobalHotkey` + `ToggleWindow()`, taskbar progress, notifications while hidden. |

## Building

Double-click `build.bat` in the repository root: it builds the DLLs and all examples into
`build\x64` (needs Visual Studio with the C++ workload).

To build one example by hand, put `simplyui.h` and `simplyui.dll` where the compiler and the
program can find them:

```
cl /EHsc /O2 /I include examples\hello\main.cpp
g++ -O2 -I include examples/hello/main.cpp -o hello.exe -mwindows
```

Then run it with `simplyui.dll` in the same folder as `hello.exe`.
