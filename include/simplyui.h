/*
  SimplyUI v1.0.0 - a small, bloat-free immediate-mode GUI library for Windows tools.
  Documentation: README.md and docs/ in the SimplyUI repository.

  ---------------------------------------------------------------------------------
  SETUP - two files, nothing to configure
  ---------------------------------------------------------------------------------
    1. Copy simplyui.h and simplyui.dll into your project folder.
    2. #include "simplyui.h"

  That's all: no .lib to link, no #define, no project settings. This header loads
  simplyui.dll by itself the first time your program uses SimplyUI. When you give
  your program to someone, put simplyui.dll next to the .exe.

  32-bit programs use simplyui32.dll instead (both DLLs can sit in the same folder).
  Works with any C++11 (or newer) compiler: Visual Studio, MinGW-w64, clang.

  ---------------------------------------------------------------------------------
  HELLO WORLD
  ---------------------------------------------------------------------------------
      #include "simplyui.h"

      int main() {
          std::string name = "World";
          float volume = 0.5f;
          return sui::Run("My Tool", 420, 300, [&] {
              sui::Heading("Hello!");
              sui::InputText("Name", name);
              sui::Slider("Volume", volume, 0.0f, 1.0f);
              if (sui::PrimaryButton("Greet"))
                  sui::Toast("Hello, " + name + "!");
          });
      }

  No console window: put  #define SIMPLYUI_NO_CONSOLE  above the #include
  (Visual Studio), or build with -mwindows (MinGW).

  ---------------------------------------------------------------------------------
  HOW IT WORKS (immediate mode)
  ---------------------------------------------------------------------------------
  Your lambda describes the whole UI and runs every time the window needs to be
  redrawn. Widgets take references to your own variables and return true when the
  user changed or clicked them. There are no widget objects to create, store or
  delete. SimplyUI only redraws on input, animation or when you ask for it, so an
  idle tool uses 0% CPU.

  Labels: text after "##" is hidden but still part of the ID, so "OK##1" and
  "OK##2" are two different buttons both showing "OK", and "##search" has no label.
  Use PushID()/PopID() when creating widgets in a loop.

  Threads: build the UI on the thread that called Run(). Other threads may call
  Wake(), RequestRedraw(), RedrawIn(), Toast(), SetTitle(), Quit(), SaveSettings(),
  BringToFront(), HideWindow(), the dialog/message/clipboard functions and any
  sui::Log; other calls coming from other threads are ignored.

  Mistakes don't crash: a missing End*() or Pop*() is fixed at the end of the frame,
  bad numbers (NaN, negative sizes) are ignored, and the Visual Studio Output window
  shows a "[SimplyUI]" note about what went wrong.

  License: MIT (see the end of this file).
*/

#ifndef SIMPLYUI_H
#define SIMPLYUI_H

// Version (keep the string and the numbers in sync)
#define SIMPLYUI_VERSION "1.0.0"
#define SIMPLYUI_VERSION_MAJOR 1
#define SIMPLYUI_VERSION_MINOR 0
#define SIMPLYUI_VERSION_PATCH 0
#define SIMPLYUI_VERSION_NUM (SIMPLYUI_VERSION_MAJOR * 10000 + SIMPLYUI_VERSION_MINOR * 100 + SIMPLYUI_VERSION_PATCH)

#ifndef RC_INVOKED                // (the resource compiler only needs the version above)

#ifndef _WIN32
#error "SimplyUI is a Windows library."
#endif

#include <cstdint>
#include <cstddef>
#include <cstring>
#include <cstdio>
#include <cstdarg>
#include <cstdlib>
#include <cfloat>
#include <climits>
#include <cmath>
#include <string>
#include <vector>
#include <functional>
#include <initializer_list>
#include <exception>

#if defined(__MINGW32__) && defined(__MINGW_PRINTF_FORMAT)
#define SUI_PRINTF(fmt, args) __attribute__((format(__MINGW_PRINTF_FORMAT, fmt, args)))
#elif defined(__GNUC__) || defined(__clang__)
#define SUI_PRINTF(fmt, args) __attribute__((format(printf, fmt, args)))
#else
#define SUI_PRINTF(fmt, args)
#endif

#define SUI_CALL __cdecl          // calling convention of the DLL interface

#ifdef SIMPLYUI_BUILDING_DLL
#define SUI_API                   // implemented in src/simplyui.cpp (the DLL)
#else
#define SUI_API inline            // implemented at the end of this header (calls the DLL)
#endif

#pragma pack(push, 8)             // same struct layout as the DLL, whatever your settings

namespace sui {

// =================================================================================
// Basic types
// =================================================================================

typedef uint32_t ID;
typedef int FontId;

struct Vec2 {
    float x, y;
    constexpr Vec2() : x(0), y(0) {}
    constexpr Vec2(float x_, float y_) : x(x_), y(y_) {}
};
inline Vec2 operator+(Vec2 a, Vec2 b) { return Vec2(a.x + b.x, a.y + b.y); }
inline Vec2 operator-(Vec2 a, Vec2 b) { return Vec2(a.x - b.x, a.y - b.y); }
inline Vec2 operator*(Vec2 a, float s) { return Vec2(a.x * s, a.y * s); }

struct Rect {
    float x, y, w, h;
    constexpr Rect() : x(0), y(0), w(0), h(0) {}
    constexpr Rect(float x_, float y_, float w_, float h_) : x(x_), y(y_), w(w_), h(h_) {}
    float Right() const { return x + w; }
    float Bottom() const { return y + h; }
    Vec2 Pos() const { return Vec2(x, y); }
    Vec2 Size() const { return Vec2(w, h); }
    Vec2 Center() const { return Vec2(x + w * 0.5f, y + h * 0.5f); }
    bool Contains(Vec2 p) const { return p.x >= x && p.y >= y && p.x < x + w && p.y < y + h; }
    Rect Shrink(float d) const { return Rect(x + d, y + d, w - d * 2, h - d * 2); }
    Rect Expand(float d) const { return Rect(x - d, y - d, w + d * 2, h + d * 2); }
};

// Colors are linear 0..1 floats.  Rgb(0x4C8DFF), Rgba(0x4C8DFF80), Color(1, 0.5f, 0)
struct Color {
    float r, g, b, a;
    constexpr Color() : r(0), g(0), b(0), a(1) {}
    constexpr Color(float r_, float g_, float b_, float a_ = 1.0f) : r(r_), g(g_), b(b_), a(a_) {}
    Color WithAlpha(float alpha) const { return Color(r, g, b, alpha); }
    Color Lighter(float t) const { return Color(r + (1 - r) * t, g + (1 - g) * t, b + (1 - b) * t, a); }
    Color Darker(float t) const { return Color(r * (1 - t), g * (1 - t), b * (1 - t), a); }
};
inline Color Rgb(uint32_t hex);                                  // 0xRRGGBB
inline Color Rgba(uint32_t hex);                                 // 0xRRGGBBAA
inline Color Hsv(float h, float s, float v, float a = 1.0f);     // all 0..1
inline void  ToHsv(Color c, float& h, float& s, float& v);
inline Color Mix(Color a, Color b, float t);
inline std::string ToHex(Color c, bool withAlpha = false);       // "#RRGGBB" / "#RRGGBBAA"
inline bool  ParseHex(const char* text, Color& out);             // accepts "#RGB", "#RRGGBB", "#RRGGBBAA"

// A string view that accepts both const char* and std::string, so every function
// below works with either.  (Only valid during the call.)
struct Str {
    const char* s;
    size_t n;
    Str() : s(""), n(0) {}
    Str(const char* str) : s(str ? str : ""), n(str ? std::strlen(str) : 0) {}
    Str(const char* str, size_t len) : s(str ? str : ""), n(str ? len : 0) {}
    Str(const std::string& str) : s(str.c_str()), n(str.size()) {}
    bool Empty() const { return n == 0; }
    std::string ToString() const { return std::string(s, n); }
};

enum class Theme { Dark, Light, System };
enum class Align { Left, Center, Right };
enum class Level { Info, Success, Warning, Error };

// Built-in vector icons (outline style). Use with IconButton, Button(label, icon),
// Canvas::IconAt, Toast and more.
enum class Icon {
    None, Check, Close, Plus, Minus,
    ChevronDown, ChevronUp, ChevronLeft, ChevronRight,
    ArrowUp, ArrowDown, ArrowLeft, ArrowRight,
    Search, Folder, File, Copy, Paste, Trash, Edit, Save, Download, Upload, Refresh,
    Play, Pause, Stop, Settings, Gear, Info, Warning, Error, Success,
    Home, Star, Heart, Link, ExternalLink, Eye, Lock, Sun, Moon, Menu, More,
    Filter, Clock, Calendar, User, Bell, Terminal, Code, Image, Globe, Bolt, Undo, Redo,
    Count_
};

// Keys map 1:1 to Windows virtual-key codes, so Key(VK_F13) works too.
enum class Key : int {
    None = 0, Backspace = 0x08, Tab = 0x09, Enter = 0x0D, Shift = 0x10, Ctrl = 0x11, Alt = 0x12,
    Pause = 0x13, CapsLock = 0x14, Escape = 0x1B, Space = 0x20, PageUp = 0x21, PageDown = 0x22,
    End = 0x23, Home = 0x24, Left = 0x25, Up = 0x26, Right = 0x27, Down = 0x28, Insert = 0x2D,
    Delete = 0x2E,
    Num0 = '0', Num1, Num2, Num3, Num4, Num5, Num6, Num7, Num8, Num9,
    A = 'A', B, C, D, E, F, G, H, I, J, K, L, M, N, O, P, Q, R, S, T, U, V, W, X, Y, Z,
    F1 = 0x70, F2, F3, F4, F5, F6, F7, F8, F9, F10, F11, F12
};

// Flags for InputText / InputMultiline (combine with |)
enum InputFlags {
    InputNone     = 0,
    InputPassword = 1 << 0,   // show dots instead of characters
    InputReadOnly = 1 << 1,   // selectable and copyable, not editable
    InputNumbers  = 1 << 2,   // only allow 0-9 . - + e
    InputAllowTab = 1 << 3,   // (multiline) Tab inserts a tab instead of moving focus
    InputNoClear  = 1 << 4    // (SearchBox) hide the clear button
};

// Flags for BeginTable
enum TableFlags {
    TableNone      = 0,
    TableStriped   = 1 << 0,  // alternate row background
    TableSortable  = 1 << 1,  // click headers to sort (read with TableSorting())
    TableResizable = 1 << 2,  // drag header dividers to resize columns
    TableNoHeader  = 1 << 3
};

// Flags for TreeNode
enum TreeFlags {
    TreeNone        = 0,
    TreeLeaf        = 1 << 0, // no arrow, never opens
    TreeSelected    = 1 << 1, // draw as selected
    TreeDefaultOpen = 1 << 2
};

// Built-in fonts. Create more with AddFont().
enum : int { FontBody = 0, FontBold, FontSmall, FontHeading, FontTitle, FontMono };

// =================================================================================
// Running an app
// =================================================================================

struct Config {
    std::string title = "SimplyUI";
    int  width  = 480;          // client size in DIPs (automatically scaled for DPI)
    int  height = 360;
    int  minWidth  = 260;
    int  minHeight = 160;
    bool resizable = true;
    bool topmost = false;       // always on top
    bool scroll = true;         // scroll the window when content is taller than it
    bool rememberWindow = false;// restore size/position from the last run
    Theme theme = Theme::Dark;
    Color accent = Color(0, 0, 0, 0);   // alpha 0 = default accent
    std::string appId;          // settings folder name (defaults to the title)
    int  iconResource = 0;      // icon resource id (0 = first icon in the exe, if any)
    bool tray = false;          // icon in the notification area; the X button hides the window there
    bool startHidden = false;   // start hidden (BringToFront(), the tray icon or a hotkey shows the window)
    bool singleInstance = false;// starting the program again brings this window to the front instead
                                // (files given to that start arrive here through DroppedFiles())
};

// Opens the window and calls `frame` whenever the UI must be drawn.
// Returns the exit code passed to Quit() (0 by default).
SUI_API int Run(const Config& config, const std::function<void()>& frame);
SUI_API int Run(const char* title, int width, int height, const std::function<void()>& frame);

SUI_API void   Quit(int exitCode = 0);  // any thread
SUI_API bool   CloseRequested();          // true for one frame after the user clicked the X button
SUI_API void   CancelClose();             // call in that frame to keep the window open
SUI_API void   SetTitle(Str title);     // any thread
SUI_API void   SetWindowSize(int width, int height);   // DIPs
SUI_API Vec2   WindowSize();              // client size in DIPs
SUI_API void   SetTopmost(bool topmost);
SUI_API bool   IsTopmost();
SUI_API void   Minimize();                // any thread
SUI_API void   BringToFront();            // also shows a hidden window (any thread)
SUI_API void   HideWindow();              // hide the window; the program keeps running (any thread)
SUI_API void   ToggleWindow();            // for a hotkey: hide the window if it is in front, else bring it to front
SUI_API void*  NativeWindow();            // HWND
SUI_API float  DpiScale();                // 1.0 = 96 DPI

SUI_API double   Time();                  // seconds since start
SUI_API float    DeltaTime();             // seconds since the previous frame
SUI_API uint64_t FrameCount();
SUI_API void     RequestRedraw();         // draw another frame as soon as possible (any thread)
SUI_API void     RedrawIn(float seconds); // draw a frame after a delay, e.g. RedrawIn(1) for a clock (any thread)
SUI_API void     Wake();                  // redraw from a worker thread (any thread)

// =================================================================================
// Style, themes and fonts
// =================================================================================

// A font family name such as "Segoe UI" (up to 63 bytes). Assign a string to it.
struct FontName {
    char name[64];
    FontName() { name[0] = 0; }
    FontName(const char* s) { Set(s); }
    FontName(const std::string& s) { Set(s.c_str()); }
    FontName& operator=(const char* s) { Set(s); return *this; }
    FontName& operator=(const std::string& s) { Set(s.c_str()); return *this; }
    const char* c_str() const { return name; }
    bool empty() const { return name[0] == 0; }
    void clear() { name[0] = 0; }
private:
    void Set(const char* s) {
        size_t i = 0;
        if (s) for (; s[i] && i + 1 < sizeof(name); i++) name[i] = s[i];
        name[i] = 0;
    }
};

struct Style {
    // Metrics (DIPs)
    float fontSize      = 14.0f;
    float frameHeight   = 30.0f;  // buttons, inputs, sliders ...
    float rowHeight     = 28.0f;  // lists, tables, menus, tree nodes
    float padding       = 14.0f;  // window padding
    float spacing       = 8.0f;   // vertical space between items
    float spacingX      = 8.0f;   // horizontal space between items on one line
    float framePadding  = 10.0f;  // horizontal padding inside frames
    float rounding      = 6.0f;
    float cardRounding  = 8.0f;
    float cardPadding   = 14.0f;
    float indent        = 18.0f;
    float scrollbarSize = 10.0f;
    float labelMaxFraction = 0.45f; // the label column may use at most this much width
    bool  labelsAbove   = false;    // labels above controls instead of to the left
    bool  animations    = true;
    FontName fontFamily;            // empty = Segoe UI (or best available)
    FontName monoFamily;            // empty = Cascadia Mono / Consolas

