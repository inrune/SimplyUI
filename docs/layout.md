# Layout guide

SimplyUI lays items out automatically. You describe *what* goes in the window, in order; SimplyUI
decides *where*. This page explains the rules and the tools for changing them.

## The basics: lines

Every item starts on a new line, uses the full available width (if it is a "field" like an input or
slider) or its natural width (buttons, checkboxes, text), and is separated from the next line by
`style.spacing` (8 px).

```cpp
sui::Text("One line");
sui::Button("Natural width");
sui::InputText("Name", name);        // fills the width
```

`SameLine()` puts the next item on the current line:

```cpp
sui::Button("Back");
sui::SameLine();
sui::Button("Next");
sui::SameLine(30);                   // custom gap in px
sui::Text("Step 2 of 5");            // text is centered vertically on the line automatically
```

Other line helpers:

| Function | What it does |
|---|---|
| `NewLine()` | Ends the current line (or adds an empty line). |
| `Spacing(px)` | Extra vertical space (default: `style.spacing`). |
| `Spacer(w, h)` | An empty item of a fixed size. |
| `Indent(px)` / `Unindent(px)` | Shift following items to the right / back (default `style.indent`). |
| `Separator()` | A horizontal line. |
| `SeparatorText("Title")` | A section title followed by a line. |

## The label column

Widgets with a label (`InputText`, `Slider`, `Combo`, `ColorEdit`, `LabelText`, ...) draw the label
on the left and the control on the right. All labels in the same container share one column, sized
automatically to the longest label (at most `style.labelMaxFraction` of the width):

```
Name       [________________]
Volume     [=======o--------]  0.50
Format     [PNG           v ]
```

- Hide a label but keep a unique ID with `##`: `sui::InputText("##search", query)`.
- Put labels above the controls instead: `sui::GetStyle().labelsAbove = true;`

## Widths

`NextWidth(w)` sets the width of the next item (for labeled widgets: the control part):

| Value | Meaning |
|---|---|
| `NextWidth(200)` | 200 px |
| `NextWidth(0.5f)` | 50% of the available width |
| `NextWidth(1.0f)` | 100% (full width, also for buttons) |
| `NextWidth(-80)` | everything except the last 80 px |

`AvailableWidth()` returns the space left on the current line.

## Rows: several items on one line, equal or weighted

```cpp
sui::BeginRow(3);                    // three equal cells
sui::Button("Copy");
sui::Button("Paste");
sui::Button("Clear");
sui::EndRow();

sui::BeginRow({3, 1});               // weights: 75% / 25%
sui::InputText("##path", path);
if (sui::Button("Browse")) path = sui::PickFolder();
sui::EndRow();
```

More items than cells wrap to a new row, so `BeginRow(2)` with four buttons makes a 2x2 grid.

## Columns: side-by-side stacks

```cpp
sui::BeginColumns(2);                // or BeginColumns({2, 1})
sui::Heading("Left");
sui::Checkbox("Option A", a);
sui::NextColumn();
sui::Heading("Right");
sui::Checkbox("Option B", b);
sui::EndColumns();
```

Each column has its own label column. The block is as tall as the tallest column.

## Cards

A card draws a rounded panel around its content, with an optional title:

```cpp
sui::BeginCard("Output");
sui::Toggle("Overwrite files", overwrite);
sui::InputText("Folder", folder);
sui::EndCard();
```

Cards stack vertically and work nicely inside columns.

## Horizontal groups and alignment

`BeginHorizontal()` makes the following items flow left to right at their natural size. It also
lets you align a group, which is handy for dialog buttons:

```cpp
sui::BeginHorizontal(sui::Align::Right);   // Left, Center or Right
if (sui::Button("Cancel")) Close();
if (sui::PrimaryButton("Save")) Save();
sui::EndHorizontal();
```

(Alignment uses the group's width from the previous frame, so it settles instantly.)

## Scroll areas

The window itself scrolls when the content is too tall (turn off with `Config::scroll = false`).
For a separate scrollable region, use a child:

```cpp
sui::BeginChild("files", 220);             // 220 px tall
for (int i = 0; i < (int)files.size(); i++) {
    sui::PushID(i);
    if (sui::Selectable(files[i], i == selected)) selected = i;
    sui::PopID();
}
sui::EndChild();
```

Heights: `> 0` fixed, `0` fills the rest of the window, `< 0` fills the rest except that many px
(e.g. `-50` leaves room for a row of buttons below). The same convention is used by `LogView`,
`BeginCanvas` and `BeginTable` (where `0` means "grow with the rows").

## Footer and menu bar

```cpp
if (sui::BeginMenuBar()) {           // fixed at the top
    if (sui::BeginMenu("File")) {
        if (sui::MenuItem("Open...", "Ctrl+O")) Open();
        sui::EndMenu();
    }
    sui::EndMenuBar();
}

sui::BeginFooter();                  // fixed at the bottom, the content area shrinks to fit
sui::TextDim(status);
sui::EndFooter();
```

Both can be declared anywhere in your UI function; the scrolling content area is adjusted for them.

## Disabled sections

```cpp
sui::BeginDisabled(isRunning);       // greys out and blocks everything inside
sui::Button("Start");
sui::Slider("Threads", threads, 1, 16);
sui::EndDisabled();
```

## IDs in loops

Widgets are identified by their label. When you create widgets in a loop with the same labels,
push a unique ID first:

```cpp
for (int i = 0; i < (int)items.size(); i++) {
    sui::PushID(i);
    sui::Checkbox(items[i].name, items[i].done);
    if (sui::IconButton(sui::Icon::Trash, "Delete")) remove = i;
    sui::PopID();
}
```

Or add a hidden suffix: `"Delete##" + std::to_string(i)`.
