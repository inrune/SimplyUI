# Styling

## Themes

```cpp
sui::SetTheme(sui::Theme::Dark);     // default
sui::SetTheme(sui::Theme::Light);
sui::SetTheme(sui::Theme::System);   // follows Windows and updates live when the user switches
```

Or choose it at startup with `Config::theme`. `sui::IsDark()` tells you which one is active.
The window's title bar follows the theme (dark title bar on Windows 10 1809+, matching caption color
on Windows 11).

## Accent color

```cpp
sui::SetAccent(sui::Rgb(0x10B981));  // green
sui::SetAccent(sui::SystemAccent()); // the user's Windows accent color
```

Hover and pressed shades, selection color and the text color on accent backgrounds are derived
automatically. `SetTheme` keeps a custom accent.

## Colors

All colors live in the `Style` struct and can be changed at any time:

```cpp
sui::Style& s = sui::GetStyle();
s.bg = sui::Rgb(0x101014);           // window background
s.text = sui::Rgb(0xF5F5F5);
```

| Field | Used for |
|---|---|
| `bg` | Window background |
| `panel` | Cards, menu bar, footer, collapsing headers |
| `popup` | Popups, menus, modals, toasts |
| `field` | Text inputs, list boxes, plots, scroll areas |
| `frame`, `frameHover`, `frameActive` | Buttons and other frames in their three states |
| `border`, `borderStrong` | Thin outlines; stronger outline for checkboxes/toggles |
| `separator` | Separator lines |
| `text`, `textDim`, `textDisabled` | Text colors |
| `accent`, `accentHover`, `accentActive`, `accentText` | Accent and the text drawn on it |
| `success`, `warning`, `danger` | Status colors (toasts, badges, log levels) |
| `selection` | Text selection |
| `scrollbar`, `shadow`, `overlay`, `tooltip` | Misc |

`SetTheme()` resets the colors, so apply your own after calling it.

Color helpers: `Rgb(0xRRGGBB)`, `Rgba(0xRRGGBBAA)`, `Color(r, g, b, a)` (0..1 floats),
`Hsv(h, s, v)`, `Mix(a, b, t)`, `c.Lighter(t)`, `c.Darker(t)`, `c.WithAlpha(a)`, `ToHex(c)`, `ParseHex("#ff8800", c)`.

## Sizes

```cpp
sui::Style& s = sui::GetStyle();
s.fontSize = 15;        // all built-in fonts scale with this
s.frameHeight = 32;     // buttons, inputs, sliders
s.rowHeight = 28;       // list rows, table rows, menu items
s.rounding = 8;         // corner radius of frames
s.cardRounding = 10;
s.padding = 16;         // window padding
s.spacing = 8;          // vertical gap between lines
s.spacingX = 8;         // horizontal gap between items on one line
s.labelsAbove = true;   // labels above controls
s.animations = false;   // turn off all animations
```

All sizes are in DIPs (1/96 inch), so they scale with the monitor's DPI automatically.

## Fonts

SimplyUI uses Segoe UI (falling back to Tahoma/Arial) and Cascadia Mono/Consolas for monospace.
Built-in font slots:

| Font | Used by |
|---|---|
| `FontBody` | Normal text and widgets |
| `FontBold` | Card titles, table headers, collapsing headers |
| `FontSmall` | Badges, plot labels |
| `FontHeading` | `Heading()` |
| `FontTitle` | `Title()` |
| `FontMono` | Log view, color hex values |

```cpp
sui::PushFont(sui::FontMono);
sui::Text("0x7FF6A1B0");
sui::PopFont();

sui::GetStyle().fontFamily = "Bahnschrift";              // change the UI font (empty = Segoe UI)
sui::FontId big = sui::AddFont("Segoe UI", 32, true);    // extra fonts
sui::PushFont(big); sui::Text("42"); sui::PopFont();
```

Emoji and CJK text work through DirectWrite's font fallback (color emoji on Windows 8.1+).

## Icons

55 vector icons are built in (`sui::Icon::Check`, `Folder`, `Trash`, `Settings`, `Search`,
`Play`, ...; see the `Icon` enum in the header). Use them in `IconButton`, `Button(label, icon)`
or draw them anywhere with `Canvas::IconAt`.