    // Colors (set by SetTheme)
    Color bg, panel, popup, field, frame, frameHover, frameActive, border, borderStrong,
          separator, text, textDim, textDisabled, accent, accentHover, accentActive,
          accentText, success, warning, danger, selection, scrollbar, shadow, overlay,
          tooltip;
};

SUI_API Style& GetStyle();                 // edit freely; changes apply immediately
SUI_API void   SetTheme(Theme theme);      // resets colors (keeps sizes and accent)
SUI_API Theme  GetTheme();
SUI_API bool   IsDark();
SUI_API void   SetAccent(Color accent);    // also derives the hover/pressed shades
SUI_API Color  SystemAccent();             // the user's Windows accent color

SUI_API FontId AddFont(Str family, float size, bool bold = false, bool italic = false);  // same font = same id
SUI_API void   PushFont(FontId font);
SUI_API void   PopFont();
SUI_API float  TextWidth(Str text, FontId font = FontBody);
SUI_API float  LineHeight(FontId font = FontBody);

// =================================================================================
// Input
// =================================================================================

SUI_API Vec2  MousePos();
SUI_API bool  MouseDown(int button = 0);          // 0 left, 1 right, 2 middle
SUI_API bool  MouseClicked(int button = 0);
SUI_API bool  MouseReleased(int button = 0);
SUI_API bool  MouseDoubleClicked(int button = 0);
SUI_API float MouseWheel();                        // notches this frame (+ = up)
SUI_API bool  KeyDown(Key key);
SUI_API bool  KeyPressed(Key key, bool repeat = true);
SUI_API bool  KeyReleased(Key key);
SUI_API bool  CtrlDown();
SUI_API bool  ShiftDown();
SUI_API bool  AltDown();
SUI_API bool  Shortcut(Str keys);                  // "Ctrl+S", "Ctrl+Shift+N", "F5", "Alt+Enter"
SUI_API bool  GlobalHotkey(Str keys);              // system-wide, even when the window is hidden
SUI_API bool  IsEditingText();                     // a text field has keyboard focus

// =================================================================================
// Layout
// =================================================================================

SUI_API void  SameLine(float spacing = -1.0f);     // put the next item on the current line
SUI_API void  NewLine();
SUI_API void  Spacing(float height = -1.0f);       // extra vertical space
SUI_API void  Spacer(float width, float height);   // empty item of a fixed size
SUI_API void  Indent(float amount = -1.0f);
SUI_API void  Unindent(float amount = -1.0f);
SUI_API void  NextWidth(float width);              // 120 = px, 0.5 = 50%, 1 = 100%, -80 = all but 80px
SUI_API float AvailableWidth();

SUI_API void  BeginRow(int columns);               // next items share one line in equal cells
SUI_API void  BeginRow(std::initializer_list<float> weights);   // e.g. {2, 1, 1}
SUI_API void  EndRow();
SUI_API void  BeginColumns(int columns);           // side-by-side vertical stacks
SUI_API void  BeginColumns(std::initializer_list<float> weights);
SUI_API void  NextColumn();
SUI_API void  EndColumns();
SUI_API void  BeginHorizontal(Align align = Align::Left);       // items flow left to right
SUI_API void  EndHorizontal();
SUI_API void  BeginCard(Str title = Str());        // rounded panel around its content
SUI_API void  EndCard();
SUI_API void  BeginChild(Str id, float height = 0.0f, bool border = true); // scroll area; 0 = fill, <0 = fill minus
SUI_API void  EndChild();
SUI_API void  BeginFooter();                       // fixed area at the bottom of the window
SUI_API void  EndFooter();
SUI_API void  BeginDisabled(bool disabled = true); // grey out and block everything inside
SUI_API void  EndDisabled();

SUI_API void  PushID(Str id);
SUI_API void  PushID(int id);
SUI_API void  PushID(const void* ptr);
SUI_API void  PopID();
SUI_API ID    GetID(Str label);

// Information about the last item that was added.
struct ItemInfo {
    ID   id = 0;
    Rect rect;
    bool hovered = false, active = false, focused = false;
    bool clicked = false, rightClicked = false, doubleClicked = false;
    bool changed = false;      // value changed this frame
    bool submitted = false;    // Enter pressed in a text field
};
SUI_API const ItemInfo& LastItem();
SUI_API void  Tooltip(Str text);       // show a tooltip when the last item is hovered
SUI_API void  FocusNext();             // the next widget gets the keyboard focus (e.g. a text field at start)

// =================================================================================
// Text
// =================================================================================

SUI_API void Text(Str text);
inline  void Textf(const char* fmt, ...) SUI_PRINTF(1, 2);
SUI_API void TextDim(Str text);
SUI_API void TextColored(Color color, Str text);
SUI_API void TextWrapped(Str text);
SUI_API void Heading(Str text);                    // bold, larger
SUI_API void Title(Str text);                      // bold, largest
SUI_API void LabelText(Str label, Str value);      // aligned like a labeled field
SUI_API void BulletText(Str text);
SUI_API bool Link(Str text, Str url = Str());      // opens url (if given) when clicked
SUI_API void Badge(Str text, Color color);
SUI_API void Badge(Str text, Level level = Level::Info);
SUI_API void Separator();
SUI_API void SeparatorText(Str text);
inline  std::string Format(const char* fmt, ...) SUI_PRINTF(1, 2);

// =================================================================================
// Widgets
// =================================================================================

SUI_API bool Button(Str label);
SUI_API bool Button(Str label, Icon icon);
SUI_API bool PrimaryButton(Str label);
SUI_API bool DangerButton(Str label);
SUI_API bool IconButton(Icon icon, Str tooltip = Str());
SUI_API bool Checkbox(Str label, bool& value);
SUI_API bool Toggle(Str label, bool& value);       // switch, right-aligned
SUI_API bool RadioButton(Str label, int& current, int value);
SUI_API bool Selectable(Str label, bool selected = false);

SUI_API bool Slider(Str label, float& value, float min, float max, const char* format = "%.2f");
SUI_API bool Slider(Str label, int& value, int min, int max, const char* format = "%d");
SUI_API bool Drag(Str label, float& value, float speed = 0.01f, float min = -FLT_MAX, float max = FLT_MAX, const char* format = "%.3f");
SUI_API bool Drag(Str label, int& value, float speed = 0.2f, int min = INT_MIN, int max = INT_MAX);
SUI_API bool InputInt(Str label, int& value, int step = 1, int min = INT_MIN, int max = INT_MAX);
SUI_API bool InputFloat(Str label, float& value, float step = 0.1f, float min = -FLT_MAX, float max = FLT_MAX, const char* format = "%.2f");

SUI_API bool InputText(Str label, std::string& text, Str hint = Str(), int flags = 0);
SUI_API bool InputMultiline(Str label, std::string& text, float height = 120.0f, int flags = 0);
SUI_API bool SearchBox(Str id, std::string& text, Str hint = "Search", int flags = 0);

SUI_API bool Combo(Str label, int& current, const std::vector<std::string>& items);
SUI_API bool Combo(Str label, int& current, std::initializer_list<const char*> items);
SUI_API bool ListBox(Str label, int& current, const std::vector<std::string>& items, int visibleItems = 6);
SUI_API bool Tabs(int& current, std::initializer_list<const char*> tabs);
SUI_API bool Tabs(int& current, const std::vector<std::string>& tabs);
SUI_API bool Segmented(Str label, int& current, std::initializer_list<const char*> options);
SUI_API bool CollapsingHeader(Str label, bool defaultOpen = false);
SUI_API bool TreeNode(Str label, int flags = 0);   // if it returns true, call TreePop() after children
SUI_API void TreePop();

SUI_API void ProgressBar(float fraction, Str overlay = Str());   // fraction < 0 = indeterminate
SUI_API void Spinner(float size = 0.0f);
SUI_API bool ColorEdit(Str label, Color& color, bool alpha = false);

SUI_API void PlotLines(Str label, const float* values, int count, float height = 80.0f, float min = 0, float max = 0);
SUI_API void PlotLines(Str label, const std::vector<float>& values, float height = 80.0f, float min = 0, float max = 0);
SUI_API void PlotBars(Str label, const float* values, int count, float height = 80.0f, float min = 0, float max = 0);
SUI_API void PlotBars(Str label, const std::vector<float>& values, float height = 80.0f, float min = 0, float max = 0);

// Files dropped on the dashed area this frame (empty most frames).
SUI_API std::vector<std::string> DropZone(Str label, float height = 90.0f);

// =================================================================================
// Tables
// =================================================================================

// height: 0 = grows with its rows, > 0 = fixed height (scrolls), < 0 = fill the rest of the window
SUI_API bool BeginTable(Str id, std::initializer_list<const char*> columns, int flags = 0, float height = 0.0f);
SUI_API bool BeginTable(Str id, const std::vector<std::string>& columns, int flags = 0, float height = 0.0f);
SUI_API bool TableNextRow(bool selected = false);  // returns true if the row was clicked
SUI_API void TableNextCell();                      // move to the next cell of the row
inline  bool TableRow(std::initializer_list<Str> cells, bool selected = false);   // text-only row
SUI_API void EndTable();
SUI_API void NextTableWeights(std::initializer_list<float> weights);  // initial column widths, e.g. {3, 1, 1}
struct TableSort { int column = -1; bool ascending = true; bool changed = false; };
SUI_API TableSort TableSorting();                  // call between BeginTable and EndTable

// =================================================================================
// Log view (thread-safe: add lines from any thread)
// =================================================================================

class Log {
public:
    explicit Log(size_t maxLines = 2000);
    ~Log();
    void Add(Level level, Str text);
    void Info(const char* fmt, ...) SUI_PRINTF(2, 3);
    void Success(const char* fmt, ...) SUI_PRINTF(2, 3);
    void Warn(const char* fmt, ...) SUI_PRINTF(2, 3);
    void Error(const char* fmt, ...) SUI_PRINTF(2, 3);
    void Clear();
    size_t Count() const;
    std::string Text() const;              // everything as plain text
    void* Handle() const { return handle_; }
private:
    Log(const Log&) = delete;              // a Log lives inside the DLL; pass it by reference
    Log& operator=(const Log&) = delete;
    void* handle_;
};
SUI_API void LogView(Str id, Log& log, float height = 0.0f, bool showTime = true);

// =================================================================================
// Images and custom drawing
// =================================================================================

struct Texture {
    int id = 0, width = 0, height = 0;
    explicit operator bool() const { return id != 0; }
};
SUI_API Texture LoadTexture(Str path);                              // png, jpg, bmp, gif, ico, tiff
SUI_API Texture LoadTextureFromMemory(const void* data, size_t size);
SUI_API Texture CreateTexture(const void* rgba, int width, int height);
SUI_API void    UpdateTexture(Texture& texture, const void* rgba);
SUI_API void    FreeTexture(Texture& texture);
SUI_API void    Image(const Texture& texture, Vec2 size = Vec2()); // 0 = natural size (fits width)

// A drawing surface. Coordinates are relative to the canvas' top-left corner.
struct Canvas {
    Rect  rect;                 // screen rect
    Vec2  size;
    Vec2  mouse;                // mouse position relative to the canvas
    bool  hovered = false, clicked = false, down = false, released = false;
    bool  rightClicked = false;
    float wheel = 0;

