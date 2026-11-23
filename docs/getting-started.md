# Getting started

SimplyUI is two files:

| File | What it is |
|---|---|
| `simplyui.h` | The header your code includes. The whole API is at the top, with comments. |
| `simplyui.dll` | The library. 32-bit programs use `simplyui32.dll` instead. |

Get both from the **SimplyUI-x.y.z.zip** on the [Releases](../../../releases) page (or build them
yourself, see the end of this page).

## 1. Add it to your project

Copy `simplyui.h` and `simplyui.dll` into your project folder, next to your `.cpp` files.

That's all. There is no `.lib` to link, no `#define` to add and no project setting to change: the
header loads `simplyui.dll` by itself the first time your program uses SimplyUI.

## 2. Write your tool

```cpp
#include "simplyui.h"

int main() {
    std::string input;
    std::string output;

    return sui::Run("Upper-caser", 460, 300, [&] {
        sui::InputMultiline("Input", input, 100);
        if (sui::PrimaryButton("Convert")) {
            output = input;
            for (char& c : output) c = (char)toupper((unsigned char)c);
        }
        sui::InputMultiline("Output", output, 100, sui::InputReadOnly);
    });
}
```

`sui::Run` opens the window and calls your lambda every time the window needs to be drawn. Keep your
state in normal variables (captured by reference with `[&]`). Widgets read and write those variables
directly. Include `simplyui.h` in as many `.cpp` files as you like.

For more control over the window, pass a `sui::Config`:

```cpp
sui::Config cfg;
cfg.title = "My Tool";
cfg.width = 640;                 // client size in DIPs (scaled automatically for DPI)
cfg.height = 480;
cfg.minWidth = 400;
cfg.resizable = true;
cfg.topmost = false;
cfg.theme = sui::Theme::System;  // follow the Windows light/dark setting
cfg.accent = sui::SystemAccent();
cfg.rememberWindow = true;       // restore size and position next time
return sui::Run(cfg, [&] { /* ... */ });
```

Tools that run in the background can also set `cfg.tray`, `cfg.startHidden` and
`cfg.singleInstance` (see [A tool that lives in the tray](recipes.md#a-tool-that-lives-in-the-tray)).

## 3. Build and run

### Visual Studio

1. *File → New → Project → Console App* (C++).
2. Copy `simplyui.h` and `simplyui.dll` into the project folder (the one with the `.cpp` file).
3. Replace the code in the `.cpp` file with your tool and press **F5**.

When you run from Visual Studio the program finds `simplyui.dll` in the project folder. When you
run the `.exe` directly (for example from `x64\Release`), copy `simplyui.dll` next to it.

- **No console window:** put `#define SIMPLYUI_NO_CONSOLE` above `#include "simplyui.h"` (in one
  file). Or set *Linker → System → SubSystem* to *Windows* and *Linker → Advanced → Entry Point*
  to `mainCRTStartup`.
- **32-bit (x86) builds** need `simplyui32.dll` instead. You can keep both DLLs in the folder.
- **Non-English text** in your string literals: add `/utf-8` to *C/C++ → Command Line* and save the
  file as UTF-8, so the text reaches SimplyUI as UTF-8.

From a *Developer Command Prompt*: `cl /EHsc /O2 main.cpp` (nothing else to add).

### MinGW-w64 (MSYS2, WinLibs, ...)

```
g++ -O2 main.cpp -o tool.exe -mwindows
```

- `-mwindows` hides the console window.
- Add `-static` to bundle the C++ runtime, so your `.exe` needs nothing but `simplyui.dll`.

### clang

```
clang++ -O2 main.cpp -o tool.exe
```

Any C++11 (or newer) compiler works: the DLL only exchanges plain C data with your program, so it
does not matter which compiler built it.

## 4. Ship it

Give people your `.exe` and `simplyui.dll` in the same folder. That's the whole tool: SimplyUI draws
with Direct2D and DirectWrite, which are part of Windows, and the DLL has the C++ runtime built in.

- **App icon:** add a resource file, e.g. `app.rc` containing `1 ICON "app.ico"`, and compile it
  into the exe (Visual Studio: *Add → Resource → Icon*; MinGW: `windres app.rc -O coff -o app.res`
  and pass `app.res` to g++). SimplyUI uses the first icon in the exe for the window automatically.
- **Settings** saved with `sui::Persist()` go to `%APPDATA%\<appId or title>\settings.ini`.
- **Keeping the DLL elsewhere:** `#define SIMPLYUI_DLL_NAME L"bin\\simplyui.dll"` before the include
  (a path relative to the program's folder, or a full path).

## Troubleshooting

| Problem | Fix |
|---|---|
| *"simplyui.dll was not found"* | Put `simplyui.dll` next to your `.exe`. The message shows the folder. |
| *"simplyui.dll is for a different CPU type"* | 64-bit programs need `simplyui.dll`, 32-bit programs need `simplyui32.dll`. |
| *"simplyui.dll (version X) does not match the simplyui.h..."* | Use the header and the DLL from the same release. |
| Non-ASCII text shows garbage | Save the source as UTF-8 and compile with `/utf-8` (Visual Studio). |
| A console window appears | Visual Studio: `#define SIMPLYUI_NO_CONSOLE`. MinGW: `-mwindows`. |
| `WinMain` not found (Visual Studio) | You chose the Windows subsystem: add `/ENTRY:mainCRTStartup` or use `SIMPLYUI_NO_CONSOLE`. |
| Two buttons with the same label act as one | Make the IDs unique: `"Delete##1"`, or wrap them in `PushID(i)` / `PopID()`. |
| A widget called from a worker thread does nothing | Build the UI on the `Run()` thread; from workers use `Wake()`, `Toast()`, `Log`... (see [Recipes](recipes.md)). |
| Layout looks wrong after a code change | Look for `[SimplyUI]` notes in the Visual Studio *Output* window: a missing `End...()` or `Pop...()` is reported there. |

## Building SimplyUI yourself

Only needed if you change SimplyUI or want to make a release.

1. Install Visual Studio 2017 or newer (Community is free) with **Desktop development with C++**.
2. Double-click **`build.bat`** in the repository.

It builds `build\x64\simplyui.dll`, `build\x86\simplyui32.dll` and the examples, then packs
`release\SimplyUI-<version>.zip` with the header, both DLLs, the examples, a README and the license.
Upload that zip to a new GitHub release.

The DLL is built with the static C++ runtime (`/MT`), so it depends only on Windows itself. The
version comes from `SIMPLYUI_VERSION` in `include/simplyui.h`.
