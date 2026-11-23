# Recipes

Small, copy-pasteable patterns for common tool features.

## Run work in the background with a progress bar

```cpp
#include <atomic>
#include <thread>

std::atomic<bool> running{false};
std::atomic<float> progress{0};
std::thread worker;

// inside the UI function:
if (!running && sui::PrimaryButton("Start")) {
    if (worker.joinable()) worker.join();
    running = true;
    worker = std::thread([&] {
        for (int i = 1; i <= 100; i++) {
            Sleep(30);                    // the real work goes here
            progress = i / 100.0f;
            sui::Wake();                  // thread-safe "please redraw"
        }
        running = false;
        sui::Toast("Done!", sui::Level::Success);   // Toast works from any thread
    });
}
if (running) {
    sui::ProgressBar(progress);
    sui::TaskbarProgress(progress);      // the taskbar button fills up too (gone when you stop calling it)
}

// after sui::Run returns:
if (worker.joinable()) worker.join();
```

Build the UI only on the thread that called `sui::Run`; widget calls from other threads are ignored.
From a worker you can call `Wake()`, `RequestRedraw()`, `RedrawIn()`, `Toast()`, `SetTitle()`,
`Quit()`, `SaveSettings()`, `BringToFront()`, `HideWindow()`, the dialog/message/clipboard
functions and any `sui::Log`. Share
your own data with `std::atomic` or a `std::mutex`, and call `sui::Wake()` after changing it.

## Refresh periodically (clocks, monitors)

```cpp
sui::Textf("%s", CurrentTimeString().c_str());
sui::RedrawIn(1.0f);                      // draw again in one second
```

## Remember settings between runs

```cpp
static std::string folder;
static int quality = 80;
sui::Persist("folder", folder);           // loads on the first call, saves on exit
sui::Persist("quality", quality);
sui::InputText("Folder", folder);
sui::Slider("Quality", quality, 1, 100);
```

Set `Config::appId` to choose the folder name under `%APPDATA%`.

## Ask before closing

```cpp
if (sui::CloseRequested() && hasUnsavedChanges) {
    sui::CancelClose();
    sui::OpenModal("Unsaved changes");
}
if (sui::BeginModal("Unsaved changes")) {
    sui::Text("Save before closing?");
    sui::BeginHorizontal(sui::Align::Right);
    if (sui::Button("Cancel")) sui::CloseModal();
    if (sui::Button("Don't save")) sui::Quit();
    if (sui::PrimaryButton("Save")) { Save(); sui::Quit(); }
    sui::EndHorizontal();
    sui::EndModal();
}
```

## Keyboard shortcuts

```cpp
if (sui::Shortcut("Ctrl+S")) Save();
if (sui::Shortcut("F5")) Refresh();
if (sui::GlobalHotkey("Ctrl+Alt+Space")) sui::ToggleWindow();   // works from any app, even minimized
```

Show the shortcut in a menu with `sui::MenuItem("Save", "Ctrl+S")`.

## Pick files and folders

```cpp
if (sui::Button("Open image...")) {
    std::string path = sui::OpenFileDialog("Images|*.png;*.jpg;*.bmp|All files|*.*");
    if (!path.empty()) image = sui::LoadTexture(path);
}
if (sui::Button("Export...")) {
    std::string path = sui::SaveFileDialog("CSV|*.csv", "export.csv");
    if (!path.empty()) WriteCsv(path);
}
std::string dir = sui::PickFolder("Choose the output folder");
```

## Drag and drop files

```cpp
for (const std::string& file : sui::DroppedFiles()) AddFile(file);     // anywhere on the window
for (const std::string& file : sui::DropZone("Drop images here")) AddImage(file);   // on a target
```

Files dropped onto the .exe in Explorer (or opened with it) are on the command line:

```cpp
std::vector<std::string> files = sui::CommandLine();   // before sui::Run
```

With `cfg.singleInstance = true`, files dropped onto the .exe while the tool is already running
arrive in the running window through `DroppedFiles()`.

## A tool that lives in the tray

```cpp
sui::Config cfg;
cfg.title = "Clipboard Notes";
cfg.tray = true;              // the X button hides the window next to the clock
cfg.singleInstance = true;    // starting it again shows the running one
cfg.startHidden = true;       // e.g. when it starts with Windows

return sui::Run(cfg, [&] {
    if (sui::GlobalHotkey("Ctrl+Alt+N")) sui::ToggleWindow();
    // ... the UI ...
    // While the window is hidden, Toast() shows a Windows notification instead.
});
```

Clicking the tray icon shows or hides the window; right-clicking it offers *Show/Hide* and *Exit*.
Frames keep running while the window is hidden, so hotkeys, `RedrawIn()` timers and worker
threads keep working. See the [timer example](../examples/timer/main.cpp).

## A filterable list

```cpp
static std::string filter;
sui::SearchBox("##filter", filter, "Filter...");
sui::BeginChild("items", 0);
for (int i = 0; i < (int)items.size(); i++) {
    if (!filter.empty() && items[i].find(filter) == std::string::npos) continue;
    sui::PushID(i);
    if (sui::Selectable(items[i], i == selected)) selected = i;
    if (sui::BeginContextMenu()) {
        if (sui::MenuItem("Copy")) sui::SetClipboard(items[i]);
        sui::EndContextMenu();
    }
    sui::PopID();
}
sui::EndChild();
```

## Submit a text field with Enter

```cpp
if (sui::FrameCount() == 1) sui::FocusNext();   // optional: start typing right away
sui::InputText("Command", command);
if (sui::LastItem().submitted) { Execute(command); command.clear(); }
```

## Settings page

```cpp
static int tab = 0;
sui::Tabs(tab, {"General", "Appearance", "About"});
if (tab == 0) {
    sui::BeginCard("Startup");
    sui::Toggle("Launch at login", launchAtLogin);
    sui::Toggle("Start minimized", startMinimized);
    sui::EndCard();
}
if (tab == 1) {
    static int theme = 0;
    if (sui::Segmented("Theme", theme, {"Dark", "Light", "System"}))
        sui::SetTheme(theme == 0 ? sui::Theme::Dark : theme == 1 ? sui::Theme::Light : sui::Theme::System);
}
if (tab == 2) {
    sui::LabelText("Version", "1.0");
    sui::Link("Website", "https://example.com");
}
```

## Custom drawing and custom widgets

```cpp
sui::Canvas c = sui::BeginCanvas(160);
float t = (float)sui::Time();
for (int i = 0; i < 12; i++) {
    float a = t + i * 0.52f;
    c.FillCircle(sui::Vec2(c.size.x * 0.5f + cosf(a) * 60, 80 + sinf(a) * 60), 6, sui::Hsv(i / 12.0f, 0.6f, 1));
}
if (c.hovered) c.Text(sui::Vec2(8, 8), sui::Format("%.0f, %.0f", c.mouse.x, c.mouse.y), sui::GetStyle().textDim);
sui::EndCanvas();
sui::RequestRedraw();                     // keep animating
```

## An image from memory

```cpp
// e.g. bytes generated with a bin2c tool or a resource you loaded
static sui::Texture logo = sui::LoadTextureFromMemory(logo_png, sizeof(logo_png));
sui::Image(logo, sui::Vec2(64, 64));
```

## Mixing with Win32

`sui::NativeWindow()` returns the `HWND`, and `sui::ToWide` / `sui::ToUtf8` convert strings for Win32
calls. Include `<windows.h>` in your own files as usual.
