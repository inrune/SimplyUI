// System Monitor - live CPU / memory / disk usage in a small always-on-top window.
// Shows: plots, timed redraws (RedrawIn), always-on-top, global hotkeys, badges.
#include "simplyui.h"

#include <windows.h>   // GetSystemTimes, GlobalMemoryStatusEx, GetDiskFreeSpaceEx

static unsigned long long FileTimeToU64(const FILETIME& ft) {
    return ((unsigned long long)ft.dwHighDateTime << 32) | ft.dwLowDateTime;
}

struct Drive {
    std::string root;
    double totalGb = 0, freeGb = 0;
};

int main() {
    std::vector<float> cpu(60, 0.0f), mem(60, 0.0f);
    unsigned long long lastIdle = 0, lastTotal = 0;
    double nextSample = 0;
    bool onTop = false, paused = false;
    float interval = 1.0f;
    MEMORYSTATUSEX ms = {};
    ms.dwLength = sizeof(ms);
    std::vector<Drive> drives;

    auto sample = [&] {
        FILETIME idle, kernel, user;
        if (GetSystemTimes(&idle, &kernel, &user)) {
            unsigned long long i = FileTimeToU64(idle), t = FileTimeToU64(kernel) + FileTimeToU64(user);
            if (lastTotal) {
                double busy = 1.0 - double(i - lastIdle) / double(t - lastTotal > 0 ? t - lastTotal : 1);
                cpu.erase(cpu.begin());
                cpu.push_back((float)(busy < 0 ? 0 : busy > 1 ? 100 : busy * 100));
            }
            lastIdle = i;
            lastTotal = t;
        }
        GlobalMemoryStatusEx(&ms);
        mem.erase(mem.begin());
        mem.push_back((float)ms.dwMemoryLoad);
        drives.clear();
        DWORD mask = GetLogicalDrives();
        for (int d = 0; d < 26; d++) {
            if (!(mask & (1u << d))) continue;
            wchar_t root[4] = {(wchar_t)(L'A' + d), L':', L'\\', 0};
            if (GetDriveTypeW(root) != DRIVE_FIXED) continue;
            ULARGE_INTEGER freeBytes, total;
            if (GetDiskFreeSpaceExW(root, &freeBytes, &total, nullptr)) {
                Drive dr;
                dr.root = std::string(1, (char)('A' + d)) + ":";
                dr.totalGb = total.QuadPart / 1073741824.0;
                dr.freeGb = freeBytes.QuadPart / 1073741824.0;
                drives.push_back(dr);
            }
        }
    };
    sample();

    sui::Config cfg;
    cfg.title = "System Monitor";
    cfg.width = 380;
    cfg.height = 560;
    cfg.rememberWindow = true;

    return sui::Run(cfg, [&] {
        sui::Persist("topmost", onTop);
        sui::Persist("interval", interval);
        static bool applied = false;
        if (!applied) { sui::SetTopmost(onTop); applied = true; }

        // Sample on a timer. The window only redraws when needed, so this costs nothing in between.
        if (!paused && sui::Time() >= nextSample) {
            sample();
            nextSample = sui::Time() + interval;
        }
        if (!paused) sui::RedrawIn((float)(nextSample - sui::Time()));

        // Ctrl+Shift+M anywhere in Windows brings the monitor to the front.
        if (sui::GlobalHotkey("Ctrl+Shift+M")) sui::BringToFront();

        float load = cpu.back();
        sui::BeginRow({1, 1});
        sui::Title(sui::Format("%.0f%%", load));
        sui::Title(sui::Format("%lu%%", (unsigned long)ms.dwMemoryLoad));
        sui::TextDim("CPU");
        sui::TextDim("Memory");
        sui::EndRow();

        sui::Badge(load > 80 ? "Busy" : load > 40 ? "Working" : "Relaxed",
                   load > 80 ? sui::Level::Error : load > 40 ? sui::Level::Warning : sui::Level::Success);
        sui::Spacing(4);

        sui::PlotLines("CPU %", cpu, 90, 0, 100);
        sui::PlotLines("Memory %", mem, 90, 0, 100);
        sui::LabelText("Physical memory", sui::Format("%.1f / %.1f GB",
            (ms.ullTotalPhys - ms.ullAvailPhys) / 1073741824.0, ms.ullTotalPhys / 1073741824.0));

        sui::SeparatorText("Drives");
        for (const Drive& d : drives) {
            float used = d.totalGb > 0 ? (float)(1.0 - d.freeGb / d.totalGb) : 0.0f;
            sui::ProgressBar(used, sui::Format("%s  %.0f GB free", d.root.c_str(), d.freeGb));
        }

        sui::BeginFooter();
        sui::BeginHorizontal();
        if (sui::Toggle("On top", onTop)) sui::SetTopmost(onTop);
        if (sui::IconButton(paused ? sui::Icon::Play : sui::Icon::Pause, paused ? "Resume" : "Pause")) paused = !paused;
        static const float rates[] = {0.5f, 1.0f, 2.0f, 5.0f};
        int rate = 1;
        for (int i = 0; i < 4; i++)
            if (rates[i] == interval) rate = i;
        sui::NextWidth(90);
        if (sui::Combo("##rate", rate, {"0.5 s", "1 s", "2 s", "5 s"})) {
            interval = rates[rate];
            nextSample = 0;
        }
        sui::Tooltip("Refresh rate");
        sui::EndHorizontal();
        sui::EndFooter();
    });
}