    void Line(Vec2 a, Vec2 b, Color c, float thickness = 1.0f) const;
    void FillRect(Rect r, Color c, float rounding = 0.0f) const;
    void StrokeRect(Rect r, Color c, float rounding = 0.0f, float thickness = 1.0f) const;
    void FillCircle(Vec2 center, float radius, Color c) const;
    void StrokeCircle(Vec2 center, float radius, Color c, float thickness = 1.0f) const;
    void Polyline(const std::vector<Vec2>& points, Color c, float thickness = 1.0f, bool closed = false) const;
    void FillPolygon(const std::vector<Vec2>& points, Color c) const;
    void Bezier(Vec2 p0, Vec2 p1, Vec2 p2, Vec2 p3, Color c, float thickness = 1.0f) const;
    void Text(Vec2 pos, Str text, Color c, FontId font = FontBody) const;
    void Image(const Texture& texture, Rect dest, float opacity = 1.0f) const;
    void IconAt(Icon icon, Rect r, Color c) const;
};
SUI_API Canvas BeginCanvas(float height, bool border = true);
SUI_API void   EndCanvas();

// =================================================================================
// Popups, menus, modals, notifications
// =================================================================================

SUI_API void OpenPopup(Str id);                    // opens at the mouse position
SUI_API bool BeginPopup(Str id);
SUI_API void EndPopup();
SUI_API void ClosePopup();                         // close the popup you are inside
SUI_API bool BeginContextMenu();                   // right-click menu for the previous item
SUI_API void EndContextMenu();

SUI_API void OpenModal(Str title);
SUI_API bool BeginModal(Str title);                // dims the window; Escape closes it
SUI_API void EndModal();
SUI_API void CloseModal();

SUI_API bool BeginMenuBar();
SUI_API void EndMenuBar();
SUI_API bool BeginMenu(Str label);                 // inside a menu bar or another menu
SUI_API void EndMenu();
SUI_API bool MenuItem(Str label, Str shortcut = Str(), bool checked = false, bool enabled = true);
SUI_API bool MenuItem(Str label, Str shortcut, bool* toggle, bool enabled = true);
SUI_API void MenuSeparator();

SUI_API void Toast(Str text, Level level = Level::Info, float seconds = 3.0f);   // any thread

// =================================================================================
// Native dialogs and system helpers
// =================================================================================

// filter: "Images|*.png;*.jpg|All files|*.*". Returns "" when cancelled.
SUI_API std::string OpenFileDialog(Str filter = Str(), Str title = Str());
SUI_API std::vector<std::string> OpenFilesDialog(Str filter = Str(), Str title = Str());
SUI_API std::string SaveFileDialog(Str filter = Str(), Str defaultName = Str(), Str title = Str());
SUI_API std::string PickFolder(Str title = Str());
SUI_API void ShowMessage(Str title, Str text, Level level = Level::Info);
SUI_API bool Confirm(Str title, Str text);         // Yes / No
SUI_API void Beep(Level level = Level::Info);      // the Windows sound for that kind of message

SUI_API void OpenURL(Str urlOrPath);
SUI_API void ShowInExplorer(Str path);
SUI_API void SetClipboard(Str text);
SUI_API std::string GetClipboard();
SUI_API const std::vector<std::string>& DroppedFiles();   // files dropped on the window this frame
SUI_API std::vector<std::string> CommandLine();    // the program's arguments as UTF-8 (without the .exe)
// Progress on the taskbar button. Call it every frame while busy; it goes away by itself.
// fraction 0..1 (< 0 = busy without a known end); Level::Warning = yellow, Level::Error = red.
SUI_API void TaskbarProgress(float fraction, Level level = Level::Info);
SUI_API std::wstring ToWide(Str utf8);             // UTF-8 -> UTF-16 (for Win32 APIs)
SUI_API std::string  ToUtf8(const std::wstring& wide);

// =================================================================================
// Settings that survive restarts (%APPDATA%\<appId>\settings.ini)
// =================================================================================

// Call every frame: the first call loads the saved value, later calls remember the
// current one. Everything is written on exit (or call SaveSettings()).
SUI_API void Persist(Str key, bool& value);
SUI_API void Persist(Str key, int& value);
SUI_API void Persist(Str key, float& value);
SUI_API void Persist(Str key, std::string& value);
SUI_API void Persist(Str key, Color& value);
SUI_API void SaveSettings();
SUI_API std::string SettingsPath();

// #################################################################################
//                      Everything below is implementation
// #################################################################################

// ---------------------------------------------------------------------------------
// Small helpers that run in your program (no DLL call needed)
// ---------------------------------------------------------------------------------

namespace api {
inline std::string VFormat(const char* fmt, va_list args) {
    if (!fmt) return std::string();
    char buf[512];
    va_list copy;
    va_copy(copy, args);
    int n = std::vsnprintf(buf, sizeof(buf), fmt, copy);
    va_end(copy);
    if (n < 0) return std::string();
    if (n < static_cast<int>(sizeof(buf))) return std::string(buf, static_cast<size_t>(n));
    std::string s(static_cast<size_t>(n) + 1, '\0');
    std::vsnprintf(&s[0], s.size(), fmt, args);
    s.resize(static_cast<size_t>(n));
    return s;
}
} // namespace api

namespace api {
inline float Byte(uint32_t v) { return static_cast<float>(v & 0xFFu) / 255.0f; }
}
inline Color Rgb(uint32_t hex) {
    return Color(api::Byte(hex >> 16), api::Byte(hex >> 8), api::Byte(hex), 1.0f);
}
inline Color Rgba(uint32_t hex) {
    return Color(api::Byte(hex >> 24), api::Byte(hex >> 16), api::Byte(hex >> 8), api::Byte(hex));
}
inline Color Hsv(float h, float s, float v, float a) {
    h = h - std::floor(h);
    float r = v, g = v, b = v;
    if (s > 0) {
        float hh = h * 6.0f;
        int i = static_cast<int>(hh);
        float f = hh - static_cast<float>(i);
        float p = v * (1 - s), q = v * (1 - s * f), t = v * (1 - s * (1 - f));
        switch (i % 6) {
        case 0: r = v; g = t; b = p; break;
        case 1: r = q; g = v; b = p; break;
        case 2: r = p; g = v; b = t; break;
        case 3: r = p; g = q; b = v; break;
        case 4: r = t; g = p; b = v; break;
        default: r = v; g = p; b = q; break;
        }
    }
    return Color(r, g, b, a);
}
inline void ToHsv(Color c, float& h, float& s, float& v) {
    float mx = c.r > c.g ? (c.r > c.b ? c.r : c.b) : (c.g > c.b ? c.g : c.b);
    float mn = c.r < c.g ? (c.r < c.b ? c.r : c.b) : (c.g < c.b ? c.g : c.b);
    float d = mx - mn;
    v = mx;
    s = mx > 0 ? d / mx : 0;
    if (d <= 0) { h = 0; return; }
    if (mx == c.r) h = (c.g - c.b) / d + (c.g < c.b ? 6.0f : 0.0f);
    else if (mx == c.g) h = (c.b - c.r) / d + 2.0f;
    else h = (c.r - c.g) / d + 4.0f;
    h /= 6.0f;
}
inline Color Mix(Color a, Color b, float t) {
    return Color(a.r + (b.r - a.r) * t, a.g + (b.g - a.g) * t, a.b + (b.b - a.b) * t, a.a + (b.a - a.a) * t);
}
inline std::string ToHex(Color c, bool withAlpha) {
    struct B { static int Byte(float v) { return static_cast<int>((v < 0 ? 0 : v > 1 ? 1 : v) * 255.0f + 0.5f); } };
    char buf[16];
    if (withAlpha) std::snprintf(buf, sizeof(buf), "#%02X%02X%02X%02X", B::Byte(c.r), B::Byte(c.g), B::Byte(c.b), B::Byte(c.a));
    else std::snprintf(buf, sizeof(buf), "#%02X%02X%02X", B::Byte(c.r), B::Byte(c.g), B::Byte(c.b));
    return buf;
}
inline bool ParseHex(const char* text, Color& out) {
    if (!text) return false;
    while (*text == ' ') text++;
    if (*text == '#') text++;
    unsigned v[8] = {0, 0, 0, 0, 0, 0, 0, 0};
    int n = 0;
    for (; text[n] && n < 9; n++) {
        char ch = text[n];
        if (ch >= '0' && ch <= '9') v[n % 8] = static_cast<unsigned>(ch - '0');
        else if (ch >= 'a' && ch <= 'f') v[n % 8] = static_cast<unsigned>(ch - 'a' + 10);
        else if (ch >= 'A' && ch <= 'F') v[n % 8] = static_cast<unsigned>(ch - 'A' + 10);
        else break;
    }
    if (text[n] != 0 && text[n] != ' ') return false;
    if (n == 3) { out = Color(api::Byte(v[0] * 17), api::Byte(v[1] * 17), api::Byte(v[2] * 17), 1); return true; }
    if (n == 6 || n == 8) {
        out = Color(api::Byte(v[0] * 16 + v[1]), api::Byte(v[2] * 16 + v[3]), api::Byte(v[4] * 16 + v[5]),
                    n == 8 ? api::Byte(v[6] * 16 + v[7]) : 1.0f);
        return true;
    }
    return false;
}

inline std::string Format(const char* fmt, ...) {
    va_list args;
    va_start(args, fmt);
    std::string s = api::VFormat(fmt, args);
    va_end(args);
    return s;
}
inline void Textf(const char* fmt, ...) {
    va_list args;
    va_start(args, fmt);
    std::string s = api::VFormat(fmt, args);
    va_end(args);
    Text(s);
}
inline bool TableRow(std::initializer_list<Str> cells, bool selected) {
    bool clicked = TableNextRow(selected);
    for (const Str* c = cells.begin(); c != cells.end(); ++c) { TableNextCell(); Text(*c); }
    return clicked;
}

// ---------------------------------------------------------------------------------
// The DLL interface: a table of plain C functions, so simplyui.dll works with
// programs made by any compiler. You never need to use it directly.
// ---------------------------------------------------------------------------------

namespace api {

enum { kAbiVersion = 1 };

typedef void (SUI_CALL* FrameFn)(void* user);
typedef void (SUI_CALL* StrFn)(void* user, const char* text, size_t length);
typedef void (SUI_CALL* WStrFn)(void* user, const wchar_t* text, size_t length);
typedef const char* (SUI_CALL* ItemFn)(const void* list, int index, size_t* length);

struct RunConfig {
    uint32_t size;
    const char* title;
    int width, height, minWidth, minHeight;
    int resizable, topmost, scroll, rememberWindow;
    int theme;
    Color accent;
    const char* appId;
    int iconResource;
    int tray, startHidden, singleInstance;
};

struct Table {
    uint32_t size;          // sizeof(Table) in the DLL
    uint32_t versionNum;    // SIMPLYUI_VERSION_NUM of the DLL
    const char* version;    // SIMPLYUI_VERSION of the DLL

