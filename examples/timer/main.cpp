// Focus Timer - a countdown that lives next to the clock in the notification area.
// Shows: tray icon (Config::tray), single instance, a global hotkey with ToggleWindow(),
//        taskbar progress, RedrawIn() timers that keep running while the window is
//        hidden, Windows notifications from Toast(), FocusNext() and AddFont().
#include "simplyui.h"

#include <cmath>

int main() {
    int minutes = 25;             // length of a session
    std::string task;             // what the session is for
    bool showWhenDone = true;     // bring the window back when the time is up
    double endsAt = 0;            // > 0 while running (in sui::Time() seconds)
    double pausedLeft = 0;        // > 0 while paused

    sui::Config cfg;
    cfg.title = "Focus Timer";
    cfg.width = 400;
    cfg.height = 380;
    cfg.resizable = false;
    cfg.rememberWindow = true;
    cfg.tray = true;              // the X button hides the timer in the notification area
    cfg.singleInstance = true;    // starting it again just shows this window

    return sui::Run(cfg, [&] {
        sui::Persist("minutes", minutes);
        sui::Persist("task", task);
        sui::Persist("showWhenDone", showWhenDone);
        if (sui::GlobalHotkey("Ctrl+Alt+T")) sui::ToggleWindow();

        if (minutes < 1 || minutes > 90) minutes = 25;   // (a hand-edited settings file)
        const double now = sui::Time();
        const double total = minutes * 60.0;
        bool running = endsAt > 0, paused = pausedLeft > 0;
        double left = running ? endsAt - now : (paused ? pausedLeft : total);

        if (running && left <= 0) {            // time is up
            endsAt = 0;
            running = false;
            left = total;
            sui::Beep(sui::Level::Success);
            // A toast in the window, or a Windows notification while it is hidden in the tray.
            sui::Toast(task.empty() ? "Time is up!" : "Time is up: " + task, sui::Level::Success, 8);
            if (showWhenDone) sui::BringToFront();
        }
        if (running) sui::RedrawIn(0.25f);     // keeps ticking while minimized or hidden too

        // The clock
        const sui::FontId clockFont = sui::AddFont("Segoe UI Light", 72);   // same font = same id
        const int secs = (int)std::ceil(left);
        sui::Spacing(6);
        sui::BeginHorizontal(sui::Align::Center);
        sui::PushFont(clockFont);
        sui::Text(sui::Format("%02d:%02d", secs / 60, secs % 60));
        sui::PopFont();
        sui::EndHorizontal();

        const float done = (float)(1.0 - left / total);
        sui::ProgressBar(running || paused ? done : 0.0f);
        if (running) sui::TaskbarProgress(done);                         // green on the taskbar
        else if (paused) sui::TaskbarProgress(done, sui::Level::Warning); // yellow while paused
        sui::Spacing(6);

        // What and how long (locked while a session runs)
        sui::BeginDisabled(running || paused);
        if (sui::FrameCount() == 1) sui::FocusNext();   // type right away
        sui::InputText("Task", task, "What are you working on?");
        bool startNow = sui::LastItem().submitted;      // Enter starts the timer
        sui::Slider("Minutes", minutes, 1, 90, "%d min");
        sui::EndDisabled();
        sui::Checkbox("Show the window when the time is up", showWhenDone);
        sui::Spacing(6);

        sui::BeginRow(2);
        if (running) {
            if (sui::Button("Pause", sui::Icon::Pause)) { pausedLeft = endsAt - now; endsAt = 0; }
        } else if (sui::PrimaryButton(paused ? "Resume" : "Start") || startNow) {
            endsAt = now + (paused ? pausedLeft : total);
            pausedLeft = 0;
        }
        sui::BeginDisabled(!running && !paused);
        if (sui::Button("Reset", sui::Icon::Refresh)) { endsAt = 0; pausedLeft = 0; }
        sui::EndDisabled();
        sui::EndRow();

        sui::BeginFooter();
        sui::PushFont(sui::FontSmall);
        sui::TextDim("Ctrl+Alt+T shows or hides the timer. Closing keeps it in the tray.");
        sui::PopFont();
        sui::EndFooter();
    });
}
