// SimplyUI showcase - every widget in one window.
// Build: g++ -O2 main.cpp -I../../include -o showcase.exe -mwindows   (run it with simplyui.dll next to it)
#include "simplyui.h"

#include <algorithm>
#include <cmath>

struct FileRow {
    std::string name;
    int sizeKb;
    std::string modified;
};

int main() {
    // ---- state lives in plain variables ------------------------------------------
    int tab = 0;
    bool check = true, notify = true, autoUpdate = false, darkMenu = true;
    int radio = 1;
    float opacity = 0.65f, speed = 1.5f, price = 9.99f, progress = 0.0f;
    int count = 42, quantity = 3;
    std::string name = "Ada Lovelace", password = "hunter2", search;
    std::string notes = "Multi-line editing with selection,\nundo/redo, clipboard and word navigation.";
    int format = 1, quality = 1, fruit = 0, themeIdx = 0, selectedRow = 1;
    sui::Color accent = sui::Rgb(0x4C8DFF), overlay = sui::Rgba(0xF0B34199);
    std::vector<std::string> fruits = {"Apple", "Banana", "Cherry", "Dragon fruit", "Elderberry", "Fig", "Grape"};
    std::vector<FileRow> files = {
        {"report.pdf", 2048, "2026-11-02"}, {"photo.png", 5120, "2026-10-28"}, {"notes.txt", 4, "2026-11-09"},
        {"song.mp3", 7340, "2026-09-15"}, {"tool.exe", 860, "2026-11-10"}, {"data.csv", 312, "2026-11-01"},
        {"archive.zip", 12800, "2026-08-21"}, {"readme.md", 9, "2026-11-08"}};
    std::vector<std::string> dropped;
    std::vector<sui::Vec2> stroke;
    std::vector<std::vector<sui::Vec2>> strokes;
    sui::Log log;
    log.Info("SimplyUI started");
    log.Success("Everything is fine");
    log.Warn("This is a warning");
    log.Error("And this is an error");
    int clicks = 0;

    sui::Config cfg;
    cfg.title = "SimplyUI Showcase";
    cfg.width = 780;
    cfg.height = 640;
    cfg.appId = "SimplyUI Showcase";

    return sui::Run(cfg, [&] {
        sui::Persist("clicks", clicks);

        // ---- menu bar --------------------------------------------------------------
        if (sui::BeginMenuBar()) {
            if (sui::BeginMenu("File")) {
                if (sui::MenuItem("New", "Ctrl+N")) sui::Toast("New file");
                if (sui::MenuItem("Open...", "Ctrl+O")) {
                    std::string path = sui::OpenFileDialog("All files|*.*");
                    if (!path.empty()) log.Info("Opened %s", path.c_str());
                }
                sui::MenuSeparator();
                if (sui::BeginMenu("Recent")) {
                    sui::MenuItem("project.json");
                    sui::MenuItem("settings.ini");
                    sui::EndMenu();
                }
                sui::MenuSeparator();
                if (sui::MenuItem("Exit", "Alt+F4")) sui::Quit();
                sui::EndMenu();
            }
            if (sui::BeginMenu("View")) {
                if (sui::MenuItem("Dark theme", "", &darkMenu)) sui::SetTheme(darkMenu ? sui::Theme::Dark : sui::Theme::Light);
                sui::MenuItem("Disabled item", "", false, false);
                sui::EndMenu();
            }
            if (sui::BeginMenu("Help")) {
                if (sui::MenuItem("About")) sui::OpenModal("About SimplyUI");
                sui::EndMenu();
            }
            sui::EndMenuBar();
        }
        if (sui::Shortcut("Ctrl+N")) sui::Toast("New file (Ctrl+N)");

        sui::Tabs(tab, {"Basics", "Inputs", "Data", "Layout", "Extras"});
        sui::Spacing(2);

        // ---- Basics ------------------------------------------------------------------
        if (tab == 0) {
            sui::BeginColumns(2);
            sui::BeginCard("Buttons");
            if (sui::Button("Button")) clicks++;
            sui::SameLine();
            if (sui::PrimaryButton("Primary")) clicks++;
            sui::SameLine();
            if (sui::DangerButton("Delete")) sui::Toast("Deleted (not really)", sui::Level::Warning);
            if (sui::Button("Save", sui::Icon::Save)) sui::Toast("Saved!", sui::Level::Success);
            sui::SameLine();
            sui::IconButton(sui::Icon::Settings, "Settings");
            sui::SameLine();
            sui::IconButton(sui::Icon::Trash, "Delete");
            sui::SameLine();
            sui::IconButton(sui::Icon::Refresh, "Refresh");
            sui::Textf("Clicked %d times (remembered after restart)", clicks);
            sui::Link("Visit the SimplyUI repository", "https://github.com/");
            sui::EndCard();

            sui::BeginCard("Toggles");
            sui::Checkbox("Checkbox", check);
            sui::Toggle("Notifications", notify);
            sui::Toggle("Automatic updates", autoUpdate);
            sui::RadioButton("Small", radio, 0);
            sui::SameLine();
            sui::RadioButton("Medium", radio, 1);
            sui::SameLine();
            sui::RadioButton("Large", radio, 2);
            sui::EndCard();

            sui::NextColumn();
            sui::BeginCard("Text");
            sui::Heading("Heading");
            sui::Text("Regular text with emoji support");
            sui::TextDim("Dimmed secondary text");
            sui::TextColored(sui::GetStyle().success, "Colored text");
            sui::BulletText("Bullet point");
            sui::LabelText("Version", SIMPLYUI_VERSION);
            sui::Badge("New", sui::Level::Info);
            sui::SameLine();
            sui::Badge("Stable", sui::Level::Success);
            sui::SameLine();
            sui::Badge("Beta", sui::Level::Warning);
            sui::SameLine();
            sui::Badge("Failed", sui::Level::Error);
            sui::EndCard();

            sui::BeginCard("Progress");
            progress += sui::DeltaTime() * 0.15f;
            if (progress > 1.0f) progress = 0.0f;
            sui::ProgressBar(progress);
            sui::ProgressBar(-1.0f, "Waiting");
            sui::Spinner();
            sui::SameLine();
            sui::TextDim("Working on something...");
            sui::EndCard();
            sui::EndColumns();
        }

        // ---- Inputs ------------------------------------------------------------------
        if (tab == 1) {
            sui::BeginColumns(2);
            sui::BeginCard("Text input");
            sui::InputText("Name", name, "Your name");
            sui::InputText("Password", password, "", sui::InputPassword);
            sui::SearchBox("##search", search, "Search files...");
            sui::InputMultiline("Notes", notes, 96);
            sui::EndCard();
            sui::BeginCard("Choices");
            sui::Combo("Format", format, {"PNG", "JPEG", "WebP", "BMP", "GIF"});
            sui::Segmented("Quality", quality, {"Low", "Medium", "High"});
            sui::ListBox("Fruit", fruit, fruits, 4);
            sui::EndCard();
            sui::NextColumn();
            sui::BeginCard("Numbers");
            sui::Slider("Opacity", opacity, 0.0f, 1.0f);
            sui::Slider("Count", count, 0, 100);
            sui::Drag("Speed", speed, 0.01f, 0.0f, 10.0f, "%.2f x");
            sui::InputInt("Quantity", quantity, 1, 0, 99);
            sui::InputFloat("Price", price, 0.5f, 0.0f, 1000.0f, "%.2f");
            sui::TextDim("Tip: double-click a slider to type a value");
            sui::EndCard();
            sui::BeginCard("Colors");
            if (sui::ColorEdit("Accent", accent)) sui::SetAccent(accent);
            sui::ColorEdit("Overlay", overlay, true);
            sui::EndCard();
            sui::EndColumns();
        }

        // ---- Data --------------------------------------------------------------------
        if (tab == 2) {
            static float wave[96];
            double t = sui::Time();
            for (int i = 0; i < 96; i++)
                wave[i] = (float)(std::sin(t * 2.0 + i * 0.18) * 0.6 + std::sin(t * 0.7 + i * 0.05) * 0.4);
            static const float bars[] = {3, 7, 4, 9, 12, 8, 5, 10, 14, 9, 6, 11};
            sui::BeginColumns({3, 2});
            sui::PlotLines("Signal", wave, 96, 110, -1.1f, 1.1f);
            sui::NextColumn();
            sui::PlotBars("Downloads per month", bars, 12, 110);
            sui::EndColumns();
            sui::RedrawIn(1.0f / 30.0f);   // keep the plot animating

            if (sui::BeginTable("files", {"Name", "Size", "Modified"}, sui::TableStriped | sui::TableSortable | sui::TableResizable, 230)) {
                sui::TableSort sort = sui::TableSorting();
                if (sort.changed) {
                    std::sort(files.begin(), files.end(), [&](const FileRow& a, const FileRow& b) {
                        bool less = sort.column == 0 ? a.name < b.name : sort.column == 1 ? a.sizeKb < b.sizeKb : a.modified < b.modified;
                        return sort.ascending ? less : !less && !(a.name == b.name);
                    });
                }
                for (int i = 0; i < (int)files.size(); i++) {
                    if (sui::TableRow({files[i].name, sui::Format("%d KB", files[i].sizeKb), files[i].modified}, i == selectedRow))
                        selectedRow = i;
                }
                sui::EndTable();
            }
            sui::BeginRow({1, 1, 1, 1});
            if (sui::Button("Add info")) log.Info("Info message #%zu", log.Count());
            if (sui::Button("Add success")) log.Success("It worked");
            if (sui::Button("Add warning")) log.Warn("Careful");
            if (sui::Button("Clear log")) log.Clear();
            sui::EndRow();
            sui::LogView("log", log, 0);
        }

        // ---- Layout ------------------------------------------------------------------
        if (tab == 3) {
            sui::SeparatorText("Rows share a line");
            sui::BeginRow(3);
            sui::Button("One");
            sui::Button("Two");
            sui::Button("Three");
            sui::EndRow();
            sui::BeginRow({3, 1});
            sui::InputText("##path", name, "Path");
            sui::Button("Browse", sui::Icon::Folder);
            sui::EndRow();

            sui::SeparatorText("Widths");
            sui::NextWidth(0.5f);
            sui::Button("Half width");
            sui::NextWidth(1.0f);
            sui::PrimaryButton("Full width");

            sui::SeparatorText("Collapsing header + tree");
            if (sui::CollapsingHeader("Project", true)) {
                if (sui::TreeNode("src", sui::TreeDefaultOpen)) {
                    sui::TreeNode("main.cpp", sui::TreeLeaf | sui::TreeSelected);
                    sui::TreeNode("simplyui.h", sui::TreeLeaf);
                    sui::TreePop();
                }
                if (sui::TreeNode("docs")) {
                    sui::TreeNode("README.md", sui::TreeLeaf);
                    sui::TreePop();
                }
            }
            sui::SeparatorText("Scroll area");
            sui::BeginChild("scroller", 120);
            for (int i = 0; i < 30; i++) {
                sui::PushID(i);
                sui::Selectable(sui::Format("Item %d", i + 1), i == 4);
                sui::PopID();
            }
            sui::EndChild();
            sui::SeparatorText("Disabled");
            sui::BeginDisabled();
            sui::Button("Can't click me");
            sui::SameLine();
            sui::Checkbox("Locked", check);
            sui::EndDisabled();
            sui::BeginHorizontal(sui::Align::Right);
            sui::Button("Cancel");
            sui::PrimaryButton("Apply");
            sui::EndHorizontal();
        }

        // ---- Extras ------------------------------------------------------------------
        if (tab == 4) {
            sui::BeginColumns(2);
            sui::BeginCard("Theme");
            if (sui::Segmented("Theme", themeIdx, {"Dark", "Light", "System"}))
                sui::SetTheme(themeIdx == 0 ? sui::Theme::Dark : themeIdx == 1 ? sui::Theme::Light : sui::Theme::System);
            const uint32_t swatches[] = {0x4C8DFF, 0x8B5CF6, 0x10B981, 0xF59E0B, 0xEF4444, 0xEC4899};
            sui::BeginRow(6);
            for (int i = 0; i < 6; i++) {
                sui::PushID(i);
                sui::Canvas c = sui::BeginCanvas(28, false);
                c.FillRect(sui::Rect(2, 2, c.size.x - 4, 24), sui::Rgb(swatches[i]), 6);
                if (c.hovered) c.StrokeRect(sui::Rect(0, 0, c.size.x, 28), sui::GetStyle().text, 7, 2);
                if (c.clicked) { accent = sui::Rgb(swatches[i]); sui::SetAccent(accent); }
                sui::EndCanvas();
                sui::PopID();
            }
            sui::EndRow();
            sui::EndCard();

            sui::BeginCard("Notifications");
            sui::BeginRow(2);
            if (sui::Button("Info")) sui::Toast("Just so you know", sui::Level::Info);
            if (sui::Button("Success")) sui::Toast("File saved", sui::Level::Success);
            if (sui::Button("Warning")) sui::Toast("Disk almost full", sui::Level::Warning);
            if (sui::Button("Error")) sui::Toast("Could not connect", sui::Level::Error);
            sui::EndRow();
            if (sui::Button("Open modal")) sui::OpenModal("About SimplyUI");
            sui::SameLine();
            if (sui::Button("Message box")) sui::ShowMessage("SimplyUI", "A native message box.");
            sui::EndCard();
            sui::NextColumn();

            sui::BeginCard("Canvas (draw with the mouse)");
            sui::Canvas c = sui::BeginCanvas(150);
            if (c.down) stroke.push_back(c.mouse);
            if (c.released && !stroke.empty()) { strokes.push_back(stroke); stroke.clear(); }
            if (c.rightClicked) strokes.clear();
            for (auto& s : strokes) c.Polyline(s, sui::GetStyle().accent, 3);
            c.Polyline(stroke, sui::GetStyle().accent, 3);
            if (strokes.empty() && stroke.empty())
                c.Text(sui::Vec2(12, 10), "Drag to draw, right-click to clear", sui::GetStyle().textDim);
            sui::EndCanvas();
            sui::EndCard();

            sui::BeginCard("Files");
            std::vector<std::string> got = sui::DropZone("Drop files here", 86);
            if (!got.empty()) dropped = got;
            for (auto& f : dropped) sui::BulletText(f);
            sui::EndCard();
            sui::EndColumns();
        }

        // ---- modal ---------------------------------------------------------------------
        if (sui::BeginModal("About SimplyUI")) {
            sui::TextWrapped("SimplyUI is a tiny immediate-mode GUI library for quick Windows tools. "
                             "Just a header and a DLL, crisp DirectWrite text and smooth animations.");
            sui::Spacing();
            sui::BeginHorizontal(sui::Align::Right);
            if (sui::PrimaryButton("Close")) sui::CloseModal();
            sui::EndHorizontal();
            sui::EndModal();
        }

        // ---- footer ----------------------------------------------------------------------
        sui::BeginFooter();
        sui::TextDim(sui::Format("SimplyUI %s  |  one header + one DLL  |  0%% CPU when idle", SIMPLYUI_VERSION));
        sui::EndFooter();
    });
}