    // Running an app
    int    (SUI_CALL* Run)(const RunConfig* config, FrameFn frame, void* user);
    void    (SUI_CALL* Quit)(int exitCode);
    int     (SUI_CALL* CloseRequested)(void);
    void    (SUI_CALL* CancelClose)(void);
    void    (SUI_CALL* SetTitle)(const char* title, size_t title_n);
    void    (SUI_CALL* SetWindowSize)(int width, int height);
    void    (SUI_CALL* WindowSize)(Vec2* out);
    void    (SUI_CALL* SetTopmost)(int topmost);
    int     (SUI_CALL* IsTopmost)(void);
    void    (SUI_CALL* Minimize)(void);
    void    (SUI_CALL* BringToFront)(void);
    void*   (SUI_CALL* NativeWindow)(void);
    float   (SUI_CALL* DpiScale)(void);
    double  (SUI_CALL* Time)(void);
    float   (SUI_CALL* DeltaTime)(void);
    uint64_t (SUI_CALL* FrameCount)(void);
    void    (SUI_CALL* RequestRedraw)(void);
    void    (SUI_CALL* RedrawIn)(float seconds);
    void    (SUI_CALL* Wake)(void);

    // Style, themes and fonts
    Style*  (SUI_CALL* GetStyle)(void);
    void    (SUI_CALL* SetTheme)(int theme);
    int     (SUI_CALL* GetTheme)(void);
    int     (SUI_CALL* IsDark)(void);
    void    (SUI_CALL* SetAccent)(const Color* accent);
    void    (SUI_CALL* SystemAccent)(Color* out);
    int     (SUI_CALL* AddFont)(const char* family, size_t family_n, float size, int bold, int italic);
    void    (SUI_CALL* PushFont)(int font);
    void    (SUI_CALL* PopFont)(void);
    float   (SUI_CALL* TextWidth)(const char* text, size_t text_n, int font);
    float   (SUI_CALL* LineHeight)(int font);

    // Input
    void    (SUI_CALL* MousePos)(Vec2* out);
    int     (SUI_CALL* MouseDown)(int button);
    int     (SUI_CALL* MouseClicked)(int button);
    int     (SUI_CALL* MouseReleased)(int button);
    int     (SUI_CALL* MouseDoubleClicked)(int button);
    float   (SUI_CALL* MouseWheel)(void);
    int     (SUI_CALL* KeyDown)(int key);
    int     (SUI_CALL* KeyPressed)(int key, int repeat);
    int     (SUI_CALL* KeyReleased)(int key);
    int     (SUI_CALL* CtrlDown)(void);
    int     (SUI_CALL* ShiftDown)(void);
    int     (SUI_CALL* AltDown)(void);
    int     (SUI_CALL* Shortcut)(const char* keys, size_t keys_n);
    int     (SUI_CALL* GlobalHotkey)(const char* keys, size_t keys_n);
    int     (SUI_CALL* IsEditingText)(void);

    // Layout
    void    (SUI_CALL* SameLine)(float spacing);
    void    (SUI_CALL* NewLine)(void);
    void    (SUI_CALL* Spacing)(float height);
    void    (SUI_CALL* Spacer)(float width, float height);
    void    (SUI_CALL* Indent)(float amount);
    void    (SUI_CALL* Unindent)(float amount);
    void    (SUI_CALL* NextWidth)(float width);
    float   (SUI_CALL* AvailableWidth)(void);
    void    (SUI_CALL* BeginRow)(int columns);
    void    (SUI_CALL* BeginRowWeights)(const float* weights, int weights_count);
    void    (SUI_CALL* EndRow)(void);
    void    (SUI_CALL* BeginColumns)(int columns);
    void    (SUI_CALL* BeginColumnsWeights)(const float* weights, int weights_count);
    void    (SUI_CALL* NextColumn)(void);
    void    (SUI_CALL* EndColumns)(void);
    void    (SUI_CALL* BeginHorizontal)(int align);
    void    (SUI_CALL* EndHorizontal)(void);
    void    (SUI_CALL* BeginCard)(const char* title, size_t title_n);
    void    (SUI_CALL* EndCard)(void);
    void    (SUI_CALL* BeginChild)(const char* id, size_t id_n, float height, int border);
    void    (SUI_CALL* EndChild)(void);
    void    (SUI_CALL* BeginFooter)(void);
    void    (SUI_CALL* EndFooter)(void);
    void    (SUI_CALL* BeginDisabled)(int disabled);
    void    (SUI_CALL* EndDisabled)(void);
    void    (SUI_CALL* PushIDStr)(const char* id, size_t id_n);
    void    (SUI_CALL* PushIDInt)(int id);
    void    (SUI_CALL* PushIDPtr)(const void* ptr);
    void    (SUI_CALL* PopID)(void);
    uint32_t (SUI_CALL* GetID)(const char* label, size_t label_n);
    const ItemInfo* (SUI_CALL* LastItem)(void);
    void    (SUI_CALL* Tooltip)(const char* text, size_t text_n);

    // Text
    void    (SUI_CALL* Text)(const char* text, size_t text_n);
    void    (SUI_CALL* TextDim)(const char* text, size_t text_n);
    void    (SUI_CALL* TextColored)(const Color* color, const char* text, size_t text_n);
    void    (SUI_CALL* TextWrapped)(const char* text, size_t text_n);
    void    (SUI_CALL* Heading)(const char* text, size_t text_n);
    void    (SUI_CALL* Title)(const char* text, size_t text_n);
    void    (SUI_CALL* LabelText)(const char* label, size_t label_n, const char* value, size_t value_n);
    void    (SUI_CALL* BulletText)(const char* text, size_t text_n);
    int     (SUI_CALL* Link)(const char* text, size_t text_n, const char* url, size_t url_n);
    void    (SUI_CALL* BadgeColor)(const char* text, size_t text_n, const Color* color);
    void    (SUI_CALL* BadgeLevel)(const char* text, size_t text_n, int level);
    void    (SUI_CALL* Separator)(void);
    void    (SUI_CALL* SeparatorText)(const char* text, size_t text_n);

