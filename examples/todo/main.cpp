// To-do list that remembers your tasks between runs.
// Shows: Persist(), Enter-to-submit text fields, FocusNext(), segmented filters,
//        icon buttons, keyboard shortcuts, context menus and a confirmation modal.
#include "simplyui.h"

struct Task {
    std::string text;
    bool done;
};

// Tasks are stored as one string: "1|Buy milk\n0|Write code\n"
static std::string Save(const std::vector<Task>& tasks) {
    std::string s;
    for (const Task& t : tasks) s += (t.done ? "1|" : "0|") + t.text + "\n";
    return s;
}
static std::vector<Task> Load(const std::string& s) {
    std::vector<Task> tasks;
    size_t pos = 0;
    while (pos < s.size()) {
        size_t end = s.find('\n', pos);
        if (end == std::string::npos) end = s.size();
        std::string line = s.substr(pos, end - pos);
        if (line.size() >= 2 && line[1] == '|') tasks.push_back({line.substr(2), line[0] == '1'});
        pos = end + 1;
    }
    return tasks;
}

int main() {
    std::string stored = "0|Try SimplyUI\n0|Build a quick tool\n1|Read the README\n";
    std::vector<Task> tasks;
    std::string input;
    int filter = 0;
    bool loaded = false;

    sui::Config cfg;
    cfg.title = "To-do";
    cfg.width = 440;
    cfg.height = 560;
    cfg.rememberWindow = true;

    return sui::Run(cfg, [&] {
        sui::Persist("tasks", stored);              // loads once, saved on exit
        if (!loaded) { tasks = Load(stored); loaded = true; }

        int open = 0;
        for (const Task& t : tasks) open += t.done ? 0 : 1;
        sui::Title("To-do");
        sui::TextDim(sui::Format("%d open, %d done", open, (int)tasks.size() - open));
        sui::Spacing(4);

        // Add a task: type and press Enter (or click the button).
        sui::BeginRow({5, 1});
        bool add = false;
        if (sui::FrameCount() == 1) sui::FocusNext();   // ready to type as soon as the window opens
        sui::InputText("##new", input, "What needs to be done?");
        if (sui::LastItem().submitted) add = true;
        if (sui::PrimaryButton("Add")) add = true;
        sui::EndRow();
        if (add && !input.empty()) {
            tasks.push_back({input, false});
            input.clear();
        }

        sui::Segmented("##filter", filter, {"All", "Open", "Done"});
        sui::Spacing(4);

        int removeIndex = -1;
        sui::BeginChild("list", -48, false);
        for (int i = 0; i < (int)tasks.size(); i++) {
            Task& t = tasks[i];
            if ((filter == 1 && t.done) || (filter == 2 && !t.done)) continue;
            sui::PushID(i);
            sui::BeginRow({10, 1});
            sui::Checkbox(t.text, t.done);
            if (sui::BeginContextMenu()) {
                if (sui::MenuItem(t.done ? "Mark as open" : "Mark as done")) t.done = !t.done;
                if (sui::MenuItem("Copy text")) sui::SetClipboard(t.text);
                sui::MenuSeparator();
                if (sui::MenuItem("Delete")) removeIndex = i;
                sui::EndContextMenu();
            }
            if (sui::IconButton(sui::Icon::Trash, "Delete")) removeIndex = i;
            sui::EndRow();
            sui::PopID();
        }
        if (tasks.empty()) sui::TextDim("Nothing to do. Enjoy your day!");
        sui::EndChild();
        if (removeIndex >= 0) tasks.erase(tasks.begin() + removeIndex);

        sui::BeginHorizontal(sui::Align::Right);
        sui::BeginDisabled(open == (int)tasks.size());
        if (sui::Button("Clear completed")) sui::OpenModal("Clear completed tasks?");
        sui::EndDisabled();
        sui::EndHorizontal();

        if (sui::BeginModal("Clear completed tasks?")) {
            sui::TextWrapped("Completed tasks will be removed. This cannot be undone.");
            sui::Spacing();
            sui::BeginHorizontal(sui::Align::Right);
            if (sui::Button("Cancel")) sui::CloseModal();
            if (sui::DangerButton("Remove")) {
                std::vector<Task> keep;
                for (const Task& t : tasks) if (!t.done) keep.push_back(t);
                tasks = keep;
                sui::CloseModal();
            }
            sui::EndHorizontal();
            sui::EndModal();
        }

        if (sui::Shortcut("Ctrl+Shift+Delete")) sui::OpenModal("Clear completed tasks?");
        stored = Save(tasks);                       // Persist() writes this value on exit
    });
}
