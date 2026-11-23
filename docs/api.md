# API reference

Everything is in the `sui` namespace. Strings are UTF-8; every text parameter accepts both
`const char*` and `std::string`. Widgets that edit a value take it by reference and return `true`
when the user changed it (buttons: when clicked).

Setup is just `#include "simplyui.h"` with `simplyui.dll` next to your program (see
[Getting started](getting-started.md)). The functions below call into the DLL; you never deal with
the DLL directly.

**Threads:** call everything from the thread that runs `Run()`, except the functions marked
*any thread* below (and `sui::Log`), which are safe from worker threads. Other calls from other
threads are ignored rather than crashing.

**Mistakes:** a missing `End...()`/`Pop...()` is closed automatically at the end of the frame, an
extra one is ignored, and bad values (NaN, negative sizes, broken format strings) are clamped. Each
problem is reported once as a `[SimplyUI]` line in the debugger output (Visual Studio *Output*
window, or DebugView).

- [Application](#application) · [Frame and time](#frame-and-time) · [Input](#input)
- [Layout](#layout) · [IDs and items](#ids-and-items)
- [Text](#text) · [Buttons and toggles](#buttons-and-toggles) · [Numbers](#numbers) · [Text input](#text-input) · [Choices](#choices) · [Feedback](#feedback)
- [Tables](#tables) · [Plots](#plots) · [Log](#log) · [Images and canvas](#images-and-canvas)
- [Popups and menus](#popups-and-menus) · [Dialogs and system](#dialogs-and-system) · [Settings](#settings) · [Style](#style)

---

## Application

| Function | Description |
|---|---|
| `int Run(const Config& config, const std::function<void()>& frame)` | Opens the window and calls `frame` whenever it must be drawn. Returns the exit code. |
| `int Run(const char* title, int width, int height, frame)` | Shortcut with default settings. |
| `void Quit(int exitCode = 0)` | Closes the window after the current frame. *Any thread.* |
| `bool CloseRequested()` | `true` for one frame after the user clicked the X button (or pressed Alt+F4). |
| `void CancelClose()` | Call during that frame to keep the window open (e.g. to ask "Save changes?"). |
| `void SetTitle(Str)` | Changes the window title. *Any thread.* |
| `void SetWindowSize(int w, int h)` / `Vec2 WindowSize()` | Client size in DIPs. *Any thread.* |
| `void SetTopmost(bool)` / `bool IsTopmost()` | Always on top. *Any thread.* |
| `void Minimize()` | *Any thread.* |
| `void BringToFront()` | Restores, shows (also when hidden) and activates the window. *Any thread.* |
| `void HideWindow()` | Hides the window; the program keeps running (tray icon, hotkeys, timers). *Any thread.* |
| `void ToggleWindow()` | For a hotkey: hides the window if it is in front, otherwise brings it to front. |
| `void* NativeWindow()` | The `HWND`. |
| `float DpiScale()` | 1.0 at 96 DPI, 1.5 at 144 DPI, ... |

`Config` fields: `title`, `width`, `height`, `minWidth`, `minHeight`, `resizable`, `topmost`,
`scroll` (window scrolls when content is taller), `rememberWindow`, `theme`, `accent`, `appId`
(settings folder name), `iconResource`, and for tools that run in the background:

| Field | What it does |
|---|---|
| `tray` | Shows the program's icon in the notification area. The X button hides the window there; clicking the icon shows or hides it; right-click offers *Show/Hide* and *Exit*. While hidden, `Toast()` becomes a Windows notification. |
| `startHidden` | Starts without showing the window (show it with `BringToFront()`, the tray icon or a hotkey). |
| `singleInstance` | Starting the program again brings the running window to the front instead of opening a second one (`Run()` returns 0 in the new start). Files given to that start, e.g. dropped onto the .exe, arrive in the running window through `DroppedFiles()`. |

A minimized or hidden window keeps running frames when something happens (hotkeys, `RedrawIn`,
`Wake`, worker threads), so timers and hotkeys keep working; it just draws nothing. Toasts wait
until the window can be seen again.

## Frame and time

| Function | Description |
|---|---|
| `double Time()` | Seconds since start. |
| `float DeltaTime()` | Seconds since the previous frame. |
| `uint64_t FrameCount()` | Frames drawn so far. |
| `void RequestRedraw()` | Draw another frame as soon as possible (e.g. while animating something yourself). *Any thread.* |
| `void RedrawIn(float seconds)` | Draw a frame after a delay. Call it every frame for a steady refresh rate, e.g. `RedrawIn(1)` for a clock. *Any thread.* |
| `void Wake()` | Redraw soon; call it from worker threads after changing shared data. *Any thread.* |

SimplyUI only draws when something happens (input, animation, `Wake`, `RedrawIn`), so idle tools use
no CPU. If your UI shows data that changes by itself, use `RedrawIn` or `Wake`.

## Input

| Function | Description |
|---|---|
| `Vec2 MousePos()` | Mouse position in DIPs, relative to the window. |
| `bool MouseDown/MouseClicked/MouseReleased/MouseDoubleClicked(int button = 0)` | 0 left, 1 right, 2 middle. |
| `float MouseWheel()` | Wheel notches this frame. |
| `bool KeyDown(Key)` / `KeyPressed(Key, bool repeat = true)` / `KeyReleased(Key)` | `Key::A`, `Key::Enter`, `Key::F5`, ... (Windows virtual-key codes). |
| `bool CtrlDown()`, `ShiftDown()`, `AltDown()` | Modifier state. |
| `bool Shortcut(Str keys)` | `true` when the combination was pressed this frame: `"Ctrl+S"`, `"Ctrl+Shift+N"`, `"F5"`, `"Alt+Enter"`. Ignores keys that a focused text field needs (e.g. Ctrl+C). |
| `bool GlobalHotkey(Str keys)` | System-wide hotkey that works even when the window is not focused or minimized. Call every frame. |
| `bool IsEditingText()` | A text field has keyboard focus. |

## Layout

| Function | Description |
|---|---|
| `SameLine(float spacing = -1)` | Next item on the current line. |
| `NewLine()`, `Spacing(float h = -1)`, `Spacer(w, h)` | Line breaks and empty space. |
| `Indent(float = -1)`, `Unindent(float = -1)` | |
| `NextWidth(float w)` | Width of the next item: `>1` px, `0..1` fraction, `<=0` "all but". |
| `float AvailableWidth()` | Width left for the next item. |
| `BeginRow(int n)` / `BeginRow({weights})` ... `EndRow()` | Following items share one line in cells. |
| `BeginColumns(int n)` / `BeginColumns({weights})`, `NextColumn()`, `EndColumns()` | Side-by-side stacks. |
| `BeginHorizontal(Align = Left)` ... `EndHorizontal()` | Items flow left to right at natural size; `Align::Center`/`Right` aligns the group. |
| `BeginCard(Str title = "")` ... `EndCard()` | Rounded panel around the content. |
| `BeginChild(Str id, float height = 0, bool border = true)` ... `EndChild()` | Scrollable region. Height `0` = fill, `<0` = fill minus. |
| `BeginFooter()` ... `EndFooter()` | Fixed bar at the bottom of the window. |
| `BeginDisabled(bool = true)` ... `EndDisabled()` | Greys out and blocks everything inside. |

See the [layout guide](layout.md).

## IDs and items

| Function | Description |
|---|---|
| `PushID(Str / int / const void*)`, `PopID()` | Make IDs unique inside loops. |
| `ID GetID(Str label)` | The ID a widget with this label would get. |
| `const ItemInfo& LastItem()` | Info about the previous item: `rect`, `hovered`, `active`, `focused`, `clicked`, `rightClicked`, `doubleClicked`, `changed`, `submitted` (Enter in a text field). |
| `Tooltip(Str text)` | Tooltip for the previous item (shown after a short hover). |
| `FocusNext()` | The next widget that can take keyboard focus gets it, e.g. a text field when the window opens: `if (sui::FrameCount() == 1) sui::FocusNext();` |

## Text

| Function | Description |
|---|---|
| `Text(Str)` | Text (newlines allowed). |
| `Textf(fmt, ...)` | printf-style text. `std::string Format(fmt, ...)` returns a formatted string. |
| `TextDim(Str)`, `TextColored(Color, Str)`, `TextWrapped(Str)` | |
| `Heading(Str)`, `Title(Str)` | Larger bold text. |
| `LabelText(Str label, Str value)` | Read-only key/value line aligned with the label column. |
| `BulletText(Str)` | |
| `bool Link(Str text, Str url = "")` | Hyperlink; opens `url` when clicked (if given). |
| `Badge(Str, Color)` / `Badge(Str, Level)` | Small colored pill. |
| `Separator()`, `SeparatorText(Str)` | |

## Buttons and toggles

| Function | Description |
|---|---|
| `bool Button(Str label)`, `Button(Str label, Icon)` | |
| `bool PrimaryButton(Str)`, `DangerButton(Str)` | Accent / red buttons. |
| `bool IconButton(Icon, Str tooltip = "")` | Square, borderless toolbar button. |
| `bool Checkbox(Str, bool&)` | |
| `bool Toggle(Str, bool&)` | Switch with the label on the left and the switch right-aligned. |
| `bool RadioButton(Str, int& current, int value)` | Selects `value`; returns `true` when it changed. |
| `bool Selectable(Str, bool selected = false)` | Full-width list row. |

Buttons react to Space/Enter when focused (Tab moves focus).

## Numbers

| Function | Description |
|---|---|
| `bool Slider(Str, float&, float min, float max, const char* fmt = "%.2f")` | Double-click to type a value. Arrow keys when focused. |
| `bool Slider(Str, int&, int min, int max, const char* fmt = "%d")` | |
| `bool Drag(Str, float&, float speed, float min, float max, fmt)` | Drag left/right to change (Shift = slow, Ctrl = fast). Double-click to type. |
| `bool Drag(Str, int&, float speed, int min, int max)` | |
| `bool InputInt(Str, int&, int step = 1, int min, int max)` | Text field with -/+ steppers (hold to repeat, Up/Down keys). |
| `bool InputFloat(Str, float&, float step, float min, float max, fmt)` | |

## Text input

| Function | Description |
|---|---|
| `bool InputText(Str label, std::string&, Str hint = "", int flags = 0)` | Single line. `LastItem().submitted` is `true` when Enter was pressed. |
| `bool InputMultiline(Str label, std::string&, float height = 120, int flags = 0)` | Word-wrapped, scrollable. |
| `bool SearchBox(Str id, std::string&, Str hint = "Search", int flags = 0)` | Search icon + clear button. |

Flags: `InputPassword`, `InputReadOnly`, `InputNumbers`, `InputAllowTab` (multiline), `InputNoClear` (search box).
Editing keys: arrows, Home/End, Ctrl+arrows (words), Shift to select, Ctrl+A/C/X/V/Z/Y, double-click selects a word, Escape leaves the field.

## Choices

| Function | Description |
|---|---|
| `bool Combo(Str, int& current, {"A", "B"})` / `Combo(Str, int&, const std::vector<std::string>&)` | Drop-down list. Up/Down keys change the value when focused. |
| `bool ListBox(Str, int& current, const std::vector<std::string>&, int visibleItems = 6)` | Scrollable list. |
| `bool Tabs(int& current, {"General", "Advanced"})` | Tab bar; show the content of the selected tab yourself with `if (tab == 0) ...`. |
| `bool Segmented(Str, int& current, {"Low", "High"})` | Segmented control (pill buttons). |
| `bool CollapsingHeader(Str, bool defaultOpen = false)` | Returns `true` while open. |
| `bool TreeNode(Str, int flags = 0)` + `TreePop()` | Returns `true` while open: then draw the children and call `TreePop()`. Flags: `TreeLeaf`, `TreeSelected`, `TreeDefaultOpen`. |
| `bool ColorEdit(Str, Color&, bool alpha = false)` | Swatch + hex; click for a picker (saturation/value square, hue bar, alpha bar, hex input). |

## Feedback

| Function | Description |
|---|---|
| `ProgressBar(float fraction, Str overlay = "")` | `fraction < 0` = indeterminate animation. |
| `Spinner(float size = 0)` | Busy indicator. |
| `Toast(Str text, Level = Info, float seconds = 3)` | Notification at the bottom of the window (a Windows notification while the window is hidden in the tray). Levels: `Info`, `Success`, `Warning`, `Error`. Click to dismiss. *Any thread.* |
| `std::vector<std::string> DropZone(Str label, float height = 90)` | Dashed drop area; returns the files dropped on it this frame (clicking it opens a file dialog). |

## Tables

```cpp
sui::NextTableWeights({3, 1, 1});    // optional initial column widths
if (sui::BeginTable("files", {"Name", "Size", "Date"}, sui::TableStriped | sui::TableSortable, 300)) {
    sui::TableSort sort = sui::TableSorting();
    if (sort.changed) SortMyData(sort.column, sort.ascending);
    for (int i = 0; i < (int)files.size(); i++) {
        if (sui::TableRow({files[i].name, files[i].size, files[i].date}, i == selected)) selected = i;
    }
    sui::EndTable();
}
```

| Function | Description |
|---|---|
| `bool BeginTable(Str id, {columns}, int flags = 0, float height = 0)` | Height: `0` grows with the rows, `>0` fixed and scrollable, `<0` fills the window. |
| `bool TableNextRow(bool selected = false)` | Starts a row; returns `true` if the row was clicked. |
| `void TableNextCell()` | Moves to the next cell; any widgets can go in a cell. |
| `bool TableRow({cells}, bool selected = false)` | Shortcut for a text-only row. |
| `void EndTable()` | |
| `TableSort TableSorting()` | `{column, ascending, changed}`; with `TableSortable`. |
| `void NextTableWeights({...})` | Initial relative column widths for the next table. |

Flags: `TableStriped`, `TableSortable`, `TableResizable` (drag header dividers), `TableNoHeader`.

## Plots

| Function | Description |
|---|---|
| `PlotLines(Str label, const float* values, int count, float height = 80, float min = 0, float max = 0)` | Line chart with area fill. `min == max` = auto range. Hover to read values. |
| `PlotLines(Str, const std::vector<float>&, height, min, max)` | |
| `PlotBars(...)` | Same arguments, bar chart. |

## Log

```cpp
sui::Log log;                        // keep it alive as long as the UI
log.Info("Started %s", name.c_str());
log.Success("Done in %.1f s", 2.5);
log.Warn("Disk almost full");
log.Error("Failed with code %d", 42);
sui::LogView("log", log, 200);       // height 0 = fill the window
```

`Log` is **thread-safe** (add lines from any thread, the view redraws automatically). The lines live
inside the DLL, so a `Log` can't be copied: keep one and pass it by reference.
Methods: `Add(Level, Str)`, `Info/Success/Warn/Error(fmt, ...)`, `Clear()`, `Count()`, `Text()`.
`LogView(id, log, height = 0, showTime = true)`: auto-scrolls when at the bottom, click selects a line,
Ctrl+C copies it, Ctrl+A copies everything, right-click for a menu.

## Images and canvas

| Function | Description |
|---|---|
| `Texture LoadTexture(Str path)` | Loads PNG, JPG, BMP, GIF, ICO, TIFF (via Windows Imaging Component). `if (!tex)` on failure. |
| `Texture LoadTextureFromMemory(const void*, size_t)` | E.g. an embedded PNG. |
| `Texture CreateTexture(const void* rgba, int w, int h)` / `UpdateTexture(Texture&, const void* rgba)` | From raw RGBA pixels. |
| `FreeTexture(Texture&)` | |
| `Image(const Texture&, Vec2 size = {})` | Draws it as an item; `{0, 0}` = natural size (fits the width). |

```cpp
sui::Canvas c = sui::BeginCanvas(200);           // full width, 200 px tall
c.FillRect(sui::Rect(10, 10, 80, 40), sui::Rgb(0x4C8DFF), 6);
c.Line(sui::Vec2(0, 0), c.mouse, sui::GetStyle().text, 2);
if (c.clicked) points.push_back(c.mouse);
sui::EndCanvas();
```

`Canvas` fields: `rect`, `size`, `mouse` (relative), `hovered`, `clicked`, `down`, `released`,
`rightClicked`, `wheel`. Methods: `Line`, `FillRect`, `StrokeRect`, `FillCircle`, `StrokeCircle`,
`Polyline`, `FillPolygon`, `Bezier`, `Text`, `Image`, `IconAt`. Coordinates are relative to the canvas.

## Popups and menus

| Function | Description |
|---|---|
| `BeginMenuBar()` ... `EndMenuBar()` | Menu bar at the top of the window. |
| `BeginMenu(Str)` ... `EndMenu()` | A menu (in the menu bar) or a sub-menu (inside another menu). |
| `bool MenuItem(Str label, Str shortcut = "", bool checked = false, bool enabled = true)` | The shortcut text is only displayed: handle it with `Shortcut()`. |
| `bool MenuItem(Str label, Str shortcut, bool* toggle, bool enabled = true)` | Toggles `*toggle`. |
| `MenuSeparator()` | |
| `BeginContextMenu()` ... `EndContextMenu()` | Right-click menu for the previous item. |
| `OpenPopup(Str id)`, `BeginPopup(Str id)` ... `EndPopup()`, `ClosePopup()` | Generic popup at the mouse position. |
| `OpenModal(Str title)`, `BeginModal(Str title)` ... `EndModal()`, `CloseModal()` | Modal dialog; dims the window, Escape closes it. |

```cpp
if (sui::Button("Delete")) sui::OpenModal("Delete file?");
if (sui::BeginModal("Delete file?")) {
    sui::TextWrapped("This cannot be undone.");
    sui::BeginHorizontal(sui::Align::Right);
    if (sui::Button("Cancel")) sui::CloseModal();
    if (sui::DangerButton("Delete")) { RemoveSelectedFile(); sui::CloseModal(); }
    sui::EndHorizontal();
    sui::EndModal();
}
```

## Dialogs and system

All of these work from *any thread* (except `DroppedFiles` and `TaskbarProgress`, which belong to the frame).

| Function | Description |
|---|---|
| `std::string OpenFileDialog(Str filter = "", Str title = "")` | `filter`: `"Images|*.png;*.jpg|All files|*.*"`. Returns `""` if cancelled. |
| `std::vector<std::string> OpenFilesDialog(filter, title)` | Multiple files. |
| `std::string SaveFileDialog(Str filter, Str defaultName, Str title)` | Adds the extension of the first filter automatically. |
| `std::string PickFolder(Str title = "")` | |
| `ShowMessage(Str title, Str text, Level = Info)` | Native message box. |
| `bool Confirm(Str title, Str text)` | Yes/No message box. |
| `Beep(Level = Info)` | Plays the Windows sound for that kind of message. |
| `OpenURL(Str)` | Opens a URL, file or folder with its default program. |
| `ShowInExplorer(Str path)` | Opens Explorer with the file selected. |
| `SetClipboard(Str)`, `std::string GetClipboard()` | |
| `const std::vector<std::string>& DroppedFiles()` | Files dropped anywhere on the window this frame (and, with `Config::singleInstance`, files given to a second start). |
| `std::vector<std::string> CommandLine()` | The program's arguments as UTF-8, without the program itself (e.g. files dropped onto the .exe). |
| `TaskbarProgress(float fraction, Level = Info)` | Progress on the taskbar button. Call it every frame while busy; it disappears in the first frame without a call. `fraction` 0..1, below 0 = busy without a known end; `Level::Warning` shows it yellow, `Level::Error` red. |
| `std::wstring ToWide(Str)`, `std::string ToUtf8(const std::wstring&)` | UTF-8 / UTF-16 conversion for Win32 calls. |

## Settings

```cpp
sui::Persist("volume", volume);      // call every frame, with the same key
sui::Persist("lastFolder", folder);
```

The first call loads the saved value (if any) into your variable; on exit the latest values are written
to `%APPDATA%\<appId>\settings.ini`. Supported types: `bool`, `int`, `float`, `std::string`, `Color`.
`SaveSettings()` writes immediately (from another thread: at the next frame); `SettingsPath()` returns the file path.
`Config::rememberWindow = true` also restores the window size, position and maximized state.

## Style

| Function | Description |
|---|---|
| `Style& GetStyle()` | All colors and sizes; edit freely. |
| `SetTheme(Theme)` / `GetTheme()` / `IsDark()` | `Theme::Dark`, `Light`, `System`. |
| `SetAccent(Color)` / `Color SystemAccent()` | |
| `FontId AddFont(Str family, float size, bool bold = false, bool italic = false)` | A font for `PushFont`. Unknown families fall back to Segoe UI; asking for the same font again returns the same id, so calling it every frame is fine. |
| `PushFont(FontId)`, `PopFont()` | Built-in: `FontBody`, `FontBold`, `FontSmall`, `FontHeading`, `FontTitle`, `FontMono`. |
| `float TextWidth(Str, FontId = FontBody)`, `float LineHeight(FontId = FontBody)` | Measurements. |

Colors: `Rgb(0xRRGGBB)`, `Rgba(0xRRGGBBAA)`, `Hsv(h, s, v, a)`, `ToHsv`, `Mix`, `ToHex`, `ParseHex`.
See [styling](styling.md).