    // Widgets
    int     (SUI_CALL* Button)(const char* label, size_t label_n);
    int     (SUI_CALL* ButtonIcon)(const char* label, size_t label_n, int icon);
    int     (SUI_CALL* PrimaryButton)(const char* label, size_t label_n);
    int     (SUI_CALL* DangerButton)(const char* label, size_t label_n);
    int     (SUI_CALL* IconButton)(int icon, const char* tooltip, size_t tooltip_n);
    int     (SUI_CALL* Checkbox)(const char* label, size_t label_n, bool* value);
    int     (SUI_CALL* Toggle)(const char* label, size_t label_n, bool* value);
    int     (SUI_CALL* RadioButton)(const char* label, size_t label_n, int* current, int value);
    int     (SUI_CALL* Selectable)(const char* label, size_t label_n, int selected);
    int     (SUI_CALL* SliderFloat)(const char* label, size_t label_n, float* value, float min, float max, const char* format);
    int     (SUI_CALL* SliderInt)(const char* label, size_t label_n, int* value, int min, int max, const char* format);
    int     (SUI_CALL* DragFloat)(const char* label, size_t label_n, float* value, float speed, float min, float max, const char* format);
    int     (SUI_CALL* DragInt)(const char* label, size_t label_n, int* value, float speed, int min, int max);
    int     (SUI_CALL* InputInt)(const char* label, size_t label_n, int* value, int step, int min, int max);
    int     (SUI_CALL* InputFloat)(const char* label, size_t label_n, float* value, float step, float min, float max, const char* format);
    int     (SUI_CALL* InputText)(const char* label, size_t label_n, const char* text, size_t text_n, StrFn text_set, void* text_user, const char* hint, size_t hint_n, int flags);
    int     (SUI_CALL* InputMultiline)(const char* label, size_t label_n, const char* text, size_t text_n, StrFn text_set, void* text_user, float height, int flags);
    int     (SUI_CALL* SearchBox)(const char* id, size_t id_n, const char* text, size_t text_n, StrFn text_set, void* text_user, const char* hint, size_t hint_n, int flags);
    int     (SUI_CALL* Combo)(const char* label, size_t label_n, int* current, const void* items, int items_count, ItemFn items_get);
    int     (SUI_CALL* ListBox)(const char* label, size_t label_n, int* current, const void* items, int items_count, ItemFn items_get, int visibleItems);
    int     (SUI_CALL* Tabs)(int* current, const void* tabs, int tabs_count, ItemFn tabs_get);
    int     (SUI_CALL* Segmented)(const char* label, size_t label_n, int* current, const void* options, int options_count, ItemFn options_get);
    int     (SUI_CALL* CollapsingHeader)(const char* label, size_t label_n, int defaultOpen);
    int     (SUI_CALL* TreeNode)(const char* label, size_t label_n, int flags);
    void    (SUI_CALL* TreePop)(void);
    void    (SUI_CALL* ProgressBar)(float fraction, const char* overlay, size_t overlay_n);
    void    (SUI_CALL* Spinner)(float size);
    int     (SUI_CALL* ColorEdit)(const char* label, size_t label_n, Color* color, int alpha);
    void    (SUI_CALL* PlotLines)(const char* label, size_t label_n, const float* values, int count, float height, float min, float max);
    void    (SUI_CALL* PlotBars)(const char* label, size_t label_n, const float* values, int count, float height, float min, float max);
    void    (SUI_CALL* DropZone)(const char* label, size_t label_n, float height, StrFn add, void* add_user);

    // Tables
    int     (SUI_CALL* BeginTable)(const char* id, size_t id_n, const void* columns, int columns_count, ItemFn columns_get, int flags, float height);
    int     (SUI_CALL* TableNextRow)(int selected);
    void    (SUI_CALL* TableNextCell)(void);
    void    (SUI_CALL* EndTable)(void);
    void    (SUI_CALL* NextTableWeights)(const float* weights, int weights_count);
    void    (SUI_CALL* TableSorting)(TableSort* out);

    // Log view
    void*   (SUI_CALL* LogCreate)(size_t maxLines);
    void   (SUI_CALL* LogDestroy)(void* log);
    void   (SUI_CALL* LogAdd)(void* log, int level, const char* text, size_t text_n);
    void   (SUI_CALL* LogClear)(void* log);
    size_t (SUI_CALL* LogCount)(void* log);
    void   (SUI_CALL* LogText)(void* log, StrFn out, void* out_user);
    void   (SUI_CALL* LogView)(const char* id, size_t id_n, void* log, float height, int showTime);

    // Images and custom drawing
    void    (SUI_CALL* LoadTexture)(const char* path, size_t path_n, Texture* out);
    void    (SUI_CALL* LoadTextureFromMemory)(const void* data, size_t size, Texture* out);
    void    (SUI_CALL* CreateTexture)(const void* rgba, int width, int height, Texture* out);
    void    (SUI_CALL* UpdateTexture)(Texture* texture, const void* rgba);
    void    (SUI_CALL* FreeTexture)(Texture* texture);
    void    (SUI_CALL* Image)(const Texture* texture, const Vec2* size);
    void    (SUI_CALL* BeginCanvas)(float height, int border, Canvas* out);
    void    (SUI_CALL* EndCanvas)(void);
    void    (SUI_CALL* CanvasLine)(const Canvas* self, const Vec2* a, const Vec2* b, const Color* c, float thickness);
    void    (SUI_CALL* CanvasFillRect)(const Canvas* self, const Rect* r, const Color* c, float rounding);
    void    (SUI_CALL* CanvasStrokeRect)(const Canvas* self, const Rect* r, const Color* c, float rounding, float thickness);
    void    (SUI_CALL* CanvasFillCircle)(const Canvas* self, const Vec2* center, float radius, const Color* c);
    void    (SUI_CALL* CanvasStrokeCircle)(const Canvas* self, const Vec2* center, float radius, const Color* c, float thickness);
    void    (SUI_CALL* CanvasPolyline)(const Canvas* self, const Vec2* points, int points_count, const Color* c, float thickness, int closed);
    void    (SUI_CALL* CanvasFillPolygon)(const Canvas* self, const Vec2* points, int points_count, const Color* c);
    void    (SUI_CALL* CanvasBezier)(const Canvas* self, const Vec2* p0, const Vec2* p1, const Vec2* p2, const Vec2* p3, const Color* c, float thickness);
    void    (SUI_CALL* CanvasText)(const Canvas* self, const Vec2* pos, const char* text, size_t text_n, const Color* c, int font);
    void    (SUI_CALL* CanvasImage)(const Canvas* self, const Texture* texture, const Rect* dest, float opacity);
    void    (SUI_CALL* CanvasIconAt)(const Canvas* self, int icon, const Rect* r, const Color* c);

    // Popups, menus, modals, notifications
    void    (SUI_CALL* OpenPopup)(const char* id, size_t id_n);
    int     (SUI_CALL* BeginPopup)(const char* id, size_t id_n);
    void    (SUI_CALL* EndPopup)(void);
    void    (SUI_CALL* ClosePopup)(void);
    int     (SUI_CALL* BeginContextMenu)(void);
    void    (SUI_CALL* EndContextMenu)(void);
    void    (SUI_CALL* OpenModal)(const char* title, size_t title_n);
    int     (SUI_CALL* BeginModal)(const char* title, size_t title_n);
    void    (SUI_CALL* EndModal)(void);
    void    (SUI_CALL* CloseModal)(void);
    int     (SUI_CALL* BeginMenuBar)(void);
    void    (SUI_CALL* EndMenuBar)(void);
    int     (SUI_CALL* BeginMenu)(const char* label, size_t label_n);
    void    (SUI_CALL* EndMenu)(void);
    int     (SUI_CALL* MenuItem)(const char* label, size_t label_n, const char* shortcut, size_t shortcut_n, int checked, int enabled);
    int     (SUI_CALL* MenuItemToggle)(const char* label, size_t label_n, const char* shortcut, size_t shortcut_n, bool* toggle, int enabled);
    void    (SUI_CALL* MenuSeparator)(void);
    void    (SUI_CALL* Toast)(const char* text, size_t text_n, int level, float seconds);

    // Native dialogs and system helpers
    void    (SUI_CALL* OpenFileDialog)(const char* filter, size_t filter_n, const char* title, size_t title_n, StrFn out, void* out_user);
    void    (SUI_CALL* OpenFilesDialog)(const char* filter, size_t filter_n, const char* title, size_t title_n, StrFn add, void* add_user);
    void    (SUI_CALL* SaveFileDialog)(const char* filter, size_t filter_n, const char* defaultName, size_t defaultName_n, const char* title, size_t title_n, StrFn out, void* out_user);
    void    (SUI_CALL* PickFolder)(const char* title, size_t title_n, StrFn out, void* out_user);
    void    (SUI_CALL* ShowMessage)(const char* title, size_t title_n, const char* text, size_t text_n, int level);
    int     (SUI_CALL* Confirm)(const char* title, size_t title_n, const char* text, size_t text_n);
    void    (SUI_CALL* OpenURL)(const char* urlOrPath, size_t urlOrPath_n);
    void    (SUI_CALL* ShowInExplorer)(const char* path, size_t path_n);
    void    (SUI_CALL* SetClipboard)(const char* text, size_t text_n);
    void    (SUI_CALL* GetClipboard)(StrFn out, void* out_user);
    void   (SUI_CALL* DroppedFiles)(StrFn add, void* add_user);
    void   (SUI_CALL* ToWide)(const char* utf8, size_t utf8_n, WStrFn out, void* out_user);
    void   (SUI_CALL* ToUtf8)(const wchar_t* wide, size_t wide_n, StrFn out, void* out_user);

    // Settings
    void    (SUI_CALL* PersistBool)(const char* key, size_t key_n, bool* value);
    void    (SUI_CALL* PersistInt)(const char* key, size_t key_n, int* value);
    void    (SUI_CALL* PersistFloat)(const char* key, size_t key_n, float* value);
    void    (SUI_CALL* PersistString)(const char* key, size_t key_n, const char* value, size_t value_n, StrFn value_set, void* value_user);
    void    (SUI_CALL* PersistColor)(const char* key, size_t key_n, Color* value);
    void    (SUI_CALL* SaveSettings)(void);
    void    (SUI_CALL* SettingsPath)(StrFn out, void* out_user);

    // Window, focus, taskbar and command line
    void    (SUI_CALL* HideWindow)(void);
    void    (SUI_CALL* ToggleWindow)(void);
    void    (SUI_CALL* FocusNext)(void);
    void    (SUI_CALL* TaskbarProgress)(float fraction, int level);
    void    (SUI_CALL* CommandLine)(StrFn add, void* add_user);
    void    (SUI_CALL* Beep)(int level);
};

// The two functions simplyui.dll exports (extern "C", SUI_CALL).
typedef const Table* (SUI_CALL* GetApiFn)(int abiVersion, uint32_t tableSize, uint32_t styleSize,
                                          uint32_t itemInfoSize, uint32_t canvasSize);
typedef const char* (SUI_CALL* VersionFn)(void);

} // namespace api
} // namespace sui

#ifndef SIMPLYUI_BUILDING_DLL

// ---------------------------------------------------------------------------------
// Loading simplyui.dll
// ---------------------------------------------------------------------------------

// The Win32 functions used to load the DLL, declared exactly like <windows.h> does,
// so this header does not need <windows.h> and including both is fine.
#if !defined(_WINDOWS_) && !defined(_INC_WINDOWS)
struct HINSTANCE__;
#ifdef _WIN64
typedef long long (__stdcall* SimplyUI_FarProc)();
#else
typedef int (__stdcall* SimplyUI_FarProc)();
#endif
extern "C" {
__declspec(dllimport) HINSTANCE__* __stdcall LoadLibraryW(const wchar_t* lpLibFileName);
__declspec(dllimport) SimplyUI_FarProc __stdcall GetProcAddress(HINSTANCE__* hModule, const char* lpProcName);
__declspec(dllimport) unsigned long __stdcall GetLastError(void);
__declspec(dllimport) unsigned long __stdcall GetModuleFileNameW(HINSTANCE__* hModule, wchar_t* lpFilename, unsigned long nSize);
}
#endif

// Override to load the DLL from somewhere else, e.g. #define SIMPLYUI_DLL_NAME L"bin\\simplyui.dll"
#ifndef SIMPLYUI_DLL_NAME
#ifdef _WIN64
#define SIMPLYUI_DLL_NAME L"simplyui.dll"
#else
#define SIMPLYUI_DLL_NAME L"simplyui32.dll"
#endif
#endif

#if defined(SIMPLYUI_NO_CONSOLE) && defined(_MSC_VER)
#pragma comment(linker, "/SUBSYSTEM:WINDOWS /ENTRY:mainCRTStartup")
#endif

namespace sui {
namespace api {

template <class F, class Module> inline F GetProc(Module module, const char* name) {
    return module ? reinterpret_cast<F>(reinterpret_cast<void (*)()>(GetProcAddress(module, name))) : nullptr;
}

// Shows what went wrong and ends the program (it cannot run without the DLL).
[[noreturn]] inline void Fail(const std::wstring& message) {
    typedef int (__stdcall* MessageBoxFn)(void* owner, const wchar_t* text, const wchar_t* caption, unsigned int type);
    MessageBoxFn box = GetProc<MessageBoxFn>(LoadLibraryW(L"user32.dll"), "MessageBoxW");
    if (box) box(nullptr, message.c_str(), L"SimplyUI", 0x10u /* MB_ICONERROR */);
    std::_Exit(1);
}

inline std::wstring ProgramFolder() {
    wchar_t path[1024];
    unsigned long n = GetModuleFileNameW(nullptr, path, 1024);
    std::wstring s(path, n > 0 && n < 1024 ? n : 0);
    size_t slash = s.find_last_of(L"\\/");
    return slash == std::wstring::npos ? s : s.substr(0, slash);
}

inline const Table* Load() {
    const std::wstring name = SIMPLYUI_DLL_NAME;
    auto dll = LoadLibraryW(name.c_str());
    if (!dll) {
        unsigned long error = GetLastError();
        if (error == 126) // ERROR_MOD_NOT_FOUND
            Fail(name + L" was not found.\n\nPut " + name + L" in the same folder as the program:\n" + ProgramFolder());
        if (error == 193) // ERROR_BAD_EXE_FORMAT
            Fail(name + L" is for a different CPU type than this program.\n\n"
                 L"64-bit programs use simplyui.dll, 32-bit programs use simplyui32.dll.");
        Fail(name + L" could not be loaded (error " + std::to_wstring(error) + L").");
    }
    GetApiFn getApi = GetProc<GetApiFn>(dll, "sui_GetApi");
    VersionFn version = GetProc<VersionFn>(dll, "sui_Version");
    if (!getApi || !version) Fail(name + L" is not a SimplyUI DLL.");
    const Table* table = getApi(kAbiVersion, static_cast<uint32_t>(sizeof(Table)), static_cast<uint32_t>(sizeof(Style)),
                                static_cast<uint32_t>(sizeof(ItemInfo)), static_cast<uint32_t>(sizeof(Canvas)));
    if (!table) {
        std::wstring dllVersion;
        for (const char* v = version(); v && *v; v++) dllVersion += static_cast<wchar_t>(static_cast<unsigned char>(*v));
        Fail(name + L" (version " + dllVersion + L") does not match the simplyui.h this program was built with "
             L"(version " SIMPLYUI_VERSION L").\n\nUse simplyui.h and the DLL from the same SimplyUI release.");
    }
    return table;
}

// The function table, loaded on first use.
inline const Table& Get() {
    static const Table* table = Load();
    return *table;
}

// Callbacks the DLL uses to hand strings and lists back to your program.
inline void SUI_CALL SetString(void* user, const char* text, size_t length) {
    static_cast<std::string*>(user)->assign(text, length);
}
inline void SUI_CALL AddString(void* user, const char* text, size_t length) {
    static_cast<std::vector<std::string>*>(user)->push_back(std::string(text, length));
}
inline void SUI_CALL SetWString(void* user, const wchar_t* text, size_t length) {
    static_cast<std::wstring*>(user)->assign(text, length);
}
inline const char* SUI_CALL ItemFromCStrs(const void* list, int index, size_t* length) {
    const char* s = static_cast<const char* const*>(list)[index];
    if (!s) s = "";
    *length = std::strlen(s);
    return s;
}
inline const char* SUI_CALL ItemFromStrings(const void* list, int index, size_t* length) {
    const std::string& s = (*static_cast<const std::vector<std::string>*>(list))[static_cast<size_t>(index)];
    *length = s.size();
    return s.c_str();
}

struct RunState {
    const std::function<void()>* frame;
    std::exception_ptr error;
};
inline void SUI_CALL RunFrame(void* user) {
    RunState* state = static_cast<RunState*>(user);
    if (state->error) return;
    try {
        (*state->frame)();
    } catch (...) {   // exceptions must not travel through the DLL: rethrown when Run() returns
        state->error = std::current_exception();
        Get().Quit(1);
    }
}

} // namespace api

// ---------------------------------------------------------------------------------
// The functions above, forwarded to simplyui.dll
// ---------------------------------------------------------------------------------

inline int Run(const Config& config, const std::function<void()>& frame) {
    api::RunConfig rc;
    rc.size = static_cast<uint32_t>(sizeof(rc));
    rc.title = config.title.c_str();
    rc.width = config.width;
    rc.height = config.height;
    rc.minWidth = config.minWidth;
    rc.minHeight = config.minHeight;
    rc.resizable = config.resizable ? 1 : 0;
    rc.topmost = config.topmost ? 1 : 0;
    rc.scroll = config.scroll ? 1 : 0;
    rc.rememberWindow = config.rememberWindow ? 1 : 0;
    rc.theme = static_cast<int>(config.theme);
    rc.accent = config.accent;
    rc.appId = config.appId.c_str();
    rc.iconResource = config.iconResource;
    rc.tray = config.tray ? 1 : 0;
    rc.startHidden = config.startHidden ? 1 : 0;
    rc.singleInstance = config.singleInstance ? 1 : 0;
    api::RunState state;
    state.frame = &frame;
    int code = api::Get().Run(&rc, &api::RunFrame, &state);
    if (state.error) std::rethrow_exception(state.error);
    return code;
}
inline int Run(const char* title, int width, int height, const std::function<void()>& frame) {
    Config config;
    config.title = title ? title : "SimplyUI";
    config.width = width;
    config.height = height;
    return Run(config, frame);
}

inline void PlotLines(Str label, const std::vector<float>& values, float height, float min, float max) {
    PlotLines(label, values.data(), static_cast<int>(values.size()), height, min, max);
}
inline void PlotBars(Str label, const std::vector<float>& values, float height, float min, float max) {
    PlotBars(label, values.data(), static_cast<int>(values.size()), height, min, max);
}

inline const std::vector<std::string>& DroppedFiles() {
    static std::vector<std::string> files;
    static uint64_t filesFrame = ~static_cast<uint64_t>(0);
    uint64_t frame = FrameCount();
    if (frame != filesFrame) {     // fetch once per frame, so the list stays valid during it
        filesFrame = frame;
        files.clear();
        api::Get().DroppedFiles(&api::AddString, &files);
    }
    return files;
}
inline std::wstring ToWide(Str utf8) {
    std::wstring r;
    api::Get().ToWide(utf8.s, utf8.n, &api::SetWString, &r);
    return r;
}
inline std::string ToUtf8(const std::wstring& wide) {
    std::string r;
    api::Get().ToUtf8(wide.c_str(), wide.size(), &api::SetString, &r);
    return r;
}

inline Log::Log(size_t maxLines) : handle_(api::Get().LogCreate(maxLines)) {}
inline Log::~Log() { api::Get().LogDestroy(handle_); }
inline void Log::Add(Level level, Str text) { api::Get().LogAdd(handle_, static_cast<int>(level), text.s, text.n); }
inline void Log::Info(const char* fmt, ...) {
    va_list args; va_start(args, fmt); std::string s = api::VFormat(fmt, args); va_end(args);
    Add(Level::Info, s);
}
inline void Log::Success(const char* fmt, ...) {
    va_list args; va_start(args, fmt); std::string s = api::VFormat(fmt, args); va_end(args);
    Add(Level::Success, s);
}
inline void Log::Warn(const char* fmt, ...) {
    va_list args; va_start(args, fmt); std::string s = api::VFormat(fmt, args); va_end(args);
    Add(Level::Warning, s);
}
inline void Log::Error(const char* fmt, ...) {
    va_list args; va_start(args, fmt); std::string s = api::VFormat(fmt, args); va_end(args);
    Add(Level::Error, s);
}
inline void Log::Clear() { api::Get().LogClear(handle_); }
inline size_t Log::Count() const { return api::Get().LogCount(handle_); }
inline std::string Log::Text() const {
    std::string r;
    api::Get().LogText(handle_, &api::SetString, &r);
    return r;
}
inline void LogView(Str id, Log& log, float height, bool showTime) {
    api::Get().LogView(id.s, id.n, log.Handle(), height, showTime ? 1 : 0);
}

// Running an app
inline void Quit(int exitCode) { api::Get().Quit(exitCode); }
inline bool CloseRequested() { return api::Get().CloseRequested() != 0; }
inline void CancelClose() { api::Get().CancelClose(); }
inline void SetTitle(Str title) { api::Get().SetTitle(title.s, title.n); }
inline void SetWindowSize(int width, int height) { api::Get().SetWindowSize(width, height); }
inline Vec2 WindowSize() { Vec2 r; api::Get().WindowSize(&r); return r; }
inline void SetTopmost(bool topmost) { api::Get().SetTopmost(topmost ? 1 : 0); }
inline bool IsTopmost() { return api::Get().IsTopmost() != 0; }
inline void Minimize() { api::Get().Minimize(); }
inline void BringToFront() { api::Get().BringToFront(); }
inline void HideWindow() { api::Get().HideWindow(); }
inline void ToggleWindow() { api::Get().ToggleWindow(); }
inline void* NativeWindow() { return api::Get().NativeWindow(); }
inline float DpiScale() { return api::Get().DpiScale(); }
inline double Time() { return api::Get().Time(); }
inline float DeltaTime() { return api::Get().DeltaTime(); }
inline uint64_t FrameCount() { return api::Get().FrameCount(); }
inline void RequestRedraw() { api::Get().RequestRedraw(); }
inline void RedrawIn(float seconds) { api::Get().RedrawIn(seconds); }
inline void Wake() { api::Get().Wake(); }

// Style, themes and fonts
inline Style& GetStyle() { return *api::Get().GetStyle(); }
inline void SetTheme(Theme theme) { api::Get().SetTheme(static_cast<int>(theme)); }
inline Theme GetTheme() { return static_cast<Theme>(api::Get().GetTheme()); }
inline bool IsDark() { return api::Get().IsDark() != 0; }
inline void SetAccent(Color accent) { api::Get().SetAccent(&accent); }
inline Color SystemAccent() { Color r; api::Get().SystemAccent(&r); return r; }
inline FontId AddFont(Str family, float size, bool bold, bool italic) { return api::Get().AddFont(family.s, family.n, size, bold ? 1 : 0, italic ? 1 : 0); }
inline void PushFont(FontId font) { api::Get().PushFont(font); }
inline void PopFont() { api::Get().PopFont(); }
inline float TextWidth(Str text, FontId font) { return api::Get().TextWidth(text.s, text.n, font); }
inline float LineHeight(FontId font) { return api::Get().LineHeight(font); }

// Input
inline Vec2 MousePos() { Vec2 r; api::Get().MousePos(&r); return r; }
inline bool MouseDown(int button) { return api::Get().MouseDown(button) != 0; }
inline bool MouseClicked(int button) { return api::Get().MouseClicked(button) != 0; }
inline bool MouseReleased(int button) { return api::Get().MouseReleased(button) != 0; }
inline bool MouseDoubleClicked(int button) { return api::Get().MouseDoubleClicked(button) != 0; }
inline float MouseWheel() { return api::Get().MouseWheel(); }
inline bool KeyDown(Key key) { return api::Get().KeyDown(static_cast<int>(key)) != 0; }
inline bool KeyPressed(Key key, bool repeat) { return api::Get().KeyPressed(static_cast<int>(key), repeat ? 1 : 0) != 0; }
inline bool KeyReleased(Key key) { return api::Get().KeyReleased(static_cast<int>(key)) != 0; }
inline bool CtrlDown() { return api::Get().CtrlDown() != 0; }
inline bool ShiftDown() { return api::Get().ShiftDown() != 0; }
inline bool AltDown() { return api::Get().AltDown() != 0; }
inline bool Shortcut(Str keys) { return api::Get().Shortcut(keys.s, keys.n) != 0; }
inline bool GlobalHotkey(Str keys) { return api::Get().GlobalHotkey(keys.s, keys.n) != 0; }
inline bool IsEditingText() { return api::Get().IsEditingText() != 0; }

// Layout
inline void SameLine(float spacing) { api::Get().SameLine(spacing); }
inline void NewLine() { api::Get().NewLine(); }
inline void Spacing(float height) { api::Get().Spacing(height); }
inline void Spacer(float width, float height) { api::Get().Spacer(width, height); }
inline void Indent(float amount) { api::Get().Indent(amount); }
inline void Unindent(float amount) { api::Get().Unindent(amount); }
inline void NextWidth(float width) { api::Get().NextWidth(width); }
inline float AvailableWidth() { return api::Get().AvailableWidth(); }
inline void BeginRow(int columns) { api::Get().BeginRow(columns); }
inline void BeginRow(std::initializer_list<float> weights) { api::Get().BeginRowWeights(weights.begin(), static_cast<int>(weights.size())); }
inline void EndRow() { api::Get().EndRow(); }
inline void BeginColumns(int columns) { api::Get().BeginColumns(columns); }
inline void BeginColumns(std::initializer_list<float> weights) { api::Get().BeginColumnsWeights(weights.begin(), static_cast<int>(weights.size())); }
inline void NextColumn() { api::Get().NextColumn(); }
inline void EndColumns() { api::Get().EndColumns(); }
inline void BeginHorizontal(Align align) { api::Get().BeginHorizontal(static_cast<int>(align)); }
inline void EndHorizontal() { api::Get().EndHorizontal(); }
inline void BeginCard(Str title) { api::Get().BeginCard(title.s, title.n); }
inline void EndCard() { api::Get().EndCard(); }
inline void BeginChild(Str id, float height, bool border) { api::Get().BeginChild(id.s, id.n, height, border ? 1 : 0); }
inline void EndChild() { api::Get().EndChild(); }
inline void BeginFooter() { api::Get().BeginFooter(); }
inline void EndFooter() { api::Get().EndFooter(); }
inline void BeginDisabled(bool disabled) { api::Get().BeginDisabled(disabled ? 1 : 0); }
inline void EndDisabled() { api::Get().EndDisabled(); }
inline void PushID(Str id) { api::Get().PushIDStr(id.s, id.n); }
inline void PushID(int id) { api::Get().PushIDInt(id); }
inline void PushID(const void* ptr) { api::Get().PushIDPtr(ptr); }
inline void PopID() { api::Get().PopID(); }
inline ID GetID(Str label) { return api::Get().GetID(label.s, label.n); }
inline const ItemInfo& LastItem() { return *api::Get().LastItem(); }
inline void Tooltip(Str text) { api::Get().Tooltip(text.s, text.n); }
inline void FocusNext() { api::Get().FocusNext(); }

// Text
inline void Text(Str text) { api::Get().Text(text.s, text.n); }
inline void TextDim(Str text) { api::Get().TextDim(text.s, text.n); }
inline void TextColored(Color color, Str text) { api::Get().TextColored(&color, text.s, text.n); }
inline void TextWrapped(Str text) { api::Get().TextWrapped(text.s, text.n); }
inline void Heading(Str text) { api::Get().Heading(text.s, text.n); }
inline void Title(Str text) { api::Get().Title(text.s, text.n); }
inline void LabelText(Str label, Str value) { api::Get().LabelText(label.s, label.n, value.s, value.n); }
inline void BulletText(Str text) { api::Get().BulletText(text.s, text.n); }
inline bool Link(Str text, Str url) { return api::Get().Link(text.s, text.n, url.s, url.n) != 0; }
inline void Badge(Str text, Color color) { api::Get().BadgeColor(text.s, text.n, &color); }
inline void Badge(Str text, Level level) { api::Get().BadgeLevel(text.s, text.n, static_cast<int>(level)); }
inline void Separator() { api::Get().Separator(); }
inline void SeparatorText(Str text) { api::Get().SeparatorText(text.s, text.n); }

// Widgets
inline bool Button(Str label) { return api::Get().Button(label.s, label.n) != 0; }
inline bool Button(Str label, Icon icon) { return api::Get().ButtonIcon(label.s, label.n, static_cast<int>(icon)) != 0; }
inline bool PrimaryButton(Str label) { return api::Get().PrimaryButton(label.s, label.n) != 0; }
inline bool DangerButton(Str label) { return api::Get().DangerButton(label.s, label.n) != 0; }
inline bool IconButton(Icon icon, Str tooltip) { return api::Get().IconButton(static_cast<int>(icon), tooltip.s, tooltip.n) != 0; }
inline bool Checkbox(Str label, bool& value) { return api::Get().Checkbox(label.s, label.n, &value) != 0; }
inline bool Toggle(Str label, bool& value) { return api::Get().Toggle(label.s, label.n, &value) != 0; }
inline bool RadioButton(Str label, int& current, int value) { return api::Get().RadioButton(label.s, label.n, &current, value) != 0; }
inline bool Selectable(Str label, bool selected) { return api::Get().Selectable(label.s, label.n, selected ? 1 : 0) != 0; }
inline bool Slider(Str label, float& value, float min, float max, const char* format) { return api::Get().SliderFloat(label.s, label.n, &value, min, max, format) != 0; }
inline bool Slider(Str label, int& value, int min, int max, const char* format) { return api::Get().SliderInt(label.s, label.n, &value, min, max, format) != 0; }
inline bool Drag(Str label, float& value, float speed, float min, float max, const char* format) { return api::Get().DragFloat(label.s, label.n, &value, speed, min, max, format) != 0; }
inline bool Drag(Str label, int& value, float speed, int min, int max) { return api::Get().DragInt(label.s, label.n, &value, speed, min, max) != 0; }
inline bool InputInt(Str label, int& value, int step, int min, int max) { return api::Get().InputInt(label.s, label.n, &value, step, min, max) != 0; }
inline bool InputFloat(Str label, float& value, float step, float min, float max, const char* format) { return api::Get().InputFloat(label.s, label.n, &value, step, min, max, format) != 0; }
inline bool InputText(Str label, std::string& text, Str hint, int flags) { return api::Get().InputText(label.s, label.n, text.data(), text.size(), &api::SetString, &text, hint.s, hint.n, flags) != 0; }
inline bool InputMultiline(Str label, std::string& text, float height, int flags) { return api::Get().InputMultiline(label.s, label.n, text.data(), text.size(), &api::SetString, &text, height, flags) != 0; }
inline bool SearchBox(Str id, std::string& text, Str hint, int flags) { return api::Get().SearchBox(id.s, id.n, text.data(), text.size(), &api::SetString, &text, hint.s, hint.n, flags) != 0; }
inline bool Combo(Str label, int& current, const std::vector<std::string>& items) { return api::Get().Combo(label.s, label.n, &current, &items, static_cast<int>(items.size()), &api::ItemFromStrings) != 0; }
inline bool Combo(Str label, int& current, std::initializer_list<const char*> items) { return api::Get().Combo(label.s, label.n, &current, items.begin(), static_cast<int>(items.size()), &api::ItemFromCStrs) != 0; }
inline bool ListBox(Str label, int& current, const std::vector<std::string>& items, int visibleItems) { return api::Get().ListBox(label.s, label.n, &current, &items, static_cast<int>(items.size()), &api::ItemFromStrings, visibleItems) != 0; }
inline bool Tabs(int& current, std::initializer_list<const char*> tabs) { return api::Get().Tabs(&current, tabs.begin(), static_cast<int>(tabs.size()), &api::ItemFromCStrs) != 0; }
inline bool Tabs(int& current, const std::vector<std::string>& tabs) { return api::Get().Tabs(&current, &tabs, static_cast<int>(tabs.size()), &api::ItemFromStrings) != 0; }
inline bool Segmented(Str label, int& current, std::initializer_list<const char*> options) { return api::Get().Segmented(label.s, label.n, &current, options.begin(), static_cast<int>(options.size()), &api::ItemFromCStrs) != 0; }
inline bool CollapsingHeader(Str label, bool defaultOpen) { return api::Get().CollapsingHeader(label.s, label.n, defaultOpen ? 1 : 0) != 0; }
inline bool TreeNode(Str label, int flags) { return api::Get().TreeNode(label.s, label.n, flags) != 0; }
inline void TreePop() { api::Get().TreePop(); }
inline void ProgressBar(float fraction, Str overlay) { api::Get().ProgressBar(fraction, overlay.s, overlay.n); }
inline void Spinner(float size) { api::Get().Spinner(size); }
inline bool ColorEdit(Str label, Color& color, bool alpha) { return api::Get().ColorEdit(label.s, label.n, &color, alpha ? 1 : 0) != 0; }
inline void PlotLines(Str label, const float* values, int count, float height, float min, float max) { api::Get().PlotLines(label.s, label.n, values, count, height, min, max); }
inline void PlotBars(Str label, const float* values, int count, float height, float min, float max) { api::Get().PlotBars(label.s, label.n, values, count, height, min, max); }
inline std::vector<std::string> DropZone(Str label, float height) { std::vector<std::string> r; api::Get().DropZone(label.s, label.n, height, &api::AddString, &r); return r; }

// Tables
inline bool BeginTable(Str id, std::initializer_list<const char*> columns, int flags, float height) { return api::Get().BeginTable(id.s, id.n, columns.begin(), static_cast<int>(columns.size()), &api::ItemFromCStrs, flags, height) != 0; }
inline bool BeginTable(Str id, const std::vector<std::string>& columns, int flags, float height) { return api::Get().BeginTable(id.s, id.n, &columns, static_cast<int>(columns.size()), &api::ItemFromStrings, flags, height) != 0; }
inline bool TableNextRow(bool selected) { return api::Get().TableNextRow(selected ? 1 : 0) != 0; }
inline void TableNextCell() { api::Get().TableNextCell(); }
inline void EndTable() { api::Get().EndTable(); }
inline void NextTableWeights(std::initializer_list<float> weights) { api::Get().NextTableWeights(weights.begin(), static_cast<int>(weights.size())); }
inline TableSort TableSorting() { TableSort r; api::Get().TableSorting(&r); return r; }

// Log view

// Images and custom drawing
inline Texture LoadTexture(Str path) { Texture r; api::Get().LoadTexture(path.s, path.n, &r); return r; }
inline Texture LoadTextureFromMemory(const void* data, size_t size) { Texture r; api::Get().LoadTextureFromMemory(data, size, &r); return r; }
inline Texture CreateTexture(const void* rgba, int width, int height) { Texture r; api::Get().CreateTexture(rgba, width, height, &r); return r; }
inline void UpdateTexture(Texture& texture, const void* rgba) { api::Get().UpdateTexture(&texture, rgba); }
inline void FreeTexture(Texture& texture) { api::Get().FreeTexture(&texture); }
inline void Image(const Texture& texture, Vec2 size) { api::Get().Image(&texture, &size); }
inline Canvas BeginCanvas(float height, bool border) { Canvas r; api::Get().BeginCanvas(height, border ? 1 : 0, &r); return r; }
inline void EndCanvas() { api::Get().EndCanvas(); }
inline void Canvas::Line(Vec2 a, Vec2 b, Color c, float thickness) const { api::Get().CanvasLine(this, &a, &b, &c, thickness); }
inline void Canvas::FillRect(Rect r, Color c, float rounding) const { api::Get().CanvasFillRect(this, &r, &c, rounding); }
inline void Canvas::StrokeRect(Rect r, Color c, float rounding, float thickness) const { api::Get().CanvasStrokeRect(this, &r, &c, rounding, thickness); }
inline void Canvas::FillCircle(Vec2 center, float radius, Color c) const { api::Get().CanvasFillCircle(this, &center, radius, &c); }
inline void Canvas::StrokeCircle(Vec2 center, float radius, Color c, float thickness) const { api::Get().CanvasStrokeCircle(this, &center, radius, &c, thickness); }
inline void Canvas::Polyline(const std::vector<Vec2>& points, Color c, float thickness, bool closed) const { api::Get().CanvasPolyline(this, points.data(), static_cast<int>(points.size()), &c, thickness, closed ? 1 : 0); }
inline void Canvas::FillPolygon(const std::vector<Vec2>& points, Color c) const { api::Get().CanvasFillPolygon(this, points.data(), static_cast<int>(points.size()), &c); }
inline void Canvas::Bezier(Vec2 p0, Vec2 p1, Vec2 p2, Vec2 p3, Color c, float thickness) const { api::Get().CanvasBezier(this, &p0, &p1, &p2, &p3, &c, thickness); }
inline void Canvas::Text(Vec2 pos, Str text, Color c, FontId font) const { api::Get().CanvasText(this, &pos, text.s, text.n, &c, font); }
inline void Canvas::Image(const Texture& texture, Rect dest, float opacity) const { api::Get().CanvasImage(this, &texture, &dest, opacity); }
inline void Canvas::IconAt(Icon icon, Rect r, Color c) const { api::Get().CanvasIconAt(this, static_cast<int>(icon), &r, &c); }

// Popups, menus, modals, notifications
inline void OpenPopup(Str id) { api::Get().OpenPopup(id.s, id.n); }
inline bool BeginPopup(Str id) { return api::Get().BeginPopup(id.s, id.n) != 0; }
inline void EndPopup() { api::Get().EndPopup(); }
inline void ClosePopup() { api::Get().ClosePopup(); }
inline bool BeginContextMenu() { return api::Get().BeginContextMenu() != 0; }
inline void EndContextMenu() { api::Get().EndContextMenu(); }
inline void OpenModal(Str title) { api::Get().OpenModal(title.s, title.n); }
inline bool BeginModal(Str title) { return api::Get().BeginModal(title.s, title.n) != 0; }
inline void EndModal() { api::Get().EndModal(); }
inline void CloseModal() { api::Get().CloseModal(); }
inline bool BeginMenuBar() { return api::Get().BeginMenuBar() != 0; }
inline void EndMenuBar() { api::Get().EndMenuBar(); }
inline bool BeginMenu(Str label) { return api::Get().BeginMenu(label.s, label.n) != 0; }
inline void EndMenu() { api::Get().EndMenu(); }
inline bool MenuItem(Str label, Str shortcut, bool checked, bool enabled) { return api::Get().MenuItem(label.s, label.n, shortcut.s, shortcut.n, checked ? 1 : 0, enabled ? 1 : 0) != 0; }
inline bool MenuItem(Str label, Str shortcut, bool* toggle, bool enabled) { return api::Get().MenuItemToggle(label.s, label.n, shortcut.s, shortcut.n, toggle, enabled ? 1 : 0) != 0; }
inline void MenuSeparator() { api::Get().MenuSeparator(); }
inline void Toast(Str text, Level level, float seconds) { api::Get().Toast(text.s, text.n, static_cast<int>(level), seconds); }

// Native dialogs and system helpers
inline std::string OpenFileDialog(Str filter, Str title) { std::string r; api::Get().OpenFileDialog(filter.s, filter.n, title.s, title.n, &api::SetString, &r); return r; }
inline std::vector<std::string> OpenFilesDialog(Str filter, Str title) { std::vector<std::string> r; api::Get().OpenFilesDialog(filter.s, filter.n, title.s, title.n, &api::AddString, &r); return r; }
inline std::string SaveFileDialog(Str filter, Str defaultName, Str title) { std::string r; api::Get().SaveFileDialog(filter.s, filter.n, defaultName.s, defaultName.n, title.s, title.n, &api::SetString, &r); return r; }
inline std::string PickFolder(Str title) { std::string r; api::Get().PickFolder(title.s, title.n, &api::SetString, &r); return r; }
inline void ShowMessage(Str title, Str text, Level level) { api::Get().ShowMessage(title.s, title.n, text.s, text.n, static_cast<int>(level)); }
inline bool Confirm(Str title, Str text) { return api::Get().Confirm(title.s, title.n, text.s, text.n) != 0; }
inline void Beep(Level level) { api::Get().Beep(static_cast<int>(level)); }
inline void OpenURL(Str urlOrPath) { api::Get().OpenURL(urlOrPath.s, urlOrPath.n); }
inline void ShowInExplorer(Str path) { api::Get().ShowInExplorer(path.s, path.n); }
inline void SetClipboard(Str text) { api::Get().SetClipboard(text.s, text.n); }
inline std::string GetClipboard() { std::string r; api::Get().GetClipboard(&api::SetString, &r); return r; }
inline std::vector<std::string> CommandLine() { std::vector<std::string> r; api::Get().CommandLine(&api::AddString, &r); return r; }
inline void TaskbarProgress(float fraction, Level level) { api::Get().TaskbarProgress(fraction, static_cast<int>(level)); }

// Settings
inline void Persist(Str key, bool& value) { api::Get().PersistBool(key.s, key.n, &value); }
inline void Persist(Str key, int& value) { api::Get().PersistInt(key.s, key.n, &value); }
inline void Persist(Str key, float& value) { api::Get().PersistFloat(key.s, key.n, &value); }
inline void Persist(Str key, std::string& value) { api::Get().PersistString(key.s, key.n, value.data(), value.size(), &api::SetString, &value); }
inline void Persist(Str key, Color& value) { api::Get().PersistColor(key.s, key.n, &value); }
inline void SaveSettings() { api::Get().SaveSettings(); }
inline std::string SettingsPath() { std::string r; api::Get().SettingsPath(&api::SetString, &r); return r; }
} // namespace sui

#endif // !SIMPLYUI_BUILDING_DLL

#pragma pack(pop)

#endif // RC_INVOKED
#endif // SIMPLYUI_H

/*
-----------------------------------------------------------------------------------
MIT License

Copyright (c) 2026 inrune

Permission is hereby granted, free of charge, to any person obtaining a copy of this
software and associated documentation files (the "Software"), to deal in the Software
without restriction, including without limitation the rights to use, copy, modify,
merge, publish, distribute, sublicense, and/or sell copies of the Software, and to
permit persons to whom the Software is furnished to do so, subject to the following
conditions:

The above copyright notice and this permission notice shall be included in all copies
or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED,
INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A
PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT
HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF
CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE
OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
-----------------------------------------------------------------------------------
*/
