/*
  SimplyUI v1.0.0 - DLL source.

  This file is compiled into simplyui.dll (64-bit) and simplyui32.dll (32-bit).
  Programs never include it: they only need include/simplyui.h and the DLL.
  To build the DLLs, the examples and the release zip, double-click build.bat.

  The DLL exports two plain C functions (sui_GetApi and sui_Version). sui_GetApi
  returns the table of C functions declared in simplyui.h (sui::api::Table), so
  a program made with any compiler can use a DLL made with Visual Studio.

  Adding a function: declare it in simplyui.h, implement it here, then add a
  pointer to sui::api::Table (at the END, so older programs keep working), a
  c_... adapter below, and an inline wrapper at the end of simplyui.h.
*/

#define SIMPLYUI_BUILDING_DLL
#include "../include/simplyui.h"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <shellapi.h>
#include <ole2.h>
#include <shobjidl.h>
#include <imm.h>
#include <d2d1.h>
#include <d2d1helper.h>
#include <dwrite.h>
#include <wincodec.h>

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstdarg>
#include <algorithm>
#include <unordered_map>
#include <map>
#include <memory>
#include <exception>
#include <atomic>
#include <cctype>
#include <cwctype>
#include <deque>
#include <mutex>
#include <new>

#ifdef _MSC_VER
#pragma comment(lib, "user32.lib")
#pragma comment(lib, "shell32.lib")
#pragma comment(lib, "advapi32.lib")
#endif

namespace sui {
// Overloads used by the DLL interface (declared here, defined near the end).
bool ListBox(Str label, int& current, const std::vector<Str>& items, int visibleItems);
void BeginRow(const float* weights, int count);
void BeginColumns(const float* weights, int count);
void NextTableWeights(const float* weights, int count);
bool Combo(Str label, int& current, const std::vector<Str>& items);
bool Tabs(int& current, const std::vector<Str>& tabs);
bool Segmented(Str label, int& current, const std::vector<Str>& options);
bool BeginTable(Str id, const std::vector<Str>& columns, int flags, float height);

namespace detail {

// ---------------------------------------------------------------------------------
// Small helpers
// ---------------------------------------------------------------------------------

static const float kPi = 3.14159265358979f;
template <class T> static inline T Mn(T a, T b) { return a < b ? a : b; }
template <class T> static inline T Mx(T a, T b) { return a > b ? a : b; }
// (NaN-safe: NaN -> lo)
static inline float Clampf(float v, float lo, float hi) { return v > lo ? (v < hi ? v : hi) : lo; }
static inline double ClampD(double v, double lo, double hi) { return v > lo ? (v < hi ? v : hi) : lo; }
static inline float Lerpf(float a, float b, float t) { return a + (b - a) * t; }
static inline Color LerpC(Color a, Color b, float t) {
    return Color(a.r + (b.r - a.r) * t, a.g + (b.g - a.g) * t, a.b + (b.b - a.b) * t, a.a + (b.a - a.a) * t);
}
static inline Color MulA(Color c, float a) { c.a *= a; return c; }
static inline bool Overlaps(const Rect& a, const Rect& b) {
    return a.x < b.x + b.w && b.x < a.x + a.w && a.y < b.y + b.h && b.y < a.y + a.h;
}
static inline Rect Intersect(const Rect& a, const Rect& b) {
    float x0 = Mx(a.x, b.x), y0 = Mx(a.y, b.y);
    float x1 = Mn(a.x + a.w, b.x + b.w), y1 = Mn(a.y + a.h, b.y + b.h);
    return Rect(x0, y0, Mx(0.0f, x1 - x0), Mx(0.0f, y1 - y0));
}
template <class T> static inline void SafeRelease(T*& p) { if (p) { p->Release(); p = nullptr; } }

static std::wstring Widen(const char* s, size_t n) {
    if (!s || n == 0) return std::wstring();
    int len = MultiByteToWideChar(CP_UTF8, 0, s, (int)n, nullptr, 0);
    std::wstring w((size_t)Mx(len, 0), L'\0');
    if (len > 0) MultiByteToWideChar(CP_UTF8, 0, s, (int)n, &w[0], len);
    return w;
}
static std::wstring Widen(Str s) { return Widen(s.s, s.n); }
static std::string Narrow(const wchar_t* w, size_t n) {
    if (!w || n == 0) return std::string();
    int len = WideCharToMultiByte(CP_UTF8, 0, w, (int)n, nullptr, 0, nullptr, nullptr);
    std::string s((size_t)Mx(len, 0), '\0');
    if (len > 0) WideCharToMultiByte(CP_UTF8, 0, w, (int)n, &s[0], len, nullptr, nullptr);
    return s;
}
static std::string Narrow(const std::wstring& w) { return Narrow(w.data(), w.size()); }

static inline uint32_t Hash32(const void* data, size_t n, uint32_t seed) {
    const unsigned char* p = (const unsigned char*)data;
    uint32_t h = seed ^ 2166136261u;
    for (size_t i = 0; i < n; i++) { h ^= p[i]; h *= 16777619u; }
    return h ? h : 1u;
}
static inline uint64_t Hash64(const void* data, size_t n, uint64_t seed) {
    const unsigned char* p = (const unsigned char*)data;
    uint64_t h = seed ^ 1469598103934665603ull;
    for (size_t i = 0; i < n; i++) { h ^= p[i]; h *= 1099511628211ull; }
    return h;
}

// "Label##id" -> "Label"
static Str DisplayText(Str s) {
    for (size_t i = 0; i + 1 < s.n; i++)
        if (s.s[i] == '#' && s.s[i + 1] == '#') return Str(s.s, i);
    return s;
}

static void DebugLog(const char* msg) {
    OutputDebugStringA("[SimplyUI] ");
    OutputDebugStringA(msg);
    OutputDebugStringA("\n");
}
// Reports a usage mistake once (shown in the Visual Studio Output window / DebugView).
static void Note(const char* msg) {
    static const char* said[64];
    static int count = 0;
    for (int i = 0; i < count; i++) if (said[i] == msg) return;
    if (count < 64) said[count++] = msg;
    DebugLog(msg);
}

// GUIDs (defined here so nothing has to be linked)
static const GUID kIID_ID2D1Factory = {0x06152247, 0x6f50, 0x465a, {0x92, 0x45, 0x11, 0x8b, 0xfd, 0x3b, 0x60, 0x07}};
static const GUID kIID_IDWriteFactory = {0xb859ee5a, 0xd838, 0x4b5b, {0xa2, 0xe8, 0x1a, 0xdc, 0x7d, 0x93, 0xdb, 0x48}};
static const GUID kIID_ID2D1DeviceContext1 = {0xd37f57e4, 0x6908, 0x459f, {0xa1, 0x99, 0xe7, 0x2f, 0x24, 0xf7, 0x99, 0x87}};
static const GUID kCLSID_TaskbarList = {0x56fdf344, 0xfd6d, 0x11d0, {0x95, 0x8a, 0x00, 0x60, 0x97, 0xc9, 0xa0, 0x90}};
static const GUID kIID_ITaskbarList3 = {0xea1afb91, 0x9e28, 0x4b86, {0x90, 0xe9, 0x9e, 0x9f, 0x8a, 0x5e, 0xef, 0xaf}};
static const GUID kCLSID_WICImagingFactory = {0xcacaf262, 0x9370, 0x4615, {0xa1, 0x3b, 0x9f, 0x55, 0x39, 0xda, 0x4c, 0x0a}};
static const GUID kIID_IWICImagingFactory = {0xec5ec8a9, 0xc395, 0x4314, {0x9c, 0x77, 0x54, 0xd7, 0xa9, 0x35, 0xff, 0x70}};
static const GUID kGUID_WICPixelFormat32bppPBGRA = {0x6fddc324, 0x4e03, 0x4bfe, {0xb1, 0x85, 0x3d, 0x77, 0x76, 0x8d, 0xc9, 0x10}};
static const GUID kCLSID_FileOpenDialog = {0xdc1c5a9c, 0xe88a, 0x4dde, {0xa5, 0xa1, 0x60, 0xf8, 0x2a, 0x20, 0xae, 0xf7}};
static const GUID kCLSID_FileSaveDialog = {0xc0b4e2f3, 0xba21, 0x4773, {0x8d, 0xba, 0x33, 0x5e, 0xc9, 0x46, 0xeb, 0x8b}};
static const GUID kIID_IFileOpenDialog = {0xd57c7288, 0xd4ad, 0x4768, {0xbe, 0x02, 0x9d, 0x96, 0x95, 0x32, 0xd9, 0x60}};
static const GUID kIID_IFileSaveDialog = {0x84bccd23, 0x5fde, 0x4cdb, {0xae, 0xa4, 0xaf, 0x64, 0xb8, 0x3d, 0x78, 0xab}};

// System cursors (MAKEINTRESOURCEW so this works with and without UNICODE defined)
static const LPCWSTR kCurArrow = MAKEINTRESOURCEW(32512);
static const LPCWSTR kCurIBeam = MAKEINTRESOURCEW(32513);
static const LPCWSTR kCurSizeWE = MAKEINTRESOURCEW(32644);
static const LPCWSTR kCurHand = MAKEINTRESOURCEW(32649);

// Dynamically loaded functions (keeps the link line empty and supports old Windows)
typedef HRESULT (WINAPI* PFN_D2D1CreateFactory)(D2D1_FACTORY_TYPE, REFIID, const D2D1_FACTORY_OPTIONS*, void**);
typedef HRESULT (WINAPI* PFN_DWriteCreateFactory)(DWRITE_FACTORY_TYPE, REFIID, IUnknown**);
typedef HRESULT (WINAPI* PFN_CoInitializeEx)(LPVOID, DWORD);
typedef void    (WINAPI* PFN_CoUninitialize)(void);
typedef HRESULT (WINAPI* PFN_CoCreateInstance)(REFCLSID, LPUNKNOWN, DWORD, REFIID, LPVOID*);
typedef void    (WINAPI* PFN_CoTaskMemFree)(LPVOID);
typedef HRESULT (WINAPI* PFN_DwmSetWindowAttribute)(HWND, DWORD, LPCVOID, DWORD);
typedef HRESULT (WINAPI* PFN_DwmGetColorizationColor)(DWORD*, BOOL*);
typedef BOOL    (WINAPI* PFN_SetProcessDpiAwarenessContext)(HANDLE);
typedef HRESULT (WINAPI* PFN_SetProcessDpiAwareness)(int);
typedef BOOL    (WINAPI* PFN_SetProcessDPIAware)(void);
typedef UINT    (WINAPI* PFN_GetDpiForWindow)(HWND);
typedef BOOL    (WINAPI* PFN_AdjustWindowRectExForDpi)(LPRECT, DWORD, BOOL, DWORD, UINT);
typedef HIMC    (WINAPI* PFN_ImmGetContext)(HWND);
typedef BOOL    (WINAPI* PFN_ImmReleaseContext)(HWND, HIMC);
typedef BOOL    (WINAPI* PFN_ImmSetCompositionWindow)(HIMC, LPCOMPOSITIONFORM);
typedef BOOL    (WINAPI* PFN_ImmSetCandidateWindow)(HIMC, LPCANDIDATEFORM);

// ---------------------------------------------------------------------------------
// Internal data structures
// ---------------------------------------------------------------------------------

enum TextFlags { TF_Wrap = 1, TF_Ellipsis = 2, TF_Center = 4 };

struct FontSlot {
    std::wstring family;
    float size = 14;
    int weight = 400;
    bool italic = false;
    IDWriteTextFormat* fmt = nullptr;
    IDWriteInlineObject* ellipsis = nullptr;
    float lineH = 18;
};

struct TextEntry {
    IDWriteTextLayout* layout = nullptr;
    std::string key;
    int font = 0, flags = 0, widthKey = 0;
    bool wide = false;
    float w = 0, h = 0;
    uint64_t frame = 0;
};

enum CmdKind : uint8_t {
    CK_Rect, CK_RectStroke, CK_Line, CK_Circle, CK_CircleStroke, CK_Text, CK_Image,
    CK_ClipPush, CK_ClipPop, CK_Poly, CK_PolyStroke, CK_Icon, CK_Gradient, CK_Shadow,
    CK_Arc, CK_Bezier
};

struct Cmd {
    uint8_t kind = 0, flags = 0;
    float x = 0, y = 0, w = 0, h = 0;
    float r = 0, t = 0, u = 0;
    Color c, c2;
    void* p = nullptr;
    int i0 = 0, i1 = 0;
};

struct Layer {
    ID key = 0;
    int z = 0;
    float alpha = 1, scale = 1;
    Vec2 center;
    std::vector<Cmd> cmds;
    std::vector<Vec2> pts;
};

struct TextureSlot {
    std::vector<uint8_t> pixels;     // premultiplied BGRA
    int w = 0, h = 0;
    ID2D1Bitmap* bmp = nullptr;
    bool used = false, dirty = false;
};

struct WState {                      // per-widget persistent state (animations etc.)
    float a = 0, b = 0, c = 0, d = 0;
    int i = 0;
    bool open = false, init = false;
    bool keep = false;               // never garbage-collect (open/closed state of headers, trees)
    uint64_t frame = 0;
};

struct ContState {                   // per-container persistent state
    float labelW = 0;
    float scroll = 0, scrollTarget = 0, contentH = 0, viewH = 0;
    float lastW = 0, lastH = 0;
    bool hadBar = false;
    float barHover = 0;
    float grab = 0;
};

struct AlignFix { int layer; int start, end; float h; };

enum LayoutKind { LK_Root, LK_Child, LK_Card, LK_Columns, LK_Popup, LK_Footer, LK_MenuBar, LK_Table, LK_Cell };

struct LayoutState {
    int kind = 0;
    ID id = 0;
    Rect rect, clip;
    float left = 0, right = 0, indent = 0;
    float lineY = 0, lineH = 0, lineX = 0;
    bool lineItems = false;
    bool sameLine = false;
    float sameSpacing = -1;
    bool hasForceX = false;
    float forceX = 0;
    float startY = 0, maxX = 0, maxY = 0;
    float labelW = 0, labelMeasure = 0;
    ID labelKey = 0;
    int rowN = 0, rowCell = 0;
    float rowX[16], rowW[16];
    int horiz = 0;
    ID horizId = 0;
    float horizStartX = 0;
    int colN = 0, colIdx = 0;
    float colX[16], colW[16];
    float colStartY = 0, colMaxY = 0;
    ID colId = 0;
    std::vector<AlignFix> fixes;
    int layer = 0;
    size_t cmdMark = 0;
    float pad = 0;
    bool border = false;
    int popupIdx = -1;
    float savedFrameH = 0;
    float scrollY = 0;
    float natW = 0;
    ID layerKey = 0;
    int counter = 0;
    float spacingY = -1;
    bool lineCompact = false;
    int savedDepth = 0;
};

enum PopupKind { PK_Popup, PK_Modal, PK_Menu, PK_Combo, PK_Context, PK_Color };

struct PopupState {
    ID id = 0;
    int kind = PK_Popup;
    Vec2 openMouse;
    Rect anchor;
    Rect rect;            // rect from the previous frame (hit testing)
    Vec2 size;            // measured content size
    float anim = 0;
    bool submitted = false, sized = false, fromMenuBar = false, closing = false;
    uint64_t openFrame = 0;
};

struct ToastItem {
    std::string text;
    Level level = Level::Info;
    double start = 0;
    float duration = 3, anim = 0, y = 0;
    bool dying = false;
};

enum EventType { EV_Key, EV_Char };
struct InputEvent { int type; int vk; wchar_t ch; bool ctrl, shift, alt; };

struct TextEdit {
    ID id = 0;
    std::wstring text;
    int cursor = 0, anchor = 0;
    float scrollX = 0, scrollY = 0;
    bool multiline = false;
    int flags = 0;
    std::string synced;              // the user string as we last saw / wrote it
    struct Snap { std::wstring text; int cursor, anchor; };
    std::vector<Snap> undo, redo;
    int lastKind = 0;
    double lastEditTime = 0;
    float wantX = -1;
    bool seen = false, dragging = false;
    float contentH = 0;
};

struct TableState {
    ID id = 0;
    int cols = 0, flags = 0;
    std::vector<std::string> names;
    float x = 0, w = 0, y0 = 0;
    float colX[64], colW[64];
    int row = -1, cell = -1;
    float rowY = 0, rowH = 0;
    bool active = false, scroll = false, cellOpen = false;
    TableSort sort;
    int layoutDepth = 0;
    int clickedRow = -1;
    float height = 0, headH = 0, savedFrameH = 0;
};

struct PersistEntry { int type = 0; std::string value; bool loaded = false; };

// ---------------------------------------------------------------------------------
// The global context
// ---------------------------------------------------------------------------------

struct Context {
    // App
    Config cfg;
    const std::function<void()>* frameFn = nullptr;
    HWND hwnd = nullptr;
    HINSTANCE hinst = nullptr;
    bool running = false, inFrame = false, shown = false;
    std::atomic<bool> quit{false};
    std::atomic<int> exitCode{0};
    DWORD uiThread = 0;                  // the thread running Run()

    // Requests from other threads, applied at the start of the next frame
    std::mutex xmutex;
    std::vector<ToastItem> xToasts;
    std::wstring xTitle;
    bool xHasTitle = false, xSave = false;
    double xWakeAt = 1e300;
    bool closeRequested = false, cancelClose = false;
    std::exception_ptr pendingException;

    // Tray icon, taskbar button, single instance
    bool trayShown = false, exiting = false;
    HICON iconSmall = nullptr;
    double deactivatedAt = -1e9, raisedAt = -1e9;   // for toggling from the tray icon
    double hiddenAt = -1;                            // toasts wait while the window can't be seen
    HANDLE instanceMutex = nullptr;
    ITaskbarList3* taskbar = nullptr;
    bool taskbarReady = false, tbCalled = false;
    float tbFraction = 0, tbValue = -1;
    Level tbLevel = Level::Info;
    int tbState = -1;
    bool focusNext = false;
    bool trackingMouse = false, sizing = false;
    bool rendering = false, resizePending = false;   // a WM_SIZE that arrives mid-draw is applied after EndDraw
    LPCWSTR cursor = kCurArrow;
    std::wstring title;

    // DPI / size
    float dpi = 96, scale = 1;
    int pxW = 1, pxH = 1;
    float viewW = 1, viewH = 1;

    // Time
    LARGE_INTEGER qpf, qpc0;
    double time = 0, prevTime = 0;
    float dt = 0.016f;
    uint64_t frame = 0;
    int wantFrames = 0;
    double nextWake = 1e300;

    // Input
    Vec2 mouse = Vec2(-100000.0f, -100000.0f);
    bool mouseInside = false;
    bool mDown[3] = {false, false, false}, mClicked[3] = {false, false, false};
    bool mReleased[3] = {false, false, false}, mDouble[3] = {false, false, false};
    Vec2 mClickPos[3];
    float wheel = 0, wheelH = 0;
    bool kDown[256], kPressed[256], kPressedFirst[256], kReleased[256];
    std::vector<InputEvent> events;
    std::vector<std::string> droppedPending, droppedFrame;
    Vec2 dropPosPending, dropPos;
    std::vector<int> hotkeysFired, hotkeysFrame;
    std::unordered_map<std::string, int> hotkeyIds;
    int nextHotkeyId = 1;

    // Modules
    HMODULE d2dDll = nullptr, dwDll = nullptr, oleDll = nullptr, dwmDll = nullptr, immDll = nullptr, shcoreDll = nullptr;
    PFN_CoInitializeEx coInit = nullptr;
    PFN_CoUninitialize coUninit = nullptr;
    PFN_CoCreateInstance coCreate = nullptr;
    PFN_CoTaskMemFree coFree = nullptr;
    PFN_DwmSetWindowAttribute dwmSetAttr = nullptr;
    PFN_DwmGetColorizationColor dwmGetColor = nullptr;
    PFN_GetDpiForWindow getDpiForWindow = nullptr;
    PFN_AdjustWindowRectExForDpi adjustForDpi = nullptr;
    PFN_ImmGetContext immGet = nullptr;
    PFN_ImmReleaseContext immRelease = nullptr;
    PFN_ImmSetCompositionWindow immSetComp = nullptr;
    PFN_ImmSetCandidateWindow immSetCand = nullptr;
    bool comInit = false;

    // Direct2D / DirectWrite
    ID2D1Factory* d2d = nullptr;
    IDWriteFactory* dw = nullptr;
    ID2D1HwndRenderTarget* rt = nullptr;
    ID2D1SolidColorBrush* brush = nullptr;
    ID2D1StrokeStyle* roundStroke = nullptr;
    ID2D1StrokeStyle* dashStroke = nullptr;
    IWICImagingFactory* wic = nullptr;
    bool colorFonts = false;
    ID2D1PathGeometry* iconGeo[(int)Icon::Count_];

    // Fonts and text
    std::vector<FontSlot> fonts;
    float builtSize = 0;
    std::string builtFamily, builtMono;
    std::vector<FontId> fontStack;
    FontId font = FontBody;
    std::unordered_map<uint64_t, TextEntry> textCache;
    std::vector<IUnknown*> deferredRelease;

    // Textures
    std::vector<TextureSlot> textures;

    // Draw layers
    std::deque<Layer> layers;
    int layerCount = 0, curLayer = 0;
    ID curLayerKey = 0, hoverLayerKey = 0;
    float alphaMul = 1;
    std::vector<float> alphaStack;
    int disabled = 0;
    std::vector<bool> disabledStack;

    // Style
    Style style;
    Theme theme = Theme::Dark;
    bool customAccent = false;
    Color accentUser;
    float frameH = 30;

    // IDs, items, focus
    std::vector<ID> idStack;
    ItemInfo last;
    ID hotId = 0, activeId = 0, focusId = 0, prevFocusId = 0;
    bool activeAlive = false, focusVisible = false, focusClaimed = false, mouseConsumed = false;
    std::vector<ID> focusOrder;
    int navDir = 0;
    ID navArrived = 0;
    bool hasNextW = false;
    float nextW = 0;
    std::unordered_map<ID, WState> wstate;
    std::unordered_map<ID, ContState> cstate;

    // Layout
    std::deque<LayoutState> lay;
    int layDepth = 0;
    float menubarH = 0, footerH = 0;
    bool menubarSeen = false, footerSeen = false;
    ID rootId = 0;

    // Popups
    std::vector<PopupState> popups;
    int popupDepth = 0;
    std::vector<int> popupBeginStack;
    bool menubarOpen = false;
    ID menubarActive = 0;

    // Tooltip, toasts
    ID tipId = 0;
    double tipStart = 0;
    bool tipVisible = false;
    std::string tipText;
    bool tipRequested = false;
    std::vector<ToastItem> toasts;
    Rect toastArea;

    // Text editing
    TextEdit te;
    double caretReset = 0;
    ID tempInputId = 0;
    std::string tempInputText;

    // Tables, canvas
    TableState table;
    bool inCanvas = false;
    int canvasClip = 0;
    int treeDepth = 0;

    // Misc frame state
    bool wheelConsumed = false, tipSeen = false, tabConsumed = false;
    ID scrollReqId = 0, navLayerKey = 0;
    Rect scrollReqRect;
    std::vector<Rect> toastRects;
    std::unordered_map<ID, std::string> numText;
    double pressTime = 0, lastRepeat = 0;
    std::unordered_map<ID, std::vector<float>> tableWeights;
    std::unordered_map<ID, TableSort> tableSorts;
    std::unordered_map<ID, int> tableClicks;
    std::vector<float> nextTableWeights;
    ID menuNoReopen = 0;      // submenu closed with Escape: don't reopen until the mouse leaves it

    // Settings
    bool settingsLoaded = false;
    std::map<std::string, std::string> settings;
    std::map<std::string, PersistEntry> persist;

    Context() {
        memset(kDown, 0, sizeof(kDown));
        memset(kPressed, 0, sizeof(kPressed));
        memset(kPressedFirst, 0, sizeof(kPressedFirst));
        memset(kReleased, 0, sizeof(kReleased));
        for (int i = 0; i < (int)Icon::Count_; i++) iconGeo[i] = nullptr;
    }
};

static Context* g = nullptr;

static inline double Now() {
    LARGE_INTEGER t;
    QueryPerformanceCounter(&t);
    return (double)(t.QuadPart - g->qpc0.QuadPart) / (double)g->qpf.QuadPart;
}
static inline void WantFrames(int n) { if (g && g->wantFrames < n) g->wantFrames = n; }
static inline void WakeAt(double t) { if (g && t < g->nextWake) g->nextWake = t; }
static inline float Snap(float v) { return floorf(v * g->scale + 0.5f) / g->scale; }
static inline float Px() { return 1.0f / g->scale; }

// ---------------------------------------------------------------------------------
// Fonts
// ---------------------------------------------------------------------------------

static std::wstring PickFamily(const char* wantedName, std::initializer_list<const wchar_t*> fallbacks) {
    IDWriteFontCollection* coll = nullptr;
    g->dw->GetSystemFontCollection(&coll, FALSE);
    std::wstring result;
    if (coll) {
        std::string wanted = wantedName ? wantedName : "";
        if (!wanted.empty()) {
            std::wstring w = Widen(wanted.c_str(), wanted.size());
            UINT32 idx = 0; BOOL exists = FALSE;
            if (SUCCEEDED(coll->FindFamilyName(w.c_str(), &idx, &exists)) && exists) result = w;
        }
        for (const wchar_t* f : fallbacks) {
            if (!result.empty()) break;
            UINT32 idx = 0; BOOL exists = FALSE;
            if (SUCCEEDED(coll->FindFamilyName(f, &idx, &exists)) && exists) result = f;
        }
        coll->Release();
    }
    if (result.empty()) result = *fallbacks.begin();
    return result;
}

static bool BuildFont(FontSlot& f) {
    if (!(f.size >= 1 && f.size <= 1000)) f.size = 14;
    // Text already laid out this frame may still use the old format: release it after drawing.
    if (f.fmt) { g->deferredRelease.push_back(f.fmt); f.fmt = nullptr; }
    if (f.ellipsis) { g->deferredRelease.push_back(f.ellipsis); f.ellipsis = nullptr; }
    HRESULT hr = g->dw->CreateTextFormat(f.family.c_str(), nullptr, (DWRITE_FONT_WEIGHT)f.weight,
        f.italic ? DWRITE_FONT_STYLE_ITALIC : DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL,
        f.size, L"en-us", &f.fmt);
    if (FAILED(hr) || !f.fmt) return false;
    f.fmt->SetWordWrapping(DWRITE_WORD_WRAPPING_NO_WRAP);
    g->dw->CreateEllipsisTrimmingSign(f.fmt, &f.ellipsis);
    IDWriteTextLayout* tl = nullptr;
    if (SUCCEEDED(g->dw->CreateTextLayout(L"Ag", 2, f.fmt, 1000.0f, 1000.0f, &tl)) && tl) {
        DWRITE_TEXT_METRICS m;
        if (SUCCEEDED(tl->GetMetrics(&m))) f.lineH = ceilf(m.height);
        tl->Release();
    }
    return true;
}

static void ClearTextCache() {
    for (auto& kv : g->textCache)
        if (kv.second.layout) g->deferredRelease.push_back(kv.second.layout);
    g->textCache.clear();
}

static void EnsureFonts() {
    Style& s = g->style;
    // (can run mid-frame, e.g. from AddFont(), so check the size here too: 6..72, NaN -> 14)
    if (!(s.fontSize >= 6 && s.fontSize <= 72)) s.fontSize = s.fontSize > 72 ? 72.0f : (s.fontSize < 6 ? 6.0f : 14.0f);
    if (g->fonts.size() >= 6 && s.fontSize == g->builtSize && g->builtFamily == s.fontFamily.c_str() && g->builtMono == s.monoFamily.c_str())
        return;
    std::wstring fam = PickFamily(s.fontFamily.c_str(), {L"Segoe UI", L"Tahoma", L"Arial"});
    std::wstring mono = PickFamily(s.monoFamily.c_str(), {L"Cascadia Mono", L"Consolas", L"Courier New"});
    float fs = s.fontSize;
    const std::wstring* fams[6] = {&fam, &fam, &fam, &fam, &fam, &mono};
    float sizes[6] = {fs, fs, fs - 1.5f, floorf(fs * 1.36f + 0.5f), floorf(fs * 1.86f + 0.5f), fs - 0.5f};
    int weights[6] = {400, 600, 400, 600, 600, 400};
    if (g->fonts.size() < 6) g->fonts.resize(6);
    for (int i = 0; i < 6; i++) {
        FontSlot& f = g->fonts[i];
        f.family = *fams[i];
        f.size = sizes[i];
        f.weight = weights[i];
        f.italic = false;
        BuildFont(f);
    }
    g->builtSize = fs;
    g->builtFamily = s.fontFamily.c_str();
    g->builtMono = s.monoFamily.c_str();
    ClearTextCache();
}

static inline FontId ValidFont(FontId f) {
    return (f >= 0 && f < (int)g->fonts.size() && g->fonts[f].fmt) ? f : FontBody;
}
static inline float LineH(FontId f) { return g->fonts[ValidFont(f)].lineH; }

// ---------------------------------------------------------------------------------
// Text layout cache. Layouts are created once and reused while the text is visible.
// ---------------------------------------------------------------------------------

static TextEntry* GetTextImpl(const void* data, size_t bytes, bool wide, FontId font, int flags, float maxW) {
    font = ValidFont(font);
    int wkey = 0;
    if (flags & (TF_Wrap | TF_Ellipsis)) wkey = Mx(1, (int)ceilf(maxW));
    uint64_t seed = ((uint64_t)(uint32_t)font << 40) ^ ((uint64_t)(uint32_t)flags << 32) ^ (uint64_t)(uint32_t)wkey;
    if (wide) seed ^= 0x9E3779B97F4A7C15ull;
    uint64_t h = Hash64(data, bytes, seed);
    auto it = g->textCache.find(h);
    if (it != g->textCache.end()) {
        TextEntry& e = it->second;
        if (e.font == font && e.flags == flags && e.widthKey == wkey && e.wide == wide && e.key.size() == bytes &&
            (bytes == 0 || memcmp(e.key.data(), data, bytes) == 0)) {
            e.frame = g->frame;
            return &e;
        }
        if (e.layout) g->deferredRelease.push_back(e.layout);
        e.layout = nullptr;
    }
    TextEntry& e = g->textCache[h];
    e.key.assign((const char*)data, bytes);
    e.font = font; e.flags = flags; e.widthKey = wkey; e.wide = wide; e.frame = g->frame;
    e.w = 0; e.h = g->fonts[font].lineH;
    std::wstring tmp;
    const wchar_t* wp;
    UINT32 wn;
    if (wide) { wp = (const wchar_t*)data; wn = (UINT32)(bytes / sizeof(wchar_t)); }
    else { tmp = Widen((const char*)data, bytes); wp = tmp.c_str(); wn = (UINT32)tmp.size(); }
    float lw = (flags & (TF_Wrap | TF_Ellipsis)) ? (float)wkey : 100000.0f;
    IDWriteTextLayout* tl = nullptr;
    g->dw->CreateTextLayout(wn ? wp : L"", wn, g->fonts[font].fmt, lw, 100000.0f, &tl);
    if (tl) {
        tl->SetWordWrapping((flags & TF_Wrap) ? DWRITE_WORD_WRAPPING_WRAP : DWRITE_WORD_WRAPPING_NO_WRAP);
        if ((flags & TF_Ellipsis) && g->fonts[font].ellipsis) {
            DWRITE_TRIMMING tr;
            tr.granularity = DWRITE_TRIMMING_GRANULARITY_CHARACTER;
            tr.delimiter = 0;
            tr.delimiterCount = 0;
            tl->SetTrimming(&tr, g->fonts[font].ellipsis);
        }
        if (flags & TF_Center) tl->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER);
        DWRITE_TEXT_METRICS m;
        if (SUCCEEDED(tl->GetMetrics(&m))) {
            e.w = (flags & TF_Ellipsis) ? m.width : m.widthIncludingTrailingWhitespace;
            e.h = m.height;
        }
    }
    e.layout = tl;
    return &e;
}

static inline TextEntry* GetText(Str s, FontId font, int flags = 0, float maxW = 0) {
    return GetTextImpl(s.s, s.n, false, font, flags, maxW);
}
static inline TextEntry* GetTextW(const wchar_t* s, size_t n, FontId font, int flags = 0, float maxW = 0) {
    return GetTextImpl(s, n * sizeof(wchar_t), true, font, flags, maxW);
}
static inline float TextW(Str s, FontId font) { return s.n ? GetText(s, font)->w : 0.0f; }

static void EvictTextCache() {
    if (g->frame % 30 != 0 && g->textCache.size() < 6000) return;
    uint64_t maxAge = g->textCache.size() > 6000 ? 4 : 120;
    for (auto it = g->textCache.begin(); it != g->textCache.end();) {
        if (g->frame - it->second.frame > maxAge) {
            if (it->second.layout) it->second.layout->Release();
            it = g->textCache.erase(it);
        } else {
            ++it;
        }
    }
}

static void FlushDeferred() {
    for (IUnknown* u : g->deferredRelease) if (u) u->Release();
    g->deferredRelease.clear();
}

} // namespace detail

namespace detail {

// ---------------------------------------------------------------------------------
// Context creation and system modules
// ---------------------------------------------------------------------------------

static Context* Ctx() {
    if (!g) {
        static std::mutex creating;
        std::lock_guard<std::mutex> lock(creating);
        if (!g) {
            Context* c = new Context();
            QueryPerformanceFrequency(&c->qpf);
            QueryPerformanceCounter(&c->qpc0);
            g = c;
        }
    }
    return g;
}

// True on the thread running the UI (and before/after Run(), when there is none).
static inline bool OnUiThread() { return !g || !g->running || g->uiThread == GetCurrentThreadId(); }

// COM for one call, on whatever thread we are (balanced, so it is harmless if already initialized).
struct ComScope {
    HRESULT hr = E_FAIL;
    ComScope() { if (g && g->coInit) hr = g->coInit(nullptr, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE); }
    ~ComScope() { if (SUCCEEDED(hr) && g && g->coUninit) g->coUninit(); }
    ComScope(const ComScope&) = delete;
    ComScope& operator=(const ComScope&) = delete;
};

static void LoadModules() {
    Context* c = Ctx();
    if (c->oleDll) return;
    c->oleDll = LoadLibraryW(L"ole32.dll");
    if (c->oleDll) {
        c->coInit = (PFN_CoInitializeEx)(void*)GetProcAddress(c->oleDll, "CoInitializeEx");
        c->coUninit = (PFN_CoUninitialize)(void*)GetProcAddress(c->oleDll, "CoUninitialize");
        c->coCreate = (PFN_CoCreateInstance)(void*)GetProcAddress(c->oleDll, "CoCreateInstance");
        c->coFree = (PFN_CoTaskMemFree)(void*)GetProcAddress(c->oleDll, "CoTaskMemFree");
    }
    c->dwmDll = LoadLibraryW(L"dwmapi.dll");
    if (c->dwmDll) {
        c->dwmSetAttr = (PFN_DwmSetWindowAttribute)(void*)GetProcAddress(c->dwmDll, "DwmSetWindowAttribute");
        c->dwmGetColor = (PFN_DwmGetColorizationColor)(void*)GetProcAddress(c->dwmDll, "DwmGetColorizationColor");
    }
    c->immDll = LoadLibraryW(L"imm32.dll");
    if (c->immDll) {
        c->immGet = (PFN_ImmGetContext)(void*)GetProcAddress(c->immDll, "ImmGetContext");
        c->immRelease = (PFN_ImmReleaseContext)(void*)GetProcAddress(c->immDll, "ImmReleaseContext");
        c->immSetComp = (PFN_ImmSetCompositionWindow)(void*)GetProcAddress(c->immDll, "ImmSetCompositionWindow");
        c->immSetCand = (PFN_ImmSetCandidateWindow)(void*)GetProcAddress(c->immDll, "ImmSetCandidateWindow");
    }
    HMODULE user = GetModuleHandleW(L"user32.dll");
    if (user) {
        c->getDpiForWindow = (PFN_GetDpiForWindow)(void*)GetProcAddress(user, "GetDpiForWindow");
        c->adjustForDpi = (PFN_AdjustWindowRectExForDpi)(void*)GetProcAddress(user, "AdjustWindowRectExForDpi");
    }
}

static void EnsureCom() {
    LoadModules();
    if (!g->comInit && g->coInit) {
        HRESULT hr = g->coInit(nullptr, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);
        g->comInit = SUCCEEDED(hr) || hr == RPC_E_CHANGED_MODE;
    }
}

static IWICImagingFactory* Wic() {
    EnsureCom();
    if (!g->wic && g->coCreate)
        g->coCreate(kCLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, kIID_IWICImagingFactory, (void**)&g->wic);
    return g->wic;
}

// ---------------------------------------------------------------------------------
// Draw layers and commands
// ---------------------------------------------------------------------------------

static const ID kOverlayKey = 0xFFFFFFF1u;

static int LayerIndex(ID key, int z) {
    for (int i = 0; i < g->layerCount; i++)
        if (g->layers[i].key == key && g->layers[i].z == z) return i;
    if (g->layerCount >= (int)g->layers.size()) g->layers.emplace_back();
    Layer& L = g->layers[g->layerCount];
    L.key = key; L.z = z; L.alpha = 1; L.scale = 1; L.center = Vec2();
    L.cmds.clear(); L.pts.clear();
    return g->layerCount++;
}
static inline Layer& CurL() { return g->layers[g->curLayer]; }

static inline Rect CullRect() {
    return g->layDepth > 0 ? g->lay[g->layDepth - 1].clip : Rect(-100000.0f, -100000.0f, 200000.0f, 200000.0f);
}

static inline Cmd& PushCmd(uint8_t kind) {
    Layer& L = CurL();
    L.cmds.emplace_back();
    Cmd& c = L.cmds.back();
    c.kind = kind;
    return c;
}

static void AddRect(Rect r, Color c, float rounding = 0) {
    if (c.a * g->alphaMul <= 0.002f || r.w <= 0 || r.h <= 0 || !Overlaps(r, CullRect())) return;
    Cmd& k = PushCmd(CK_Rect);
    k.x = r.x; k.y = r.y; k.w = r.w; k.h = r.h; k.r = Mn(rounding, Mn(r.w, r.h) * 0.5f);
    k.c = MulA(c, g->alphaMul);
}
static void AddRectStroke(Rect r, Color c, float rounding = 0, float thick = 0, bool dashed = false) {
    if (c.a * g->alphaMul <= 0.002f || r.w <= 0 || r.h <= 0 || !Overlaps(r.Expand(2), CullRect())) return;
    Cmd& k = PushCmd(CK_RectStroke);
    k.x = r.x; k.y = r.y; k.w = r.w; k.h = r.h; k.r = Mn(rounding, Mn(r.w, r.h) * 0.5f);
    k.t = thick > 0 ? thick : Px();
    k.flags = dashed ? 1 : 0;
    k.c = MulA(c, g->alphaMul);
}
static void AddLine(Vec2 a, Vec2 b, Color c, float thick = 1.0f) {
    if (c.a * g->alphaMul <= 0.002f) return;
    Rect bb(Mn(a.x, b.x) - thick, Mn(a.y, b.y) - thick, fabsf(a.x - b.x) + thick * 2, fabsf(a.y - b.y) + thick * 2);
    if (!Overlaps(bb, CullRect())) return;
    Cmd& k = PushCmd(CK_Line);
    k.x = a.x; k.y = a.y; k.w = b.x; k.h = b.y; k.t = thick;
    k.c = MulA(c, g->alphaMul);
}
static void AddCircle(Vec2 center, float radius, Color c) {
    if (c.a * g->alphaMul <= 0.002f || radius <= 0) return;
    if (!Overlaps(Rect(center.x - radius, center.y - radius, radius * 2, radius * 2), CullRect())) return;
    Cmd& k = PushCmd(CK_Circle);
    k.x = center.x; k.y = center.y; k.r = radius;
    k.c = MulA(c, g->alphaMul);
}
static void AddCircleStroke(Vec2 center, float radius, Color c, float thick = 1.0f) {
    if (c.a * g->alphaMul <= 0.002f || radius <= 0) return;
    Cmd& k = PushCmd(CK_CircleStroke);
    k.x = center.x; k.y = center.y; k.r = radius; k.t = thick;
    k.c = MulA(c, g->alphaMul);
}
static void AddTextEntry(TextEntry* e, Vec2 pos, Color c) {
    if (!e || !e->layout || c.a * g->alphaMul <= 0.002f) return;
    if (!Overlaps(Rect(pos.x, pos.y, Mx(e->w, 1.0f) + 4, e->h), CullRect())) return;
    Cmd& k = PushCmd(CK_Text);
    k.x = pos.x; k.y = pos.y; k.w = e->w; k.h = e->h;
    k.p = e->layout;
    k.c = MulA(c, g->alphaMul);
}
static void AddImage(int tex, Rect r, float opacity) {
    if (tex <= 0 || r.w <= 0 || r.h <= 0 || !Overlaps(r, CullRect())) return;
    Cmd& k = PushCmd(CK_Image);
    k.x = r.x; k.y = r.y; k.w = r.w; k.h = r.h; k.i0 = tex; k.t = opacity * g->alphaMul;
}
static void AddClipPush(Rect r) {
    Cmd& k = PushCmd(CK_ClipPush);
    k.x = r.x; k.y = r.y; k.w = Mx(0.0f, r.w); k.h = Mx(0.0f, r.h);
}
static void AddClipPop() { PushCmd(CK_ClipPop); }
static void AddPoly(const Vec2* pts, int n, Color c, bool fill, bool closed, float thick = 1.0f) {
    if (n < 2 || c.a * g->alphaMul <= 0.002f) return;
    Layer& L = CurL();
    int start = (int)L.pts.size();
    if (fill) {
        L.pts.insert(L.pts.end(), pts, pts + n);
    } else {
        // Drop interior points that lie on a straight line (fewer segments, cleaner joins).
        L.pts.push_back(pts[0]);
        for (int i = 1; i < n - 1; i++) {
            Vec2 a = L.pts.back(), b = pts[i], d = pts[i + 1];
            float ux = b.x - a.x, uy = b.y - a.y, vx = d.x - b.x, vy = d.y - b.y;
            float cross = ux * vy - uy * vx, dot = ux * vx + uy * vy;
            float len2 = (ux * ux + uy * uy) * (vx * vx + vy * vy);
            if (dot > 0 && cross * cross <= 1e-6f * len2) continue;
            if (ux * ux + uy * uy < 1e-8f) continue;
            L.pts.push_back(b);
        }
        L.pts.push_back(pts[n - 1]);
        n = (int)L.pts.size() - start;
        if (n < 2) { L.pts.resize((size_t)start); return; }
    }
    Cmd& k = PushCmd(fill ? CK_Poly : CK_PolyStroke);
    k.i0 = start; k.i1 = n; k.t = thick; k.flags = closed ? 1 : 0;
    k.c = MulA(c, g->alphaMul);
}
static void AddBezier(Vec2 p0, Vec2 p1, Vec2 p2, Vec2 p3, Color c, float thick) {
    if (c.a * g->alphaMul <= 0.002f) return;
    Layer& L = CurL();
    int start = (int)L.pts.size();
    L.pts.push_back(p0); L.pts.push_back(p1); L.pts.push_back(p2); L.pts.push_back(p3);
    Cmd& k = PushCmd(CK_Bezier);
    k.i0 = start; k.i1 = 4; k.t = thick;
    k.c = MulA(c, g->alphaMul);
}
static void AddIcon(Icon icon, Rect r, Color c, float rotation = 0, float stroke = 2.0f) {
    if (icon == Icon::None || (int)icon >= (int)Icon::Count_ || c.a * g->alphaMul <= 0.002f) return;
    if (!Overlaps(r, CullRect())) return;
    Cmd& k = PushCmd(CK_Icon);
    k.x = r.x; k.y = r.y; k.w = r.w; k.h = r.h; k.i0 = (int)icon; k.r = rotation; k.t = stroke;
    k.c = MulA(c, g->alphaMul);
}
static void AddGradient(Rect r, Color a, Color b, bool vertical, float rounding = 0) {
    if (r.w <= 0 || r.h <= 0 || !Overlaps(r, CullRect())) return;
    Cmd& k = PushCmd(CK_Gradient);
    k.x = r.x; k.y = r.y; k.w = r.w; k.h = r.h; k.r = rounding;
    k.c = MulA(a, g->alphaMul); k.c2 = MulA(b, g->alphaMul); k.flags = vertical ? 1 : 0;
}
static void AddShadow(Rect r, float rounding, float size, Color c) {
    if (c.a * g->alphaMul <= 0.002f) return;
    Cmd& k = PushCmd(CK_Shadow);
    k.x = r.x; k.y = r.y; k.w = r.w; k.h = r.h; k.r = rounding; k.t = size;
    k.c = MulA(c, g->alphaMul);
}
static void AddArc(Vec2 center, float radius, float a0, float sweep, Color c, float thick) {
    if (c.a * g->alphaMul <= 0.002f || radius <= 0) return;
    Cmd& k = PushCmd(CK_Arc);
    k.x = center.x; k.y = center.y; k.r = radius; k.t = a0; k.u = sweep; k.w = thick;
    k.c = MulA(c, g->alphaMul);
}

// Draws text inside a rect (vertically centered), with an ellipsis when it does not fit.
static void DrawTextIn(Str s, Rect r, FontId font, Color c, Align align = Align::Left, bool ellipsis = true) {
    if (s.n == 0 || r.w <= 0) return;
    TextEntry* e = GetText(s, font);
    if (ellipsis && e->w > r.w + 0.5f) e = GetText(s, font, TF_Ellipsis, r.w);
    float x = r.x;
    if (align == Align::Center) x = r.x + (r.w - e->w) * 0.5f;
    else if (align == Align::Right) x = r.x + r.w - e->w;
    float y = r.y + (r.h - e->h) * 0.5f;
    AddTextEntry(e, Vec2(Snap(x), Snap(y)), c);
}

// Moves already recorded commands (used to vertically center text on a line).
static void ShiftCmds(int layer, int start, int end, float dx, float dy) {
    if (layer < 0 || layer >= g->layerCount) return;
    Layer& L = g->layers[layer];
    end = Mn(end, (int)L.cmds.size());
    for (int i = start; i < end; i++) {
        Cmd& c = L.cmds[i];
        if (c.kind == CK_Poly || c.kind == CK_PolyStroke || c.kind == CK_Bezier) {
            for (int j = c.i0; j < c.i0 + c.i1 && j < (int)L.pts.size(); j++) { L.pts[j].x += dx; L.pts[j].y += dy; }
            continue;
        }
        c.x += dx; c.y += dy;
        if (c.kind == CK_Line) { c.w += dx; c.h += dy; }
    }
}

// ---------------------------------------------------------------------------------
// Vector icons: tiny SVG-path subset (M L H V C A Z, absolute) in a 24x24 box.
// ---------------------------------------------------------------------------------

static const char* const kIconPaths[(int)Icon::Count_] = {
    "",                                                                                       // None
    "M5 12.5L9.5 17L19 7",                                                                    // Check
    "M6.5 6.5L17.5 17.5M17.5 6.5L6.5 17.5",                                                   // Close
    "M12 5V19M5 12H19",                                                                       // Plus
    "M5 12H19",                                                                               // Minus
    "M6 9L12 15L18 9",                                                                        // ChevronDown
    "M6 15L12 9L18 15",                                                                       // ChevronUp
    "M15 6L9 12L15 18",                                                                       // ChevronLeft
    "M9 6L15 12L9 18",                                                                        // ChevronRight
    "M12 19V5M6 11L12 5L18 11",                                                               // ArrowUp
    "M12 5V19M6 13L12 19L18 13",                                                              // ArrowDown
    "M19 12H5M11 6L5 12L11 18",                                                               // ArrowLeft
    "M5 12H19M13 6L19 12L13 18",                                                              // ArrowRight
    "M17 10.5A6.5 6.5 0 1 1 4 10.5A6.5 6.5 0 1 1 17 10.5ZM15.2 15.2L20 20",                   // Search
    "M3 7.5C3 6.7 3.7 6 4.5 6H9.3L11.3 8.3H19.5C20.3 8.3 21 9 21 9.8V17.5C21 18.3 20.3 19 19.5 19H4.5C3.7 19 3 18.3 3 17.5Z", // Folder
    "M13.5 3H7C6.2 3 5.5 3.7 5.5 4.5V19.5C5.5 20.3 6.2 21 7 21H17C17.8 21 18.5 20.3 18.5 19.5V8ZM13.5 3V8H18.5", // File
    "M10 8H18.5C19.3 8 20 8.7 20 9.5V18.5C20 19.3 19.3 20 18.5 20H10C9.2 20 8.5 19.3 8.5 18.5V9.5C8.5 8.7 9.2 8 10 8ZM5.5 15.5H5C4.4 15.5 4 15.1 4 14.5V5.5C4 4.7 4.7 4 5.5 4H14.5C15.1 4 15.5 4.4 15.5 5V5.5", // Copy
    "M9 4H15V7H9ZM15 5.5H17C17.8 5.5 18.5 6.2 18.5 7V19.5C18.5 20.3 17.8 21 17 21H7C6.2 21 5.5 20.3 5.5 19.5V7C5.5 6.2 6.2 5.5 7 5.5H9", // Paste
    "M4 7H20M10 11V17M14 11V17M6 7L7 19C7.1 19.6 7.6 20 8.2 20H15.8C16.4 20 16.9 19.6 17 19L18 7M9 7V4.8C9 4.4 9.4 4 9.8 4H14.2C14.6 4 15 4.4 15 4.8V7", // Trash
    "M4 20H8L18.5 9.5C19.6 8.4 19.6 6.6 18.5 5.5C17.4 4.4 15.6 4.4 14.5 5.5L4 16ZM13 7L17 11", // Edit
    "M5 4H16L20 8V19C20 19.6 19.6 20 19 20H5C4.4 20 4 19.6 4 19V5C4 4.4 4.4 4 5 4ZM8 4V8.5H15V4M7.5 20V14H16.5V20", // Save
    "M12 4V15M7 10L12 15L17 10M5 20H19",                                                      // Download
    "M12 16V5M7 9.5L12 4.5L17 9.5M5 20H19",                                                   // Upload
    "M19.1 14.5A7.5 7.5 0 1 1 17.3 6.7L20 9.5M20 4.5V9.5H15",                                 // Refresh
    "M8 5.8V18.2C8 19 8.9 19.5 9.6 19L18.8 12.8C19.4 12.4 19.4 11.6 18.8 11.2L9.6 5C8.9 4.5 8 5 8 5.8Z", // Play
    "M8.5 5V19M15.5 5V19",                                                                    // Pause
    "M7.5 6H16.5C17.3 6 18 6.7 18 7.5V16.5C18 17.3 17.3 18 16.5 18H7.5C6.7 18 6 17.3 6 16.5V7.5C6 6.7 6.7 6 7.5 6Z", // Stop
    "M4 7H12M18 7H20M4 17H6M12 17H20M18 7A3 3 0 1 1 12 7A3 3 0 1 1 18 7ZM12 17A3 3 0 1 1 6 17A3 3 0 1 1 12 17Z", // Settings
    "M18.5 12A6.5 6.5 0 1 1 5.5 12A6.5 6.5 0 1 1 18.5 12ZM14.5 12A2.5 2.5 0 1 1 9.5 12A2.5 2.5 0 1 1 14.5 12ZM12 3V5.5M12 18.5V21M3 12H5.5M18.5 12H21M5.6 5.6L7.4 7.4M16.6 16.6L18.4 18.4M5.6 18.4L7.4 16.6M16.6 7.4L18.4 5.6", // Gear
    "M21 12A9 9 0 1 1 3 12A9 9 0 1 1 21 12ZM12 11V16.5M12 7.8V7.9",                           // Info
    "M10.3 4.4L2.7 17.7C1.9 19 2.9 20.5 4.4 20.5H19.6C21.1 20.5 22.1 19 21.3 17.7L13.7 4.4C12.9 3.1 11.1 3.1 10.3 4.4ZM12 9.5V13.5M12 16.8V16.9", // Warning
    "M21 12A9 9 0 1 1 3 12A9 9 0 1 1 21 12ZM9 9L15 15M15 9L9 15",                             // Error
    "M21 12A9 9 0 1 1 3 12A9 9 0 1 1 21 12ZM8 12.5L11 15.5L16.5 9.5",                         // Success
    "M4 10.5L12 4L20 10.5V19C20 19.6 19.6 20 19 20H15V14.5H9V20H5C4.4 20 4 19.6 4 19Z",       // Home
    "M12 3.5L14.6 8.9L20.5 9.7L16.2 13.8L17.3 19.7L12 16.9L6.7 19.7L7.8 13.8L3.5 9.7L9.4 8.9Z", // Star
    "M12 20C12 20 3 14.6 3 8.9C3 6.2 5.1 4 7.8 4C9.5 4 11.1 5 12 6.4C12.9 5 14.5 4 16.2 4C18.9 4 21 6.2 21 8.9C21 14.6 12 20 12 20Z", // Heart
    "M10 14L14 10M10.5 6.5L12 5C13.9 3.1 17 3.1 19 5C20.9 7 20.9 10.1 19 12L17.5 13.5M13.5 17.5L12 19C10.1 20.9 7 20.9 5 19C3.1 17 3.1 13.9 5 12L6.5 10.5", // Link
    "M14 4H20V10M20 4L11 13M18 14V19C18 19.6 17.6 20 17 20H5C4.4 20 4 19.6 4 19V7C4 6.4 4.4 6 5 6H10", // ExternalLink
    "M2.5 12C4.5 7.6 8 5 12 5C16 5 19.5 7.6 21.5 12C19.5 16.4 16 19 12 19C8 19 4.5 16.4 2.5 12ZM15 12A3 3 0 1 1 9 12A3 3 0 1 1 15 12Z", // Eye
    "M6.5 11H17.5C18.3 11 19 11.7 19 12.5V18.5C19 19.3 18.3 20 17.5 20H6.5C5.7 20 5 19.3 5 18.5V12.5C5 11.7 5.7 11 6.5 11ZM8 11V7.5C8 5.3 9.8 3.5 12 3.5C14.2 3.5 16 5.3 16 7.5V11", // Lock
    "M16 12A4 4 0 1 1 8 12A4 4 0 1 1 16 12ZM12 2.5V4.5M12 19.5V21.5M2.5 12H4.5M19.5 12H21.5M5.3 5.3L6.7 6.7M17.3 17.3L18.7 18.7M5.3 18.7L6.7 17.3M17.3 6.7L18.7 5.3", // Sun
    "M20.5 14.5A8.5 8.5 0 1 1 9.5 3.5A8 8 0 0 0 20.5 14.5Z",                                   // Moon
    "M4 6.5H20M4 12H20M4 17.5H20",                                                            // Menu
    "M6.5 12A1 1 0 1 1 4.5 12A1 1 0 1 1 6.5 12ZM13 12A1 1 0 1 1 11 12A1 1 0 1 1 13 12ZM19.5 12A1 1 0 1 1 17.5 12A1 1 0 1 1 19.5 12Z", // More
    "M4 5H20L14 12.5V18.5L10 20V12.5Z",                                                       // Filter
    "M21 12A9 9 0 1 1 3 12A9 9 0 1 1 21 12ZM12 7V12L15.5 14",                                 // Clock
    "M5.5 5H18.5C19.3 5 20 5.7 20 6.5V18.5C20 19.3 19.3 20 18.5 20H5.5C4.7 20 4 19.3 4 18.5V6.5C4 5.7 4.7 5 5.5 5ZM4 10H20M8.5 3V7M15.5 3V7", // Calendar
    "M16 8A4 4 0 1 1 8 8A4 4 0 1 1 16 8ZM4.5 20C4.5 16.5 7.9 14 12 14C16.1 14 19.5 16.5 19.5 20", // User
    "M6 16.5V11C6 7.7 8.7 5 12 5C15.3 5 18 7.7 18 11V16.5L19.5 18H4.5ZM10 20.5C10.5 21.1 11.2 21.5 12 21.5C12.8 21.5 13.5 21.1 14 20.5M12 3V5", // Bell
    "M4.5 5H19.5C20.3 5 21 5.7 21 6.5V17.5C21 18.3 20.3 19 19.5 19H4.5C3.7 19 3 18.3 3 17.5V6.5C3 5.7 3.7 5 4.5 5ZM7 9.5L10 12L7 14.5M12.5 15H17", // Terminal
    "M8.5 7L3.5 12L8.5 17M15.5 7L20.5 12L15.5 17M13.5 4.5L10.5 19.5",                         // Code
    "M5.5 4H18.5C19.3 4 20 4.7 20 5.5V18.5C20 19.3 19.3 20 18.5 20H5.5C4.7 20 4 19.3 4 18.5V5.5C4 4.7 4.7 4 5.5 4ZM4 16.5L9 11.5L15.5 18M13.5 16L16 13.5L20 17.5M17 8.5A1.5 1.5 0 1 1 14 8.5A1.5 1.5 0 1 1 17 8.5Z", // Image
    "M21 12A9 9 0 1 1 3 12A9 9 0 1 1 21 12ZM3 12H21M12 3C14.4 5.5 15.5 8.6 15.5 12C15.5 15.4 14.4 18.5 12 21M12 3C9.6 5.5 8.5 8.6 8.5 12C8.5 15.4 9.6 18.5 12 21", // Globe
    "M13 3L5 13.5H11.5L10.5 21L18.5 10.5H12Z",                                                // Bolt
    "M9 14L4 9L9 4M4 9H14.5C17.5 9 20 11.5 20 14.5C20 17.5 17.5 20 14.5 20H11",               // Undo
    "M15 14L20 9L15 4M20 9H9.5C6.5 9 4 11.5 4 14.5C4 17.5 6.5 20 9.5 20H13",                  // Redo
};

static float ParseNum(const char*& p) {
    while (*p == ' ' || *p == ',') p++;
    bool neg = false;
    if (*p == '-') { neg = true; p++; } else if (*p == '+') p++;
    float v = 0;
    while (*p >= '0' && *p <= '9') { v = v * 10 + (float)(*p - '0'); p++; }
    if (*p == '.') {
        p++;
        float f = 0.1f;
        while (*p >= '0' && *p <= '9') { v += (float)(*p - '0') * f; f *= 0.1f; p++; }
    }
    return neg ? -v : v;
}

// Converts an SVG-style elliptical arc into cubic Bezier segments (works on every
// Direct2D implementation and keeps icons identical everywhere).
template <class Emit>
static void ArcToBeziers(float x0, float y0, float rx, float ry, float rotDeg, bool large, bool sweep, float x, float y, Emit emit) {
    if (rx <= 0 || ry <= 0 || (x0 == x && y0 == y)) { emit(x0, y0, x, y, x, y, true); return; }
    float phi = rotDeg * kPi / 180.0f, cphi = cosf(phi), sphi = sinf(phi);
    float dx = (x0 - x) * 0.5f, dy = (y0 - y) * 0.5f;
    float x1p = cphi * dx + sphi * dy, y1p = -sphi * dx + cphi * dy;
    float lam = (x1p * x1p) / (rx * rx) + (y1p * y1p) / (ry * ry);
    if (lam > 1) { float k = sqrtf(lam); rx *= k; ry *= k; }
    float num = rx * rx * ry * ry - rx * rx * y1p * y1p - ry * ry * x1p * x1p;
    float den = rx * rx * y1p * y1p + ry * ry * x1p * x1p;
    float coef = den > 0 ? sqrtf(Mx(0.0f, num / den)) : 0.0f;
    if (large == sweep) coef = -coef;
    float cxp = coef * (rx * y1p / ry), cyp = coef * (-ry * x1p / rx);
    float cx = cphi * cxp - sphi * cyp + (x0 + x) * 0.5f, cy = sphi * cxp + cphi * cyp + (y0 + y) * 0.5f;
    auto ang = [](float ux, float uy, float vx, float vy) { return atan2f(ux * vy - uy * vx, ux * vx + uy * vy); };
    float t1 = ang(1, 0, (x1p - cxp) / rx, (y1p - cyp) / ry);
    float dt = ang((x1p - cxp) / rx, (y1p - cyp) / ry, (-x1p - cxp) / rx, (-y1p - cyp) / ry);
    if (!sweep && dt > 0) dt -= 2 * kPi;
    else if (sweep && dt < 0) dt += 2 * kPi;
    int segs = Mx(1, (int)ceilf(fabsf(dt) / (kPi * 0.5f) - 0.001f));
    float step = dt / segs, k = 4.0f / 3.0f * tanf(step * 0.25f);
    float t = t1;
    for (int i = 0; i < segs; i++) {
        float c1 = cosf(t), s1 = sinf(t), c2 = cosf(t + step), s2 = sinf(t + step);
        float ax = c1 - k * s1, ay = s1 + k * c1, bx = c2 + k * s2, by = s2 - k * c2;
        float p1x = cx + cphi * rx * ax - sphi * ry * ay;
        float p1y = cy + sphi * rx * ax + cphi * ry * ay;
        float p2x = cx + cphi * rx * bx - sphi * ry * by;
        float p2y = cy + sphi * rx * bx + cphi * ry * by;
        float ex = (i == segs - 1) ? x : cx + cphi * rx * c2 - sphi * ry * s2;
        float ey = (i == segs - 1) ? y : cy + sphi * rx * c2 + cphi * ry * s2;
        emit(p1x, p1y, p2x, p2y, ex, ey, false);
        t += step;
    }
}

static ID2D1PathGeometry* BuildPath(const char* d) {
    if (!g->d2d || !d || !*d) return nullptr;
    ID2D1PathGeometry* geo = nullptr;
    if (FAILED(g->d2d->CreatePathGeometry(&geo)) || !geo) return nullptr;
    ID2D1GeometrySink* sink = nullptr;
    if (FAILED(geo->Open(&sink)) || !sink) { geo->Release(); return nullptr; }
    const char* p = d;
    char cmd = 0;
    float cx = 0, cy = 0, sx = 0, sy = 0;
    bool open = false;
    for (;;) {
        while (*p == ' ' || *p == ',') p++;
        if (!*p) break;
        if ((*p >= 'A' && *p <= 'Z') || (*p >= 'a' && *p <= 'z')) cmd = *p++;
        switch (cmd) {
        case 'M': {
            float x = ParseNum(p), y = ParseNum(p);
            if (open) sink->EndFigure(D2D1_FIGURE_END_OPEN);
            sink->BeginFigure(D2D1::Point2F(x, y), D2D1_FIGURE_BEGIN_HOLLOW);
            open = true; cx = sx = x; cy = sy = y; cmd = 'L';
            break;
        }
        case 'L': { float x = ParseNum(p), y = ParseNum(p); sink->AddLine(D2D1::Point2F(x, y)); cx = x; cy = y; break; }
        case 'H': { float x = ParseNum(p); sink->AddLine(D2D1::Point2F(x, cy)); cx = x; break; }
        case 'V': { float y = ParseNum(p); sink->AddLine(D2D1::Point2F(cx, y)); cy = y; break; }
        case 'C': {
            D2D1_BEZIER_SEGMENT b;
            b.point1.x = ParseNum(p); b.point1.y = ParseNum(p);
            b.point2.x = ParseNum(p); b.point2.y = ParseNum(p);
            b.point3.x = ParseNum(p); b.point3.y = ParseNum(p);
            sink->AddBezier(&b);
            cx = b.point3.x; cy = b.point3.y;
            break;
        }
        case 'A': {
            float rx = ParseNum(p), ry = ParseNum(p), rot = ParseNum(p);
            float large = ParseNum(p), sweep = ParseNum(p), x = ParseNum(p), y = ParseNum(p);
            ArcToBeziers(cx, cy, rx, ry, rot, large != 0, sweep != 0, x, y,
                [&](float ax, float ay, float bx, float by, float ex, float ey, bool line) {
                    if (line) { sink->AddLine(D2D1::Point2F(ex, ey)); return; }
                    D2D1_BEZIER_SEGMENT b;
                    b.point1 = D2D1::Point2F(ax, ay);
                    b.point2 = D2D1::Point2F(bx, by);
                    b.point3 = D2D1::Point2F(ex, ey);
                    sink->AddBezier(&b);
                });
            cx = x; cy = y;
            break;
        }
        case 'Z':
            if (open) { sink->EndFigure(D2D1_FIGURE_END_CLOSED); open = false; }
            cx = sx; cy = sy; cmd = 0;
            break;
        default:
            p++;
            break;
        }
    }
    if (open) sink->EndFigure(D2D1_FIGURE_END_OPEN);
    sink->Close();
    sink->Release();
    return geo;
}

static ID2D1PathGeometry* IconGeometry(int icon) {
    if (icon <= 0 || icon >= (int)Icon::Count_) return nullptr;
    if (!g->iconGeo[icon]) g->iconGeo[icon] = BuildPath(kIconPaths[icon]);
    return g->iconGeo[icon];
}

// ---------------------------------------------------------------------------------
// Textures (WIC decoding, kept in memory so they survive device loss)
// ---------------------------------------------------------------------------------

static int AllocTexture() {
    if (g->textures.empty()) g->textures.resize(1);   // id 0 = invalid
    for (size_t i = 1; i < g->textures.size(); i++)
        if (!g->textures[i].used) return (int)i;
    g->textures.emplace_back();
    return (int)g->textures.size() - 1;
}

static Texture DecodeTexture(IWICBitmapDecoder* dec) {
    Texture t;
    if (!dec) return t;
    IWICBitmapFrameDecode* frame = nullptr;
    IWICFormatConverter* conv = nullptr;
    if (SUCCEEDED(dec->GetFrame(0, &frame)) && frame && SUCCEEDED(g->wic->CreateFormatConverter(&conv)) && conv &&
        SUCCEEDED(conv->Initialize(frame, kGUID_WICPixelFormat32bppPBGRA, WICBitmapDitherTypeNone, nullptr, 0.0, WICBitmapPaletteTypeMedianCut))) {
        UINT w = 0, h = 0;
        conv->GetSize(&w, &h);
        if (w > 0 && h > 0 && w < 32768 && h < 32768) {
            int id = AllocTexture();
            TextureSlot& s = g->textures[id];
            s.pixels.resize((size_t)w * h * 4);
            if (SUCCEEDED(conv->CopyPixels(nullptr, w * 4, (UINT)s.pixels.size(), s.pixels.data()))) {
                s.w = (int)w; s.h = (int)h; s.used = true; s.dirty = true;
                t.id = id; t.width = (int)w; t.height = (int)h;
            } else {
                s.pixels.clear();
            }
        }
    }
    SafeRelease(conv);
    SafeRelease(frame);
    return t;
}

static void RgbaToPbgra(const uint8_t* src, uint8_t* dst, size_t count) {
    for (size_t i = 0; i < count; i++) {
        unsigned a = src[3];
        dst[0] = (uint8_t)((src[2] * a + 127) / 255);
        dst[1] = (uint8_t)((src[1] * a + 127) / 255);
        dst[2] = (uint8_t)((src[0] * a + 127) / 255);
        dst[3] = (uint8_t)a;
        src += 4; dst += 4;
    }
}

static ID2D1Bitmap* EnsureBitmap(int id) {
    if (id <= 0 || id >= (int)g->textures.size() || !g->rt) return nullptr;
    TextureSlot& s = g->textures[id];
    if (!s.used || s.w <= 0) return nullptr;
    if (s.bmp && s.dirty) {
        s.bmp->CopyFromMemory(nullptr, s.pixels.data(), (UINT32)s.w * 4);
        s.dirty = false;
    }
    if (!s.bmp) {
        D2D1_BITMAP_PROPERTIES bp = D2D1::BitmapProperties(
            D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED), 96.0f, 96.0f);
        g->rt->CreateBitmap(D2D1::SizeU((UINT32)s.w, (UINT32)s.h), s.pixels.data(), (UINT32)s.w * 4, &bp, &s.bmp);
        s.dirty = false;
    }
    return s.bmp;
}

// ---------------------------------------------------------------------------------
// Device resources and rendering
// ---------------------------------------------------------------------------------

static void DiscardDevice() {
    for (auto& t : g->textures) SafeRelease(t.bmp);
    SafeRelease(g->brush);
    SafeRelease(g->rt);
}

static bool EnsureDevice() {
    if (g->rt) return true;
    if (!g->hwnd || !g->d2d) return false;
    RECT rc;
    GetClientRect(g->hwnd, &rc);
    D2D1_SIZE_U size = D2D1::SizeU((UINT32)Mx(1L, rc.right - rc.left), (UINT32)Mx(1L, rc.bottom - rc.top));
    D2D1_RENDER_TARGET_PROPERTIES props = D2D1::RenderTargetProperties(D2D1_RENDER_TARGET_TYPE_DEFAULT,
        D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_IGNORE), g->dpi, g->dpi);
    D2D1_HWND_RENDER_TARGET_PROPERTIES hp = D2D1::HwndRenderTargetProperties(g->hwnd, size, D2D1_PRESENT_OPTIONS_NONE);
    HRESULT hr = g->d2d->CreateHwndRenderTarget(&props, &hp, &g->rt);
    if (FAILED(hr) || !g->rt) {
        props.type = D2D1_RENDER_TARGET_TYPE_SOFTWARE;
        hr = g->d2d->CreateHwndRenderTarget(&props, &hp, &g->rt);
    }
    if (FAILED(hr) || !g->rt) { g->rt = nullptr; return false; }
    g->rt->SetDpi(g->dpi, g->dpi);
    D2D1_COLOR_F white = {1, 1, 1, 1};
    g->rt->CreateSolidColorBrush(&white, nullptr, &g->brush);
    IUnknown* dc1 = nullptr;
    g->colorFonts = SUCCEEDED(g->rt->QueryInterface(kIID_ID2D1DeviceContext1, (void**)&dc1)) && dc1;
    SafeRelease(dc1);
    for (auto& t : g->textures) t.dirty = true;
    return g->brush != nullptr;
}

static inline float Unit(float v) { return v > 0 ? (v < 1 ? v : 1) : 0; }   // 0..1, NaN -> 0
static inline D2D1_COLOR_F ToD2D(Color c, float alpha) {
    D2D1_COLOR_F d;
    d.r = Unit(c.r); d.g = Unit(c.g); d.b = Unit(c.b); d.a = Unit(c.a * alpha);
    return d;
}

// Bad numbers (NaN, infinity, absurd sizes) must never reach Direct2D.
static inline bool Sane(float v) { return v > -1.0e6f && v < 1.0e6f; }
static inline bool SaneWidth(float t) { return t >= 0 && t <= 1.0e4f; }
static bool SaneCmd(const Layer& L, const Cmd& c, const Rect& view) {
    // Text only passes its origin to Direct2D (a long multi-line text can be very tall);
    // text that cannot be seen is skipped.
    if (c.kind == CK_Text)
        return c.x > -1.0e7f && c.x < 1.0e7f && c.y > -1.0e7f && c.y < 1.0e7f && Overlaps(Rect(c.x, c.y, c.w + 4, c.h), view);
    if (!Sane(c.x) || !Sane(c.y) || !Sane(c.w) || !Sane(c.h) || !Sane(c.r) || !Sane(c.t) || !Sane(c.u)) return false;
    switch (c.kind) {
    case CK_RectStroke: case CK_Line: case CK_CircleStroke: case CK_Icon: return SaneWidth(c.t);
    case CK_Arc: return SaneWidth(c.w);
    case CK_Poly: case CK_PolyStroke: case CK_Bezier:
        if (!SaneWidth(c.t) || c.i0 < 0 || c.i1 < 2 || (size_t)c.i0 + (size_t)c.i1 > L.pts.size()) return false;
        for (int i = 0; i < c.i1; i++) {
            const Vec2& p = L.pts[(size_t)(c.i0 + i)];
            if (!Sane(p.x) || !Sane(p.y)) return false;
        }
        return true;
    default: return true;
    }
}
static inline void SetBrush(Color c, float alpha) { D2D1_COLOR_F d = ToD2D(c, alpha); g->brush->SetColor(&d); }
static inline D2D1_RECT_F ToRectF(float x, float y, float w, float h) { D2D1_RECT_F r = {x, y, x + w, y + h}; return r; }

static D2D1_MATRIX_3X2_F MatMul(const D2D1_MATRIX_3X2_F& a, const D2D1_MATRIX_3X2_F& b) {
    D2D1_MATRIX_3X2_F m;
    m._11 = a._11 * b._11 + a._12 * b._21;
    m._12 = a._11 * b._12 + a._12 * b._22;
    m._21 = a._21 * b._11 + a._22 * b._21;
    m._22 = a._21 * b._12 + a._22 * b._22;
    m._31 = a._31 * b._11 + a._32 * b._21 + b._31;
    m._32 = a._31 * b._12 + a._32 * b._22 + b._32;
    return m;
}
static D2D1_MATRIX_3X2_F MatScaleTranslate(float s, float tx, float ty) {
    D2D1_MATRIX_3X2_F m;
    m._11 = s; m._12 = 0; m._21 = 0; m._22 = s; m._31 = tx; m._32 = ty;
    return m;
}
static D2D1_MATRIX_3X2_F MatRotate(float rad, float cx, float cy) {
    float c = cosf(rad), s = sinf(rad);
    D2D1_MATRIX_3X2_F m;
    m._11 = c; m._12 = s; m._21 = -s; m._22 = c;
    m._31 = cx - c * cx + s * cy;
    m._32 = cy - s * cx - c * cy;
    return m;
}

static void RenderLayer(Layer& L) {
    ID2D1HwndRenderTarget* rt = g->rt;
    D2D1_MATRIX_3X2_F base = MatScaleTranslate(1, 0, 0);
    if (L.scale != 1.0f) base = MatScaleTranslate(L.scale, L.center.x * (1 - L.scale), L.center.y * (1 - L.scale));
    rt->SetTransform(&base);
    const float A = L.alpha;
    // The part of the window this layer covers, in layer coordinates.
    const float vs = L.scale > 0.05f ? L.scale : 1.0f;
    const Rect view(L.center.x - L.center.x / vs - 2, L.center.y - L.center.y / vs - 2, g->viewW / vs + 4, g->viewH / vs + 4);
    int clipDepth = 0;
    for (size_t i = 0; i < L.cmds.size(); i++) {
        const Cmd& c = L.cmds[i];
        if (!SaneCmd(L, c, view)) {
            if (c.kind == CK_ClipPush) {           // keep pushes and pops balanced: clip everything away
                D2D1_RECT_F none = {0, 0, 0, 0};
                rt->PushAxisAlignedClip(&none, D2D1_ANTIALIAS_MODE_ALIASED);
                clipDepth++;
            }
            continue;
        }
        switch (c.kind) {
        case CK_Rect: {
            SetBrush(c.c, A);
            D2D1_RECT_F r = ToRectF(c.x, c.y, c.w, c.h);
            if (c.r > 0) { D2D1_ROUNDED_RECT rr = {r, c.r, c.r}; rt->FillRoundedRectangle(&rr, g->brush); }
            else rt->FillRectangle(&r, g->brush);
            break;
        }
        case CK_RectStroke: {
            SetBrush(c.c, A);
            float hw = c.t * 0.5f;
            D2D1_RECT_F r = ToRectF(c.x + hw, c.y + hw, c.w - c.t, c.h - c.t);
            ID2D1StrokeStyle* ss = (c.flags & 1) ? g->dashStroke : nullptr;
            if (c.r > 0) { float rr_ = Mx(0.0f, c.r - hw); D2D1_ROUNDED_RECT rr = {r, rr_, rr_}; rt->DrawRoundedRectangle(&rr, g->brush, c.t, ss); }
            else rt->DrawRectangle(&r, g->brush, c.t, ss);
            break;
        }
        case CK_Line:
            SetBrush(c.c, A);
            rt->DrawLine(D2D1::Point2F(c.x, c.y), D2D1::Point2F(c.w, c.h), g->brush, c.t, g->roundStroke);
            break;
        case CK_Circle: {
            SetBrush(c.c, A);
            D2D1_ELLIPSE e = {D2D1::Point2F(c.x, c.y), c.r, c.r};
            rt->FillEllipse(&e, g->brush);
            break;
        }
        case CK_CircleStroke: {
            SetBrush(c.c, A);
            D2D1_ELLIPSE e = {D2D1::Point2F(c.x, c.y), c.r, c.r};
            rt->DrawEllipse(&e, g->brush, c.t, nullptr);
            break;
        }
        case CK_Text:
            SetBrush(c.c, A);
            rt->DrawTextLayout(D2D1::Point2F(c.x, c.y), (IDWriteTextLayout*)c.p, g->brush,
                g->colorFonts ? (D2D1_DRAW_TEXT_OPTIONS)4 /* ENABLE_COLOR_FONT */ : D2D1_DRAW_TEXT_OPTIONS_NONE);
            break;
        case CK_Image: {
            ID2D1Bitmap* b = EnsureBitmap(c.i0);
            if (b) {
                D2D1_RECT_F r = ToRectF(c.x, c.y, c.w, c.h);
                rt->DrawBitmap(b, &r, Clampf(c.t * A, 0, 1), D2D1_BITMAP_INTERPOLATION_MODE_LINEAR, nullptr);
            }
            break;
        }
        case CK_ClipPush: {
            D2D1_RECT_F r = ToRectF(c.x, c.y, c.w, c.h);
            rt->PushAxisAlignedClip(&r, D2D1_ANTIALIAS_MODE_ALIASED);
            clipDepth++;
            break;
        }
        case CK_ClipPop:
            if (clipDepth > 0) { rt->PopAxisAlignedClip(); clipDepth--; }
            break;
        case CK_Poly: case CK_PolyStroke: case CK_Bezier: {
            if (c.i1 < 2 || c.i0 + c.i1 > (int)L.pts.size()) break;
            ID2D1PathGeometry* geo = nullptr;
            ID2D1GeometrySink* sink = nullptr;
            if (FAILED(g->d2d->CreatePathGeometry(&geo)) || !geo) break;
            if (SUCCEEDED(geo->Open(&sink)) && sink) {
                const D2D1_POINT_2F* pts = reinterpret_cast<const D2D1_POINT_2F*>(&L.pts[c.i0]);
                bool fill = c.kind == CK_Poly;
                sink->BeginFigure(pts[0], fill ? D2D1_FIGURE_BEGIN_FILLED : D2D1_FIGURE_BEGIN_HOLLOW);
                if (c.kind == CK_Bezier) {
                    D2D1_BEZIER_SEGMENT b = {pts[1], pts[2], pts[3]};
                    sink->AddBezier(&b);
                } else {
                    sink->AddLines(pts + 1, (UINT32)(c.i1 - 1));
                }
                sink->EndFigure((fill || (c.flags & 1)) ? D2D1_FIGURE_END_CLOSED : D2D1_FIGURE_END_OPEN);
                sink->Close();
                sink->Release();
                SetBrush(c.c, A);
                if (fill) rt->FillGeometry(geo, g->brush, nullptr);
                else rt->DrawGeometry(geo, g->brush, c.t, g->roundStroke);
            }
            geo->Release();
            break;
        }
        case CK_Icon: {
            ID2D1PathGeometry* geo = IconGeometry(c.i0);
            if (!geo) break;
            SetBrush(c.c, A);
            float k = c.w / 24.0f;
            D2D1_MATRIX_3X2_F m = MatScaleTranslate(k, c.x, c.y);
            if (c.r != 0) m = MatMul(m, MatRotate(c.r, c.x + c.w * 0.5f, c.y + c.h * 0.5f));
            m = MatMul(m, base);
            rt->SetTransform(&m);
            rt->DrawGeometry(geo, g->brush, c.t, g->roundStroke);
            rt->SetTransform(&base);
            break;
        }
        case CK_Gradient: {
            D2D1_GRADIENT_STOP stops[2];
            stops[0].position = 0; stops[0].color = ToD2D(c.c, A);
            stops[1].position = 1; stops[1].color = ToD2D(c.c2, A);
            ID2D1GradientStopCollection* coll = nullptr;
            ID2D1LinearGradientBrush* gb = nullptr;
            if (SUCCEEDED(rt->CreateGradientStopCollection(stops, 2, D2D1_GAMMA_2_2, D2D1_EXTEND_MODE_CLAMP, &coll)) && coll) {
                D2D1_LINEAR_GRADIENT_BRUSH_PROPERTIES lp;
                lp.startPoint = D2D1::Point2F(c.x, c.y);
                lp.endPoint = (c.flags & 1) ? D2D1::Point2F(c.x, c.y + c.h) : D2D1::Point2F(c.x + c.w, c.y);
                if (SUCCEEDED(rt->CreateLinearGradientBrush(&lp, nullptr, coll, &gb)) && gb) {
                    D2D1_RECT_F r = ToRectF(c.x, c.y, c.w, c.h);
                    if (c.r > 0) { D2D1_ROUNDED_RECT rr = {r, c.r, c.r}; rt->FillRoundedRectangle(&rr, gb); }
                    else rt->FillRectangle(&r, gb);
                }
            }
            SafeRelease(gb);
            SafeRelease(coll);
            break;
        }
        case CK_Shadow: {
            const int N = 6;
            for (int k = N; k >= 1; k--) {
                float e = c.t * (float)k / N;
                Color col = c.c;
                col.a = c.c.a * 1.6f / N;
                SetBrush(col, A);
                D2D1_RECT_F r = ToRectF(c.x - e, c.y - e + c.t * 0.35f, c.w + e * 2, c.h + e * 2);
                D2D1_ROUNDED_RECT rr = {r, c.r + e, c.r + e};
                rt->FillRoundedRectangle(&rr, g->brush);
            }
            break;
        }
        case CK_Arc: {
            SetBrush(c.c, A);
            if (fabsf(c.u) >= 2 * kPi - 0.001f) {
                D2D1_ELLIPSE e = {D2D1::Point2F(c.x, c.y), c.r, c.r};
                rt->DrawEllipse(&e, g->brush, c.w, nullptr);
                break;
            }
            ID2D1PathGeometry* geo = nullptr;
            ID2D1GeometrySink* sink = nullptr;
            if (FAILED(g->d2d->CreatePathGeometry(&geo)) || !geo) break;
            if (SUCCEEDED(geo->Open(&sink)) && sink) {
                D2D1_POINT_2F p0 = D2D1::Point2F(c.x + cosf(c.t) * c.r, c.y + sinf(c.t) * c.r);
                D2D1_POINT_2F p1 = D2D1::Point2F(c.x + cosf(c.t + c.u) * c.r, c.y + sinf(c.t + c.u) * c.r);
                sink->BeginFigure(p0, D2D1_FIGURE_BEGIN_HOLLOW);
                ArcToBeziers(p0.x, p0.y, c.r, c.r, 0, fabsf(c.u) > kPi, c.u > 0, p1.x, p1.y,
                    [&](float ax, float ay, float bx, float by, float ex, float ey, bool line) {
                        if (line) { sink->AddLine(D2D1::Point2F(ex, ey)); return; }
                        D2D1_BEZIER_SEGMENT b;
                        b.point1 = D2D1::Point2F(ax, ay);
                        b.point2 = D2D1::Point2F(bx, by);
                        b.point3 = D2D1::Point2F(ex, ey);
                        sink->AddBezier(&b);
                    });
                sink->EndFigure(D2D1_FIGURE_END_OPEN);
                sink->Close();
                sink->Release();
                rt->DrawGeometry(geo, g->brush, c.w, g->roundStroke);
            }
            geo->Release();
            break;
        }
        default:
            break;
        }
    }
    while (clipDepth-- > 0) rt->PopAxisAlignedClip();
}

static void RenderFrame() {
    if (!EnsureDevice()) return;
    if (g->rt->CheckWindowState() & D2D1_WINDOW_STATE_OCCLUDED) return;
    std::vector<int> order;
    order.reserve((size_t)g->layerCount);
    for (int i = 0; i < g->layerCount; i++) order.push_back(i);
    std::stable_sort(order.begin(), order.end(), [](int a, int b) { return g->layers[a].z < g->layers[b].z; });
    g->rendering = true;
    g->rt->BeginDraw();
    D2D1_MATRIX_3X2_F I = MatScaleTranslate(1, 0, 0);
    g->rt->SetTransform(&I);
    D2D1_COLOR_F bg = ToD2D(g->style.bg, 1);
    bg.a = 1;
    g->rt->Clear(&bg);
    for (int idx : order) RenderLayer(g->layers[idx]);
    g->rt->SetTransform(&I);
    HRESULT hr = g->rt->EndDraw();
    g->rendering = false;
    if (hr == (HRESULT)D2DERR_RECREATE_TARGET) {
        DiscardDevice();
        WantFrames(1);
    } else if (g->resizePending && g->rt) {
        g->resizePending = false;
        D2D1_SIZE_U sz = D2D1::SizeU((UINT32)g->pxW, (UINT32)g->pxH);
        g->rt->Resize(&sz);
        WantFrames(1);
    }
}

} // namespace detail

namespace detail {

static void UiNewFrame();
static void UiEndFrame();
static void PostFrame();

static const UINT WM_SUI_WAKE = WM_APP + 0x51;
static const UINT WM_SUI_TRAY = WM_APP + 0x52;            // tray icon callback
static const ULONG_PTR kInstanceMagic = 0x53554931;       // WM_COPYDATA from a second start ("SUI1")
static UINT g_msgTaskbarCreated = 0;                      // Explorer (re)started: add the tray icon again
static UINT g_msgTaskbarButton = 0;                       // the taskbar button exists: progress can be shown
static std::atomic<bool> g_wakePending(false);
static const wchar_t* const kClassName = L"SimplyUIWindow";

// ---------------------------------------------------------------------------------
// Themes
// ---------------------------------------------------------------------------------

static bool SystemUsesDark() {
    DWORD value = 1, size = sizeof(value);
    if (RegGetValueW(HKEY_CURRENT_USER, L"Software\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize",
                     L"AppsUseLightTheme", RRF_RT_REG_DWORD, nullptr, &value, &size) == ERROR_SUCCESS)
        return value == 0;
    return false;
}

static bool ThemeIsDark() {
    return g->theme == Theme::Dark || (g->theme == Theme::System && SystemUsesDark());
}

static void DeriveAccent(Color a) {
    Style& s = g->style;
    bool dark = ThemeIsDark();
    s.accent = a;
    s.accentHover = dark ? a.Lighter(0.12f) : a.Darker(0.08f);
    s.accentActive = dark ? a.Darker(0.14f) : a.Darker(0.18f);
    float lum = 0.2126f * a.r + 0.7152f * a.g + 0.0722f * a.b;
    s.accentText = lum > 0.62f ? Rgb(0x111111) : Color(1, 1, 1, 1);
    s.selection = a.WithAlpha(dark ? 0.40f : 0.28f);
}

static void ApplyWindowTheme() {
    if (!g->hwnd || !g->dwmSetAttr) return;
    BOOL dark = ThemeIsDark() ? TRUE : FALSE;
    if (FAILED(g->dwmSetAttr(g->hwnd, 20, &dark, sizeof(dark))))   // DWMWA_USE_IMMERSIVE_DARK_MODE
        g->dwmSetAttr(g->hwnd, 19, &dark, sizeof(dark));           // pre-20H1 builds
    Color b = g->style.bg;
    COLORREF caption = RGB((int)(b.r * 255 + 0.5f), (int)(b.g * 255 + 0.5f), (int)(b.b * 255 + 0.5f));
    g->dwmSetAttr(g->hwnd, 35, &caption, sizeof(caption));         // DWMWA_CAPTION_COLOR (Windows 11)
    Color t = g->style.text;
    COLORREF text = RGB((int)(t.r * 255 + 0.5f), (int)(t.g * 255 + 0.5f), (int)(t.b * 255 + 0.5f));
    g->dwmSetAttr(g->hwnd, 36, &text, sizeof(text));               // DWMWA_TEXT_COLOR
}

static void ApplyTheme() {
    Style& s = g->style;
    bool dark = ThemeIsDark();
    if (dark) {
        s.bg = Rgb(0x17181C); s.panel = Rgb(0x1F2026); s.popup = Rgb(0x26272E); s.field = Rgb(0x1B1C21);
        s.frame = Rgb(0x2A2B33); s.frameHover = Rgb(0x33343D); s.frameActive = Rgb(0x3C3D48);
        s.border = Rgb(0x31323C); s.borderStrong = Rgb(0x4B4C58); s.separator = Rgb(0x2B2C35);
        s.text = Rgb(0xE9EAEF); s.textDim = Rgb(0x9A9CA8); s.textDisabled = Rgb(0x5F606C);
        s.success = Rgb(0x3CCB8C); s.warning = Rgb(0xF0B341); s.danger = Rgb(0xEF5A5F);
        s.scrollbar = Color(1, 1, 1, 0.20f); s.shadow = Color(0, 0, 0, 0.50f);
        s.overlay = Color(0, 0, 0, 0.50f); s.tooltip = Rgb(0x2F3038);
    } else {
        s.bg = Rgb(0xF3F4F7); s.panel = Rgb(0xFFFFFF); s.popup = Rgb(0xFFFFFF); s.field = Rgb(0xFFFFFF);
        s.frame = Rgb(0xFFFFFF); s.frameHover = Rgb(0xF0F1F4); s.frameActive = Rgb(0xE5E6EB);
        s.border = Rgb(0xD8DAE0); s.borderStrong = Rgb(0xA6A9B4); s.separator = Rgb(0xE2E4E9);
        s.text = Rgb(0x1B1C21); s.textDim = Rgb(0x5F6170); s.textDisabled = Rgb(0xA4A6B0);
        s.success = Rgb(0x1E9A62); s.warning = Rgb(0xC1830A); s.danger = Rgb(0xD63B41);
        s.scrollbar = Color(0, 0, 0, 0.24f); s.shadow = Color(0, 0, 0, 0.18f);
        s.overlay = Color(0, 0, 0, 0.30f); s.tooltip = Rgb(0xFFFFFF);
    }
    DeriveAccent(g->customAccent ? g->accentUser : (dark ? Rgb(0x4C8DFF) : Rgb(0x2F6FED)));
    ApplyWindowTheme();
    WantFrames(2);
}

// ---------------------------------------------------------------------------------
// Settings file
// ---------------------------------------------------------------------------------

static std::wstring SettingsDir() {
    wchar_t buf[MAX_PATH * 2];
    DWORD n = GetEnvironmentVariableW(L"APPDATA", buf, MAX_PATH * 2);
    std::wstring dir = (n > 0 && n < MAX_PATH * 2) ? std::wstring(buf, n) : std::wstring(L".");
    std::string app = !g->cfg.appId.empty() ? g->cfg.appId : g->cfg.title;
    std::string clean;
    for (char ch : app)
        if (!strchr("\\/:*?\"<>|", ch) && (unsigned char)ch >= 32) clean += ch;
    while (!clean.empty() && (clean.back() == ' ' || clean.back() == '.')) clean.pop_back();
    if (clean.empty()) clean = "SimplyUI";
    return dir + L"\\" + Widen(clean.c_str(), clean.size());
}

static std::string EscapeValue(const std::string& v) {
    std::string out;
    for (char ch : v) {
        if (ch == '\\') out += "\\\\";
        else if (ch == '\n') out += "\\n";
        else if (ch == '\r') out += "\\r";
        else out += ch;
    }
    return out;
}
static std::string UnescapeValue(const std::string& v) {
    std::string out;
    for (size_t i = 0; i < v.size(); i++) {
        if (v[i] == '\\' && i + 1 < v.size()) {
            char n = v[++i];
            out += n == 'n' ? '\n' : n == 'r' ? '\r' : n;
        } else {
            out += v[i];
        }
    }
    return out;
}

static void LoadSettingsFile() {
    if (g->settingsLoaded) return;
    g->settingsLoaded = true;
    std::wstring path = SettingsDir() + L"\\settings.ini";
    HANDLE f = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (f == INVALID_HANDLE_VALUE) return;
    std::string data;
    char buf[4096];
    DWORD got = 0;
    while (ReadFile(f, buf, sizeof(buf), &got, nullptr) && got > 0) data.append(buf, got);
    CloseHandle(f);
    size_t pos = 0;
    while (pos < data.size()) {
        size_t end = data.find('\n', pos);
        if (end == std::string::npos) end = data.size();
        std::string line = data.substr(pos, end - pos);
        pos = end + 1;
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (line.empty() || line[0] == '#' || line[0] == ';') continue;
        size_t eq = line.find('=');
        if (eq == std::string::npos) continue;
        g->settings[line.substr(0, eq)] = UnescapeValue(line.substr(eq + 1));
    }
}

static void SaveSettingsFile() {
    LoadSettingsFile();
    for (auto& kv : g->persist) g->settings[kv.first] = kv.second.value;
    if (g->settings.empty()) return;
    std::wstring dir = SettingsDir();
    CreateDirectoryW(dir.c_str(), nullptr);
    std::string out = "# SimplyUI settings\r\n";
    for (auto& kv : g->settings) out += kv.first + "=" + EscapeValue(kv.second) + "\r\n";
    std::wstring path = dir + L"\\settings.ini";
    HANDLE f = CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (f == INVALID_HANDLE_VALUE) return;
    DWORD written = 0;
    WriteFile(f, out.data(), (DWORD)out.size(), &written, nullptr);
    CloseHandle(f);
}

static PersistEntry& PersistSlot(Str key, bool& firstTime, std::string& saved) {
    Ctx();
    LoadSettingsFile();
    std::string k;
    for (size_t i = 0; i < key.n; i++) { char ch = key.s[i]; k += (ch == '=' || ch == '\n' || ch == '\r') ? '_' : ch; }
    PersistEntry& e = g->persist[k];
    firstTime = !e.loaded;
    e.loaded = true;
    if (firstTime) {
        auto it = g->settings.find(k);
        if (it != g->settings.end()) saved = it->second; else firstTime = false;
    }
    return e;
}

// ---------------------------------------------------------------------------------
// Keyboard shortcuts
// ---------------------------------------------------------------------------------

struct KeyCombo { int vk = 0; bool ctrl = false, shift = false, alt = false, win = false; };

static bool ParseCombo(Str keys, KeyCombo& out) {
    out = KeyCombo();
    std::string s(keys.s, keys.n);
    size_t pos = 0;
    while (pos <= s.size()) {
        size_t end = s.find('+', pos);
        if (end == std::string::npos) end = s.size();
        std::string tok = s.substr(pos, end - pos);
        if (tok.empty() && end < s.size()) { tok = "+"; end++; }     // "Ctrl++"
        pos = end + 1;
        while (!tok.empty() && tok.front() == ' ') tok.erase(tok.begin());
        while (!tok.empty() && tok.back() == ' ') tok.pop_back();
        if (tok.empty()) continue;
        std::string t;
        for (char ch : tok) t += (char)tolower((unsigned char)ch);
        if (t == "ctrl" || t == "control") out.ctrl = true;
        else if (t == "shift") out.shift = true;
        else if (t == "alt") out.alt = true;
        else if (t == "win" || t == "super") out.win = true;
        else if (t.size() == 1 && ((t[0] >= 'a' && t[0] <= 'z') || (t[0] >= '0' && t[0] <= '9'))) out.vk = toupper((unsigned char)t[0]);
        else if (t[0] == 'f' && t.size() <= 3 && t.size() >= 2 && isdigit((unsigned char)t[1])) {
            int n = atoi(t.c_str() + 1);
            if (n >= 1 && n <= 24) out.vk = VK_F1 + n - 1;
        }
        else if (t == "enter" || t == "return") out.vk = VK_RETURN;
        else if (t == "esc" || t == "escape") out.vk = VK_ESCAPE;
        else if (t == "space") out.vk = VK_SPACE;
        else if (t == "tab") out.vk = VK_TAB;
        else if (t == "backspace") out.vk = VK_BACK;
        else if (t == "delete" || t == "del") out.vk = VK_DELETE;
        else if (t == "insert" || t == "ins") out.vk = VK_INSERT;
        else if (t == "home") out.vk = VK_HOME;
        else if (t == "end") out.vk = VK_END;
        else if (t == "pageup" || t == "pgup") out.vk = VK_PRIOR;
        else if (t == "pagedown" || t == "pgdn") out.vk = VK_NEXT;
        else if (t == "up") out.vk = VK_UP;
        else if (t == "down") out.vk = VK_DOWN;
        else if (t == "left") out.vk = VK_LEFT;
        else if (t == "right") out.vk = VK_RIGHT;
        else if (t == "plus" || t == "+" || t == "=") out.vk = VK_OEM_PLUS;
        else if (t == "minus" || t == "-") out.vk = VK_OEM_MINUS;
        else if (t == "comma" || t == ",") out.vk = VK_OEM_COMMA;
        else if (t == "period" || t == ".") out.vk = VK_OEM_PERIOD;
        else return false;
    }
    return out.vk != 0;
}

// ---------------------------------------------------------------------------------
// Window procedure
// ---------------------------------------------------------------------------------

static void DoFrame();

static inline Vec2 LParamToDip(LPARAM lp) {
    return Vec2((float)(short)LOWORD(lp) / g->scale, (float)(short)HIWORD(lp) / g->scale);
}

static void MouseButton(int b, bool down, bool dbl, LPARAM lp) {
    g->mouse = LParamToDip(lp);
    g->mouseInside = true;
    if (down) {
        if (!g->mDown[0] && !g->mDown[1] && !g->mDown[2]) SetCapture(g->hwnd);
        g->mDown[b] = true;
        g->mClicked[b] = true;
        g->mClickPos[b] = g->mouse;
        if (dbl) g->mDouble[b] = true;
    } else {
        if (g->mDown[b]) g->mReleased[b] = true;
        g->mDown[b] = false;
        if (!g->mDown[0] && !g->mDown[1] && !g->mDown[2] && GetCapture() == g->hwnd) ReleaseCapture();
    }
    WantFrames(2);
}

static void AdjustForDpi(RECT* r, DWORD style, DWORD ex, UINT dpi) {
    if (g->adjustForDpi) g->adjustForDpi(r, style, FALSE, ex, dpi);
    else AdjustWindowRectEx(r, style, FALSE, ex);
}

static Vec2 g_imeCaret;   // caret position in DIPs for IME candidate windows

static void PlaceIme() {
    if (!g->immGet || !g->immSetComp || !g->hwnd) return;
    HIMC imc = g->immGet(g->hwnd);
    if (!imc) return;
    COMPOSITIONFORM cf;
    memset(&cf, 0, sizeof(cf));
    cf.dwStyle = CFS_POINT;
    cf.ptCurrentPos.x = (LONG)(g_imeCaret.x * g->scale);
    cf.ptCurrentPos.y = (LONG)(g_imeCaret.y * g->scale);
    g->immSetComp(imc, &cf);
    if (g->immSetCand) {
        CANDIDATEFORM cand;
        memset(&cand, 0, sizeof(cand));
        cand.dwIndex = 0;
        cand.dwStyle = CFS_CANDIDATEPOS;
        cand.ptCurrentPos = cf.ptCurrentPos;
        g->immSetCand(imc, &cand);
    }
    g->immRelease(g->hwnd, imc);
}

// ---------------------------------------------------------------------------------
// Showing and hiding, tray icon, taskbar button, single instance
// ---------------------------------------------------------------------------------

// From another thread, don't wait for the UI thread (it may be waiting for us).
static void ShowWindowFrom(HWND h, int cmd) { if (OnUiThread()) ShowWindow(h, cmd); else ShowWindowAsync(h, cmd); }

static void RaiseWindow() {
    HWND h = g->hwnd;
    if (!h) return;
    ShowWindowFrom(h, IsIconic(h) ? SW_RESTORE : SW_SHOW);
    SetForegroundWindow(h);
    g->raisedAt = Now();
}
static void HideMainWindow() { if (g->hwnd) ShowWindowFrom(g->hwnd, SW_HIDE); }

// The window is visible, not minimized and in front (or was, until a click on the tray icon).
static bool WindowInFront() {
    HWND h = g->hwnd;
    return h && IsWindowVisible(h) && !IsIconic(h) && (GetForegroundWindow() == h || Now() - g->deactivatedAt < 0.5);
}

static void CopyTruncated(wchar_t* dst, size_t capacity, const std::wstring& src) {
    size_t n = Mn(src.size(), capacity - 1);
    if (n) memcpy(dst, src.data(), n * sizeof(wchar_t));
    dst[n] = 0;
}

static void TrayIcon(DWORD action) {            // NIM_ADD, NIM_MODIFY or NIM_DELETE
    if (!g->hwnd || (action != NIM_ADD && !g->trayShown)) return;
    NOTIFYICONDATAW nid;
    memset(&nid, 0, sizeof(nid));
    nid.cbSize = sizeof(nid);
    nid.hWnd = g->hwnd;
    nid.uID = 1;
    if (action != NIM_DELETE) {
        nid.uFlags = NIF_MESSAGE | NIF_ICON | NIF_TIP;
        nid.uCallbackMessage = WM_SUI_TRAY;
        nid.hIcon = g->iconSmall;
        CopyTruncated(nid.szTip, sizeof(nid.szTip) / sizeof(nid.szTip[0]), g->title);
    }
    BOOL ok = Shell_NotifyIconW(action, &nid);
    if (action == NIM_ADD) g->trayShown = ok != FALSE;
    if (action == NIM_DELETE) g->trayShown = false;
}

// A Windows notification from the tray icon (used for Toast() while the window is hidden).
static void TrayBalloon(const ToastItem& t) {
    NOTIFYICONDATAW nid;
    memset(&nid, 0, sizeof(nid));
    nid.cbSize = sizeof(nid);
    nid.hWnd = g->hwnd;
    nid.uID = 1;
    nid.uFlags = NIF_INFO;
    CopyTruncated(nid.szInfoTitle, sizeof(nid.szInfoTitle) / sizeof(nid.szInfoTitle[0]), g->title);
    std::wstring text = Widen(t.text.c_str(), t.text.size());
    CopyTruncated(nid.szInfo, sizeof(nid.szInfo) / sizeof(nid.szInfo[0]), text.empty() ? std::wstring(L" ") : text);
    nid.dwInfoFlags = t.level == Level::Error ? NIIF_ERROR : (t.level == Level::Warning ? NIIF_WARNING : NIIF_INFO);
    Shell_NotifyIconW(NIM_MODIFY, &nid);
}

static void TrayMenu() {
    HMENU m = CreatePopupMenu();
    if (!m) return;
    bool shown = IsWindowVisible(g->hwnd) && !IsIconic(g->hwnd);
    AppendMenuW(m, MF_STRING, 1, shown ? L"Hide" : L"Show");
    AppendMenuW(m, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(m, MF_STRING, 2, L"Exit");
    SetMenuDefaultItem(m, 1, FALSE);
    POINT pt = {0, 0};
    GetCursorPos(&pt);
    SetForegroundWindow(g->hwnd);              // so the menu closes when the user clicks elsewhere
    UINT cmd = (UINT)TrackPopupMenu(m, TPM_RETURNCMD | TPM_NONOTIFY | TPM_RIGHTBUTTON, pt.x, pt.y, 0, g->hwnd, nullptr);
    PostMessageW(g->hwnd, WM_NULL, 0, 0);
    DestroyMenu(m);
    if (cmd == 1) {
        if (shown) HideMainWindow();
        else RaiseWindow();
    } else if (cmd == 2) {
        g->exiting = true;                       // a real close (CloseRequested() can still cancel it)
        SendMessageW(g->hwnd, WM_CLOSE, 0, 0);
        g->exiting = false;
        if (!g->quit) RaiseWindow();             // cancelled (e.g. "Save changes?"): show why
    }
}

static void OnTrayMessage(LPARAM lp) {
    switch (LOWORD(lp)) {
    case WM_LBUTTONUP:
    case NIN_SELECT:
    case NIN_KEYSELECT:
        if (WindowInFront()) { if (Now() - g->raisedAt > 0.4) HideMainWindow(); }   // (the 2nd click of a double-click)
        else RaiseWindow();
        break;
    case WM_RBUTTONUP:
    case WM_CONTEXTMENU:
        TrayMenu();
        break;
    default:
        break;
    }
}

static void AllowMessage(UINT msg) {           // through UIPI, when the program runs as administrator
    typedef BOOL (WINAPI* PFN_ChangeWindowMessageFilterEx)(HWND, UINT, DWORD, void*);
    static PFN_ChangeWindowMessageFilterEx fn =
        (PFN_ChangeWindowMessageFilterEx)(void*)GetProcAddress(GetModuleHandleW(L"user32.dll"), "ChangeWindowMessageFilterEx");
    if (fn && g->hwnd && msg) fn(g->hwnd, msg, 1 /* MSGFLT_ALLOW */, nullptr);
}

// Taskbar progress: TaskbarProgress() is called every frame while busy; a frame without
// a call removes it.
static void ApplyTaskbarProgress() {
    int state = TBPF_NOPROGRESS;
    float value = 0;
    if (g->tbCalled) {
        if (!(g->tbFraction >= 0)) state = TBPF_INDETERMINATE;              // < 0 or NaN: busy
        else {
            state = g->tbLevel == Level::Error ? TBPF_ERROR : (g->tbLevel == Level::Warning ? TBPF_PAUSED : TBPF_NORMAL);
            value = Mn(g->tbFraction, 1.0f);
        }
    }
    g->tbCalled = false;
    if (!g->taskbarReady || !g->hwnd) return;
    if (state == g->tbState && fabsf(value - g->tbValue) < 0.0005f) return;
    if (!g->taskbar) {
        if (state == TBPF_NOPROGRESS) { g->tbState = state; return; }      // never used: nothing to create
        if (!g->coCreate || FAILED(g->coCreate(kCLSID_TaskbarList, nullptr, CLSCTX_INPROC_SERVER, kIID_ITaskbarList3,
                                               (void**)&g->taskbar)) || !g->taskbar) {
            g->taskbar = nullptr;
            g->taskbarReady = false;
            return;
        }
        if (FAILED(g->taskbar->HrInit())) { SafeRelease(g->taskbar); g->taskbarReady = false; return; }
    }
    g->taskbar->SetProgressState(g->hwnd, (TBPFLAG)state);
    if (state == TBPF_NORMAL || state == TBPF_PAUSED || state == TBPF_ERROR)
        g->taskbar->SetProgressValue(g->hwnd, (ULONGLONG)(value * 1000.0f + 0.5f), 1000);
    g->tbState = state;
    g->tbValue = value;
}

// Single instance: the key is the appId (or the title).
static std::wstring InstanceKey() {
    const std::string& k = g->cfg.appId.empty() ? g->cfg.title : g->cfg.appId;
    return Widen(k.c_str(), Mn(k.size(), (size_t)200));
}
static uint32_t InstanceTag(const std::wstring& key) {
    uint32_t h = 2166136261u;
    for (wchar_t ch : key) { h ^= (uint32_t)towlower(ch); h *= 16777619u; }
    return h | 1u;
}
static const wchar_t* const kInstanceProp = L"SimplyUI.Instance";

static HWND FindInstanceWindow(uint32_t tag) {
    HWND w = nullptr;
    while ((w = FindWindowExW(nullptr, w, kClassName, nullptr)) != nullptr) {
        DWORD pid = 0;
        GetWindowThreadProcessId(w, &pid);
        if (pid != GetCurrentProcessId() && (uint32_t)(uintptr_t)GetPropW(w, kInstanceProp) == tag) return w;
    }
    return nullptr;
}

// When the program is already running, brings that window to the front, gives it the
// files from our command line and returns true (this start then ends).
static bool HandToRunningInstance() {
    std::wstring key = InstanceKey();
    std::wstring name = L"Local\\SimplyUI.";
    for (wchar_t ch : key) name += ch == L'\\' ? L'/' : ch;
    HANDLE m = CreateMutexW(nullptr, FALSE, name.c_str());
    DWORD err = GetLastError();
    if (!m) return false;
    if (err != ERROR_ALREADY_EXISTS) { g->instanceMutex = m; return false; }
    CloseHandle(m);
    uint32_t tag = InstanceTag(key);
    HWND other = nullptr;
    for (int i = 0; i < 50 && !other; i++) {      // it may still be starting
        other = FindInstanceWindow(tag);
        if (!other) Sleep(40);
    }
    if (!other) return false;                     // (it is closing): just start normally
    DWORD pid = 0;
    GetWindowThreadProcessId(other, &pid);
    AllowSetForegroundWindow(pid);
    std::wstring files;
    int argc = 0;
    LPWSTR* argv = CommandLineToArgvW(GetCommandLineW(), &argc);
    for (int i = 1; argv && i < argc; i++) {
        wchar_t full[4096];
        DWORD n = GetFullPathNameW(argv[i], 4096, full, nullptr);
        if (n == 0 || n >= 4096 || GetFileAttributesW(full) == INVALID_FILE_ATTRIBUTES) continue;
        files.append(full, n);
        files.push_back(L'\0');
    }
    if (argv) LocalFree(argv);
    COPYDATASTRUCT cd;
    cd.dwData = kInstanceMagic;
    cd.cbData = (DWORD)(files.size() * sizeof(wchar_t));
    cd.lpData = files.empty() ? nullptr : (void*)files.data();
    DWORD_PTR result = 0;
    SendMessageTimeoutW(other, WM_COPYDATA, 0, (LPARAM)&cd, SMTO_ABORTIFHUNG, 5000, &result);
    return true;
}

static void OnSecondStart(const COPYDATASTRUCT* cd) {
    const wchar_t* p = (const wchar_t*)cd->lpData;
    size_t n = p ? Mn((size_t)cd->cbData / sizeof(wchar_t), (size_t)1 << 20) : 0;
    bool any = false;
    for (size_t i = 0; i < n;) {
        size_t j = i;
        while (j < n && p[j]) j++;
        if (j > i) { g->droppedPending.push_back(Narrow(p + i, j - i)); any = true; }
        i = j + 1;
    }
    if (any) g->dropPosPending = Vec2(-1e6f, -1e6f);     // not dropped on any DropZone
    RaiseWindow();
    WantFrames(2);
}

static void SaveWindowPlacement();

static LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    if (!g || (g->hwnd && hwnd != g->hwnd)) return DefWindowProcW(hwnd, msg, wp, lp);
    switch (msg) {
    case WM_ERASEBKGND:
        if (g->shown) return 1;
        break;
    case WM_PAINT:
        ValidateRect(hwnd, nullptr);
        if (g->running && !g->inFrame) { WantFrames(1); DoFrame(); }
        return 0;
    case WM_SIZE: {
        if (wp == SIZE_MINIMIZED) { WantFrames(1); return 0; }   // keep laying out at the normal size
        g->pxW = Mx(1, (int)LOWORD(lp));
        g->pxH = Mx(1, (int)HIWORD(lp));
        g->viewW = g->pxW / g->scale;
        g->viewH = g->pxH / g->scale;
        if (g->rt && g->rendering) g->resizePending = true;
        else if (g->rt) { D2D1_SIZE_U sz = D2D1::SizeU((UINT32)g->pxW, (UINT32)g->pxH); g->rt->Resize(&sz); }
        WantFrames(2);
        if (g->running && !g->inFrame) DoFrame();
        return 0;
    }
    case WM_DPICHANGED: {
        g->dpi = (float)HIWORD(wp);
        g->scale = g->dpi / 96.0f;
        if (g->rt) g->rt->SetDpi(g->dpi, g->dpi);
        RECT* r = (RECT*)lp;
        SetWindowPos(hwnd, nullptr, r->left, r->top, r->right - r->left, r->bottom - r->top, SWP_NOZORDER | SWP_NOACTIVATE);
        WantFrames(2);
        return 0;
    }
    case WM_GETMINMAXINFO: {
        MINMAXINFO* mm = (MINMAXINFO*)lp;
        RECT r = {0, 0, (LONG)(g->cfg.minWidth * g->scale), (LONG)(g->cfg.minHeight * g->scale)};
        AdjustForDpi(&r, (DWORD)GetWindowLongW(hwnd, GWL_STYLE), (DWORD)GetWindowLongW(hwnd, GWL_EXSTYLE), (UINT)g->dpi);
        mm->ptMinTrackSize.x = r.right - r.left;
        mm->ptMinTrackSize.y = r.bottom - r.top;
        return 0;
    }
    case WM_MOUSEMOVE:
        g->mouse = LParamToDip(lp);
        g->mouseInside = true;
        if (!g->trackingMouse) {
            TRACKMOUSEEVENT tme = {sizeof(tme), TME_LEAVE, hwnd, 0};
            TrackMouseEvent(&tme);
            g->trackingMouse = true;
        }
        WantFrames(1);
        return 0;
    case WM_MOUSELEAVE:
        g->trackingMouse = false;
        if (!g->mDown[0] && !g->mDown[1] && !g->mDown[2]) { g->mouseInside = false; g->mouse = Vec2(-100000.0f, -100000.0f); }
        WantFrames(1);
        return 0;
    case WM_LBUTTONDOWN: MouseButton(0, true, false, lp); return 0;
    case WM_LBUTTONDBLCLK: MouseButton(0, true, true, lp); return 0;
    case WM_LBUTTONUP: MouseButton(0, false, false, lp); return 0;
    case WM_RBUTTONDOWN: MouseButton(1, true, false, lp); return 0;
    case WM_RBUTTONDBLCLK: MouseButton(1, true, true, lp); return 0;
    case WM_RBUTTONUP: MouseButton(1, false, false, lp); return 0;
    case WM_MBUTTONDOWN: MouseButton(2, true, false, lp); return 0;
    case WM_MBUTTONDBLCLK: MouseButton(2, true, true, lp); return 0;
    case WM_MBUTTONUP: MouseButton(2, false, false, lp); return 0;
    case WM_CAPTURECHANGED:
        if ((HWND)lp != hwnd) {
            for (int b = 0; b < 3; b++) { if (g->mDown[b]) g->mReleased[b] = true; g->mDown[b] = false; }
            WantFrames(2);
        }
        return 0;
    case WM_MOUSEWHEEL:
        g->wheel += (float)(short)HIWORD(wp) / (float)WHEEL_DELTA;
        WantFrames(2);
        return 0;
    case WM_MOUSEHWHEEL:
        g->wheelH += (float)(short)HIWORD(wp) / (float)WHEEL_DELTA;
        WantFrames(2);
        return 0;
    case WM_KEYDOWN:
    case WM_SYSKEYDOWN: {
        int vk = (int)wp & 0xFF;
        g->kDown[vk] = true;
        g->kPressed[vk] = true;
        if (!(lp & (1 << 30))) g->kPressedFirst[vk] = true;
        InputEvent e;
        e.type = EV_Key; e.vk = vk; e.ch = 0;
        e.ctrl = (GetKeyState(VK_CONTROL) & 0x8000) != 0;
        e.shift = (GetKeyState(VK_SHIFT) & 0x8000) != 0;
        e.alt = (GetKeyState(VK_MENU) & 0x8000) != 0;
        g->events.push_back(e);
        WantFrames(2);
        if (msg == WM_SYSKEYDOWN && vk != VK_F10) break;   // keep Alt+F4 etc.
        return 0;
    }
    case WM_KEYUP:
    case WM_SYSKEYUP: {
        int vk = (int)wp & 0xFF;
        g->kDown[vk] = false;
        g->kReleased[vk] = true;
        WantFrames(1);
        if (msg == WM_SYSKEYUP) break;
        return 0;
    }
    case WM_CHAR:
        if (wp >= 32 && wp != 127) {
            InputEvent e;
            e.type = EV_Char; e.vk = 0; e.ch = (wchar_t)wp;
            e.ctrl = e.shift = e.alt = false;
            g->events.push_back(e);
            WantFrames(2);
        }
        return 0;
    case WM_SYSCHAR:
        if (wp == ' ') break;
        return 0;
    case WM_SYSCOMMAND:
        if ((wp & 0xFFF0) == SC_KEYMENU && lp == 0) return 0;   // don't open the system menu on Alt
        break;
    case WM_SETCURSOR:
        if (LOWORD(lp) == HTCLIENT) { SetCursor(LoadCursorW(nullptr, g->cursor)); return TRUE; }
        break;
    case WM_KILLFOCUS:
        memset(g->kDown, 0, sizeof(g->kDown));
        WantFrames(1);
        return 0;
    case WM_ACTIVATE:
        if (LOWORD(wp) == WA_INACTIVE) g->deactivatedAt = Now();
        WantFrames(1);
        break;
    case WM_SETFOCUS:
        WantFrames(1);
        break;
    case WM_COPYDATA: {
        const COPYDATASTRUCT* cd = (const COPYDATASTRUCT*)lp;
        if (!cd || cd->dwData != kInstanceMagic || !g->cfg.singleInstance) break;
        OnSecondStart(cd);
        return TRUE;
    }
    case WM_DROPFILES: {
        HDROP drop = (HDROP)wp;
        UINT count = DragQueryFileW(drop, 0xFFFFFFFF, nullptr, 0);
        for (UINT i = 0; i < count; i++) {
            UINT len = DragQueryFileW(drop, i, nullptr, 0);
            std::wstring w(len, L'\0');
            DragQueryFileW(drop, i, &w[0], len + 1);
            g->droppedPending.push_back(Narrow(w));
        }
        POINT pt = {0, 0};
        DragQueryPoint(drop, &pt);
        g->dropPosPending = Vec2(pt.x / g->scale, pt.y / g->scale);
        DragFinish(drop);
        WantFrames(2);
        return 0;
    }
    case WM_HOTKEY:
        g->hotkeysFired.push_back((int)wp);
        WantFrames(2);
        return 0;
    case WM_SETTINGCHANGE:
        if (g->theme == Theme::System && lp && wcscmp((const wchar_t*)lp, L"ImmersiveColorSet") == 0) ApplyTheme();
        break;
    case WM_ENTERSIZEMOVE:
        g->sizing = true;
        SetTimer(hwnd, 1, 15, nullptr);
        break;
    case WM_EXITSIZEMOVE:
        g->sizing = false;
        KillTimer(hwnd, 1);
        break;
    case WM_TIMER:
        if (wp == 1 && g->running && !g->inFrame && (g->wantFrames > 0 || Now() >= g->nextWake)) {
            if (Now() >= g->nextWake) { g->nextWake = 1e300; WantFrames(1); }
            DoFrame();
        }
        return 0;
    case WM_IME_STARTCOMPOSITION:
    case WM_IME_COMPOSITION:
        PlaceIme();
        break;
    case WM_CLOSE:
        if (g->trayShown && !g->exiting) { HideMainWindow(); return 0; }   // the X hides the window in the tray
        if (g->running && !g->inFrame) {
            g->closeRequested = true;
            g->cancelClose = false;
            WantFrames(1);
            DoFrame();
            g->closeRequested = false;
            if (g->cancelClose) { g->cancelClose = false; return 0; }
        }
        g->quit = true;
        return 0;
    case WM_ENDSESSION:
        // Signing out or shutting down: Windows ends the program without the normal exit, so
        // save now (tray tools are rarely closed any other way).
        if (wp) {
            SaveWindowPlacement();
            if (!g->persist.empty() || g->cfg.rememberWindow) SaveSettingsFile();
        }
        return 0;
    case WM_DESTROY:
        g->quit = true;
        return 0;
    default:
        if (msg == WM_SUI_WAKE) { g_wakePending = false; WantFrames(1); return 0; }
        if (msg == WM_SUI_TRAY) { OnTrayMessage(lp); return 0; }
        if (msg != 0 && msg == g_msgTaskbarCreated) { if (g->cfg.tray) TrayIcon(NIM_ADD); break; }
        if (msg != 0 && msg == g_msgTaskbarButton) { g->taskbarReady = true; g->tbState = -1; WantFrames(1); return 0; }
        break;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

// ---------------------------------------------------------------------------------
// Startup, frame and main loop
// ---------------------------------------------------------------------------------

static void SetupDpiAwareness() {
    HMODULE user = GetModuleHandleW(L"user32.dll");
    if (!user) return;
    PFN_SetProcessDpiAwarenessContext setCtx = (PFN_SetProcessDpiAwarenessContext)(void*)GetProcAddress(user, "SetProcessDpiAwarenessContext");
    if (setCtx && setCtx((HANDLE)(LONG_PTR)-4)) return;   // per-monitor v2
    if (!g->shcoreDll) g->shcoreDll = LoadLibraryW(L"shcore.dll");
    if (g->shcoreDll) {
        PFN_SetProcessDpiAwareness setAw = (PFN_SetProcessDpiAwareness)(void*)GetProcAddress(g->shcoreDll, "SetProcessDpiAwareness");
        if (setAw && SUCCEEDED(setAw(2))) return;
    }
    PFN_SetProcessDPIAware aware = (PFN_SetProcessDPIAware)(void*)GetProcAddress(user, "SetProcessDPIAware");
    if (aware) aware();
}

static bool CreateFactories() {
    if (g->d2d && g->dw) return true;
    if (!g->d2dDll) g->d2dDll = LoadLibraryW(L"d2d1.dll");
    if (!g->dwDll) g->dwDll = LoadLibraryW(L"dwrite.dll");
    PFN_D2D1CreateFactory d2dCreate = g->d2dDll ? (PFN_D2D1CreateFactory)(void*)GetProcAddress(g->d2dDll, "D2D1CreateFactory") : nullptr;
    PFN_DWriteCreateFactory dwCreate = g->dwDll ? (PFN_DWriteCreateFactory)(void*)GetProcAddress(g->dwDll, "DWriteCreateFactory") : nullptr;
    if (!d2dCreate || !dwCreate) return false;
    D2D1_FACTORY_OPTIONS opts;
    opts.debugLevel = D2D1_DEBUG_LEVEL_NONE;
    if (FAILED(d2dCreate(D2D1_FACTORY_TYPE_SINGLE_THREADED, kIID_ID2D1Factory, &opts, (void**)&g->d2d)) || !g->d2d) return false;
    if (FAILED(dwCreate(DWRITE_FACTORY_TYPE_SHARED, kIID_IDWriteFactory, (IUnknown**)&g->dw)) || !g->dw) return false;
    D2D1_STROKE_STYLE_PROPERTIES sp = D2D1::StrokeStyleProperties(D2D1_CAP_STYLE_ROUND, D2D1_CAP_STYLE_ROUND,
        D2D1_CAP_STYLE_ROUND, D2D1_LINE_JOIN_ROUND, 10.0f, D2D1_DASH_STYLE_SOLID, 0.0f);
    g->d2d->CreateStrokeStyle(&sp, nullptr, 0, &g->roundStroke);
    float dashes[2] = {4.0f, 3.0f};
    D2D1_STROKE_STYLE_PROPERTIES dp = D2D1::StrokeStyleProperties(D2D1_CAP_STYLE_FLAT, D2D1_CAP_STYLE_FLAT,
        D2D1_CAP_STYLE_FLAT, D2D1_LINE_JOIN_MITER, 10.0f, D2D1_DASH_STYLE_CUSTOM, 0.0f);
    g->d2d->CreateStrokeStyle(&dp, dashes, 2, &g->dashStroke);
    return true;
}

static float SystemDpi() {
    HMODULE user = GetModuleHandleW(L"user32.dll");
    typedef UINT (WINAPI* PFN_GetDpiForSystem)(void);
    PFN_GetDpiForSystem fn = user ? (PFN_GetDpiForSystem)(void*)GetProcAddress(user, "GetDpiForSystem") : nullptr;
    if (fn) return (float)fn();
    FLOAT dx = 96, dy = 96;
#if defined(_MSC_VER)
#pragma warning(push)
#pragma warning(disable : 4996)
#endif
    if (g->d2d) g->d2d->GetDesktopDpi(&dx, &dy);
#if defined(_MSC_VER)
#pragma warning(pop)
#endif
    return dx > 0 ? dx : 96.0f;
}

static bool CreateMainWindow(bool& maximize) {
    g->hinst = GetModuleHandleW(nullptr);
    HICON big = nullptr, small_ = nullptr;
    if (g->cfg.iconResource) {
        big = LoadIconW(g->hinst, MAKEINTRESOURCEW(g->cfg.iconResource));
        small_ = big;
    } else {
        wchar_t exe[MAX_PATH];
        if (GetModuleFileNameW(nullptr, exe, MAX_PATH)) ExtractIconExW(exe, 0, &big, &small_, 1);
    }
    if (!big) big = LoadIconW(nullptr, MAKEINTRESOURCEW(32512));   // IDI_APPLICATION
    if (!small_) small_ = big;
    g->iconSmall = small_;

    WNDCLASSEXW wc;
    memset(&wc, 0, sizeof(wc));
    wc.cbSize = sizeof(wc);
    wc.style = CS_DBLCLKS;
    wc.lpfnWndProc = WndProc;
    wc.hInstance = g->hinst;
    wc.hCursor = LoadCursorW(nullptr, kCurArrow);
    wc.hIcon = big;
    wc.hIconSm = small_;
    wc.lpszClassName = kClassName;
    // Paint the window with the theme background before the first frame is presented.
    HMODULE gdi = GetModuleHandleW(L"gdi32.dll");
    typedef HBRUSH (WINAPI* PFN_CreateSolidBrush)(COLORREF);
    PFN_CreateSolidBrush mkBrush = gdi ? (PFN_CreateSolidBrush)(void*)GetProcAddress(gdi, "CreateSolidBrush") : nullptr;
    Color b = g->style.bg;
    if (mkBrush) wc.hbrBackground = mkBrush(RGB((int)(b.r * 255), (int)(b.g * 255), (int)(b.b * 255)));
    UnregisterClassW(kClassName, g->hinst);
    RegisterClassExW(&wc);

    DWORD style = WS_OVERLAPPEDWINDOW;
    if (!g->cfg.resizable) style &= ~(DWORD)(WS_THICKFRAME | WS_MAXIMIZEBOX);
    DWORD ex = g->cfg.topmost ? WS_EX_TOPMOST : 0;

    float dpi = SystemDpi();
    float scale = dpi / 96.0f;
    RECT r = {0, 0, (LONG)(Mx(g->cfg.width, 50) * scale), (LONG)(Mx(g->cfg.height, 50) * scale)};
    AdjustForDpi(&r, style, ex, (UINT)dpi);
    int w = r.right - r.left, h = r.bottom - r.top;
    RECT wa;
    SystemParametersInfoW(SPI_GETWORKAREA, 0, &wa, 0);
    int x = wa.left + Mx(0, (int)((wa.right - wa.left) - w) / 2);
    int y = wa.top + Mx(0, (int)((wa.bottom - wa.top) - h) / 2);
    maximize = false;
    if (g->cfg.rememberWindow) {
        LoadSettingsFile();
        auto get = [](const char* k, int& out) {
            auto it = g->settings.find(k);
            if (it == g->settings.end()) return false;
            out = atoi(it->second.c_str());
            return true;
        };
        int sx, sy, sw, sh, sm = 0;
        if (get("window.x", sx) && get("window.y", sy) && get("window.w", sw) && get("window.h", sh) && sw > 100 && sh > 80) {
            RECT test = {sx, sy, sx + sw, sy + sh};
            if (MonitorFromRect(&test, MONITOR_DEFAULTTONULL)) { x = sx; y = sy; w = sw; h = sh; }
        }
        if (get("window.max", sm)) maximize = sm != 0;
    }
    g->hwnd = CreateWindowExW(ex, kClassName, g->title.c_str(), style, x, y, w, h, nullptr, nullptr, g->hinst, nullptr);
    if (!g->hwnd) return false;

    UINT realDpi = g->getDpiForWindow ? g->getDpiForWindow(g->hwnd) : (UINT)dpi;
    if (realDpi == 0) realDpi = (UINT)dpi;
    g->dpi = (float)realDpi;
    g->scale = g->dpi / 96.0f;
    if (realDpi != (UINT)dpi && !g->cfg.rememberWindow) {
        RECT r2 = {0, 0, (LONG)(g->cfg.width * g->scale), (LONG)(g->cfg.height * g->scale)};
        AdjustForDpi(&r2, style, ex, realDpi);
        SetWindowPos(g->hwnd, nullptr, 0, 0, r2.right - r2.left, r2.bottom - r2.top, SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
    }
    RECT cr;
    GetClientRect(g->hwnd, &cr);
    g->pxW = Mx(1L, cr.right - cr.left);
    g->pxH = Mx(1L, cr.bottom - cr.top);
    g->viewW = g->pxW / g->scale;
    g->viewH = g->pxH / g->scale;
    DragAcceptFiles(g->hwnd, TRUE);
    ApplyWindowTheme();
    g->shown = false;
    g->sizing = false;
    return true;
}

static void SaveWindowPlacement() {
    if (!g->cfg.rememberWindow || !g->hwnd) return;
    WINDOWPLACEMENT wp;
    memset(&wp, 0, sizeof(wp));
    wp.length = sizeof(wp);
    if (!GetWindowPlacement(g->hwnd, &wp)) return;
    RECT r = wp.rcNormalPosition;
    char buf[32];
    snprintf(buf, sizeof(buf), "%ld", (long)r.left); g->settings["window.x"] = buf;
    snprintf(buf, sizeof(buf), "%ld", (long)r.top); g->settings["window.y"] = buf;
    snprintf(buf, sizeof(buf), "%ld", (long)(r.right - r.left)); g->settings["window.w"] = buf;
    snprintf(buf, sizeof(buf), "%ld", (long)(r.bottom - r.top)); g->settings["window.h"] = buf;
    g->settings["window.max"] = (wp.showCmd == SW_SHOWMAXIMIZED) ? "1" : "0";
}

static bool g_skipRender = false;

// False while the window is minimized or hidden (frames still run then, but draw nothing).
static bool WindowVisible() { return !g->shown || (IsWindowVisible(g->hwnd) && !IsIconic(g->hwnd)); }

static void DoFrame() {
    if (!g || g->inFrame || !g->frameFn || !g->hwnd) return;
    g->inFrame = true;
    if (g->wantFrames > 0) g->wantFrames--;
    UiNewFrame();
    try {
        (*g->frameFn)();
    } catch (...) {
        if (!g->pendingException) g->pendingException = std::current_exception();
        g->quit = true;
    }
    UiEndFrame();
    if (!g_skipRender && WindowVisible()) RenderFrame();
    PostFrame();
    g->inFrame = false;
}

static void MainLoop() {
    MSG msg;
    while (!g->quit) {
        while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) {
            if (msg.message == WM_QUIT) { g->quit = true; break; }
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
        if (g->quit) break;
        double now = Now();
        if (now >= g->nextWake) { g->nextWake = 1e300; WantFrames(1); }
        if (g->wantFrames > 0 && WindowVisible()) {
            double t0 = Now();
            DoFrame();
            // Safety net in case presenting does not wait for vsync (remote desktop, VMs).
            double spent = Now() - t0;
            if (spent < 0.0015) Sleep(2);
        } else {
            // Minimized or hidden windows still run frames, so hotkeys, timers and Wake() keep
            // working, but nothing is drawn and animations slow down to 4 frames a second.
            if (g->wantFrames > 0) DoFrame();
            bool visible = WindowVisible();
            double wake = g->nextWake;
            if (!visible && g->wantFrames > 0) wake = Mn(wake, Now() + 0.25);
            DWORD timeout = INFINITE;
            if (wake < 1e299) {
                double ms = (wake - Now()) * 1000.0;
                timeout = ms <= 0 ? 0 : (DWORD)Mn(ms + 1.0, 3600000.0);
            }
            if (visible) {
                MsgWaitForMultipleObjectsEx(0, nullptr, timeout, QS_ALLINPUT, MWMO_INPUTAVAILABLE);
            } else {
                // A minimized window can keep reporting input that never arrives, so forget the
                // old "new input" flags and wait only for messages that really come in.
                GetQueueStatus(QS_ALLINPUT);
                if (!PeekMessageW(&msg, nullptr, 0, 0, PM_NOREMOVE))
                    MsgWaitForMultipleObjectsEx(0, nullptr, timeout, QS_ALLINPUT, 0);
            }
        }
    }
}

static void Shutdown() {
    if (g->trayShown) TrayIcon(NIM_DELETE);
    SafeRelease(g->taskbar);
    g->taskbarReady = false;
    g->tbState = -1;
    if (g->hwnd) {
        RemovePropW(g->hwnd, kInstanceProp);
        for (auto& kv : g->hotkeyIds) UnregisterHotKey(g->hwnd, kv.second);
        DestroyWindow(g->hwnd);
        g->hwnd = nullptr;
    }
    g->hotkeyIds.clear();
    DiscardDevice();
    ClearTextCache();
    FlushDeferred();
    for (auto& f : g->fonts) { SafeRelease(f.fmt); SafeRelease(f.ellipsis); }
    g->fonts.clear();
    g->builtSize = 0;
    for (int i = 0; i < (int)Icon::Count_; i++) SafeRelease(g->iconGeo[i]);
    SafeRelease(g->roundStroke);
    SafeRelease(g->dashStroke);
    SafeRelease(g->dw);
    SafeRelease(g->d2d);
    UnregisterClassW(kClassName, g->hinst);
    if (g->instanceMutex) { CloseHandle(g->instanceMutex); g->instanceMutex = nullptr; }
}

} // namespace detail

// =================================================================================
// Public: running
// =================================================================================

int Run(const Config& config, const std::function<void()>& frame) {
    using namespace detail;
    Context* c = Ctx();
    static std::atomic<bool> claimed(false);
    if (claimed.exchange(true)) return -1;          // Run() is already running (here or on another thread)
    struct Release { ~Release() { claimed = false; } } release;
    c->uiThread = GetCurrentThreadId();
    c->cfg = config;
    c->title = Widen(config.title.c_str(), config.title.size());
    if (config.singleInstance && HandToRunningInstance()) { c->uiThread = 0; return 0; }   // it was already running
    c->frameFn = &frame;
    c->quit = false;
    c->exitCode = 0;
    c->pendingException = nullptr;
    SetupDpiAwareness();
    LoadModules();
    EnsureCom();
    if (!CreateFactories()) {
        MessageBoxW(nullptr, L"Direct2D / DirectWrite is not available on this system.", L"SimplyUI", MB_ICONERROR | MB_OK);
        return 1;
    }
    c->theme = config.theme;
    if (config.accent.a > 0) { c->customAccent = true; c->accentUser = config.accent; }
    ApplyTheme();
    EnsureFonts();
    bool maximize = false;
    if (!CreateMainWindow(maximize)) {
        MessageBoxW(nullptr, L"Could not create the window.", L"SimplyUI", MB_ICONERROR | MB_OK);
        Shutdown();
        return 1;
    }
    g_msgTaskbarButton = RegisterWindowMessageW(L"TaskbarButtonCreated");
    g_msgTaskbarCreated = RegisterWindowMessageW(L"TaskbarCreated");
    AllowMessage(g_msgTaskbarButton);
    AllowMessage(g_msgTaskbarCreated);
    if (config.singleInstance) {
        SetPropW(c->hwnd, kInstanceProp, (HANDLE)(uintptr_t)InstanceTag(InstanceKey()));
        AllowMessage(WM_COPYDATA);
    }
    if (config.tray) TrayIcon(NIM_ADD);
    c->running = true;
    // Two warm-up frames let auto-sized things (label columns, popups) settle before showing.
    g_skipRender = true;
    DoFrame();
    g_skipRender = false;
    DoFrame();
    if (!config.startHidden) ShowWindow(c->hwnd, maximize ? SW_SHOWMAXIMIZED : SW_SHOWNORMAL);
    c->shown = true;
    if (!config.startHidden) UpdateWindow(c->hwnd);
    WantFrames(1);
    MainLoop();
    SaveWindowPlacement();
    if (!c->persist.empty() || c->cfg.rememberWindow) SaveSettingsFile();
    Shutdown();
    c->running = false;
    c->shown = false;
    c->frameFn = nullptr;
    c->uiThread = 0;
    if (c->pendingException) {
        std::exception_ptr e = c->pendingException;
        c->pendingException = nullptr;
        std::rethrow_exception(e);
    }
    return c->exitCode;
}

void Quit(int exitCode) {
    if (!detail::g) return;
    detail::g->exitCode = exitCode;
    detail::g->quit = true;
    if (detail::g->hwnd) PostMessageW(detail::g->hwnd, WM_NULL, 0, 0);
}
bool CloseRequested() { return detail::g && detail::g->closeRequested; }
void CancelClose() { if (detail::g) detail::g->cancelClose = true; }
void SetTitle(Str title) {
    using namespace detail;
    Context* c = Ctx();
    if (!OnUiThread() || !c->running) {          // applied at the start of the next frame
        { std::lock_guard<std::mutex> lock(c->xmutex); c->xTitle = Widen(title); c->xHasTitle = true; }
        Wake();
        return;
    }
    c->title = Widen(title);
    if (c->hwnd) SetWindowTextW(c->hwnd, c->title.c_str());
}
void SetWindowSize(int width, int height) {
    using namespace detail;
    if (!g || !g->hwnd) return;
    RECT r = {0, 0, (LONG)(width * g->scale), (LONG)(height * g->scale)};
    AdjustForDpi(&r, (DWORD)GetWindowLongW(g->hwnd, GWL_STYLE), (DWORD)GetWindowLongW(g->hwnd, GWL_EXSTYLE), (UINT)g->dpi);
    SetWindowPos(g->hwnd, nullptr, 0, 0, Mx(1L, r.right - r.left), Mx(1L, r.bottom - r.top),
                 SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE | (OnUiThread() ? 0 : SWP_ASYNCWINDOWPOS));
}
Vec2 WindowSize() { return detail::g ? Vec2(detail::g->viewW, detail::g->viewH) : Vec2(); }
void SetTopmost(bool topmost) {
    if (detail::g && detail::g->hwnd)
        SetWindowPos(detail::g->hwnd, topmost ? HWND_TOPMOST : HWND_NOTOPMOST, 0, 0, 0, 0,
                     SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE | (detail::OnUiThread() ? 0 : SWP_ASYNCWINDOWPOS));
}
bool IsTopmost() {
    return detail::g && detail::g->hwnd && (GetWindowLongW(detail::g->hwnd, GWL_EXSTYLE) & WS_EX_TOPMOST) != 0;
}
void Minimize() { if (detail::g && detail::g->hwnd) detail::ShowWindowFrom(detail::g->hwnd, SW_MINIMIZE); }
void BringToFront() { if (detail::g) detail::RaiseWindow(); }
void HideWindow() { if (detail::g) detail::HideMainWindow(); }
void ToggleWindow() {
    using namespace detail;
    if (!g || !g->hwnd) return;
    if (WindowInFront()) HideMainWindow();
    else RaiseWindow();
}
void* NativeWindow() { return detail::g ? (void*)detail::g->hwnd : nullptr; }
float DpiScale() { return detail::g ? detail::g->scale : 1.0f; }

double Time() { return detail::g ? detail::g->time : 0.0; }
float DeltaTime() { return detail::g ? detail::g->dt : 0.0f; }
uint64_t FrameCount() { return detail::g ? detail::g->frame : 0; }
void RequestRedraw() {
    if (detail::OnUiThread()) detail::WantFrames(1);
    else Wake();
}
void RedrawIn(float seconds) {
    using namespace detail;
    if (!g) return;
    double t = Now() + (seconds > 0 ? (seconds < 1e6f ? seconds : 1e6f) : 0.0);   // NaN -> now
    if (OnUiThread()) { WakeAt(t); return; }
    { std::lock_guard<std::mutex> lock(g->xmutex); if (t < g->xWakeAt) g->xWakeAt = t; }
    Wake();
}
void Wake() {
    detail::Context* c = detail::g;
    if (c && c->hwnd && !detail::g_wakePending.exchange(true))
        if (!PostMessageW(c->hwnd, detail::WM_SUI_WAKE, 0, 0)) detail::g_wakePending = false;
}

// =================================================================================
// Public: input
// =================================================================================

Vec2 MousePos() { return detail::g ? detail::g->mouse : Vec2(); }
bool MouseDown(int b) { return detail::g && b >= 0 && b < 3 && detail::g->mDown[b]; }
bool MouseClicked(int b) { return detail::g && b >= 0 && b < 3 && detail::g->mClicked[b] && !detail::g->mouseConsumed; }
bool MouseReleased(int b) { return detail::g && b >= 0 && b < 3 && detail::g->mReleased[b]; }
bool MouseDoubleClicked(int b) { return detail::g && b >= 0 && b < 3 && detail::g->mDouble[b]; }
float MouseWheel() { return detail::g ? detail::g->wheel : 0.0f; }
bool KeyDown(Key k) { return detail::g && detail::g->kDown[(int)k & 0xFF]; }
bool KeyPressed(Key k, bool repeat) {
    if (!detail::g) return false;
    return repeat ? detail::g->kPressed[(int)k & 0xFF] : detail::g->kPressedFirst[(int)k & 0xFF];
}
bool KeyReleased(Key k) { return detail::g && detail::g->kReleased[(int)k & 0xFF]; }
bool CtrlDown() { return detail::g && detail::g->kDown[VK_CONTROL]; }
bool ShiftDown() { return detail::g && detail::g->kDown[VK_SHIFT]; }
bool AltDown() { return detail::g && detail::g->kDown[VK_MENU]; }
bool IsEditingText() { return detail::g && detail::g->te.id != 0 && detail::g->te.id == detail::g->focusId; }

bool Shortcut(Str keys) {
    using namespace detail;
    if (!g) return false;
    KeyCombo k;
    if (!ParseCombo(keys, k) || !g->kPressedFirst[k.vk]) return false;
    if (CtrlDown() != k.ctrl || ShiftDown() != k.shift || AltDown() != k.alt) return false;
    if (IsEditingText()) {
        bool fkey = k.vk >= VK_F1 && k.vk <= VK_F24;
        if (!k.ctrl && !k.alt && !fkey) return false;
        if (k.ctrl && !k.alt) {
            static const int editKeys[] = {'A', 'C', 'V', 'X', 'Z', 'Y', VK_LEFT, VK_RIGHT, VK_UP, VK_DOWN, VK_HOME, VK_END, VK_BACK, VK_DELETE};
            for (int ek : editKeys) if (ek == k.vk) return false;
        }
    }
    return true;
}

bool GlobalHotkey(Str keys) {
    using namespace detail;
    if (!g || !g->hwnd) return false;
    std::string key(keys.s, keys.n);
    auto it = g->hotkeyIds.find(key);
    int id;
    if (it == g->hotkeyIds.end()) {
        KeyCombo k;
        id = 0;
        if (ParseCombo(keys, k)) {
            UINT mods = 0x4000;   // MOD_NOREPEAT
            if (k.ctrl) mods |= MOD_CONTROL;
            if (k.shift) mods |= MOD_SHIFT;
            if (k.alt) mods |= MOD_ALT;
            if (k.win) mods |= MOD_WIN;
            int candidate = g->nextHotkeyId++;
            if (RegisterHotKey(g->hwnd, candidate, mods, (UINT)k.vk)) id = candidate;
            else DebugLog("GlobalHotkey: the key combination is already in use by another program.");
        }
        g->hotkeyIds[key] = id;
    } else {
        id = it->second;
    }
    if (id == 0) return false;
    for (int f : g->hotkeysFrame) if (f == id) return true;
    return false;
}

// =================================================================================
// Public: dialogs and system helpers
// =================================================================================

namespace detail {
static std::vector<std::string> FileDialog(bool save, bool multi, bool folder, Str filter, Str title, Str defName) {
    std::vector<std::string> out;
    Ctx();
    LoadModules();
    ComScope com;
    if (!g->coCreate) return out;
    bool ui = OnUiThread();
    IFileDialog* dlg = nullptr;
    HRESULT hr = g->coCreate(save ? kCLSID_FileSaveDialog : kCLSID_FileOpenDialog, nullptr, CLSCTX_INPROC_SERVER,
                             save ? kIID_IFileSaveDialog : kIID_IFileOpenDialog, (void**)&dlg);
    if (FAILED(hr) || !dlg) return out;
    DWORD opts = 0;
    dlg->GetOptions(&opts);
    opts |= FOS_FORCEFILESYSTEM;
    if (multi) opts |= FOS_ALLOWMULTISELECT;
    if (folder) opts |= FOS_PICKFOLDERS;
    if (save) opts |= FOS_OVERWRITEPROMPT; else if (!folder) opts |= FOS_FILEMUSTEXIST;
    dlg->SetOptions(opts);
    std::vector<std::wstring> parts;
    std::vector<COMDLG_FILTERSPEC> specs;
    if (!filter.Empty() && !folder) {
        std::wstring wf = Widen(filter);
        size_t pos = 0;
        while (pos <= wf.size()) {
            size_t end = wf.find(L'|', pos);
            if (end == std::wstring::npos) end = wf.size();
            parts.push_back(wf.substr(pos, end - pos));
            pos = end + 1;
        }
        for (size_t i = 0; i + 1 < parts.size(); i += 2) {
            COMDLG_FILTERSPEC s;
            s.pszName = parts[i].c_str();
            s.pszSpec = parts[i + 1].c_str();
            specs.push_back(s);
        }
        if (!specs.empty()) {
            dlg->SetFileTypes((UINT)specs.size(), specs.data());
            dlg->SetFileTypeIndex(1);
            if (save) {
                std::wstring first = specs[0].pszSpec;
                size_t dot = first.find(L"*.");
                if (dot != std::wstring::npos) {
                    std::wstring ext = first.substr(dot + 2);
                    size_t semi = ext.find(L';');
                    if (semi != std::wstring::npos) ext = ext.substr(0, semi);
                    if (!ext.empty() && ext != L"*") dlg->SetDefaultExtension(ext.c_str());
                }
            }
        }
    }
    if (!title.Empty()) dlg->SetTitle(Widen(title).c_str());
    if (!defName.Empty()) dlg->SetFileName(Widen(defName).c_str());
    if (SUCCEEDED(dlg->Show(ui ? g->hwnd : nullptr))) {
        if (multi) {
            IShellItemArray* arr = nullptr;
            if (SUCCEEDED(((IFileOpenDialog*)dlg)->GetResults(&arr)) && arr) {
                DWORD count = 0;
                arr->GetCount(&count);
                for (DWORD i = 0; i < count; i++) {
                    IShellItem* item = nullptr;
                    if (SUCCEEDED(arr->GetItemAt(i, &item)) && item) {
                        PWSTR path = nullptr;
                        if (SUCCEEDED(item->GetDisplayName(SIGDN_FILESYSPATH, &path)) && path) {
                            out.push_back(Narrow(path, wcslen(path)));
                            if (g->coFree) g->coFree(path);
                        }
                        item->Release();
                    }
                }
                arr->Release();
            }
        } else {
            IShellItem* item = nullptr;
            if (SUCCEEDED(dlg->GetResult(&item)) && item) {
                PWSTR path = nullptr;
                if (SUCCEEDED(item->GetDisplayName(SIGDN_FILESYSPATH, &path)) && path) {
                    out.push_back(Narrow(path, wcslen(path)));
                    if (g->coFree) g->coFree(path);
                }
                item->Release();
            }
        }
    }
    dlg->Release();
    if (ui) { memset(g->mDown, 0, sizeof(g->mDown)); WantFrames(2); }
    return out;
}
} // namespace detail

std::string OpenFileDialog(Str filter, Str title) {
    std::vector<std::string> r = detail::FileDialog(false, false, false, filter, title, Str());
    return r.empty() ? std::string() : r[0];
}
std::vector<std::string> OpenFilesDialog(Str filter, Str title) {
    return detail::FileDialog(false, true, false, filter, title, Str());
}
std::string SaveFileDialog(Str filter, Str defaultName, Str title) {
    std::vector<std::string> r = detail::FileDialog(true, false, false, filter, title, defaultName);
    return r.empty() ? std::string() : r[0];
}
std::string PickFolder(Str title) {
    std::vector<std::string> r = detail::FileDialog(false, false, true, Str(), title, Str());
    return r.empty() ? std::string() : r[0];
}
namespace detail {
// A message box owned by the window when called from the UI thread.
static int MessageBoxFor(Str title, Str text, UINT type) {
    bool ui = g && OnUiThread();
    int r = MessageBoxW(ui ? g->hwnd : nullptr, Widen(text).c_str(), Widen(title).c_str(), type);
    if (ui) { memset(g->mDown, 0, sizeof(g->mDown)); WantFrames(2); }
    return r;
}
} // namespace detail

void ShowMessage(Str title, Str text, Level level) {
    UINT icon = level == Level::Error ? MB_ICONERROR : level == Level::Warning ? MB_ICONWARNING : MB_ICONINFORMATION;
    detail::MessageBoxFor(title, text, MB_OK | icon);
}
bool Confirm(Str title, Str text) {
    return detail::MessageBoxFor(title, text, MB_YESNO | MB_ICONQUESTION) == IDYES;
}
void Beep(Level level) {
    MessageBeep(level == Level::Error ? MB_ICONHAND : level == Level::Warning ? MB_ICONEXCLAMATION
                : level == Level::Success ? MB_OK : MB_ICONASTERISK);
}

void TaskbarProgress(float fraction, Level level) {
    using namespace detail;
    if (!g) return;
    g->tbCalled = true;
    g->tbFraction = fraction;
    g->tbLevel = (int)level >= 0 && (int)level <= (int)Level::Error ? level : Level::Info;
}

std::vector<std::string> CommandLine() {
    std::vector<std::string> out;
    int argc = 0;
    LPWSTR* argv = CommandLineToArgvW(GetCommandLineW(), &argc);
    for (int i = 1; argv && i < argc; i++) out.push_back(detail::Narrow(argv[i], wcslen(argv[i])));
    if (argv) LocalFree(argv);
    return out;
}

void OpenURL(Str url) {
    if (url.n == 0) return;
    detail::Ctx();
    detail::LoadModules();
    detail::ComScope com;
    ShellExecuteW(nullptr, L"open", detail::Widen(url).c_str(), nullptr, nullptr, SW_SHOWNORMAL);
}
void ShowInExplorer(Str path) {
    if (path.n == 0) return;
    detail::Ctx();
    detail::LoadModules();
    detail::ComScope com;
    std::wstring args = L"/select,\"" + detail::Widen(path) + L"\"";
    ShellExecuteW(nullptr, L"open", L"explorer.exe", args.c_str(), nullptr, SW_SHOWNORMAL);
}
void SetClipboard(Str text) {
    HWND owner = detail::g ? detail::g->hwnd : nullptr;
    std::wstring w = detail::Widen(text), crlf;
    crlf.reserve(w.size() + 16);
    for (size_t i = 0; i < w.size(); i++) {
        if (w[i] == L'\n' && (i == 0 || w[i - 1] != L'\r')) crlf += L'\r';
        crlf += w[i];
    }
    if (!OpenClipboard(owner)) return;
    EmptyClipboard();
    HGLOBAL mem = GlobalAlloc(GMEM_MOVEABLE, (crlf.size() + 1) * sizeof(wchar_t));
    if (mem) {
        void* p = GlobalLock(mem);
        if (p) {
            memcpy(p, crlf.c_str(), (crlf.size() + 1) * sizeof(wchar_t));
            GlobalUnlock(mem);
            if (!SetClipboardData(CF_UNICODETEXT, mem)) GlobalFree(mem);
        } else {
            GlobalFree(mem);
        }
    }
    CloseClipboard();
}
std::string GetClipboard() {
    std::string out;
    if (!OpenClipboard(detail::g ? detail::g->hwnd : nullptr)) return out;
    HANDLE h = GetClipboardData(CF_UNICODETEXT);
    if (h) {
        const wchar_t* p = (const wchar_t*)GlobalLock(h);
        if (p) { out = detail::Narrow(p, wcslen(p)); GlobalUnlock(h); }
    }
    CloseClipboard();
    return out;
}
const std::vector<std::string>& DroppedFiles() {
    static const std::vector<std::string> empty;
    return detail::g ? detail::g->droppedFrame : empty;
}
std::wstring ToWide(Str utf8) { return detail::Widen(utf8); }
std::string ToUtf8(const std::wstring& wide) { return detail::Narrow(wide); }

// =================================================================================
// Public: settings
// =================================================================================

void Persist(Str key, bool& value) {
    bool first; std::string saved;
    detail::PersistEntry& e = detail::PersistSlot(key, first, saved);
    if (first) value = saved == "1" || saved == "true";
    e.value = value ? "1" : "0";
}
void Persist(Str key, int& value) {
    bool first; std::string saved;
    detail::PersistEntry& e = detail::PersistSlot(key, first, saved);
    if (first && !saved.empty()) value = (int)strtol(saved.c_str(), nullptr, 10);
    e.value = std::to_string(value);
}
void Persist(Str key, float& value) {
    bool first; std::string saved;
    detail::PersistEntry& e = detail::PersistSlot(key, first, saved);
    if (first && !saved.empty()) { float v = (float)atof(saved.c_str()); if (std::isfinite(v)) value = v; }
    char buf[48];
    snprintf(buf, sizeof(buf), "%.9g", (double)value);
    e.value = buf;
}
void Persist(Str key, std::string& value) {
    bool first; std::string saved;
    detail::PersistEntry& e = detail::PersistSlot(key, first, saved);
    if (first) value = saved;
    e.value = value;
}
void Persist(Str key, Color& value) {
    bool first; std::string saved;
    detail::PersistEntry& e = detail::PersistSlot(key, first, saved);
    if (first) { Color c; if (ParseHex(saved.c_str(), c)) value = c; }
    e.value = ToHex(value, true);
}
void SaveSettings() {
    using namespace detail;
    Context* c = Ctx();
    if (!OnUiThread()) {                 // the settings belong to the UI thread: save at the next frame
        { std::lock_guard<std::mutex> lock(c->xmutex); c->xSave = true; }
        Wake();
        return;
    }
    SaveWindowPlacement();
    SaveSettingsFile();
}
std::string SettingsPath() {
    detail::Ctx();
    return detail::Narrow(detail::SettingsDir() + L"\\settings.ini");
}

// =================================================================================
// Public: colors and textures
// =================================================================================

Texture LoadTexture(Str path) {
    using namespace detail;
    Ctx();
    Texture t;
    IWICImagingFactory* wic = Wic();
    if (!wic) return t;
    IWICBitmapDecoder* dec = nullptr;
    if (SUCCEEDED(wic->CreateDecoderFromFilename(Widen(path).c_str(), nullptr, GENERIC_READ, WICDecodeMetadataCacheOnDemand, &dec)) && dec) {
        t = DecodeTexture(dec);
        dec->Release();
    }
    return t;
}
Texture LoadTextureFromMemory(const void* data, size_t size) {
    using namespace detail;
    Ctx();
    Texture t;
    IWICImagingFactory* wic = Wic();
    if (!wic || !data || size == 0) return t;
    IWICStream* stream = nullptr;
    if (SUCCEEDED(wic->CreateStream(&stream)) && stream) {
        if (SUCCEEDED(stream->InitializeFromMemory((BYTE*)data, (DWORD)size))) {
            IWICBitmapDecoder* dec = nullptr;
            if (SUCCEEDED(wic->CreateDecoderFromStream(stream, nullptr, WICDecodeMetadataCacheOnDemand, &dec)) && dec) {
                t = DecodeTexture(dec);
                dec->Release();
            }
        }
        stream->Release();
    }
    return t;
}
Texture CreateTexture(const void* rgba, int width, int height) {
    using namespace detail;
    Ctx();
    Texture t;
    if (width <= 0 || height <= 0) return t;
    int id = AllocTexture();
    TextureSlot& s = g->textures[id];
    s.pixels.assign((size_t)width * height * 4, 0);
    if (rgba) RgbaToPbgra((const uint8_t*)rgba, s.pixels.data(), (size_t)width * height);
    s.w = width; s.h = height; s.used = true; s.dirty = true;
    SafeRelease(s.bmp);
    t.id = id; t.width = width; t.height = height;
    return t;
}
void UpdateTexture(Texture& texture, const void* rgba) {
    using namespace detail;
    if (!g || texture.id <= 0 || texture.id >= (int)g->textures.size() || !rgba) return;
    TextureSlot& s = g->textures[texture.id];
    if (!s.used) return;
    RgbaToPbgra((const uint8_t*)rgba, s.pixels.data(), (size_t)s.w * s.h);
    s.dirty = true;
    WantFrames(1);
}
void FreeTexture(Texture& texture) {
    using namespace detail;
    if (g && texture.id > 0 && texture.id < (int)g->textures.size()) {
        TextureSlot& s = g->textures[texture.id];
        if (s.bmp) g->deferredRelease.push_back(s.bmp);
        s.bmp = nullptr;
        s.pixels.clear();
        s.pixels.shrink_to_fit();
        s.used = false;
        s.w = s.h = 0;
    }
    texture = Texture();
}

namespace detail {

// ---------------------------------------------------------------------------------
// IDs and persistent state
// ---------------------------------------------------------------------------------

static inline ID SeedID() { return g->idStack.empty() ? 0x5EED1234u : g->idStack.back(); }
static inline ID MakeID(Str s) { return Hash32(s.s, s.n, SeedID()); }
static inline ID MakeIDInt(int v) { return Hash32(&v, sizeof(v), SeedID() ^ 0xA5A5A5A5u); }
static inline ID Combine(ID a, uint32_t b) { return Hash32(&b, sizeof(b), a); }

static inline WState& WS(ID id) { WState& s = g->wstate[id]; s.frame = g->frame; return s; }
static inline ContState& CS(ID id) { return g->cstate[id]; }

static float Anim(float& v, float target, float speed = 16.0f) {
    if (!g->style.animations) { v = target; return v; }
    float d = target - v;
    if (fabsf(d) < 0.002f) { v = target; return v; }
    v += d * (1.0f - expf(-speed * g->dt));
    if (fabsf(target - v) < 0.002f) v = target;
    else WantFrames(1);
    return v;
}
static inline float EaseOut(float t) { return 1.0f - (1.0f - t) * (1.0f - t); }

// ---------------------------------------------------------------------------------
// Layout
// ---------------------------------------------------------------------------------

static inline LayoutState& Lay() { return g->lay[g->layDepth - 1]; }

static LayoutState& PushLayout(int kind, ID id) {
    if (g->layDepth >= (int)g->lay.size()) g->lay.emplace_back();
    LayoutState& L = g->lay[g->layDepth++];
    std::vector<AlignFix> keep;
    keep.swap(L.fixes);
    keep.clear();
    L = LayoutState();
    L.fixes.swap(keep);
    L.kind = kind;
    L.id = id;
    L.layer = g->curLayer;
    L.layerKey = g->curLayerKey;
    return L;
}
static void PopLayout() {
    if (g->layDepth <= 0) return;
    g->layDepth--;
    if (g->layDepth > 0) { g->curLayer = Lay().layer; g->curLayerKey = Lay().layerKey; }
}

static inline float SpacingY(const LayoutState& L) { return L.spacingY >= 0 ? L.spacingY : g->style.spacing; }

static void FinishLine(LayoutState& L, bool nextCompact = false) {
    if (!L.lineItems) return;
    for (const AlignFix& f : L.fixes) {
        float dy = Snap((L.lineH - f.h) * 0.5f);
        if (dy > 0.25f) ShiftCmds(f.layer, f.start, f.end, 0, dy);
    }
    L.fixes.clear();
    float sp = SpacingY(L);
    if (nextCompact && L.lineCompact) sp = Mn(sp, 2.0f);
    L.lineY += L.lineH + sp;
    L.lineH = 0;
    L.lineItems = false;
}

static inline float SpacingX(const LayoutState& L) { return (L.sameLine && L.sameSpacing >= 0) ? L.sameSpacing : g->style.spacingX; }

static float NextItemX(const LayoutState& L) {
    if (L.rowN > 0) return L.rowX[L.rowCell >= L.rowN ? 0 : L.rowCell];
    if ((L.sameLine || L.horiz) && L.lineItems) return L.lineX + SpacingX(L);
    if (L.hasForceX) return L.forceX;
    return L.left + L.indent;
}
static float NextItemY(const LayoutState& L) {
    if (L.rowN > 0) return (L.rowCell >= L.rowN && L.lineItems) ? L.lineY + L.lineH + SpacingY(L) : L.lineY;
    if ((L.sameLine || L.horiz) && L.lineItems) return L.lineY;
    return L.lineItems ? L.lineY + L.lineH + SpacingY(L) : L.lineY;
}

static float AvailW() {
    if (g->layDepth <= 0) return 0;
    LayoutState& L = Lay();
    if (L.rowN > 0) return L.rowW[L.rowCell >= L.rowN ? 0 : L.rowCell];
    return Mx(0.0f, L.right - NextItemX(L));
}

// NextWidth(): >1 px, 0..1 fraction, <=0 "fill minus"
static float ResolveWidth(float avail, float natural, bool fill) {
    float w;
    if (g->hasNextW) {
        float v = g->nextW;
        g->hasNextW = false;
        if (v > 1.0f) w = v;
        else if (v > 0.0f) w = avail * v;
        else w = avail + v;
    } else {
        // Inside BeginHorizontal() widgets keep their natural size so several fit on a line.
        w = (fill && !(g->layDepth > 0 && Lay().horiz > 0)) ? avail : natural;
    }
    if (g->layDepth > 0 && Lay().rowN > 0) w = avail;
    return Mx(w, 1.0f);
}

static Rect PlaceItem(float w, float h, bool alignText = false, bool compact = false) {
    LayoutState& L = Lay();
    float x, y;
    if (L.rowN > 0) {
        if (L.rowCell >= L.rowN) { FinishLine(L, compact); L.rowCell = 0; }
        if (!L.lineItems) L.lineCompact = compact;
        x = L.rowX[L.rowCell];
        w = L.rowW[L.rowCell];
        y = L.lineY;
        L.rowCell++;
    } else if ((L.sameLine || L.horiz) && L.lineItems) {
        x = L.lineX + SpacingX(L);
        y = L.lineY;
        L.lineCompact = L.lineCompact && compact;
    } else {
        FinishLine(L, compact);
        x = L.hasForceX ? L.forceX : L.left + L.indent;
        L.hasForceX = false;
        y = L.lineY;
        L.lineCompact = compact;
    }
    L.sameLine = false;
    L.sameSpacing = -1;
    float availNow = L.right - x;
    x = Snap(x);
    y = Snap(y);
    float x2 = Snap(x + Mx(w, 0.0f));
    Rect r(x, y, x2 - x, Snap(h));
    if (r.w < availNow - 0.5f) L.natW = Mx(L.natW, r.Right() - L.left);
    L.lineX = r.Right();
    L.lineH = Mx(L.lineH, r.h);
    L.lineItems = true;
    L.maxX = Mx(L.maxX, r.Right());
    L.maxY = Mx(L.maxY, r.Bottom());
    if (alignText) {
        AlignFix f;
        f.layer = g->curLayer;
        f.start = (int)CurL().cmds.size();
        f.end = INT_MAX;
        f.h = r.h;
        L.fixes.push_back(f);
    }
    g->hasNextW = false;
    ItemInfo& it = g->last;
    it = ItemInfo();
    it.rect = r;
    it.hovered = g->disabled == 0 && g->curLayerKey == g->hoverLayerKey && g->mouseInside && g->activeId == 0 &&
                 r.Contains(g->mouse) && L.clip.Contains(g->mouse);
    it.clicked = it.hovered && g->mClicked[0] && !g->mouseConsumed;
    it.rightClicked = it.hovered && g->mClicked[1];
    it.doubleClicked = it.hovered && g->mDouble[0];
    return r;
}
static inline void CloseFix() {
    LayoutState& L = Lay();
    if (!L.fixes.empty() && L.fixes.back().end == INT_MAX) L.fixes.back().end = (int)CurL().cmds.size();
}
static inline void ReportNatural(float w) { if (g->layDepth > 0) { LayoutState& L = Lay(); L.natW = Mx(L.natW, w); } }

static inline bool ItemVisible(float y, float h) {
    if (g->layDepth <= 0) return true;
    const Rect& c = Lay().clip;
    return y + h >= c.y - 2 && y <= c.Bottom() + 2;
}

static void ResolveLabelColumn(LayoutState& L) {
    if (!L.labelKey) return;
    ContState& cs = CS(L.labelKey);
    float width = L.right - L.left;
    float want = 0;
    if (L.labelMeasure > 0) want = ceilf(Mn(L.labelMeasure + 12.0f, Mx(40.0f, width * g->style.labelMaxFraction)));
    if (fabsf(want - cs.labelW) > 0.5f) { cs.labelW = want; WantFrames(1); }
}

struct Labeled { Rect full, label, ctrl; Str text; bool has; };

static Labeled PlaceLabeled(Str label, float ctrlNatural, bool fill, float h) {
    LayoutState& L = Lay();
    Labeled it;
    it.text = DisplayText(label);
    it.has = it.text.n > 0;
    bool inl = L.rowN > 0 || L.horiz > 0 || (L.sameLine && L.lineItems);
    float avail = AvailW();
    if (!it.has) {
        float w = ResolveWidth(avail, ctrlNatural, fill);
        it.full = it.ctrl = PlaceItem(w, h);
        return it;
    }
    if (g->style.labelsAbove && !inl) {
        float lh = LineH(FontBody);
        float w = ResolveWidth(avail, ctrlNatural, fill);
        Rect r = PlaceItem(w, lh + 4 + h);
        it.full = r;
        it.label = Rect(r.x, r.y, r.w, lh);
        it.ctrl = Rect(r.x, r.y + lh + 4, r.w, h);
        return it;
    }
    float tw = TextW(it.text, FontBody);
    float gap = inl ? g->style.spacingX : 12.0f;
    float lw;
    if (inl) {
        lw = tw + gap;
    } else {
        L.labelMeasure = Mx(L.labelMeasure, tw);
        lw = L.labelW > 0 ? L.labelW : Mn(tw + gap, avail * g->style.labelMaxFraction);
    }
    lw = Mn(lw, avail * 0.7f);
    float cw = L.rowN > 0 ? Mx(1.0f, avail - lw) : ResolveWidth(Mx(1.0f, avail - lw), ctrlNatural, fill);
    Rect r = PlaceItem(lw + cw, h);
    it.full = r;
    it.label = Rect(r.x, r.y, Mx(0.0f, lw - gap), h);
    it.ctrl = Rect(r.x + lw, r.y, Mx(1.0f, r.w - lw), h);
    return it;
}

static void DrawLabel(const Labeled& it) {
    if (it.has) DrawTextIn(it.text, it.label, FontBody, g->style.text, Align::Left, true);
}

// ---------------------------------------------------------------------------------
// Interaction
// ---------------------------------------------------------------------------------

static bool Hoverable(const Rect& r, ID id) {
    if (g->disabled > 0 || g->curLayerKey != g->hoverLayerKey || !g->mouseInside) return false;
    if (g->activeId != 0 && g->activeId != id) return false;
    if (!r.Contains(g->mouse)) return false;
    if (g->layDepth > 0 && !Lay().clip.Contains(g->mouse)) return false;
    return true;
}

static void Focusable(ID id) {
    bool canFocus = g->disabled == 0 && g->curLayerKey == g->navLayerKey;
    if (canFocus) g->focusOrder.push_back(id);
    if (g->navArrived == id && g->layDepth > 0) {
        // ask the closest scroll container to bring the item into view
        for (int i = g->layDepth - 1; i >= 0; i--) {
            int k = g->lay[i].kind;
            if (k == LK_Child || k == LK_Root) { g->scrollReqId = g->lay[i].id; g->scrollReqRect = g->last.rect; break; }
        }
        g->navArrived = 0;
    }
    if (g->focusNext && canFocus) {             // FocusNext(): this widget takes the keyboard focus
        g->focusNext = false;
        g->focusId = id;
        g->focusVisible = true;
        g->focusClaimed = true;
        g->navArrived = id;                     // scrolled into view next frame
        WantFrames(2);
    }
}

static bool KeyActivated(ID id) {
    return g->focusId == id && g->disabled == 0 && !g->kDown[VK_CONTROL] && !g->kDown[VK_MENU] &&
           (g->kPressedFirst[VK_SPACE] || g->kPressedFirst[VK_RETURN]);
}

enum ButtonFlags { BF_PressOnClick = 1, BF_NoFocus = 2, BF_Repeat = 4 };

static bool ButtonBehavior(const Rect& r, ID id, bool& hovered, bool& held, int flags = 0) {
    hovered = Hoverable(r, id);
    bool pressed = false;
    if (hovered) g->hotId = id;
    if (hovered && g->mClicked[0] && !g->mouseConsumed) {
        g->activeId = id;
        g->mouseConsumed = true;
        g->focusClaimed = true;
        if (!(flags & BF_NoFocus)) { g->focusId = id; g->focusVisible = false; }
        if (flags & (BF_PressOnClick | BF_Repeat)) pressed = true;
        g->pressTime = g->lastRepeat = g->time;
    }
    if (g->activeId == id) {
        g->activeAlive = true;
        if (!g->mDown[0]) {
            if (hovered && !(flags & (BF_PressOnClick | BF_Repeat)) && g->mReleased[0]) pressed = true;
            g->activeId = 0;
        } else if ((flags & BF_Repeat) && hovered) {
            if (g->time - g->pressTime > 0.4) {
                if (g->time - g->lastRepeat > 0.06) { pressed = true; g->lastRepeat = g->time; }
                WantFrames(1);
            } else {
                WakeAt(g->pressTime + 0.41);
            }
        }
    }
    held = g->activeId == id && g->mDown[0];
    if (!(flags & BF_NoFocus)) {
        Focusable(id);
        if (KeyActivated(id)) pressed = true;
    }
    ItemInfo& it = g->last;
    it.id = id;
    it.hovered = hovered;
    it.active = held;
    it.clicked = pressed;
    it.focused = g->focusId == id;
    return pressed;
}

static void FocusRing(ID id, Rect r, float rounding) {
    if (g->focusId == id && g->focusVisible && g->disabled == 0)
        AddRectStroke(r.Expand(2.0f), g->style.accent, rounding + 2.0f, 2.0f);
}

// ---------------------------------------------------------------------------------
// Scroll areas
// ---------------------------------------------------------------------------------

static void Scrollbar(ID id, Rect view, float contentH, ContState& cs) {
    Style& s = g->style;
    float maxScroll = contentH - view.h;
    if (maxScroll <= 0) return;
    ID sid = Combine(id, 0x5C011u);
    float barW = s.scrollbarSize;
    Rect track(view.Right() - barW - 2, view.y + 3, barW, view.h - 6);
    float thumbH = Mx(24.0f, track.h * (view.h / contentH));
    float t = cs.scroll / maxScroll;
    Rect thumb(track.x, track.y + (track.h - thumbH) * t, barW, thumbH);
    bool hovered = Hoverable(track.Expand(2), sid);
    if (hovered && g->mClicked[0] && !g->mouseConsumed) {
        g->mouseConsumed = true;
        g->activeId = sid;
        cs.grab = thumb.Contains(g->mouse) ? g->mouse.y - thumb.y : thumbH * 0.5f;
    }
    bool active = g->activeId == sid;
    if (active) {
        g->activeAlive = true;
        if (!g->mDown[0]) {
            g->activeId = 0;
        } else {
            float tt = Clampf((g->mouse.y - cs.grab - track.y) / Mx(1.0f, track.h - thumbH), 0, 1);
            cs.scrollTarget = cs.scroll = tt * maxScroll;
            WantFrames(1);
        }
    }
    Anim(cs.barHover, (hovered || active) ? 1.0f : 0.0f, 14);
    float w = Lerpf(5.0f, barW - 2.0f, cs.barHover);
    Rect drawThumb(track.Right() - w - 1, thumb.y, w, thumb.h);
    Color c = s.scrollbar;
    c.a *= active ? 2.2f : Lerpf(1.0f, 1.7f, cs.barHover);
    AddRect(drawThumb, c, w * 0.5f);
}

static void ScrollArea(ID id, Rect view, float contentH, float topPad) {
    ContState& cs = CS(id);
    float maxScroll = Mx(0.0f, contentH - view.h);
    bool hovered = g->curLayerKey == g->hoverLayerKey && g->mouseInside && view.Contains(g->mouse) &&
                   (g->layDepth <= 0 || Lay().clip.Contains(g->mouse));
    if (hovered && g->wheel != 0 && maxScroll > 0 && !g->wheelConsumed) {
        cs.scrollTarget -= g->wheel * 80.0f;
        g->wheelConsumed = true;
    }
    if (g->scrollReqId == id) {
        Rect r = g->scrollReqRect;
        float top = r.y + cs.scroll - view.y - topPad;            // position within content
        float bottom = top + r.h + topPad * 2;
        if (top < cs.scrollTarget) cs.scrollTarget = top;
        else if (bottom > cs.scrollTarget + view.h) cs.scrollTarget = bottom - view.h;
        g->scrollReqId = 0;
    }
    cs.scrollTarget = Clampf(cs.scrollTarget, 0, maxScroll);
    float before = cs.scroll;
    Anim(cs.scroll, cs.scrollTarget, 22);
    cs.scroll = Clampf(cs.scroll, 0, maxScroll);
    if (cs.scroll != before) WantFrames(1);
    Scrollbar(id, view, contentH, cs);
    bool bar = maxScroll > 0;
    if (bar != cs.hadBar) { cs.hadBar = bar; WantFrames(1); }
    cs.contentH = contentH;
    cs.viewH = view.h;
}

// ---------------------------------------------------------------------------------
// Root container
// ---------------------------------------------------------------------------------

static void BeginRoot() {
    Style& s = g->style;
    float top = g->menubarH, bottom = g->footerH;
    Rect area(0, top, g->viewW, Mx(0.0f, g->viewH - top - bottom));
    g->rootId = Hash32("##root", 6, 0);
    ContState& cs = CS(g->rootId);
    g->curLayer = LayerIndex(0, 0);
    g->curLayerKey = 0;
    AddClipPush(area);
    LayoutState& L = PushLayout(LK_Root, g->rootId);
    L.rect = area;
    L.clip = area;
    L.pad = s.padding;
    L.left = s.padding;
    L.right = g->viewW - s.padding;
    L.scrollY = g->cfg.scroll ? cs.scroll : 0;
    L.lineY = L.startY = area.y + s.padding - L.scrollY;
    L.maxY = L.startY;
    L.maxX = L.left;
    L.labelKey = g->rootId;
    L.labelW = cs.labelW;
}

static void EndRoot() {
    LayoutState& L = Lay();
    FinishLine(L);
    ResolveLabelColumn(L);
    float contentH = (L.maxY - L.startY) + L.pad * 2;
    if (g->cfg.scroll) ScrollArea(L.id, L.rect, contentH, L.pad);
    AddClipPop();
    PopLayout();
}

// ---------------------------------------------------------------------------------
// Popups
// ---------------------------------------------------------------------------------

static int TopModal() {
    int m = -1;
    for (int i = 0; i < (int)g->popups.size(); i++) if (g->popups[i].kind == PK_Modal) m = i;
    return m;
}

static void ComputeHoverLayer() {
    g->hoverLayerKey = 0;
    g->navLayerKey = 0;
    int modal = TopModal();
    if (modal >= 0) g->navLayerKey = g->popups[modal].id;
    for (int i = (int)g->popups.size() - 1; i >= 0; i--) {
        const PopupState& p = g->popups[i];
        if (p.sized && p.rect.Contains(g->mouse)) { g->hoverLayerKey = p.id; return; }
        if (i == modal) { g->hoverLayerKey = p.id; return; }
    }
    for (const Rect& r : g->toastRects)
        if (r.Contains(g->mouse)) { g->hoverLayerKey = kOverlayKey; return; }
}

static void HandlePopupInput() {
    if (g->popups.empty()) return;
    if (g->kPressedFirst[VK_ESCAPE]) {
        if (g->popups.back().kind == PK_Menu && !g->popups.back().fromMenuBar) g->menuNoReopen = g->popups.back().id;
        g->popups.pop_back();
        g->kPressedFirst[VK_ESCAPE] = g->kPressed[VK_ESCAPE] = false;
        WantFrames(2);
        return;
    }
    if (!(g->mClicked[0] || g->mClicked[1] || g->mClicked[2])) return;
    int hovered = -1;
    for (int i = (int)g->popups.size() - 1; i >= 0; i--)
        if (g->popups[i].sized && g->popups[i].rect.Contains(g->mouse)) { hovered = i; break; }
    int keep = Mx(hovered, TopModal());
    if (keep < (int)g->popups.size() - 1) {
        g->popups.resize((size_t)(keep + 1));
        if (keep < 0) g->mouseConsumed = true;
        WantFrames(2);
    }
}

static void OpenPopupEx(ID id, int kind, Rect anchor, bool fromMenuBar = false) {
    int depth = g->popupDepth;
    if ((int)g->popups.size() > depth && g->popups[depth].id == id) {
        g->popups.resize((size_t)depth + 1);
        return;
    }
    if ((int)g->popups.size() > depth) g->popups.resize((size_t)depth);
    PopupState p;
    p.id = id;
    p.kind = kind;
    p.anchor = anchor;
    p.openMouse = g->mouse;
    p.openFrame = g->frame;
    p.fromMenuBar = fromMenuBar;
    p.submitted = false;
    g->popups.push_back(p);
    WantFrames(2);
}

static bool IsPopupOpen(ID id) {
    int depth = g->popupDepth;
    return (int)g->popups.size() > depth && g->popups[depth].id == id;
}

static Rect PositionPopup(const PopupState& p, float w, float h) {
    float m = 6, VW = g->viewW, VH = g->viewH;
    w = Mn(w, VW - m * 2);
    float x, y;
    switch (p.kind) {
    case PK_Modal:
        x = (VW - w) * 0.5f;
        y = Mx(m, (VH - h) * 0.5f - VH * 0.04f);
        break;
    case PK_Combo:
    case PK_Color:
        x = p.anchor.x;
        y = p.anchor.Bottom() + 4;
        if (y + h > VH - m && p.anchor.y - 4 - h >= m) y = p.anchor.y - 4 - h;
        break;
    case PK_Menu:
        if (p.fromMenuBar) { x = p.anchor.x; y = p.anchor.Bottom() + 2; }
        else {
            x = p.anchor.Right() + 2;
            y = p.anchor.y - 5;
            if (x + w > VW - m) x = p.anchor.x - w - 2;
        }
        break;
    default:
        x = p.openMouse.x + 2;
        y = p.openMouse.y + 2;
        if (x + w > VW - m) x = p.openMouse.x - w - 2;
        if (y + h > VH - m) y = p.openMouse.y - h - 2;
        break;
    }
    x = Clampf(x, m, Mx(m, VW - m - w));
    y = Clampf(y, m, Mx(m, VH - m - h));
    return Rect(Snap(x), Snap(y), Snap(w), Snap(Mn(h, VH - m * 2)));
}

static Cmd MakeCmd(uint8_t kind, Rect r, Color c, float rounding, float t = 0) {
    Cmd k;
    k.kind = kind;
    k.x = r.x; k.y = r.y; k.w = r.w; k.h = r.h; k.r = rounding; k.t = t;
    k.c = MulA(c, g->alphaMul);
    return k;
}

static bool BeginPopupEx(ID id, int kind, Rect anchor, float minW, float fixedW = 0) {
    int depth = g->popupDepth;
    if ((int)g->popups.size() <= depth || g->popups[depth].id != id) return false;
    PopupState& p = g->popups[depth];
    p.submitted = true;
    p.anchor = anchor;
    p.kind = kind;
    Style& s = g->style;
    float pad = kind == PK_Modal ? 18.0f : 5.0f;
    float w = fixedW > 0 ? fixedW : Mx(minW, p.sized ? p.size.x : minW);
    float h = p.sized ? p.size.y : 60.0f;
    Rect r = PositionPopup(p, w, h);
    p.rect = r;
    g->curLayer = LayerIndex(id, 1000 + depth * 10);
    g->curLayerKey = id;
    Layer& ly = CurL();
    Anim(p.anim, 1.0f, 20.0f);
    float a = p.sized ? EaseOut(p.anim) : 0.0f;
    ly.alpha = a;
    ly.scale = kind == PK_Modal ? Lerpf(0.96f, 1.0f, a) : 1.0f;
    ly.center = r.Center();
    if (!p.sized) WantFrames(2);
    LayoutState& L = PushLayout(LK_Popup, id);
    L.popupIdx = depth;
    L.pad = pad;
    L.rect = r;
    L.clip = Rect(0, 0, g->viewW, g->viewH);
    if (kind == PK_Modal) AddRect(Rect(-2, -2, g->viewW + 4, g->viewH + 4), s.overlay);
    L.clip = r;
    L.cmdMark = CurL().cmds.size();
    L.left = r.x + pad;
    L.right = r.Right() - pad;
    L.lineY = L.startY = r.y + pad;
    L.maxX = L.left;
    L.maxY = L.startY;
    L.labelKey = id;
    L.labelW = CS(id).labelW;
    AddClipPush(r);
    g->popupDepth++;
    return true;
}

static void EndPopupEx() {
    if (g->layDepth <= 0 || Lay().kind != LK_Popup) {
        Note("EndPopup()/EndMenu()/EndContextMenu() without a matching Begin call - ignored.");
        return;
    }
    LayoutState& L = Lay();
    FinishLine(L);
    ResolveLabelColumn(L);
    int depth = L.popupIdx;
    Style& s = g->style;
    AddClipPop();
    if (depth >= 0 && depth < (int)g->popups.size()) {
        PopupState& p = g->popups[depth];
        Vec2 ns(Snap(L.natW + L.pad * 2), Snap((L.maxY - L.startY) + L.pad * 2));
        if (!p.sized || fabsf(ns.x - p.size.x) > 0.5f || fabsf(ns.y - p.size.y) > 0.5f) {
            p.size = ns;
            p.sized = true;
            WantFrames(2);
        }
        float rounding = p.kind == PK_Modal ? 10.0f : 8.0f;
        Cmd bg[3];
        bg[0] = MakeCmd(CK_Shadow, L.rect, s.shadow, rounding, p.kind == PK_Modal ? 22.0f : 12.0f);
        bg[1] = MakeCmd(CK_Rect, L.rect, s.popup, rounding);
        bg[2] = MakeCmd(CK_RectStroke, L.rect, s.border, rounding, Px());
        Layer& ly = g->layers[L.layer];
        size_t at = Mn(L.cmdMark, ly.cmds.size());
        ly.cmds.insert(ly.cmds.begin() + (long)at, bg, bg + 3);
    }
    PopLayout();
    g->popupDepth = Mx(0, g->popupDepth - 1);
}

static void CloseCurrentPopup() {
    // Close the innermost popup being submitted (and its children).
    for (int i = g->layDepth - 1; i >= 0; i--) {
        if (g->lay[i].kind == LK_Popup) {
            int d = g->lay[i].popupIdx;
            if (d >= 0 && d < (int)g->popups.size()) g->popups.resize((size_t)d);
            WantFrames(2);
            return;
        }
    }
}

static void CloseAllPopups() { if (!g->popups.empty()) { g->popups.clear(); WantFrames(2); } }

// ---------------------------------------------------------------------------------
// Tooltips and toasts
// ---------------------------------------------------------------------------------

static void UseOverlayLayer() {
    g->curLayer = LayerIndex(kOverlayKey, 100000);
    g->curLayerKey = kOverlayKey;
}

static void DrawTooltip() {
    Style& s = g->style;
    UseOverlayLayer();
    Str text(g->tipText);
    TextEntry* e = GetText(text, FontBody);
    if (e->w > 320) e = GetText(text, FontBody, TF_Wrap, 320);
    float w = e->w + 18, h = e->h + 12;
    float x = g->mouse.x + 12, y = g->mouse.y + 22;
    if (x + w > g->viewW - 4) x = g->viewW - 4 - w;
    if (y + h > g->viewH - 4) y = g->mouse.y - h - 8;
    Rect r(Snap(Mx(4.0f, x)), Snap(Mx(4.0f, y)), Snap(w), Snap(h));
    float a = Clampf((float)(g->time - g->tipStart - 0.45) * 8.0f, 0, 1);
    if (a < 1) WantFrames(1);
    g->alphaMul = a;
    AddShadow(r, 6, 8, s.shadow.WithAlpha(s.shadow.a * 0.7f));
    AddRect(r, s.tooltip, 6);
    AddRectStroke(r, s.border, 6);
    AddTextEntry(e, Vec2(r.x + 9, r.y + 6), s.text);
    g->alphaMul = 1;
}

static Color LevelColor(Level l) {
    Style& s = g->style;
    switch (l) {
    case Level::Success: return s.success;
    case Level::Warning: return s.warning;
    case Level::Error: return s.danger;
    default: return s.accent;
    }
}
static Icon LevelIcon(Level l) {
    switch (l) {
    case Level::Success: return Icon::Success;
    case Level::Warning: return Icon::Warning;
    case Level::Error: return Icon::Error;
    default: return Icon::Info;
    }
}

static void DrawToasts() {
    g->toastRects.clear();
    if (g->toasts.empty() || g->hiddenAt >= 0) return;      // (they wait while the window can't be seen)
    Style& s = g->style;
    UseOverlayLayer();
    float y = g->viewH - 16 - g->footerH;
    float maxW = Mn(440.0f, g->viewW - 32);
    for (int i = (int)g->toasts.size() - 1; i >= 0; i--) {
        ToastItem& t = g->toasts[i];
        double age = g->time - t.start;
        bool expiring = age > t.duration;
        Anim(t.anim, expiring ? 0.0f : 1.0f, 16);
        if (expiring && t.anim <= 0.01f) { g->toasts.erase(g->toasts.begin() + i); continue; }
        if (!expiring) WakeAt(t.start + t.duration + 0.02);
        Str text(t.text);
        TextEntry* e = GetText(text, FontBody);
        if (e->w > maxW - 56) e = GetText(text, FontBody, TF_Wrap, maxW - 56);
        float w = e->w + 56, h = Mx(e->h + 22, 42.0f);
        float a = EaseOut(t.anim);
        Rect r(Snap((g->viewW - w) * 0.5f), Snap(y - h + (1 - a) * 14), Snap(w), Snap(h));
        bool hov = g->hoverLayerKey == kOverlayKey && r.Contains(g->mouse);
        if (hov && g->mClicked[0]) { t.start = g->time - t.duration - 1; g->mouseConsumed = true; }
        if (hov && !expiring) t.start = Mx(t.start, g->time - t.duration + 1.0);
        g->alphaMul = a;
        Color lc = LevelColor(t.level);
        AddShadow(r, 8, 14, s.shadow);
        AddRect(r, s.popup, 8);
        AddRectStroke(r, s.border, 8);
        AddRect(Rect(r.x + 1, r.y + 8, 3, r.h - 16), lc, 1.5f);
        AddIcon(LevelIcon(t.level), Rect(r.x + 14, r.y + (r.h - 18) * 0.5f, 18, 18), lc, 0, 2.0f);
        AddTextEntry(e, Vec2(r.x + 42, Snap(r.y + (r.h - e->h) * 0.5f)), s.text);
        g->alphaMul = 1;
        g->toastRects.push_back(r);
        y = r.y - 8 * a;
    }
}

// ---------------------------------------------------------------------------------
// Frame begin / end
// ---------------------------------------------------------------------------------

static void EndTextEdit();
static void EndTableIfOpen();

// Keeps Style usable whatever the program wrote into it (NaN, negative or absurd values).
static void SanitizeStyle() {
    Style& s = g->style;
    struct F { static void Fix(float& v, float lo, float hi, float def) { v = (v >= lo && v <= hi) ? v : (v > hi ? hi : (v < lo ? lo : def)); } };
    F::Fix(s.fontSize, 6, 72, 14);
    F::Fix(s.frameHeight, 12, 200, 30);
    F::Fix(s.rowHeight, 12, 200, 28);
    F::Fix(s.padding, 0, 200, 14);
    F::Fix(s.spacing, 0, 200, 8);
    F::Fix(s.spacingX, 0, 200, 8);
    F::Fix(s.framePadding, 0, 100, 10);
    F::Fix(s.rounding, 0, 100, 6);
    F::Fix(s.cardRounding, 0, 100, 8);
    F::Fix(s.cardPadding, 0, 200, 14);
    F::Fix(s.indent, 0, 400, 18);
    F::Fix(s.scrollbarSize, 2, 50, 10);
    F::Fix(s.labelMaxFraction, 0.05f, 0.95f, 0.45f);
    s.fontFamily.name[sizeof(s.fontFamily.name) - 1] = 0;
    s.monoFamily.name[sizeof(s.monoFamily.name) - 1] = 0;
    Color* colors[] = {&s.bg, &s.panel, &s.popup, &s.field, &s.frame, &s.frameHover, &s.frameActive, &s.border,
                       &s.borderStrong, &s.separator, &s.text, &s.textDim, &s.textDisabled, &s.accent, &s.accentHover,
                       &s.accentActive, &s.accentText, &s.success, &s.warning, &s.danger, &s.selection, &s.scrollbar,
                       &s.shadow, &s.overlay, &s.tooltip};
    for (Color* c : colors) { c->r = Unit(c->r); c->g = Unit(c->g); c->b = Unit(c->b); c->a = Unit(c->a); }
}

// A toast, or a Windows notification when the window is hidden in the tray.
static void AddToast(const ToastItem& t) {
    if (g->trayShown && g->shown && !WindowVisible()) { TrayBalloon(t); return; }
    g->toasts.push_back(t);
    if (g->toasts.size() > 5) g->toasts.erase(g->toasts.begin());
    WantFrames(2);
}

static void ApplyThreadRequests() {
    std::vector<ToastItem> toasts;
    std::wstring title;
    bool hasTitle, save;
    double wakeAt;
    {
        std::lock_guard<std::mutex> lock(g->xmutex);
        toasts.swap(g->xToasts);
        title.swap(g->xTitle);
        hasTitle = g->xHasTitle; g->xHasTitle = false;
        save = g->xSave; g->xSave = false;
        wakeAt = g->xWakeAt; g->xWakeAt = 1e300;
    }
    for (ToastItem& t : toasts) {
        t.start = Now();
        AddToast(t);
    }
    if (hasTitle) { g->title = title; if (g->hwnd) SetWindowTextW(g->hwnd, g->title.c_str()); }
    if (wakeAt < 1e299) WakeAt(wakeAt);
    if (save) { SaveWindowPlacement(); SaveSettingsFile(); }
}

static void UiNewFrame() {
    SanitizeStyle();
    ApplyThreadRequests();
    g->frame++;
    double now = Now();
    g->dt = g->frame <= 1 ? (1.0f / 60.0f) : (float)Clampf((float)(now - g->prevTime), 0.0f, 0.1f);
    g->prevTime = now;
    g->time = now;
    // Toasts wait while the window can't be seen (minimized or hidden).
    bool visible = WindowVisible();
    if (!visible && g->hiddenAt < 0) g->hiddenAt = now;
    else if (visible && g->hiddenAt >= 0) {
        for (ToastItem& t : g->toasts) t.start += now - g->hiddenAt;
        g->hiddenAt = -1;
    }
    EnsureFonts();
    g->droppedFrame.swap(g->droppedPending);
    g->droppedPending.clear();
    g->dropPos = g->dropPosPending;
    g->hotkeysFrame.swap(g->hotkeysFired);
    g->hotkeysFired.clear();
    g->mouseConsumed = false;
    g->wheelConsumed = false;
    g->tabConsumed = false;
    g->frameH = g->style.frameHeight;
    g->font = FontBody;
    g->fontStack.clear();
    g->alphaMul = 1;
    g->alphaStack.clear();
    g->disabled = 0;
    g->disabledStack.clear();
    g->idStack.clear();
    g->hotId = 0;
    g->activeAlive = false;
    g->focusOrder.clear();
    g->focusClaimed = false;
    g->cursor = kCurArrow;
    g->last = ItemInfo();
    g->hasNextW = false;
    g->layerCount = 0;
    g->layDepth = 0;
    g->tipSeen = false;
    g->tipRequested = false;
    g->menubarSeen = false;
    g->footerSeen = false;
    g->te.seen = false;
    g->popupDepth = 0;
    g->inCanvas = false;
    g->treeDepth = 0;
    g->table.active = false;
    for (auto& p : g->popups) p.submitted = false;
    HandlePopupInput();
    ComputeHoverLayer();
    g->navDir = 0;
    if (g->kPressed[VK_TAB] && !g->kDown[VK_CONTROL] && !g->kDown[VK_MENU]) g->navDir = g->kDown[VK_SHIFT] ? -1 : 1;
    if (g->mClicked[0] || g->mClicked[1] || g->mClicked[2]) g->tipStart = g->time + 1e9;
    LayerIndex(0, 0);
    BeginRoot();
}

static void CloseOpenContainers();

static void UiEndFrame() {
    CloseOpenContainers();
    if (g->layDepth == 1) EndRoot();
    g->layDepth = 0;
    g->curLayer = 0;
    g->curLayerKey = 0;
    ApplyTaskbarProgress();
    g->focusNext = false;                       // FocusNext() only reaches widgets of the same frame
    for (size_t i = 0; i < g->popups.size(); i++) {
        if (!g->popups[i].submitted && g->popups[i].openFrame != g->frame) {
            g->popups.resize(i);
            WantFrames(1);
            break;
        }
    }
    if (!g->tipSeen) g->tipId = 0;
    if (g->tipRequested) DrawTooltip();
    DrawToasts();
    if ((g->mClicked[0] || g->mClicked[1]) && !g->focusClaimed) g->focusId = 0;
    if (g->navDir != 0 && !g->tabConsumed && !g->focusOrder.empty()) {
        int n = (int)g->focusOrder.size(), idx = -1;
        for (int i = 0; i < n; i++) if (g->focusOrder[i] == g->focusId) { idx = i; break; }
        int next = idx < 0 ? (g->navDir > 0 ? 0 : n - 1) : (idx + g->navDir + n) % n;
        g->focusId = g->focusOrder[next];
        g->focusVisible = true;
        g->navArrived = g->focusId;
        WantFrames(2);
    }
    if (g->te.id && (!g->te.seen || g->focusId != g->te.id)) EndTextEdit();
    if (g->activeId && !g->activeAlive) g->activeId = 0;
    if (!g->menubarSeen && g->menubarH != 0) { g->menubarH = 0; WantFrames(1); }
    if (!g->footerSeen && g->footerH != 0) { g->footerH = 0; WantFrames(1); }
    if (g->mouseInside) SetCursor(LoadCursorW(nullptr, g->cursor));
    if (g->frame % 300 == 0) {
        for (auto it = g->wstate.begin(); it != g->wstate.end();) {
            if (g->frame - it->second.frame > 1200 && !it->second.keep) it = g->wstate.erase(it);
            else ++it;
        }
    }
}

static void PostFrame() {
    for (int b = 0; b < 3; b++) { g->mClicked[b] = g->mReleased[b] = g->mDouble[b] = false; }
    g->wheel = g->wheelH = 0;
    memset(g->kPressed, 0, sizeof(g->kPressed));
    memset(g->kPressedFirst, 0, sizeof(g->kPressedFirst));
    memset(g->kReleased, 0, sizeof(g->kReleased));
    g->events.clear();
    g->droppedFrame.clear();
    g->hotkeysFrame.clear();
    g->closeRequested = false;
    EvictTextCache();
    FlushDeferred();
}

} // namespace detail

// =================================================================================
// Public: layout
// =================================================================================

#define SUI_GUARD(ret) if (!detail::g || !detail::g->inFrame || detail::g->layDepth <= 0) return ret

void SameLine(float spacing) {
    SUI_GUARD();
    detail::LayoutState& L = detail::Lay();
    L.sameLine = true;
    L.sameSpacing = spacing;
}
void NewLine() {
    SUI_GUARD();
    using namespace detail;
    LayoutState& L = Lay();
    if (L.lineItems) FinishLine(L);
    else { L.lineY += LineH(g->font) + g->style.spacing; L.maxY = Mx(L.maxY, L.lineY - g->style.spacing); }
}
void Spacing(float height) {
    SUI_GUARD();
    using namespace detail;
    LayoutState& L = Lay();
    FinishLine(L);
    L.lineY += height < 0 ? g->style.spacing : height;
    L.maxY = Mx(L.maxY, L.lineY - g->style.spacing);
}
void Spacer(float width, float height) { SUI_GUARD(); detail::PlaceItem(width, height); }
void Indent(float amount) { SUI_GUARD(); detail::Lay().indent += amount < 0 ? detail::g->style.indent : amount; }
void Unindent(float amount) {
    SUI_GUARD();
    detail::LayoutState& L = detail::Lay();
    L.indent = detail::Mx(0.0f, L.indent - (amount < 0 ? detail::g->style.indent : amount));
}
void NextWidth(float width) { if (!detail::g) return; detail::g->hasNextW = true; detail::g->nextW = width; }
float AvailableWidth() { SUI_GUARD(0.0f); return detail::AvailW(); }

namespace detail {
// Copies up to 16 weights, replacing bad ones (negative, NaN, huge) and all-zero lists by equal weights.
static int CleanWeights(const float* in, int n, float* out, float& sum) {
    n = Mx(1, Mn(n, 16));
    sum = 0;
    for (int i = 0; i < n; i++) {
        float v = in ? in[i] : 1.0f;
        out[i] = (v > 0 && v < 1e6f) ? v : 0.0f;
        sum += out[i];
    }
    if (sum <= 0) { for (int i = 0; i < n; i++) out[i] = 1.0f; sum = (float)n; }
    return n;
}
} // namespace detail

static void BeginRowImpl(const float* weights, int n) {
    using namespace detail;
    LayoutState& L = Lay();
    FinishLine(L);
    float wt[16], sum;
    n = CleanWeights(weights, n, wt, sum);
    float total = Mx(0.0f, L.right - (L.left + L.indent));
    float gap = g->style.spacingX;
    float avail = Mx(0.0f, total - gap * (n - 1));
    float x = L.left + L.indent;
    for (int i = 0; i < n; i++) {
        float w = avail * wt[i] / sum;
        L.rowX[i] = x;
        L.rowW[i] = w;
        x += w + gap;
    }
    L.rowN = n;
    L.rowCell = 0;
}
void BeginRow(int columns) { SUI_GUARD(); BeginRowImpl(nullptr, columns); }
void EndRow() {
    SUI_GUARD();
    detail::LayoutState& L = detail::Lay();
    detail::FinishLine(L);
    L.rowN = 0;
    L.rowCell = 0;
}

namespace detail {
static void SetColumn(LayoutState& L, int i) {
    L.colIdx = i;
    L.left = L.colX[i];
    L.right = L.colX[i] + L.colW[i];
    L.indent = 0;
    L.lineY = L.startY = L.colStartY;
    L.lineH = 0;
    L.lineItems = false;
    L.maxY = L.colStartY;
    L.labelKey = Combine(L.id, (uint32_t)i + 1);
    L.labelW = CS(L.labelKey).labelW;
    L.labelMeasure = 0;
    L.rowN = 0;
}
static void BeginColumnsImpl(const float* weights, int n) {
    LayoutState& P = Lay();
    FinishLine(P);
    float wt[16], sum;
    n = CleanWeights(weights, n, wt, sum);
    float x0 = P.left + P.indent, y0 = P.lineY;
    float total = Mx(0.0f, P.right - x0);
    ID id = Combine(P.id, 0xC0100u + (uint32_t)P.counter++);
    Rect clip = P.clip;
    float gap = g->style.spacingX * 2;
    float avail = Mx(0.0f, total - gap * (n - 1));
    LayoutState& L = PushLayout(LK_Columns, id);
    L.clip = clip;
    L.rect = Rect(x0, y0, total, 0);
    L.colN = n;
    float x = x0;
    for (int i = 0; i < n; i++) {
        float w = Snap(avail * wt[i] / sum);
        L.colX[i] = x;
        L.colW[i] = w;
        x += w + gap;
    }
    L.colStartY = y0;
    L.colMaxY = y0;
    SetColumn(L, 0);
}
} // namespace detail

void BeginColumns(int columns) { SUI_GUARD(); detail::BeginColumnsImpl(nullptr, columns); }
void NextColumn() {
    SUI_GUARD();
    using namespace detail;
    LayoutState& L = Lay();
    if (L.kind != LK_Columns) { Note("NextColumn() outside BeginColumns() - ignored."); return; }
    FinishLine(L);
    ResolveLabelColumn(L);
    L.colMaxY = Mx(L.colMaxY, L.maxY);
    if (L.colIdx + 1 < L.colN) SetColumn(L, L.colIdx + 1);
}
void EndColumns() {
    SUI_GUARD();
    using namespace detail;
    if (Lay().kind != LK_Columns) { Note("EndColumns() without a matching BeginColumns() - ignored."); return; }
    LayoutState& L = Lay();
    FinishLine(L);
    ResolveLabelColumn(L);
    L.colMaxY = Mx(L.colMaxY, L.maxY);
    float h = L.colMaxY - L.colStartY, w = L.rect.w;
    PopLayout();
    PlaceItem(w, Mx(h, 0.0f));
}

void BeginHorizontal(Align align) {
    SUI_GUARD();
    using namespace detail;
    LayoutState& L = Lay();
    FinishLine(L);
    ID hid = Combine(L.id, 0x40210u + (uint32_t)L.counter++);
    ContState& cs = CS(hid);
    float avail = Mx(0.0f, L.right - (L.left + L.indent));
    float startX = L.left + L.indent;
    if (align == Align::Center) startX += Mx(0.0f, (avail - cs.lastW) * 0.5f);
    else if (align == Align::Right) startX += Mx(0.0f, avail - cs.lastW);
    L.horiz++;
    L.horizId = hid;
    L.horizStartX = startX;
    L.hasForceX = true;
    L.forceX = startX;
}
void EndHorizontal() {
    SUI_GUARD();
    using namespace detail;
    LayoutState& L = Lay();
    if (L.horiz <= 0) return;
    float w = L.lineItems ? L.lineX - L.horizStartX : 0.0f;
    ContState& cs = CS(L.horizId);
    if (fabsf(w - cs.lastW) > 0.5f) { cs.lastW = w; WantFrames(1); }
    L.horiz--;
    L.hasForceX = false;
    FinishLine(L);
}

void BeginCard(Str title) {
    SUI_GUARD();
    using namespace detail;
    LayoutState& P = Lay();
    FinishLine(P);
    Style& s = g->style;
    float x = P.left + P.indent, y = P.lineY;
    float w = ResolveWidth(Mx(0.0f, P.right - x), Mx(0.0f, P.right - x), true);
    ID id = Combine(Combine(P.id, 0xCA4Du + (uint32_t)P.counter++), Hash32(title.s, title.n, 0));
    Rect clip = P.clip;
    LayoutState& L = PushLayout(LK_Card, id);
    L.rect = Rect(Snap(x), Snap(y), Snap(w), 0);
    L.clip = clip;
    L.pad = s.cardPadding;
    L.left = L.rect.x + L.pad;
    L.right = L.rect.Right() - L.pad;
    L.lineY = L.startY = L.rect.y + L.pad;
    L.maxY = L.startY;
    L.maxX = L.left;
    L.cmdMark = CurL().cmds.size();
    L.labelKey = id;
    L.labelW = CS(id).labelW;
    Str t = DisplayText(title);
    if (t.n) {
        Rect r = PlaceItem(TextW(t, FontBold), LineH(FontBold));
        DrawTextIn(t, Rect(r.x, r.y, L.right - r.x, r.h), FontBold, s.text);
    }
}
void EndCard() {
    SUI_GUARD();
    using namespace detail;
    if (Lay().kind != LK_Card) { Note("EndCard() without a matching BeginCard() - ignored."); return; }
    LayoutState& L = Lay();
    FinishLine(L);
    ResolveLabelColumn(L);
    Style& s = g->style;
    float h = Snap(Mx(L.maxY, L.startY) - L.rect.y + L.pad);
    Rect r(L.rect.x, L.rect.y, L.rect.w, h);
    Cmd bg[2];
    bg[0] = MakeCmd(CK_Rect, r, s.panel, s.cardRounding);
    bg[1] = MakeCmd(CK_RectStroke, r, s.border, s.cardRounding, Px());
    Layer& ly = g->layers[L.layer];
    size_t at = Mn(L.cmdMark, ly.cmds.size());
    ly.cmds.insert(ly.cmds.begin() + (long)at, bg, bg + 2);
    PopLayout();
    PlaceItem(r.w, r.h);
}

namespace detail {
static void BeginChildRect(ID cid, Rect r, bool border, float pad, float spacingY) {
    LayoutState& P = Lay();
    Style& s = g->style;
    ContState& cs = CS(cid);
    Rect clip = Intersect(border ? r.Shrink(1) : r, P.clip);
    if (border) AddRect(r, s.field, s.rounding);
    LayoutState& L = PushLayout(LK_Child, cid);
    L.rect = r;
    L.clip = clip;
    L.border = border;
    L.pad = pad < 0 ? (border ? 8.0f : 0.0f) : pad;
    L.spacingY = spacingY;
    float sb = cs.hadBar ? s.scrollbarSize + 2.0f : 0.0f;
    L.left = r.x + L.pad;
    L.right = r.Right() - L.pad - sb;
    L.scrollY = cs.scroll;
    L.lineY = L.startY = r.y + L.pad - cs.scroll;
    L.maxY = L.startY;
    L.maxX = L.left;
    L.labelKey = cid;
    L.labelW = cs.labelW;
    AddClipPush(clip);
}
} // namespace detail

void BeginChild(Str id, float height, bool border) {
    SUI_GUARD();
    using namespace detail;
    LayoutState& P = Lay();
    ID cid = MakeID(id);
    float avail = AvailW();
    float w = ResolveWidth(avail, avail, true);
    float h = height;
    if (h <= 0) {
        float bottom = (P.kind == LK_Root || P.kind == LK_Popup) ? P.rect.Bottom() - P.pad : P.clip.Bottom() - P.pad;
        h = Mx(60.0f, bottom - NextItemY(P) + height);
    }
    Rect r = PlaceItem(w, h);
    BeginChildRect(cid, r, border, -1, -1);
}
void EndChild() {
    SUI_GUARD();
    using namespace detail;
    if (Lay().kind != LK_Child) { Note("EndChild() without a matching BeginChild() - ignored."); return; }
    LayoutState& L = Lay();
    FinishLine(L);
    ResolveLabelColumn(L);
    float contentH = (L.maxY - L.startY) + L.pad * 2;
    ScrollArea(L.id, L.rect, contentH, L.pad);
    AddClipPop();
    if (L.border) AddRectStroke(L.rect, g->style.border, g->style.rounding);
    Rect r = L.rect;
    ID id = L.id;
    PopLayout();
    g->last = ItemInfo();
    g->last.id = id;
    g->last.rect = r;
    g->last.hovered = g->curLayerKey == g->hoverLayerKey && r.Contains(g->mouse) && g->mouseInside;
    g->last.rightClicked = g->last.hovered && g->mClicked[1];
}

void BeginFooter() {
    SUI_GUARD();
    using namespace detail;
    Style& s = g->style;
    g->footerSeen = true;
    float h = g->footerH > 0 ? g->footerH : g->frameH + 20;
    Rect r(0, g->viewH - h, g->viewW, h);
    g->curLayer = LayerIndex(0, 1);
    g->curLayerKey = 0;
    LayoutState& L = PushLayout(LK_Footer, Hash32("##footer", 8, 0));
    L.rect = r;
    L.clip = r;
    L.pad = 10;
    L.left = s.padding;
    L.right = g->viewW - s.padding;
    L.lineY = L.startY = r.y + 10;
    L.maxY = L.startY;
    L.labelKey = L.id;
    L.labelW = CS(L.id).labelW;
    AddRect(r, s.panel);
    AddRect(Rect(0, r.y, g->viewW, Px()), s.separator);
}
void EndFooter() {
    SUI_GUARD();
    using namespace detail;
    if (Lay().kind != LK_Footer) { Note("EndFooter() without a matching BeginFooter() - ignored."); return; }
    LayoutState& L = Lay();
    FinishLine(L);
    ResolveLabelColumn(L);
    float h = Snap((L.maxY - L.startY) + 20);
    if (fabsf(h - g->footerH) > 0.5f) { g->footerH = h; WantFrames(1); }
    PopLayout();
}

void BeginDisabled(bool disabled) {
    if (!detail::g) return;
    detail::g->disabledStack.push_back(disabled);
    if (disabled) {
        detail::g->disabled++;
        detail::g->alphaStack.push_back(detail::g->alphaMul);
        detail::g->alphaMul *= 0.45f;
    }
}
void EndDisabled() {
    using namespace detail;
    if (!g || g->disabledStack.empty()) { if (g) Note("EndDisabled() without a matching BeginDisabled() - ignored."); return; }
    bool d = g->disabledStack.back();
    g->disabledStack.pop_back();
    if (d) {
        g->disabled = Mx(0, g->disabled - 1);
        if (!g->alphaStack.empty()) { g->alphaMul = g->alphaStack.back(); g->alphaStack.pop_back(); }
        else g->alphaMul = 1;
    }
}

void PushID(Str id) { if (detail::g) detail::g->idStack.push_back(detail::MakeID(id)); }
void PushID(int id) { if (detail::g) detail::g->idStack.push_back(detail::MakeIDInt(id)); }
void PushID(const void* ptr) {
    if (!detail::g) return;
    uintptr_t v = (uintptr_t)ptr;
    detail::g->idStack.push_back(detail::Hash32(&v, sizeof(v), detail::SeedID() ^ 0x77u));
}
void PopID() {
    if (!detail::g) return;
    if (detail::g->idStack.empty()) { detail::Note("PopID() without a matching PushID() - ignored."); return; }
    detail::g->idStack.pop_back();
}
ID GetID(Str label) { return detail::g ? detail::MakeID(label) : 0; }

void FocusNext() { if (detail::g) detail::g->focusNext = true; }

const ItemInfo& LastItem() {
    static ItemInfo empty;
    return detail::g ? detail::g->last : empty;
}

void Tooltip(Str text) {
    using namespace detail;
    if (!g || !g->inFrame || !g->last.hovered || text.n == 0) return;
    ItemInfo& it = g->last;
    ID id = it.id ? it.id : Hash32(&it.rect, sizeof(Rect), 0x7171u);
    g->tipSeen = true;
    if (g->tipId != id) { g->tipId = id; g->tipStart = g->time; }
    double shown = g->tipStart + 0.45;
    if (g->time >= shown) {
        g->tipText.assign(text.s, text.n);
        g->tipRequested = true;
    } else {
        WakeAt(shown + 0.005);
    }
}

// =================================================================================
// Public: style, fonts
// =================================================================================

Style& GetStyle() { return detail::Ctx()->style; }
void SetTheme(Theme theme) { detail::Ctx()->theme = theme; detail::ApplyTheme(); }
Theme GetTheme() { return detail::Ctx()->theme; }
bool IsDark() { detail::Ctx(); return detail::ThemeIsDark(); }
void SetAccent(Color accent) {
    detail::Context* c = detail::Ctx();
    c->customAccent = true;
    c->accentUser = accent;
    detail::DeriveAccent(accent);
    detail::WantFrames(1);
}
Color SystemAccent() {
    DWORD v = 0, size = sizeof(v);
    if (RegGetValueW(HKEY_CURRENT_USER, L"Software\\Microsoft\\Windows\\DWM", L"AccentColor", RRF_RT_REG_DWORD, nullptr, &v, &size) == ERROR_SUCCESS)
        return Rgb(((v & 0xFFu) << 16) | (v & 0xFF00u) | ((v >> 16) & 0xFFu));   // stored as 0xAABBGGRR
    detail::Ctx();
    detail::LoadModules();
    DWORD argb = 0;
    BOOL opaque = FALSE;
    if (detail::g->dwmGetColor && SUCCEEDED(detail::g->dwmGetColor(&argb, &opaque)))
        return Rgb(argb & 0xFFFFFFu);
    return Rgb(0x4C8DFF);
}

FontId AddFont(Str family, float size, bool bold, bool italic) {
    using namespace detail;
    if (!g || !g->dw) return FontBody;
    EnsureFonts();
    FontSlot f;
    f.family = PickFamily(std::string(family.s, family.n).c_str(), {L"Segoe UI", L"Tahoma", L"Arial"});  // unknown -> default
    f.size = (size >= 4 && size <= 400) ? size : (size > 400 ? 400.0f : 14.0f);   // NaN -> 14
    f.weight = bold ? 700 : 400;
    f.italic = italic;
    // The same font again (e.g. AddFont() called every frame) returns the existing one.
    for (size_t i = 6; i < g->fonts.size(); i++) {
        const FontSlot& o = g->fonts[i];
        if (o.fmt && o.size == f.size && o.weight == f.weight && o.italic == f.italic && o.family == f.family) return (FontId)i;
    }
    if (g->fonts.size() >= 256 || !BuildFont(f)) return FontBody;
    g->fonts.push_back(f);
    return (FontId)g->fonts.size() - 1;
}
void PushFont(FontId font) {
    if (!detail::g) return;
    detail::g->fontStack.push_back(detail::g->font);
    detail::g->font = detail::ValidFont(font);
}
void PopFont() {
    if (!detail::g) return;
    if (detail::g->fontStack.empty()) { detail::Note("PopFont() without a matching PushFont() - ignored."); return; }
    detail::g->font = detail::g->fontStack.back();
    detail::g->fontStack.pop_back();
}
float TextWidth(Str text, FontId font) { return detail::g && detail::g->dw && !detail::g->fonts.empty() ? detail::TextW(text, font) : 0.0f; }
float LineHeight(FontId font) { return detail::g && !detail::g->fonts.empty() ? detail::LineH(font) : 18.0f; }

// =================================================================================
// Public: popups
// =================================================================================

void OpenPopup(Str id) {
    if (!detail::g || !detail::g->inFrame) return;
    detail::OpenPopupEx(detail::MakeID(id), detail::PK_Popup, detail::g->last.rect);
}
bool BeginPopup(Str id) {
    SUI_GUARD(false);
    return detail::BeginPopupEx(detail::MakeID(id), detail::PK_Popup, detail::g->last.rect, 160.0f);
}
void EndPopup() { SUI_GUARD(); detail::EndPopupEx(); }
void ClosePopup() { SUI_GUARD(); detail::CloseCurrentPopup(); }

bool BeginContextMenu() {
    SUI_GUARD(false);
    using namespace detail;
    ItemInfo& it = g->last;
    ID base = it.id ? it.id : Hash32(&it.rect, sizeof(Rect), 0x3311u);
    ID id = Combine(base, 0xC7Eu);
    if (it.rightClicked) { g->mouseConsumed = true; OpenPopupEx(id, PK_Context, it.rect); }
    return BeginPopupEx(id, PK_Context, it.rect, 170.0f);
}
void EndContextMenu() { SUI_GUARD(); detail::EndPopupEx(); }

void OpenModal(Str title) {
    if (!detail::g || !detail::g->inFrame) return;
    detail::Context* c = detail::g;
    int saved = c->popupDepth;
    c->popupDepth = (int)c->popups.size();   // modals stack on top of everything
    for (auto& p : c->popups) if (p.id == detail::Hash32(title.s, title.n, 0x0D41u)) { c->popupDepth = saved; return; }
    detail::OpenPopupEx(detail::Hash32(title.s, title.n, 0x0D41u), detail::PK_Modal, Rect());
    c->popupDepth = saved;
}
bool BeginModal(Str title) {
    SUI_GUARD(false);
    using namespace detail;
    ID id = Hash32(title.s, title.n, 0x0D41u);
    int idx = -1;
    for (int i = 0; i < (int)g->popups.size(); i++) if (g->popups[i].id == id) { idx = i; break; }
    if (idx < 0) return false;
    int saved = g->popupDepth;
    g->popupDepth = idx;
    float w = 380.0f;
    if (g->hasNextW) { w = g->nextW > 1 ? g->nextW : 380.0f; g->hasNextW = false; }
    if (!BeginPopupEx(id, PK_Modal, Rect(), Mn(w, g->viewW - 24), Mn(w, g->viewW - 24))) { g->popupDepth = saved; return false; }
    Lay().savedDepth = saved;
    Style& s = g->style;
    Str t = DisplayText(title);
    if (t.n) {
        LayoutState& L = Lay();
        Rect r = PlaceItem(L.right - L.left - 26, LineH(FontHeading));
        DrawTextIn(t, Rect(r.x, r.y, r.w, r.h), FontHeading, s.text);
        Rect cb(L.right - 26, r.y + (r.h - 26) * 0.5f, 26, 26);
        ID cid = Combine(id, 0xC105Eu);
        bool hov, held;
        if (ButtonBehavior(cb, cid, hov, held, BF_NoFocus)) CloseCurrentPopup();
        if (hov) AddRect(cb, held ? s.frameActive : s.frameHover, 6);
        AddIcon(Icon::Close, cb.Shrink(6), hov ? s.text : s.textDim, 0, 2.0f);
        Spacing(2);
    }
    return true;
}
void EndModal() {
    SUI_GUARD();
    using namespace detail;
    if (Lay().kind != LK_Popup) { Note("EndModal() without a matching BeginModal() - ignored."); return; }
    int saved = Lay().savedDepth;
    EndPopupEx();
    g->popupDepth = saved;
}
void CloseModal() {
    SUI_GUARD();
    using namespace detail;
    for (int i = g->layDepth - 1; i >= 0; i--) {
        if (g->lay[i].kind == LK_Popup) {
            int d = g->lay[i].popupIdx;
            if (d >= 0 && d < (int)g->popups.size() && g->popups[d].kind == PK_Modal) {
                g->popups.resize((size_t)d);
                WantFrames(2);
                return;
            }
        }
    }
    int m = TopModal();
    if (m >= 0) { g->popups.resize((size_t)m); WantFrames(2); }
}

void Toast(Str text, Level level, float seconds) {
    using namespace detail;
    Context* c = Ctx();
    ToastItem t;
    t.text.assign(text.s, Mn(text.n, (size_t)2000));
    t.level = (int)level >= 0 && (int)level <= (int)Level::Error ? level : Level::Info;
    t.start = c->running ? Now() : 0.0;
    t.duration = seconds >= 0.5f ? Mn(seconds, 3600.0f) : (seconds < 0.5f ? 0.5f : 3.0f);   // NaN -> 3 s
    if (!OnUiThread() || !c->running) {          // shown at the start of the next frame
        { std::lock_guard<std::mutex> lock(c->xmutex); if (c->xToasts.size() < 20) c->xToasts.push_back(t); }
        Wake();
        return;
    }
    AddToast(t);
}

namespace detail {
static void CloseOpenContainers() {
    int guard = 64;
    while (g->layDepth > 1 && guard-- > 0) {
        switch (Lay().kind) {
        case LK_Child: Note("BeginChild() without EndChild() - closed automatically."); EndChild(); break;
        case LK_Card: Note("BeginCard() without EndCard() - closed automatically."); EndCard(); break;
        case LK_Columns: Note("BeginColumns() without EndColumns() - closed automatically."); EndColumns(); break;
        case LK_Footer: Note("BeginFooter() without EndFooter() - closed automatically."); EndFooter(); break;
        case LK_Popup: Note("A popup, menu or modal was not ended (End...() missing) - closed automatically."); EndPopupEx(); break;
        case LK_Table: case LK_Cell: Note("BeginTable() without EndTable() - closed automatically."); EndTableIfOpen(); break;
        case LK_MenuBar: Note("BeginMenuBar() without EndMenuBar() - closed automatically."); PopLayout(); break;
        default: Note("A Begin...() call is missing its End...() - closed automatically."); PopLayout(); break;
        }
    }
    if (g->layDepth > 0) {
        LayoutState& L = Lay();
        if (L.rowN > 0) { Note("BeginRow() without EndRow() - closed automatically."); FinishLine(L); L.rowN = 0; }
        if (L.horiz > 0) { Note("BeginHorizontal() without EndHorizontal() - closed automatically."); L.horiz = 0; }
    }
    if (g->canvasClip > 0) {
        Note("BeginCanvas() without EndCanvas() - closed automatically.");
        while (g->canvasClip-- > 0) AddClipPop();
        g->canvasClip = 0;
    }
    if (!g->idStack.empty()) Note("PushID() without PopID().");
    if (!g->fontStack.empty()) Note("PushFont() without PopFont().");
    if (!g->disabledStack.empty()) Note("BeginDisabled() without EndDisabled().");
    if (g->treeDepth > 0) Note("TreeNode() returned true but TreePop() was not called.");
    g->idStack.clear();
    g->disabled = 0;
    g->alphaMul = 1;
}
} // namespace detail

// =================================================================================
// Widgets: text
// =================================================================================

namespace detail {

static void TextImpl(Str s, Color c, FontId font, bool wrap) {
    LayoutState& L = Lay();
    float lh = LineH(font);
    if (s.n == 0) { PlaceItem(0, lh); return; }
    if (wrap) {
        float avail = Mx(AvailW(), 20.0f);
        TextEntry* e = GetText(s, font, TF_Wrap, avail);
        Rect r = PlaceItem(Mn(e->w, avail), e->h, true);
        AddTextEntry(e, Vec2(r.x, r.y), c);
        CloseFix();
        return;
    }
    if (!ItemVisible(NextItemY(L), lh) && !memchr(s.s, '\n', s.n) && L.rowN == 0 && !L.sameLine && !L.horiz) {
        PlaceItem(0, lh);   // off-screen: skip text layout work entirely
        return;
    }
    TextEntry* e = GetText(s, font);
    float w = e->w;
    if (L.kind == LK_Cell) w = Mn(w, Mx(AvailW(), 1.0f));   // table cells: ellipsis instead of clipping
    Rect r = PlaceItem(w, e->h, true);
    if (r.w + 0.5f < e->w) {   // squeezed (row/table cell): ellipsis
        DrawTextIn(s, Rect(r.x, r.y, r.w, r.h), font, c);
    } else {
        AddTextEntry(e, Vec2(r.x, r.y), c);
    }
    CloseFix();
}

static Color ThumbColor() { return ThemeIsDark() ? g->style.frameActive.Lighter(0.06f) : g->style.panel; }

} // namespace detail

void Text(Str text) { SUI_GUARD(); detail::TextImpl(text, detail::g->style.text, detail::g->font, false); }
void TextDim(Str text) { SUI_GUARD(); detail::TextImpl(text, detail::g->style.textDim, detail::g->font, false); }
void TextColored(Color color, Str text) { SUI_GUARD(); detail::TextImpl(text, color, detail::g->font, false); }
void TextWrapped(Str text) { SUI_GUARD(); detail::TextImpl(text, detail::g->style.text, detail::g->font, true); }
void Heading(Str text) { SUI_GUARD(); detail::TextImpl(text, detail::g->style.text, FontHeading, false); }
void Title(Str text) { SUI_GUARD(); detail::TextImpl(text, detail::g->style.text, FontTitle, false); }

void LabelText(Str label, Str value) {
    SUI_GUARD();
    using namespace detail;
    float h = LineH(FontBody) + 6;
    Labeled it = PlaceLabeled(label, TextW(value, FontBody), true, h);
    DrawTextIn(it.text, it.label, FontBody, g->style.textDim);
    DrawTextIn(value, it.ctrl, FontBody, g->style.text);
}

void BulletText(Str text) {
    SUI_GUARD();
    using namespace detail;
    TextEntry* e = GetText(text, g->font);
    Rect r = PlaceItem(e->w + 16, e->h, true);
    AddCircle(Vec2(r.x + 5, r.y + e->h * 0.5f), 2.5f, g->style.textDim);
    AddTextEntry(e, Vec2(r.x + 16, r.y), g->style.text);
    CloseFix();
}

bool Link(Str text, Str url) {
    SUI_GUARD(false);
    using namespace detail;
    ID id = Combine(MakeID(text), Hash32(url.s, url.n, 0x11u));
    Str t = DisplayText(text);
    TextEntry* e = GetText(t, g->font);
    Rect r = PlaceItem(e->w, e->h, true);
    bool hov, held;
    bool pressed = ButtonBehavior(r, id, hov, held);
    if (hov) g->cursor = kCurHand;
    Color c = hov ? g->style.accentHover : g->style.accent;
    AddTextEntry(e, r.Pos(), c);
    if (hov) AddRect(Rect(r.x, Snap(r.Bottom() - 2), r.w, Px()), c);
    FocusRing(id, r.Expand(1), 3);
    CloseFix();
    if (pressed && url.n) OpenURL(url);
    return pressed;
}

void Badge(Str text, Color color) {
    SUI_GUARD();
    using namespace detail;
    TextEntry* e = GetText(text, FontSmall);
    float h = Snap(e->h + 6);
    Rect r = PlaceItem(e->w + 16, h, true);
    AddRect(r, color.WithAlpha(color.a * 0.16f), h * 0.5f);
    AddRectStroke(r, color.WithAlpha(color.a * 0.35f), h * 0.5f);
    AddTextEntry(e, Vec2(r.x + 8, Snap(r.y + (h - e->h) * 0.5f)), color);
    CloseFix();
}
void Badge(Str text, Level level) { SUI_GUARD(); Badge(text, detail::LevelColor(level)); }

void Separator() {
    SUI_GUARD();
    using namespace detail;
    LayoutState& L = Lay();
    FinishLine(L);
    float w = Mx(0.0f, L.right - (L.left + L.indent));
    Rect r = PlaceItem(w, 1);
    AddRect(Rect(r.x, r.y, r.w, Px()), g->style.separator);
}
void SeparatorText(Str text) {
    SUI_GUARD();
    using namespace detail;
    LayoutState& L = Lay();
    FinishLine(L);
    float w = Mx(0.0f, L.right - (L.left + L.indent));
    TextEntry* e = GetText(text, FontBold);
    Rect r = PlaceItem(w, e->h + 2);
    AddTextEntry(e, Vec2(r.x, r.y + 1), g->style.textDim);
    float lx = r.x + e->w + 10;
    if (lx < r.Right()) AddRect(Rect(lx, Snap(r.y + r.h * 0.5f), r.Right() - lx, Px()), g->style.separator);
}

// =================================================================================
// Widgets: buttons and toggles
// =================================================================================

namespace detail {
enum ButtonKind { BK_Normal, BK_Primary, BK_Danger };

static bool ButtonImpl(Str label, Icon icon, int kind) {
    Style& s = g->style;
    ID id = MakeID(label);
    Str t = DisplayText(label);
    float tw = t.n ? TextW(t, FontBody) : 0;
    float iw = icon != Icon::None ? 16.0f : 0.0f;
    float gap = (iw > 0 && tw > 0) ? 7.0f : 0.0f;
    float content = iw + gap + tw;
    float natural = Mx(g->frameH, content + s.framePadding * 2 + 6);
    float w = ResolveWidth(AvailW(), natural, false);
    Rect r = PlaceItem(w, g->frameH);
    bool hov, held;
    bool pressed = ButtonBehavior(r, id, hov, held);
    WState& st = WS(id);
    Anim(st.a, hov ? 1.0f : 0.0f, 18);
    Anim(st.b, held ? 1.0f : 0.0f, 26);
    Color bg, fg;
    if (kind == BK_Primary) {
        bg = LerpC(LerpC(s.accent, s.accentHover, st.a), s.accentActive, st.b);
        fg = s.accentText;
    } else if (kind == BK_Danger) {
        bg = LerpC(LerpC(s.danger, s.danger.Lighter(0.1f), st.a), s.danger.Darker(0.15f), st.b);
        fg = Color(1, 1, 1, 1);
    } else {
        bg = LerpC(LerpC(s.frame, s.frameHover, st.a), s.frameActive, st.b);
        fg = s.text;
    }
    AddRect(r, bg, s.rounding);
    if (kind == BK_Normal) AddRectStroke(r, s.border, s.rounding);
    float x0 = r.w >= content + s.framePadding * 2 ? r.x + (r.w - content) * 0.5f : r.x + s.framePadding;
    if (iw > 0) AddIcon(icon, Rect(Snap(x0), Snap(r.y + (r.h - 16) * 0.5f), 16, 16), fg, 0, 2.0f);
    if (tw > 0) {
        float tx = x0 + iw + gap;
        DrawTextIn(t, Rect(tx, r.y, Mx(0.0f, r.Right() - tx - (r.w >= content + s.framePadding * 2 ? 0 : s.framePadding)), r.h), FontBody, fg);
    }
    FocusRing(id, r, s.rounding);
    return pressed;
}
} // namespace detail

bool Button(Str label) { SUI_GUARD(false); return detail::ButtonImpl(label, Icon::None, detail::BK_Normal); }
bool Button(Str label, Icon icon) { SUI_GUARD(false); return detail::ButtonImpl(label, icon, detail::BK_Normal); }
bool PrimaryButton(Str label) { SUI_GUARD(false); return detail::ButtonImpl(label, Icon::None, detail::BK_Primary); }
bool DangerButton(Str label) { SUI_GUARD(false); return detail::ButtonImpl(label, Icon::None, detail::BK_Danger); }

bool IconButton(Icon icon, Str tooltip) {
    SUI_GUARD(false);
    using namespace detail;
    Style& s = g->style;
    ID id = Combine(MakeID(tooltip), 0x1C0u + (uint32_t)icon);
    float sz = g->frameH;
    Rect r = PlaceItem(sz, sz);
    bool hov, held;
    bool pressed = ButtonBehavior(r, id, hov, held);
    WState& st = WS(id);
    Anim(st.a, hov ? 1.0f : 0.0f, 18);
    Anim(st.b, held ? 1.0f : 0.0f, 26);
    if (st.a > 0) AddRect(r, MulA(LerpC(s.frameHover, s.frameActive, st.b), st.a), s.rounding);
    AddIcon(icon, Rect(Snap(r.x + (r.w - 18) * 0.5f), Snap(r.y + (r.h - 18) * 0.5f), 18, 18), LerpC(s.textDim, s.text, st.a), 0, 2.0f);
    FocusRing(id, r, s.rounding);
    if (tooltip.n) Tooltip(tooltip);
    return pressed;
}

bool Checkbox(Str label, bool& value) {
    SUI_GUARD(false);
    using namespace detail;
    Style& s = g->style;
    ID id = MakeID(label);
    Str t = DisplayText(label);
    const float box = 18;
    float tw = t.n ? TextW(t, FontBody) : 0;
    float natural = box + (tw > 0 ? 8 + tw : 0);
    float w = ResolveWidth(AvailW(), natural, false);
    Rect r = PlaceItem(w, g->frameH);
    Rect hit(r.x, r.y, Mn(r.w, natural + 2), r.h);
    bool hov, held;
    bool pressed = ButtonBehavior(hit, id, hov, held);
    if (pressed) { value = !value; g->last.changed = true; }
    WState& st = WS(id);
    if (!st.init) { st.init = true; st.c = value ? 1.0f : 0.0f; }
    Anim(st.a, hov ? 1.0f : 0.0f, 18);
    Anim(st.c, value ? 1.0f : 0.0f, 22);
    Rect b(r.x, Snap(r.y + (r.h - box) * 0.5f), box, box);
    Color off = LerpC(s.field, s.frameHover, st.a);
    Color on = LerpC(s.accent, s.accentHover, st.a);
    AddRect(b, LerpC(off, on, st.c), 4);
    if (st.c < 0.99f) AddRectStroke(b, MulA(s.borderStrong, 1 - st.c), 4);
    if (st.c > 0.01f) {
        float k = EaseOut(st.c);
        AddIcon(Icon::Check, b.Shrink(2.0f + (1 - k) * 3.0f), MulA(s.accentText, k), 0, 3.0f);
    }
    if (tw > 0) DrawTextIn(t, Rect(b.Right() + 8, r.y, Mx(0.0f, r.Right() - b.Right() - 8), r.h), FontBody, s.text);
    FocusRing(id, b, 4);
    return pressed;
}

bool Toggle(Str label, bool& value) {
    SUI_GUARD(false);
    using namespace detail;
    Style& s = g->style;
    ID id = MakeID(label);
    Str t = DisplayText(label);
    const float sw = 40, sh = 22;
    float tw = t.n ? TextW(t, FontBody) : 0;
    LayoutState& L = Lay();
    bool inl = L.rowN > 0 || L.horiz > 0 || (L.sameLine && L.lineItems);
    float natural = tw + (tw > 0 ? 12 : 0) + sw;
    float w = ResolveWidth(AvailW(), natural, !inl);
    Rect r = PlaceItem(w, g->frameH);
    bool hov, held;
    bool pressed = ButtonBehavior(r, id, hov, held);
    if (pressed) { value = !value; g->last.changed = true; }
    WState& st = WS(id);
    if (!st.init) { st.init = true; st.c = value ? 1.0f : 0.0f; }
    Anim(st.a, hov ? 1.0f : 0.0f, 18);
    Anim(st.c, value ? 1.0f : 0.0f, 18);
    Rect tr(r.Right() - sw, Snap(r.y + (r.h - sh) * 0.5f), sw, sh);
    Color off = LerpC(s.field, s.frameHover, st.a);
    Color on = LerpC(s.accent, s.accentHover, st.a);
    AddRect(tr, LerpC(off, on, st.c), sh * 0.5f);
    if (st.c < 0.99f) AddRectStroke(tr, MulA(s.borderStrong, 1 - st.c), sh * 0.5f);
    float k = EaseOut(st.c);
    float kr = Lerpf(5.5f, 7.0f, k) + (held ? 1.0f : 0.0f);
    float kx = Lerpf(tr.x + sh * 0.5f, tr.Right() - sh * 0.5f, k);
    AddCircle(Vec2(kx, tr.y + sh * 0.5f), kr, LerpC(s.textDim, s.accentText, k));
    if (tw > 0) DrawTextIn(t, Rect(r.x, r.y, Mx(0.0f, tr.x - r.x - 12), r.h), FontBody, s.text);
    FocusRing(id, tr, sh * 0.5f);
    return pressed;
}

bool RadioButton(Str label, int& current, int value) {
    SUI_GUARD(false);
    using namespace detail;
    Style& s = g->style;
    ID id = Combine(MakeID(label), (uint32_t)value);
    Str t = DisplayText(label);
    const float box = 18;
    float tw = t.n ? TextW(t, FontBody) : 0;
    float natural = box + (tw > 0 ? 8 + tw : 0);
    float w = ResolveWidth(AvailW(), natural, false);
    Rect r = PlaceItem(w, g->frameH);
    bool hov, held;
    bool pressed = ButtonBehavior(Rect(r.x, r.y, Mn(r.w, natural + 2), r.h), id, hov, held);
    bool changed = false;
    if (pressed && current != value) { current = value; changed = true; g->last.changed = true; }
    bool sel = current == value;
    WState& st = WS(id);
    if (!st.init) { st.init = true; st.c = sel ? 1.0f : 0.0f; }
    Anim(st.a, hov ? 1.0f : 0.0f, 18);
    Anim(st.c, sel ? 1.0f : 0.0f, 22);
    Vec2 c(r.x + box * 0.5f, Snap(r.y + r.h * 0.5f));
    Color off = LerpC(s.field, s.frameHover, st.a);
    AddCircle(c, box * 0.5f, LerpC(off, LerpC(s.accent, s.accentHover, st.a), st.c));
    if (st.c < 0.99f) AddCircleStroke(c, box * 0.5f - 0.5f, MulA(s.borderStrong, 1 - st.c), 1.0f);
    if (st.c > 0.01f) AddCircle(c, 4.0f * EaseOut(st.c) + (hov ? 0.5f : 0.0f), s.accentText);
    if (tw > 0) DrawTextIn(t, Rect(r.x + box + 8, r.y, Mx(0.0f, r.w - box - 8), r.h), FontBody, s.text);
    FocusRing(id, Rect(r.x, c.y - box * 0.5f, box, box), box * 0.5f);
    return changed;
}

bool Selectable(Str label, bool selected) {
    SUI_GUARD(false);
    using namespace detail;
    Style& s = g->style;
    ID id = MakeID(label);
    Str t = DisplayText(label);
    float tw = TextW(t, FontBody);
    float h = s.rowHeight;
    float w = ResolveWidth(AvailW(), tw + 24, true);
    ReportNatural(tw + 28);
    Rect r = PlaceItem(w, h, false, true);
    bool hov, held;
    bool pressed = ButtonBehavior(r, id, hov, held);
    WState& st = WS(id);
    Anim(st.a, hov ? 1.0f : 0.0f, 22);
    float rr = Mx(0.0f, s.rounding - 1);
    if (selected) AddRect(r, s.accent.WithAlpha(0.20f + 0.08f * st.a), rr);
    else if (st.a > 0) AddRect(r, MulA(held ? s.frameActive : s.frameHover, st.a), rr);
    if (selected) AddRect(Rect(r.x + 1, Snap(r.y + h * 0.28f), 3, Snap(h * 0.44f)), s.accent, 1.5f);
    DrawTextIn(t, Rect(r.x + 10, r.y, Mx(0.0f, r.w - 14), h), FontBody, s.text);
    FocusRing(id, r, rr);
    return pressed;
}

// =================================================================================
// Widgets: numbers (slider, drag)
// =================================================================================

namespace detail {

// Formats one number with a printf-style format such as "%.2f", "%d%%" or "%.0f dB".
// Only one number conversion is used; anything unsafe (%s, %n, '*', a second
// conversion, ...) is printed literally or replaced by the default format, so a
// wrong format string can never crash. Int formats work for floats and vice versa.
static void FormatNumber(char* buf, size_t n, const char* fmt, double v, bool isInt) {
    if (!buf || n == 0) return;
    if (!fmt) fmt = isInt ? "%d" : "%.3f";
    char safe[128];
    size_t o = 0;
    char conv = 0;
    for (const char* p = fmt; *p && o + 16 < sizeof(safe); p++) {
        if (*p != '%') { safe[o++] = *p; continue; }
        if (p[1] == '%' || conv) {                       // "%%" or a second conversion: literal percent
            safe[o++] = '%'; safe[o++] = '%';
            if (p[1] == '%') p++;
            continue;
        }
        const char* q = p + 1;
        size_t start = o;
        safe[o++] = '%';
        for (int k = 0; *q && strchr("-+ #0", *q) && k < 4; k++) safe[o++] = *q++;
        for (int k = 0; *q >= '0' && *q <= '9'; q++) if (k++ < 2) safe[o++] = *q;
        if (*q == '.') { safe[o++] = *q++; for (int k = 0; *q >= '0' && *q <= '9'; q++) if (k++ < 2) safe[o++] = *q; }
        while (*q && strchr("hlLqjzt", *q)) q++;         // length modifiers are not needed
        if (*q && strchr("fFeEgGaA", *q)) conv = 'f';
        else if (*q && strchr("diuoxX", *q)) conv = 'd';
        else { o = start; break; }                       // %s, %n, %p, '*', ...: give up on this format
        safe[o++] = *q;
        p = q;
    }
    safe[o] = 0;
    if (!conv) {                                         // no usable conversion: default format
        if (isInt) snprintf(buf, n, "%d", (int)ClampD(std::isfinite(v) ? v : 0.0, -2147483648.0, 2147483647.0));
        else snprintf(buf, n, "%.3f", v);
        return;
    }
    if (conv == 'f') snprintf(buf, n, safe, isInt ? floor(v + 0.5) : v);
    else snprintf(buf, n, safe, (int)ClampD(std::isfinite(v) ? floor(v + 0.5) : 0.0, -2147483648.0, 2147483647.0));
}
static bool ParseNumber(const std::string& s, double& out) {
    const char* p = s.c_str();
    while (*p == ' ') p++;
    char* end = nullptr;
    double v = strtod(p, &end);
    if (end == p) return false;
    out = v;
    return std::isfinite(v);
}

static bool TempInputCore(ID id, Rect frame, std::string& text, Str hint, int flags, bool multiline, bool& submitted);

// Shared "double-click to type a value" editor for sliders and drags.
// Returns 1 = committed, -1 = still editing, 0 = not in temp-input mode.
static int TempInputNumber(ID id, Rect r, double& v, bool isInt) {
    if (g->tempInputId != id) return 0;
    ID tid = Combine(id, 0x7E3Fu);
    if (g->focusId != tid && g->te.id != tid) { g->focusId = tid; g->focusVisible = false; }
    bool submitted = false;
    TempInputCore(tid, r, g->tempInputText, Str(), InputNumbers, false, submitted);
    bool cancelled = g->kPressedFirst[VK_ESCAPE];
    bool editing = g->focusId == tid;
    if (submitted || !editing || cancelled) {
        g->tempInputId = 0;
        if (g->focusId == tid) g->focusId = id;
        double nv;
        if (!cancelled && ParseNumber(g->tempInputText, nv)) { v = isInt ? floor(nv + 0.5) : nv; return 1; }
        return -1;
    }
    return -1;
}

static bool SliderCore(Str label, double& value, double vmin, double vmax, const char* fmt, bool isInt) {
    Style& s = g->style;
    ID id = MakeID(label);
    char bmin[64], bmax[64], bcur[64];
    FormatNumber(bmin, sizeof(bmin), fmt, vmin, isInt);
    FormatNumber(bmax, sizeof(bmax), fmt, vmax, isInt);
    float valW = Mx(Mx(TextW(Str(bmin), FontBody), TextW(Str(bmax), FontBody)) + 4, 28.0f);
    Labeled it = PlaceLabeled(label, 220, true, g->frameH);
    DrawLabel(it);
    Rect c = it.ctrl;
    bool changed = false;
    int tmp = TempInputNumber(id, c, value, isInt);
    if (tmp != 0) {
        if (tmp == 1) { value = Clampf((float)value, (float)Mn(vmin, vmax), (float)Mx(vmin, vmax)); changed = true; }
        g->last.id = id;
        g->last.changed = changed;
        return changed;
    }
    bool showValue = c.w > valW + 70;
    const float kr = 9;
    Rect track(c.x + kr, c.y, Mx(1.0f, c.w - kr * 2 - (showValue ? valW + 10 : 0)), c.h);
    Rect hit(c.x, c.y, track.Right() + kr - c.x, c.h);
    bool hov, held;
    ButtonBehavior(hit, id, hov, held, BF_PressOnClick);
    if (hov && g->mDouble[0]) {
        g->tempInputId = id;
        FormatNumber(bcur, sizeof(bcur), isInt ? "%d" : "%g", value, isInt);
        g->tempInputText = bcur;
        g->activeId = 0;
        WantFrames(2);
    } else if (held && vmax != vmin) {
        float t = Clampf((g->mouse.x - track.x) / track.w, 0, 1);
        double nv = vmin + (vmax - vmin) * t;
        if (isInt) nv = floor(nv + 0.5);
        if (nv != value) { value = nv; changed = true; }
    }
    if (g->focusId == id && g->disabled == 0) {
        double step = isInt ? 1.0 : (vmax - vmin) / 100.0;
        double before = value;
        if (g->kPressed[VK_LEFT] || g->kPressed[VK_DOWN]) value -= step;
        if (g->kPressed[VK_RIGHT] || g->kPressed[VK_UP]) value += step;
        if (g->kPressed[VK_PRIOR]) value += step * 10;
        if (g->kPressed[VK_NEXT]) value -= step * 10;
        if (g->kPressed[VK_HOME]) value = vmin;
        if (g->kPressed[VK_END]) value = vmax;
        double lo = Mn(vmin, vmax), hi = Mx(vmin, vmax);
        value = value < lo ? lo : value > hi ? hi : value;
        if (value != before) changed = true;
    }
    float t = vmax != vmin ? (float)((value - vmin) / (vmax - vmin)) : 0.0f;
    t = Clampf(t, 0, 1);
    WState& st = WS(id);
    Anim(st.a, (hov || held) ? 1.0f : 0.0f, 18);
    Anim(st.b, held ? 1.0f : 0.0f, 24);
    float ty = Snap(c.y + c.h * 0.5f);
    AddRect(Rect(track.x, ty - 2, track.w, 4), s.frameActive, 2);
    AddRect(Rect(track.x, ty - 2, track.w * t, 4), s.accent, 2);
    float kx = track.x + track.w * t;
    AddCircle(Vec2(kx, ty), kr, ThumbColor());
    AddCircleStroke(Vec2(kx, ty), kr - 0.5f, s.border, 1.0f);
    AddCircle(Vec2(kx, ty), Lerpf(5.0f, 6.5f, st.a) - 1.5f * st.b, s.accent);
    if (showValue) {
        FormatNumber(bcur, sizeof(bcur), fmt, value, isInt);
        DrawTextIn(Str(bcur), Rect(c.Right() - valW, c.y, valW, c.h), FontBody, s.textDim, Align::Right);
    }
    FocusRing(id, Rect(track.x - kr, c.y + 3, track.w + kr * 2, c.h - 6), 6);
    g->last.changed = changed;
    return changed;
}

static bool DragCore(Str label, double& value, double speed, double vmin, double vmax, const char* fmt, bool isInt) {
    Style& s = g->style;
    ID id = MakeID(label);
    Labeled it = PlaceLabeled(label, 120, true, g->frameH);
    DrawLabel(it);
    Rect c = it.ctrl;
    bool changed = false;
    int tmp = TempInputNumber(id, c, value, isInt);
    if (tmp != 0) {
        if (tmp == 1) { value = value < vmin ? vmin : value > vmax ? vmax : value; changed = true; }
        g->last.id = id;
        g->last.changed = changed;
        return changed;
    }
    bool wasActive = g->activeId == id;
    bool hov, held;
    ButtonBehavior(c, id, hov, held, BF_PressOnClick);
    if (hov || held) g->cursor = kCurSizeWE;
    WState& st = WS(id);
    char buf[64];
    if (hov && g->mDouble[0]) {
        g->tempInputId = id;
        FormatNumber(buf, sizeof(buf), isInt ? "%d" : "%g", value, isInt);
        g->tempInputText = buf;
        g->activeId = 0;
        WantFrames(2);
    } else if (held) {
        if (!wasActive) { st.c = (float)value; st.d = g->mouse.x; }
        float dx = g->mouse.x - st.d;
        st.d = g->mouse.x;
        double mul = g->kDown[VK_SHIFT] ? 0.1 : g->kDown[VK_CONTROL] ? 10.0 : 1.0;
        st.c += (float)(dx * speed * mul);
        double nv = st.c;
        if (isInt) nv = floor(nv + 0.5);
        nv = nv < vmin ? vmin : nv > vmax ? vmax : nv;
        if (nv != value) { value = nv; changed = true; }
    }
    if (g->focusId == id && g->disabled == 0) {
        double step = isInt ? 1.0 : speed * 10;
        double before = value;
        if (g->kPressed[VK_LEFT] || g->kPressed[VK_DOWN]) value -= step;
        if (g->kPressed[VK_RIGHT] || g->kPressed[VK_UP]) value += step;
        value = value < vmin ? vmin : value > vmax ? vmax : value;
        if (value != before) changed = true;
    }
    Anim(st.a, (hov || held) ? 1.0f : 0.0f, 18);
    AddRect(c, LerpC(s.field, s.frameHover, st.a * 0.7f), s.rounding);
    AddRectStroke(c, held ? s.accent : s.border, s.rounding);
    AddIcon(Icon::ChevronLeft, Rect(c.x + 4, c.y + (c.h - 14) * 0.5f, 14, 14), MulA(s.textDim, st.a), 0, 2.0f);
    AddIcon(Icon::ChevronRight, Rect(c.Right() - 18, c.y + (c.h - 14) * 0.5f, 14, 14), MulA(s.textDim, st.a), 0, 2.0f);
    FormatNumber(buf, sizeof(buf), fmt, value, isInt);
    DrawTextIn(Str(buf), c.Shrink(4), FontBody, s.text, Align::Center);
    FocusRing(id, c, s.rounding);
    g->last.changed = changed;
    return changed;
}

} // namespace detail

bool Slider(Str label, float& value, float min, float max, const char* format) {
    SUI_GUARD(false);
    double v = value;
    bool c = detail::SliderCore(label, v, min, max, format, false);
    if (c) value = (float)v;
    return c;
}
bool Slider(Str label, int& value, int min, int max, const char* format) {
    SUI_GUARD(false);
    double v = value;
    bool c = detail::SliderCore(label, v, min, max, format, true);
    if (c) value = (int)v;
    return c;
}
bool Drag(Str label, float& value, float speed, float min, float max, const char* format) {
    SUI_GUARD(false);
    double v = value;
    bool c = detail::DragCore(label, v, speed, min, max, format, false);
    if (c) value = (float)v;
    return c;
}
bool Drag(Str label, int& value, float speed, int min, int max) {
    SUI_GUARD(false);
    double v = value;
    bool c = detail::DragCore(label, v, speed, min, max, "%d", true);
    if (c) value = (int)v;
    return c;
}

// =================================================================================
// Widgets: progress, spinner, images, canvas, drop zone
// =================================================================================

void ProgressBar(float fraction, Str overlay) {
    SUI_GUARD();
    using namespace detail;
    Style& s = g->style;
    char buf[32];
    Str text = overlay;
    if (overlay.n == 0 && fraction >= 0) {
        snprintf(buf, sizeof(buf), "%d%%", (int)(Clampf(fraction, 0, 1) * 100.0f + 0.5f));
        text = Str(buf);
    }
    float reserve = text.n ? Mx(TextW(text, FontBody), TextW("100%", FontBody)) + 12 : 0;
    float w = ResolveWidth(AvailW(), 200, true);
    Rect r = PlaceItem(w, LineH(FontBody) + 8);
    Rect bar(r.x, Snap(r.y + r.h * 0.5f - 3), Mx(10.0f, r.w - reserve), 6);
    AddRect(bar, s.frameActive, 3);
    if (fraction >= 0) {
        float f = Clampf(fraction, 0, 1);
        if (f > 0) AddRect(Rect(bar.x, bar.y, Mx(6.0f, bar.w * f), bar.h), s.accent, 3);
    } else {
        float t = (float)fmod(g->time, 1.5) / 1.5f;
        float e = t < 0.5f ? 2 * t * t : 1 - 2 * (1 - t) * (1 - t);
        float seg = bar.w * 0.32f;
        float x = bar.x - seg + (bar.w + seg) * e;
        Rect sr = Intersect(Rect(x, bar.y, seg, bar.h), bar);
        if (sr.w > 0) AddRect(sr, s.accent, 3);
        WantFrames(1);
    }
    if (text.n) DrawTextIn(text, Rect(bar.Right() + 10, r.y, reserve - 10, r.h), FontBody, s.textDim, Align::Right);
}

void Spinner(float size) {
    SUI_GUARD();
    using namespace detail;
    if (size <= 0) size = Snap(LineH(FontBody) * 1.05f);
    Rect r = PlaceItem(size, size, true);
    Vec2 c = r.Center();
    float rad = size * 0.5f - 2;
    float a = (float)fmod(g->time * 5.5, 2 * kPi);
    AddCircleStroke(c, rad, g->style.accent.WithAlpha(0.18f), 2.5f);
    AddArc(c, rad, a, 2.2f + 1.2f * sinf((float)g->time * 2.6f), g->style.accent, 2.5f);
    CloseFix();
    WantFrames(1);
}

void Image(const Texture& texture, Vec2 size) {
    SUI_GUARD();
    using namespace detail;
    if (!texture) { PlaceItem(size.x > 0 ? size.x : 1, size.y > 0 ? size.y : 1); return; }
    float w = size.x, h = size.y;
    float avail = AvailW();
    if (w <= 0 && h <= 0) { w = Mn((float)texture.width, avail); h = w * texture.height / Mx(1, texture.width); }
    else if (w <= 0) w = h * texture.width / Mx(1, texture.height);
    else if (h <= 0) h = w * texture.height / Mx(1, texture.width);
    Rect r = PlaceItem(w, h, true);
    AddImage(texture.id, r, 1.0f);
    CloseFix();
}

namespace detail {
static Rect g_canvasRect;
static bool g_canvasBorder = false;
}

Canvas BeginCanvas(float height, bool border) {
    Canvas c;
    SUI_GUARD(c);
    using namespace detail;
    Style& s = g->style;
    LayoutState& L = Lay();
    float avail = AvailW();
    float w = ResolveWidth(avail, avail, true);
    if (height <= 0) {
        float bottom = (L.kind == LK_Root || L.kind == LK_Popup) ? L.rect.Bottom() - L.pad : L.clip.Bottom() - L.pad;
        height = Mx(60.0f, bottom - NextItemY(L) + height);
    }
    ID id = Combine(L.id, 0xCA7A5u + (uint32_t)L.counter++);
    Rect r = PlaceItem(w, height);
    bool wasActive = g->activeId == id;
    bool hov, held;
    ButtonBehavior(r, id, hov, held, BF_PressOnClick | BF_NoFocus);
    c.rect = r;
    c.size = Vec2(r.w, r.h);
    c.mouse = Vec2(g->mouse.x - r.x, g->mouse.y - r.y);
    c.hovered = hov;
    c.clicked = hov && g->mClicked[0];
    c.down = held;
    c.released = wasActive && g->activeId != id;
    c.rightClicked = Hoverable(r, 0) && g->mClicked[1];
    c.wheel = hov ? g->wheel : 0.0f;
    if (border) AddRect(r, s.field, s.rounding);
    AddClipPush(border ? r.Shrink(1) : r);
    g->canvasClip++;
    g_canvasRect = r;
    g_canvasBorder = border;
    return c;
}
void EndCanvas() {
    SUI_GUARD();
    using namespace detail;
    if (g->canvasClip <= 0) { Note("EndCanvas() without a matching BeginCanvas() - ignored."); return; }
    AddClipPop();
    g->canvasClip--;
    if (g_canvasBorder) AddRectStroke(g_canvasRect, g->style.border, g->style.rounding);
}

void Canvas::Line(Vec2 a, Vec2 b, Color c, float thickness) const {
    if (!detail::g) return;
    detail::AddLine(a + rect.Pos(), b + rect.Pos(), c, thickness);
}
void Canvas::FillRect(sui::Rect r, Color c, float rounding) const {
    if (!detail::g) return;
    detail::AddRect(sui::Rect(r.x + rect.x, r.y + rect.y, r.w, r.h), c, rounding);
}
void Canvas::StrokeRect(sui::Rect r, Color c, float rounding, float thickness) const {
    if (!detail::g) return;
    detail::AddRectStroke(sui::Rect(r.x + rect.x, r.y + rect.y, r.w, r.h), c, rounding, thickness);
}
void Canvas::FillCircle(Vec2 center, float radius, Color c) const {
    if (!detail::g) return;
    detail::AddCircle(center + rect.Pos(), radius, c);
}
void Canvas::StrokeCircle(Vec2 center, float radius, Color c, float thickness) const {
    if (!detail::g) return;
    detail::AddCircleStroke(center + rect.Pos(), radius, c, thickness);
}
void Canvas::Polyline(const std::vector<Vec2>& points, Color c, float thickness, bool closed) const {
    if (!detail::g || points.size() < 2) return;
    std::vector<Vec2> p(points);
    for (Vec2& v : p) v = v + rect.Pos();
    detail::AddPoly(p.data(), (int)p.size(), c, false, closed, thickness);
}
void Canvas::FillPolygon(const std::vector<Vec2>& points, Color c) const {
    if (!detail::g || points.size() < 3) return;
    std::vector<Vec2> p(points);
    for (Vec2& v : p) v = v + rect.Pos();
    detail::AddPoly(p.data(), (int)p.size(), c, true, true);
}
void Canvas::Bezier(Vec2 p0, Vec2 p1, Vec2 p2, Vec2 p3, Color c, float thickness) const {
    if (!detail::g) return;
    Vec2 o = rect.Pos();
    detail::AddBezier(p0 + o, p1 + o, p2 + o, p3 + o, c, thickness);
}
void Canvas::Text(Vec2 pos, Str text, Color c, FontId font) const {
    if (!detail::g || text.n == 0) return;
    detail::AddTextEntry(detail::GetText(text, font), pos + rect.Pos(), c);
}
void Canvas::Image(const Texture& texture, sui::Rect dest, float opacity) const {
    if (!detail::g) return;
    detail::AddImage(texture.id, sui::Rect(dest.x + rect.x, dest.y + rect.y, dest.w, dest.h), opacity);
}
void Canvas::IconAt(Icon icon, sui::Rect r, Color c) const {
    if (!detail::g) return;
    float sz = detail::Mn(r.w, r.h);
    detail::AddIcon(icon, sui::Rect(rect.x + r.x + (r.w - sz) * 0.5f, rect.y + r.y + (r.h - sz) * 0.5f, sz, sz), c, 0, 2.0f);
}

std::vector<std::string> DropZone(Str label, float height) {
    std::vector<std::string> out;
    SUI_GUARD(out);
    using namespace detail;
    Style& s = g->style;
    ID id = Combine(MakeID(label), 0xD20u);
    float w = ResolveWidth(AvailW(), AvailW(), true);
    Rect r = PlaceItem(w, Mx(height, 48.0f));
    bool hov, held;
    bool pressed = ButtonBehavior(r, id, hov, held);
    WState& st = WS(id);
    if (!g->droppedFrame.empty() && r.Contains(g->dropPos) && g->disabled == 0) {
        out = g->droppedFrame;
        st.b = 1.0f;
        g->last.changed = true;
    }
    if (pressed) {
        out = OpenFilesDialog(Str(), Str());
        if (!out.empty()) { st.b = 1.0f; g->last.changed = true; }
    }
    Anim(st.a, hov ? 1.0f : 0.0f, 18);
    Anim(st.b, 0.0f, 3.5f);
    float hl = Mx(st.a * 0.6f, st.b);
    AddRect(r, LerpC(s.field, s.accent.WithAlpha(0.14f), hl), s.rounding + 2);
    AddRectStroke(r.Shrink(1), LerpC(s.borderStrong, s.accent, hl), s.rounding + 2, 1.5f, true);
    Str t = DisplayText(label);
    float lh = LineH(FontBody), sh = LineH(FontSmall);
    float total = 22 + 6 + lh + sh;
    float y = Snap(r.y + (r.h - total) * 0.5f);
    bool compact = r.h < total + 8;
    if (compact) {
        AddIcon(Icon::Upload, Rect(r.x + 14, r.y + (r.h - 18) * 0.5f, 18, 18), LerpC(s.textDim, s.accent, hl), 0, 2.0f);
        DrawTextIn(t, Rect(r.x + 42, r.y, r.w - 52, r.h), FontBody, s.text);
    } else {
        AddIcon(Icon::Upload, Rect(r.Center().x - 11, y, 22, 22), LerpC(s.textDim, s.accent, hl), 0, 2.0f);
        DrawTextIn(t, Rect(r.x + 8, y + 28, r.w - 16, lh), FontBody, s.text, Align::Center);
        DrawTextIn(Str("or click to browse"), Rect(r.x + 8, y + 28 + lh, r.w - 16, sh), FontSmall, s.textDim, Align::Center);
    }
    FocusRing(id, r, s.rounding + 2);
    return out;
}

// =================================================================================
// Text editing
// =================================================================================

namespace detail {

static int CharClass(wchar_t c) {
    if (c == L' ' || c == L'\t' || c == L'\n' || c == L'\r') return 0;
    if (iswalnum(c) || c == L'_' || c > 127) return 1;
    return 2;
}
static inline bool IsHigh(wchar_t c) { return c >= 0xD800 && c <= 0xDBFF; }
static inline bool IsLow(wchar_t c) { return c >= 0xDC00 && c <= 0xDFFF; }

static int PrevPos(const std::wstring& t, int p) {
    if (p <= 0) return 0;
    p--;
    if (p > 0 && IsLow(t[p]) && IsHigh(t[p - 1])) p--;
    return p;
}
static int NextPos(const std::wstring& t, int p) {
    int n = (int)t.size();
    if (p >= n) return n;
    p++;
    if (p < n && IsLow(t[p]) && IsHigh(t[p - 1])) p++;
    return p;
}
static int WordLeft(const std::wstring& t, int p) {
    while (p > 0 && CharClass(t[p - 1]) == 0) p--;
    if (p > 0) {
        int c = CharClass(t[p - 1]);
        while (p > 0 && CharClass(t[p - 1]) == c) p--;
    }
    return p;
}
static int WordRight(const std::wstring& t, int p) {
    int n = (int)t.size();
    if (p < n) {
        int c = CharClass(t[p]);
        while (p < n && CharClass(t[p]) == c && c != 0) p++;
    }
    while (p < n && CharClass(t[p]) == 0 && t[p] != L'\n') p++;
    return p;
}

static void BeginTextEdit(ID id, const std::string& str, int flags, bool multiline, bool selectAll) {
    TextEdit& te = g->te;
    te.id = id;
    te.text = Widen(str.c_str(), str.size());
    te.text.erase(std::remove(te.text.begin(), te.text.end(), L'\r'), te.text.end());
    te.synced = str;
    te.flags = flags;
    te.multiline = multiline;
    te.cursor = (int)te.text.size();
    te.anchor = selectAll ? 0 : te.cursor;
    te.scrollX = 0;
    te.undo.clear();
    te.redo.clear();
    te.lastKind = 0;
    te.wantX = -1;
    te.dragging = false;
    g->caretReset = g->time;
}

static void EndTextEdit() {
    g->te.id = 0;
    g->te.undo.clear();
    g->te.redo.clear();
    g->te.dragging = false;
}

static void PushUndo(int kind) {
    TextEdit& te = g->te;
    if (kind != 1 || te.lastKind != 1 || g->time - te.lastEditTime > 1.0) {
        TextEdit::Snap s;
        s.text = te.text; s.cursor = te.cursor; s.anchor = te.anchor;
        te.undo.push_back(s);
        if (te.undo.size() > 200) te.undo.erase(te.undo.begin());
        te.redo.clear();
    }
    te.lastKind = kind;
    te.lastEditTime = g->time;
}

static bool HasSel() { return g->te.cursor != g->te.anchor; }
static int SelMin() { return Mn(g->te.cursor, g->te.anchor); }
static int SelMax() { return Mx(g->te.cursor, g->te.anchor); }

static void DeleteSel() {
    TextEdit& te = g->te;
    if (!HasSel()) return;
    int a = SelMin(), b = SelMax();
    te.text.erase((size_t)a, (size_t)(b - a));
    te.cursor = te.anchor = a;
}

static void InsertText(const std::wstring& s) {
    TextEdit& te = g->te;
    DeleteSel();
    te.text.insert((size_t)te.cursor, s);
    te.cursor += (int)s.size();
    te.anchor = te.cursor;
}

static std::wstring FilterInput(const std::wstring& in, int flags, bool multiline) {
    std::wstring out;
    out.reserve(in.size());
    for (wchar_t c : in) {
        if (c == L'\r') continue;
        if (c == L'\n' && !multiline) { out += L' '; continue; }
        if (c == L'\t' && !multiline) { out += L' '; continue; }
        if (flags & InputNumbers) {
            if (!((c >= L'0' && c <= L'9') || c == L'.' || c == L'-' || c == L'+' || c == L'e' || c == L'E')) continue;
        }
        out += c;
    }
    return out;
}

// ---- Hit testing ----------------------------------------------------------------
// Uses DirectWrite's HitTest* functions when available and falls back to cluster and
// line metrics otherwise (some DirectWrite implementations, e.g. Wine, lack HitTest*).

struct TextGeo {
    std::vector<float> x;              // caret x for every text position (len + 1)
    std::vector<int> line;             // line index for every text position
    std::vector<float> top, height, endX;
    std::vector<int> start, contentEnd;
    bool ok = false;
};
static bool g_nativeHit = true;
static TextGeo g_geo;
static IDWriteTextLayout* g_geoFor = nullptr;
static int g_geoLen = -1;
static uint64_t g_geoFrame = 0;

static const TextGeo& Geo(IDWriteTextLayout* tl, int len) {
    if (g_geoFor == tl && g_geoLen == len && g_geoFrame == g->frame) return g_geo;
    g_geoFor = tl; g_geoLen = len; g_geoFrame = g->frame;
    TextGeo& G = g_geo;
    G = TextGeo();
    UINT32 nc = 0, nl = 0;
    tl->GetClusterMetrics(nullptr, 0, &nc);
    std::vector<DWRITE_CLUSTER_METRICS> cl(nc);
    if (nc && FAILED(tl->GetClusterMetrics(cl.data(), nc, &nc))) return G;
    tl->GetLineMetrics(nullptr, 0, &nl);
    if (nl == 0) return G;
    std::vector<DWRITE_LINE_METRICS> lm(nl);
    if (FAILED(tl->GetLineMetrics(lm.data(), nl, &nl))) return G;
    G.x.assign((size_t)len + 1, 0.0f);
    G.line.assign((size_t)len + 1, (int)nl - 1);
    size_t ci = 0;
    int pos = 0;
    float y = 0;
    for (UINT32 li = 0; li < nl; li++) {
        int ls = pos, le = pos + (int)lm[li].length, ce = le - (int)lm[li].newlineLength;
        G.start.push_back(ls);
        G.contentEnd.push_back(ce);
        G.top.push_back(y);
        G.height.push_back(lm[li].height);
        float x = 0;
        int p = ls;
        while (p < le && ci < cl.size()) {
            int L = Mx(1, (int)cl[ci].length);
            for (int k = 0; k < L && p + k <= len; k++) { G.x[(size_t)(p + k)] = x; G.line[(size_t)(p + k)] = (int)li; }
            if (p < ce) x += cl[ci].width;
            p += L;
            ci++;
        }
        if (ce >= 0 && ce <= len && lm[li].newlineLength > 0) { G.x[(size_t)ce] = x; G.line[(size_t)ce] = (int)li; }
        G.endX.push_back(x);
        pos = le;
        y += lm[li].height;
    }
    int last = (int)nl - 1;
    if (G.start[(size_t)last] <= len) { G.x[(size_t)len] = G.endX[(size_t)last]; G.line[(size_t)len] = last; }
    G.ok = true;
    return G;
}

static Vec2 CaretPos(IDWriteTextLayout* tl, int pos, int len, float& h) {
    h = 0;
    if (!tl) return Vec2();
    pos = Mn(Mx(pos, 0), len);
    if (g_nativeHit) {
        FLOAT x = 0, y = 0;
        DWRITE_HIT_TEST_METRICS m;
        HRESULT hr = tl->HitTestTextPosition((UINT32)pos, FALSE, &x, &y, &m);
        if (SUCCEEDED(hr)) { h = m.height; return Vec2(x, y); }
        if (hr == E_NOTIMPL) g_nativeHit = false;
        else if (len > 0 && SUCCEEDED(tl->HitTestTextPosition((UINT32)(len - 1), TRUE, &x, &y, &m))) { h = m.height; return Vec2(x, y); }
    }
    const TextGeo& G = Geo(tl, len);
    if (!G.ok) return Vec2();
    int li = G.line[(size_t)pos];
    h = G.height[(size_t)li];
    return Vec2(G.x[(size_t)pos], G.top[(size_t)li]);
}

static int HitTestLayout(IDWriteTextLayout* tl, int len, float x, float y) {
    if (!tl) return 0;
    if (g_nativeHit) {
        BOOL trailing = FALSE, inside = FALSE;
        DWRITE_HIT_TEST_METRICS m;
        HRESULT hr = tl->HitTestPoint(x, y, &trailing, &inside, &m);
        if (SUCCEEDED(hr)) return Mn(len, (int)m.textPosition + (trailing ? (int)m.length : 0));
        if (hr == E_NOTIMPL) g_nativeHit = false;
    }
    const TextGeo& G = Geo(tl, len);
    if (!G.ok) return 0;
    size_t li = 0;
    while (li + 1 < G.top.size() && y >= G.top[li] + G.height[li]) li++;
    int ls = G.start[li], ce = Mn(G.contentEnd[li], len);
    int p = ls;
    while (p < ce) {
        int q = p + 1;
        while (q < ce && G.x[(size_t)q] == G.x[(size_t)p]) q++;      // skip to the next cluster
        float x0 = G.x[(size_t)p], x1 = q < ce ? G.x[(size_t)q] : G.endX[li];
        if (x < (x0 + x1) * 0.5f) return p;
        p = q;
    }
    return ce;
}

static float LineEndX(IDWriteTextLayout* tl, int len, int ls, int contentEnd) {
    if (contentEnd <= ls) return 0;
    if (g_nativeHit) {
        FLOAT x = 0, y = 0;
        DWRITE_HIT_TEST_METRICS m;
        if (SUCCEEDED(tl->HitTestTextPosition((UINT32)(contentEnd - 1), TRUE, &x, &y, &m))) return x;
    }
    const TextGeo& G = Geo(tl, len);
    return G.ok ? G.endX[(size_t)G.line[(size_t)(contentEnd - 1)]] : 0.0f;
}

static void SelectionRects(IDWriteTextLayout* tl, int len, int a, int b, Vec2 origin, Color c) {
    if (!tl || a >= b) return;
    UINT32 count = 0;
    tl->GetLineMetrics(nullptr, 0, &count);
    if (count == 0) return;
    std::vector<DWRITE_LINE_METRICS> lines(count);
    if (FAILED(tl->GetLineMetrics(lines.data(), count, &count))) return;
    int pos = 0;
    float y = 0;
    for (UINT32 i = 0; i < count; i++) {
        const DWRITE_LINE_METRICS& lm = lines[i];
        int ls = pos, le = pos + (int)lm.length, ce = le - (int)lm.newlineLength;
        int s0 = Mx(a, ls), s1 = Mn(b, le);
        if (s0 < s1) {
            float h;
            float x0 = s0 < ce ? CaretPos(tl, s0, len, h).x : LineEndX(tl, len, ls, ce);
            float x1 = s1 < ce ? CaretPos(tl, s1, len, h).x : LineEndX(tl, len, ls, ce) + (s1 > ce ? 6.0f : 0.0f);
            AddRect(Rect(Snap(origin.x + x0), Snap(origin.y + y), Snap(Mx(x1 - x0, 2.0f)), Snap(lm.height)), c);
        }
        pos = le;
        y += lm.height;
    }
}

// The editor used by InputText, InputMultiline, SearchBox, number inputs and the
// temporary editors of sliders/drags. `fr` is the full frame; insets reserve room
// for icons/buttons inside it. Returns true when the text changed.
static bool TextFieldCore(ID id, Rect fr, std::string& str, Str hint, int flags, bool multiline,
                          bool& submitted, float leftInset = 0, float rightInset = 0, bool drawFrame = true) {
    Style& s = g->style;
    TextEdit& te = g->te;
    submitted = false;
    bool readOnly = (flags & InputReadOnly) != 0;
    bool password = (flags & InputPassword) != 0;
    float padX = s.framePadding;
    Rect inner(fr.x + padX + leftInset, fr.y + (multiline ? 6.0f : 0.0f),
               Mx(1.0f, fr.w - padX * 2 - leftInset - rightInset), Mx(1.0f, fr.h - (multiline ? 12.0f : 0.0f)));
    ContState& cs = CS(id);
    if (multiline && cs.hadBar) inner.w = Mx(1.0f, inner.w - s.scrollbarSize);

    if (g->disabled > 0 && (te.id == id || g->focusId == id)) {   // disabled while focused: let go
        if (g->focusId == id) g->focusId = 0;
        if (te.id == id) EndTextEdit();
    }
    bool editing = te.id == id;
    if (editing && str != te.synced) {
        te.text = Widen(str.c_str(), str.size());
        te.text.erase(std::remove(te.text.begin(), te.text.end(), L'\r'), te.text.end());
        te.synced = str;
        te.cursor = Mn(te.cursor, (int)te.text.size());
        te.anchor = Mn(te.anchor, (int)te.text.size());
    }

    Rect hitR(inner.x - padX * 0.5f, fr.y, inner.w + padX, fr.h);
    bool hov = Hoverable(hitR, id);
    if (hov) { g->cursor = kCurIBeam; g->hotId = id; }
    Focusable(id);
    bool focused = g->focusId == id;
    if (focused && !editing && g->disabled == 0) {
        BeginTextEdit(id, str, flags, multiline, !multiline);
        editing = true;
    }

    // Build the display layout (needed for hit testing and drawing).
    std::wstring masked;
    std::wstring plainBuf;
    const std::wstring* disp;
    if (editing) disp = &te.text;
    else { plainBuf = Widen(str.c_str(), str.size()); plainBuf.erase(std::remove(plainBuf.begin(), plainBuf.end(), L'\r'), plainBuf.end()); disp = &plainBuf; }
    if (password) { masked.assign(disp->size(), (wchar_t)0x2022); disp = &masked; }
    FontId font = g->font;
    float lh = LineH(font);
    auto layoutFor = [&](const std::wstring& t) {
        return GetTextW(t.c_str(), t.size(), font, multiline ? TF_Wrap : 0, multiline ? inner.w : 0);
    };
    TextEntry* e = layoutFor(*disp);
    float scrollX = editing ? te.scrollX : 0.0f;
    float scrollY = multiline ? cs.scroll : 0.0f;
    Vec2 origin = multiline ? Vec2(inner.x, inner.y - scrollY) : Vec2(inner.x - scrollX, Snap(fr.y + (fr.h - lh) * 0.5f));

    // Mouse
    if (hov && g->mClicked[0] && !g->mouseConsumed && g->disabled == 0) {
        g->mouseConsumed = true;
        g->focusClaimed = true;
        if (!editing || !focused) {
            g->focusId = id;
            g->focusVisible = false;
            BeginTextEdit(id, str, flags, multiline, false);
            editing = true;
            focused = true;
            disp = password ? disp : &te.text;
            if (password) { masked.assign(te.text.size(), (wchar_t)0x2022); disp = &masked; }
            e = layoutFor(*disp);
        }
        g->activeId = id;
        te.dragging = true;
        int pos = HitTestLayout(e->layout, (int)disp->size(), g->mouse.x - origin.x, g->mouse.y - origin.y);
        if (g->mDouble[0] && !password) {
            int a = pos, b = pos, n = (int)te.text.size();
            int c = (a < n) ? CharClass(te.text[a]) : (a > 0 ? CharClass(te.text[a - 1]) : 0);
            while (a > 0 && CharClass(te.text[a - 1]) == c && te.text[a - 1] != L'\n') a--;
            while (b < n && CharClass(te.text[b]) == c && te.text[b] != L'\n') b++;
            te.anchor = a;
            te.cursor = b;
            te.dragging = false;
        } else if (g->kDown[VK_SHIFT]) {
            te.cursor = pos;
        } else {
            te.cursor = te.anchor = pos;
        }
        te.wantX = -1;
        g->caretReset = g->time;
    }
    if (g->activeId == id) {
        g->activeAlive = true;
        if (g->mDown[0] && te.dragging && editing) {
            te.cursor = HitTestLayout(e->layout, (int)te.text.size(), g->mouse.x - origin.x, g->mouse.y - origin.y);
            if (multiline && (g->mouse.y < inner.y || g->mouse.y > inner.Bottom())) WantFrames(1);
        }
        if (!g->mDown[0]) { g->activeId = 0; te.dragging = false; }
    }

    // Keyboard
    bool changed = false;
    if (editing && focused) {
        te.seen = true;
        for (const InputEvent& ev : g->events) {
            if (ev.type == EV_Char) {
                if (readOnly) continue;
                wchar_t ch = ev.ch;
                if (ch == L'\t' && !multiline) continue;
                std::wstring one(1, ch);
                one = FilterInput(one, flags, multiline);
                if (one.empty()) continue;
                PushUndo(1);
                InsertText(one);
                changed = true;
                continue;
            }
            int vk = ev.vk;
            bool ctrl = ev.ctrl, shift = ev.shift;
            int len = (int)te.text.size();
            bool keepWantX = false;
            switch (vk) {
            case VK_LEFT:
                if (HasSel() && !shift) te.cursor = SelMin();
                else te.cursor = ctrl ? WordLeft(te.text, te.cursor) : PrevPos(te.text, te.cursor);
                break;
            case VK_RIGHT:
                if (HasSel() && !shift) te.cursor = SelMax();
                else te.cursor = ctrl ? WordRight(te.text, te.cursor) : NextPos(te.text, te.cursor);
                break;
            case VK_UP:
            case VK_DOWN:
            case VK_PRIOR:
            case VK_NEXT:
                if (multiline) {
                    TextEntry* le = layoutFor(password ? masked : te.text);
                    float ch_ = 0;
                    Vec2 cp = CaretPos(le->layout, te.cursor, len, ch_);
                    if (te.wantX < 0) te.wantX = cp.x;
                    float dy = (vk == VK_UP || vk == VK_DOWN) ? lh : Mx(lh, inner.h - lh);
                    float ny = (vk == VK_UP || vk == VK_PRIOR) ? cp.y - dy + ch_ * 0.5f : cp.y + dy + ch_ * 0.5f;
                    if (ny < 0) te.cursor = 0;
                    else if (ny > le->h) te.cursor = len;
                    else te.cursor = HitTestLayout(le->layout, len, te.wantX, ny);
                    keepWantX = true;
                } else if (vk == VK_UP || vk == VK_PRIOR) {
                    te.cursor = 0;
                } else {
                    te.cursor = len;
                }
                break;
            case VK_HOME:
                if (ctrl || !multiline) te.cursor = 0;
                else { int p = te.cursor; while (p > 0 && te.text[p - 1] != L'\n') p--; te.cursor = p; }
                break;
            case VK_END:
                if (ctrl || !multiline) te.cursor = len;
                else { int p = te.cursor; while (p < len && te.text[p] != L'\n') p++; te.cursor = p; }
                break;
            case VK_BACK:
                if (readOnly) break;
                if (HasSel()) { PushUndo(2); DeleteSel(); changed = true; }
                else if (te.cursor > 0) {
                    int p = ctrl ? WordLeft(te.text, te.cursor) : PrevPos(te.text, te.cursor);
                    PushUndo(3);
                    te.text.erase((size_t)p, (size_t)(te.cursor - p));
                    te.cursor = te.anchor = p;
                    changed = true;
                }
                break;
            case VK_DELETE:
                if (readOnly) break;
                if (shift && !ctrl && HasSel() && !password) {
                    SetClipboard(Str(Narrow(te.text.substr((size_t)SelMin(), (size_t)(SelMax() - SelMin())))));
                    PushUndo(2); DeleteSel(); changed = true;
                } else if (HasSel()) { PushUndo(2); DeleteSel(); changed = true; }
                else if (te.cursor < len) {
                    int p = ctrl ? WordRight(te.text, te.cursor) : NextPos(te.text, te.cursor);
                    PushUndo(4);
                    te.text.erase((size_t)te.cursor, (size_t)(p - te.cursor));
                    te.anchor = te.cursor;
                    changed = true;
                }
                break;
            case VK_RETURN:
                if (multiline && !ctrl) {
                    if (!readOnly) { PushUndo(2); InsertText(L"\n"); changed = true; }
                } else {
                    submitted = true;
                }
                break;
            case VK_TAB:
                if (multiline && (flags & InputAllowTab) && !readOnly && !ctrl) {
                    PushUndo(1);
                    InsertText(L"\t");
                    changed = true;
                    g->tabConsumed = true;
                }
                break;
            case VK_ESCAPE:
                g->focusId = 0;
                break;
            case VK_INSERT:
                if (ctrl && HasSel() && !password) SetClipboard(Str(Narrow(te.text.substr((size_t)SelMin(), (size_t)(SelMax() - SelMin())))));
                else if (shift && !readOnly) {
                    std::wstring clip = FilterInput(Widen(Str(GetClipboard())), flags, multiline);
                    if (!clip.empty() || HasSel()) { PushUndo(2); InsertText(clip); changed = true; }
                }
                break;
            default:
                if (ctrl && !ev.alt) {
                    if (vk == 'A') { te.anchor = 0; te.cursor = len; break; }
                    if ((vk == 'C' || vk == 'X') && HasSel() && !password) {
                        SetClipboard(Str(Narrow(te.text.substr((size_t)SelMin(), (size_t)(SelMax() - SelMin())))));
                        if (vk == 'X' && !readOnly) { PushUndo(2); DeleteSel(); changed = true; }
                        break;
                    }
                    if (vk == 'V' && !readOnly) {
                        std::wstring clip = FilterInput(Widen(Str(GetClipboard())), flags, multiline);
                        if (!clip.empty() || HasSel()) { PushUndo(2); InsertText(clip); changed = true; }
                        break;
                    }
                    if ((vk == 'Z' && !shift) && !readOnly && !te.undo.empty()) {
                        TextEdit::Snap cur; cur.text = te.text; cur.cursor = te.cursor; cur.anchor = te.anchor;
                        te.redo.push_back(cur);
                        TextEdit::Snap u = te.undo.back(); te.undo.pop_back();
                        te.text = u.text; te.cursor = u.cursor; te.anchor = u.anchor;
                        te.lastKind = 0;
                        changed = true;
                        break;
                    }
                    if ((vk == 'Y' || (vk == 'Z' && shift)) && !readOnly && !te.redo.empty()) {
                        TextEdit::Snap cur; cur.text = te.text; cur.cursor = te.cursor; cur.anchor = te.anchor;
                        te.undo.push_back(cur);
                        TextEdit::Snap r = te.redo.back(); te.redo.pop_back();
                        te.text = r.text; te.cursor = r.cursor; te.anchor = r.anchor;
                        te.lastKind = 0;
                        changed = true;
                        break;
                    }
                }
                break;
            }
            bool nav = vk == VK_LEFT || vk == VK_RIGHT || vk == VK_UP || vk == VK_DOWN || vk == VK_HOME ||
                       vk == VK_END || vk == VK_PRIOR || vk == VK_NEXT;
            if (nav && !shift) te.anchor = te.cursor;
            if (!keepWantX) te.wantX = -1;
            te.cursor = Mn(Mx(te.cursor, 0), (int)te.text.size());
            te.anchor = Mn(Mx(te.anchor, 0), (int)te.text.size());
            if (nav || changed) g->caretReset = g->time;
        }
        if (changed) {
            str = Narrow(te.text);
            te.synced = str;
            g->last.changed = true;
            if (password) masked.assign(te.text.size(), (wchar_t)0x2022);
            e = layoutFor(password ? masked : te.text);
        }
    }
    if (editing) e = layoutFor(password ? masked : te.text);

    // Keep the caret visible
    float caretH = lh;
    Vec2 cp;
    if (editing) {
        cp = CaretPos(e->layout, te.cursor, (int)te.text.size(), caretH);
        if (caretH <= 0) caretH = lh;
        if (!multiline) {
            float vis = inner.w - 2;
            if (cp.x - te.scrollX > vis) te.scrollX = cp.x - vis;
            if (cp.x - te.scrollX < 0) te.scrollX = cp.x;
            te.scrollX = Clampf(te.scrollX, 0, Mx(0.0f, e->w - vis));
            origin.x = inner.x - te.scrollX;
        } else if (focused && (changed || g->events.size() || (g->activeId == id && g->mDown[0]))) {
            if (cp.y < cs.scroll) cs.scroll = cs.scrollTarget = cp.y;
            else if (cp.y + caretH > cs.scroll + inner.h) cs.scroll = cs.scrollTarget = cp.y + caretH - inner.h;
            origin.y = inner.y - cs.scroll;
        }
    }

    // Draw
    bool disabled = g->disabled > 0;
    WState& st = WS(id);
    Anim(st.a, hov ? 1.0f : 0.0f, 18);
    if (drawFrame) {
        AddRect(fr, LerpC(s.field, s.frameHover, st.a * 0.45f), s.rounding);
        Color border = focused ? s.accent : LerpC(s.border, s.borderStrong, st.a);
        AddRectStroke(fr, border, s.rounding);
        if (focused && !readOnly) AddRect(Rect(fr.x + s.rounding * 0.5f, fr.Bottom() - 2, fr.w - s.rounding, 2), s.accent, 1);
    }
    Rect clip = Intersect(Rect(inner.x - 1, fr.y + 1, inner.w + 2, fr.h - 2), g->layDepth > 0 ? Lay().clip : inner);
    AddClipPush(clip);
    if (editing && HasSel()) SelectionRects(e->layout, (int)te.text.size(), SelMin(), SelMax(), origin, focused ? s.selection : s.selection.WithAlpha(s.selection.a * 0.5f));
    if (!disp->empty() || (editing && !te.text.empty())) AddTextEntry(e, origin, disabled ? s.textDisabled : s.text);
    else if (hint.n) {
        if (multiline) {
            TextEntry* he = GetText(hint, font, TF_Wrap, inner.w);
            AddTextEntry(he, Vec2(inner.x, inner.y), s.textDim.WithAlpha(0.8f));
        } else {
            DrawTextIn(hint, Rect(inner.x, fr.y, inner.w, fr.h), font, s.textDim.WithAlpha(0.8f));
        }
    }
    if (editing && focused && !disabled) {
        double t = g->time - g->caretReset;
        bool on = fmod(t, 1.06) < 0.53;
        double next = g->caretReset + (floor(t / 0.53) + 1) * 0.53;
        WakeAt(next + 0.001);
        float cw = Mx(1.0f, floorf(g->scale + 0.25f)) / g->scale;
        if (on) AddRect(Rect(Snap(origin.x + cp.x), Snap(origin.y + cp.y), cw, Snap(caretH)), s.text);
        g_imeCaret = Vec2(origin.x + cp.x, origin.y + cp.y + caretH);
    }
    AddClipPop();
    if (multiline) {
        float contentH = e->h + 2;
        Rect view(fr.x, inner.y, fr.w, inner.h);
        ScrollArea(id, view, contentH, 0);
    }
    if (!drawFrame) FocusRing(id, fr, s.rounding);
    g->last.id = id;
    g->last.focused = focused;
    g->last.active = editing;
    g->last.hovered = hov;
    g->last.submitted = submitted;
    if (changed) g->last.changed = true;
    return changed;
}

static bool TempInputCore(ID id, Rect frame, std::string& text, Str hint, int flags, bool multiline, bool& submitted) {
    return TextFieldCore(id, frame, text, hint, flags, multiline, submitted);
}

static bool InputNumberCore(Str label, double& value, double step, double vmin, double vmax, const char* fmt, bool isInt) {
    Style& s = g->style;
    ID id = MakeID(label);
    ID fid = Combine(id, 0xF1E1Du);
    Labeled it = PlaceLabeled(label, 150, true, g->frameH);
    DrawLabel(it);
    Rect c = it.ctrl;
    float bw = Snap(g->frameH - 4);
    bool showSteps = step != 0 && c.w > bw * 2 + 50;
    Rect minusR(c.Right() - bw * 2 - 3, c.y + 2, bw, c.h - 4), plusR(c.Right() - bw - 2, c.y + 2, bw, c.h - 4);
    bool changed = false;
    double lo = Mn(vmin, vmax), hi = Mx(vmin, vmax);
    if (showSteps) {
        bool h1, d1, h2, d2;
        bool m = ButtonBehavior(minusR, Combine(id, 1), h1, d1, BF_Repeat | BF_NoFocus);
        bool p = ButtonBehavior(plusR, Combine(id, 2), h2, d2, BF_Repeat | BF_NoFocus);
        if (m || p) {
            double nv = value + (p ? step : -step);
            nv = nv < lo ? lo : nv > hi ? hi : nv;
            if (isInt) nv = floor(nv + 0.5);
            if (nv != value) { value = nv; changed = true; }
        }
        Anim(WS(Combine(id, 1)).a, h1 ? 1.0f : 0.0f, 20);
        Anim(WS(Combine(id, 2)).a, h2 ? 1.0f : 0.0f, 20);
    }
    std::string& buf = g->numText[fid];
    char tmp[64];
    if (g->te.id != fid || changed) {
        FormatNumber(tmp, sizeof(tmp), fmt, value, isInt);
        buf = tmp;
    }
    if (g->focusId == fid && (g->kPressed[VK_UP] || g->kPressed[VK_DOWN]) && step != 0) {
        double nv = value + (g->kPressed[VK_UP] ? step : -step);
        nv = nv < lo ? lo : nv > hi ? hi : nv;
        if (nv != value) { value = nv; changed = true; FormatNumber(tmp, sizeof(tmp), fmt, value, isInt); buf = tmp; }
    }
    bool submitted = false;
    bool edited = TextFieldCore(fid, c, buf, Str(), InputNumbers, false, submitted, 0, showSteps ? bw * 2 + 6 : 0);
    if (edited) {
        double nv;
        if (ParseNumber(buf, nv)) {
            nv = nv < lo ? lo : nv > hi ? hi : nv;
            if (isInt) nv = floor(nv + 0.5);
            if (nv != value) { value = nv; changed = true; }
        }
    }
    if (submitted || (g->te.id != fid && g->numText.count(fid))) {
        FormatNumber(tmp, sizeof(tmp), fmt, value, isInt);
        buf = tmp;
    }
    if (showSteps) {
        WState& a = WS(Combine(id, 1));
        WState& b = WS(Combine(id, 2));
        AddRect(Rect(minusR.x - 1, c.y + 6, Px(), c.h - 12), s.border);
        if (a.a > 0) AddRect(minusR, MulA(s.frameHover, a.a), s.rounding - 2);
        if (b.a > 0) AddRect(plusR, MulA(s.frameHover, b.a), s.rounding - 2);
        AddIcon(Icon::Minus, Rect(minusR.Center().x - 7, minusR.Center().y - 7, 14, 14), LerpC(s.textDim, s.text, a.a), 0, 2.2f);
        AddIcon(Icon::Plus, Rect(plusR.Center().x - 7, plusR.Center().y - 7, 14, 14), LerpC(s.textDim, s.text, b.a), 0, 2.2f);
    }
    g->last.id = id;
    g->last.changed = changed;
    g->last.submitted = submitted;
    return changed;
}

} // namespace detail

bool InputText(Str label, std::string& text, Str hint, int flags) {
    SUI_GUARD(false);
    using namespace detail;
    Labeled it = PlaceLabeled(label, 200, true, g->frameH);
    DrawLabel(it);
    bool submitted = false;
    return TextFieldCore(MakeID(label), it.ctrl, text, hint, flags, false, submitted);
}

bool InputMultiline(Str label, std::string& text, float height, int flags) {
    SUI_GUARD(false);
    using namespace detail;
    Labeled it = PlaceLabeled(label, 200, true, Mx(height, g->frameH));
    if (it.has) DrawTextIn(it.text, Rect(it.label.x, it.label.y, it.label.w, Mn(it.label.h, g->frameH)), FontBody, g->style.text);
    bool submitted = false;
    return TextFieldCore(MakeID(label), it.ctrl, text, Str(), flags, true, submitted);
}

bool SearchBox(Str id, std::string& text, Str hint, int flags) {
    SUI_GUARD(false);
    using namespace detail;
    Style& s = g->style;
    Labeled it = PlaceLabeled(id, 200, true, g->frameH);
    DrawLabel(it);
    Rect c = it.ctrl;
    ID fid = MakeID(id);
    bool changed = false;
    bool showClear = !text.empty() && !(flags & InputNoClear) && c.w > 80;
    Rect clearR(c.Right() - g->frameH + 3, c.y + 3, g->frameH - 6, c.h - 6);
    if (showClear) {
        bool hov, held;
        if (ButtonBehavior(clearR, Combine(fid, 0xC1Eu), hov, held, BF_NoFocus)) {
            text.clear();
            changed = true;
            g->focusId = fid;
        }
        WState& cst = WS(Combine(fid, 0xC1Eu));
        Anim(cst.a, hov ? 1.0f : 0.0f, 20);
        cst.b = 1;
    }
    bool submitted = false;
    if (TextFieldCore(fid, c, text, hint, flags, false, submitted, 22, showClear ? g->frameH - 6 : 0)) changed = true;
    AddIcon(Icon::Search, Rect(c.x + 10, c.y + (c.h - 16) * 0.5f, 16, 16), s.textDim, 0, 2.0f);
    if (showClear) {
        WState& cst = WS(Combine(fid, 0xC1Eu));
        if (cst.a > 0) AddRect(clearR, MulA(s.frameHover, cst.a), s.rounding - 2);
        AddIcon(Icon::Close, Rect(clearR.Center().x - 7, clearR.Center().y - 7, 14, 14), LerpC(s.textDim, s.text, cst.a), 0, 2.2f);
    }
    g->last.id = fid;
    g->last.changed = changed;
    g->last.submitted = submitted;
    return changed;
}

bool InputInt(Str label, int& value, int step, int min, int max) {
    SUI_GUARD(false);
    double v = value;
    bool c = detail::InputNumberCore(label, v, step, min, max, "%d", true);
    if (c) value = (int)v;
    return c;
}
bool InputFloat(Str label, float& value, float step, float min, float max, const char* format) {
    SUI_GUARD(false);
    double v = value;
    bool c = detail::InputNumberCore(label, v, step, min, max, format, false);
    if (c) value = (float)v;
    return c;
}

// =================================================================================
// Widgets: choices (combo, list box, tabs, segmented, headers, tree)
// =================================================================================

namespace detail {

static bool ComboCore(Str label, int& current, const std::vector<Str>& items) {
    Style& s = g->style;
    ID id = MakeID(label);
    ID pid = Combine(id, 0xC0B0u);
    int count = (int)items.size();
    Labeled it = PlaceLabeled(label, 180, true, g->frameH);
    DrawLabel(it);
    Rect c = it.ctrl;
    bool hov, held;
    bool pressed = ButtonBehavior(c, id, hov, held, BF_PressOnClick);
    bool changed = false;
    if (pressed && !IsPopupOpen(pid)) OpenPopupEx(pid, PK_Combo, c);
    bool open = IsPopupOpen(pid);
    if (g->focusId == id && !open && g->disabled == 0 && count > 0) {
        if (g->kPressed[VK_DOWN] && current < count - 1) { current = Mx(0, current + 1); changed = true; }
        if (g->kPressed[VK_UP] && current > 0) { current = Mn(count - 1, current - 1); changed = true; }
    }
    WState& st = WS(id);
    Anim(st.a, hov ? 1.0f : 0.0f, 18);
    Anim(st.c, open ? 1.0f : 0.0f, 20);
    AddRect(c, LerpC(s.frame, s.frameHover, st.a), s.rounding);
    AddRectStroke(c, open ? s.accent : s.border, s.rounding);
    Str cur = (current >= 0 && current < count) ? items[(size_t)current] : Str();
    DrawTextIn(cur, Rect(c.x + s.framePadding, c.y, Mx(0.0f, c.w - s.framePadding - 32), c.h), FontBody, s.text);
    AddIcon(Icon::ChevronDown, Rect(c.Right() - 26, Snap(c.y + (c.h - 16) * 0.5f), 16, 16), s.textDim, st.c * kPi, 2.0f);
    FocusRing(id, c, s.rounding);
    if (open) {
        float rowH = s.rowHeight + 1;
        float full = count * rowH + 10;
        float below = g->viewH - c.Bottom() - 14, above = c.y - 14;
        float room = Mx(below, above);
        float listH = Mn(full, Mn(rowH * 9 + 10, Mx(rowH * 3 + 10, room)));
        if (BeginPopupEx(pid, PK_Combo, c, c.w, c.w)) {
            bool scroll = full > listH + 0.5f;
            ID cid = Combine(pid, 1);
            if (scroll) {
                PopupState& p = g->popups[(size_t)(g->popupDepth - 1)];
                if (p.openFrame == g->frame || !p.sized) {
                    ContState& cs = CS(cid);
                    cs.scroll = cs.scrollTarget = Mx(0.0f, current * rowH - (listH - 10) * 0.5f);
                }
                Rect cr = PlaceItem(AvailW(), listH - 10);
                BeginChildRect(cid, cr, false, 0, 1);
            } else {
                Lay().spacingY = 1;
            }
            for (int i = 0; i < count; i++) {
                PushID(i);
                if (Selectable(items[(size_t)i], i == current)) {
                    if (current != i) changed = true;
                    current = i;
                    CloseCurrentPopup();
                }
                PopID();
            }
            if (scroll) EndChild();
            EndPopupEx();
        }
    }
    g->last.id = id;
    g->last.changed = changed;
    return changed;
}

static bool TabsCore(int& current, const std::vector<Str>& tabs) {
    Style& s = g->style;
    uint32_t seed = SeedID() ^ 0x7AB5u;
    for (const Str& t : tabs) seed = Hash32(t.s, t.n, seed);
    ID id = seed;
    LayoutState& L = Lay();
    FinishLine(L);
    float h = Snap(g->frameH + 4);
    float w = Mx(0.0f, L.right - (L.left + L.indent));
    Rect r = PlaceItem(w, h);
    AddRect(Rect(r.x, r.Bottom() - Px(), r.w, Px()), s.separator);
    bool changed = false;
    float x = r.x, selX = 0, selW = 0;
    int n = (int)tabs.size();
    Focusable(id);
    for (int i = 0; i < n; i++) {
        Str t = DisplayText(tabs[(size_t)i]);
        float tw = TextW(t, FontBody);
        Rect tr(Snap(x), r.y, Snap(tw + 26), h - 3);
        ID tid = Combine(id, (uint32_t)i + 1);
        bool hov, held;
        if (ButtonBehavior(tr, tid, hov, held, BF_PressOnClick | BF_NoFocus)) {
            if (current != i) changed = true;
            current = i;
            g->focusId = id;
            g->focusVisible = false;
            g->focusClaimed = true;
        }
        WState& ts = WS(tid);
        Anim(ts.a, hov ? 1.0f : 0.0f, 20);
        bool sel = current == i;
        if (ts.a > 0) AddRect(tr.Shrink(3), MulA(s.frameHover, ts.a * 0.7f), s.rounding);
        DrawTextIn(t, tr, FontBody, sel ? s.text : LerpC(s.textDim, s.text, ts.a), Align::Center);
        if (sel) { selX = tr.x + 10; selW = tr.w - 20; FocusRing(id, tr.Shrink(2), s.rounding); }
        x += tr.w + 2;
    }
    if (g->focusId == id && g->disabled == 0 && n > 0) {
        if (g->kPressed[VK_RIGHT] && current < n - 1) { current++; changed = true; }
        if (g->kPressed[VK_LEFT] && current > 0) { current--; changed = true; }
    }
    WState& st = WS(id);
    if (!st.init) { st.init = true; st.a = selX - r.x; st.b = selW; }
    Anim(st.a, selX - r.x, 20);
    Anim(st.b, selW, 20);
    if (selW > 0) AddRect(Rect(r.x + st.a, r.Bottom() - 3, st.b, 3), s.accent, 1.5f);
    g->last.id = id;
    g->last.changed = changed;
    return changed;
}

static bool SegmentedCore(Str label, int& current, const std::vector<Str>& opts) {
    Style& s = g->style;
    ID id = MakeID(label);
    int n = Mx(1, (int)opts.size());
    float natural = 0;
    for (const Str& o : opts) natural = Mx(natural, TextW(o, FontBody) + 28);
    Labeled it = PlaceLabeled(label, natural * n + 6, true, g->frameH);
    DrawLabel(it);
    Rect c = it.ctrl;
    AddRect(c, s.field, s.rounding);
    AddRectStroke(c, s.border, s.rounding);
    float segW = (c.w - 6) / n;
    bool changed = false;
    Focusable(id);
    WState& st = WS(id);
    if (!st.init) { st.init = true; st.a = (float)current; }
    Anim(st.a, (float)Clampf((float)current, 0, (float)(n - 1)), 20);
    Rect thumb(Snap(c.x + 3 + st.a * segW), c.y + 3, Snap(segW), c.h - 6);
    if (current >= 0 && current < n) {
        if (!ThemeIsDark()) AddShadow(thumb, s.rounding - 2, 3, s.shadow.WithAlpha(0.12f));
        AddRect(thumb, ThemeIsDark() ? s.frameActive : s.panel, Mx(0.0f, s.rounding - 2));
        AddRectStroke(thumb, s.border, Mx(0.0f, s.rounding - 2));
    }
    for (int i = 0; i < (int)opts.size(); i++) {
        Rect sr(c.x + 3 + i * segW, c.y + 3, segW, c.h - 6);
        ID sid = Combine(id, (uint32_t)i + 1);
        bool hov, held;
        if (ButtonBehavior(sr, sid, hov, held, BF_PressOnClick | BF_NoFocus)) {
            if (current != i) changed = true;
            current = i;
            g->focusId = id;
            g->focusVisible = false;
            g->focusClaimed = true;
        }
        WState& ss = WS(sid);
        Anim(ss.a, hov ? 1.0f : 0.0f, 20);
        DrawTextIn(DisplayText(opts[(size_t)i]), sr, FontBody, current == i ? s.text : LerpC(s.textDim, s.text, ss.a), Align::Center);
    }
    if (g->focusId == id && g->disabled == 0) {
        if (g->kPressed[VK_RIGHT] && current < n - 1) { current++; changed = true; }
        if (g->kPressed[VK_LEFT] && current > 0) { current--; changed = true; }
    }
    FocusRing(id, c, s.rounding);
    g->last.id = id;
    g->last.changed = changed;
    return changed;
}


} // namespace detail


bool ListBox(Str label, int& current, const std::vector<Str>& items, int visibleItems) {
    SUI_GUARD(false);
    using namespace detail;
    Style& s = g->style;
    ID id = MakeID(label);
    float rowH = s.rowHeight + 1;
    float h = Mx(1, visibleItems) * rowH + 8;
    Labeled it = PlaceLabeled(label, 200, true, h);
    if (it.has) DrawTextIn(it.text, Rect(it.label.x, it.label.y, it.label.w, Mn(it.label.h, g->frameH)), FontBody, s.text);
    bool changed = false;
    BeginChildRect(Combine(id, 0x11u), it.ctrl, true, 4, 1);
    for (int i = 0; i < (int)items.size(); i++) {
        PushID(i);
        if (Selectable(items[(size_t)i], i == current)) {
            if (current != i) changed = true;
            current = i;
        }
        PopID();
    }
    EndChild();
    g->last.id = id;
    g->last.changed = changed;
    return changed;
}



bool CollapsingHeader(Str label, bool defaultOpen) {
    SUI_GUARD(false);
    using namespace detail;
    Style& s = g->style;
    ID id = MakeID(label);
    WState& st = WS(id);
    if (!st.init) { st.init = true; st.keep = true; st.open = defaultOpen; st.c = defaultOpen ? 1.0f : 0.0f; }
    LayoutState& L = Lay();
    FinishLine(L);
    float w = Mx(0.0f, L.right - (L.left + L.indent));
    Rect r = PlaceItem(w, g->frameH);
    bool hov, held;
    if (ButtonBehavior(r, id, hov, held)) { st.open = !st.open; g->last.changed = true; }
    if (g->focusId == id && g->disabled == 0) {
        if (g->kPressed[VK_RIGHT]) st.open = true;
        if (g->kPressed[VK_LEFT]) st.open = false;
    }
    Anim(st.a, hov ? 1.0f : 0.0f, 18);
    Anim(st.c, st.open ? 1.0f : 0.0f, 18);
    AddRect(r, LerpC(s.panel, s.frameHover, st.a), s.rounding);
    AddRectStroke(r, s.border.WithAlpha(s.border.a * 0.7f), s.rounding);
    AddIcon(Icon::ChevronRight, Rect(r.x + 9, Snap(r.y + (r.h - 16) * 0.5f), 16, 16), s.textDim, st.c * kPi * 0.5f, 2.0f);
    DrawTextIn(DisplayText(label), Rect(r.x + 32, r.y, Mx(0.0f, r.w - 38), r.h), FontBold, s.text);
    FocusRing(id, r, s.rounding);
    return st.open;
}

bool TreeNode(Str label, int flags) {
    SUI_GUARD(false);
    using namespace detail;
    Style& s = g->style;
    ID id = MakeID(label);
    WState& st = WS(id);
    bool leaf = (flags & TreeLeaf) != 0;
    if (!st.init) { st.init = true; st.keep = true; st.open = (flags & TreeDefaultOpen) && !leaf; st.c = st.open ? 1.0f : 0.0f; }
    float h = s.rowHeight;
    float w = ResolveWidth(AvailW(), AvailW(), true);
    Str t = DisplayText(label);
    ReportNatural(Lay().indent + TextW(t, FontBody) + 34);
    Rect r = PlaceItem(w, h, false, true);
    bool hov, held;
    bool pressed = ButtonBehavior(r, id, hov, held);
    if (pressed && !leaf) st.open = !st.open;
    if (g->focusId == id && !leaf && g->disabled == 0) {
        if (g->kPressed[VK_RIGHT]) st.open = true;
        if (g->kPressed[VK_LEFT]) st.open = false;
    }
    Anim(st.a, hov ? 1.0f : 0.0f, 22);
    Anim(st.c, st.open ? 1.0f : 0.0f, 20);
    float rr = Mx(0.0f, s.rounding - 1);
    if (flags & TreeSelected) AddRect(r, s.accent.WithAlpha(0.20f + 0.06f * st.a), rr);
    else if (st.a > 0) AddRect(r, MulA(s.frameHover, st.a), rr);
    if (!leaf) AddIcon(Icon::ChevronRight, Rect(r.x + 4, Snap(r.y + (h - 14) * 0.5f), 14, 14), s.textDim, st.c * kPi * 0.5f, 2.2f);
    else AddCircle(Vec2(r.x + 11, Snap(r.y + h * 0.5f)), 2.0f, s.textDim.WithAlpha(0.6f));
    DrawTextIn(t, Rect(r.x + 24, r.y, Mx(0.0f, r.w - 28), h), FontBody, s.text);
    FocusRing(id, r, rr);
    if (st.open && !leaf) { Indent(); g->treeDepth++; return true; }
    return false;
}
void TreePop() {
    SUI_GUARD();
    if (detail::g->treeDepth <= 0) { detail::Note("TreePop() without an open TreeNode() - ignored."); return; }
    detail::g->treeDepth--;
    Unindent();
}

// =================================================================================
// Color picker
// =================================================================================

namespace detail {
static void Checkerboard(Rect r, float cell) {
    Color a = Rgb(0xCFCFD4), b = Rgb(0xF4F4F6);
    if (!Overlaps(r, CullRect())) return;
    AddRect(r, b);
    AddClipPush(r);
    int rows = (int)ClampD(ceil(r.h / cell), 0.0, 400.0), cols = (int)ClampD(ceil(r.w / cell), 0.0, 400.0);
    for (int j = 0; j < rows; j++)
        for (int i = j & 1; i < cols; i += 2)
            AddRect(Rect(r.x + i * cell, r.y + j * cell, cell, cell), a);
    AddClipPop();
}
} // namespace detail

bool ColorEdit(Str label, Color& color, bool alpha) {
    SUI_GUARD(false);
    using namespace detail;
    Style& s = g->style;
    ID id = MakeID(label);
    ID pid = Combine(id, 0xC010u);
    Labeled it = PlaceLabeled(label, 170, true, g->frameH);
    DrawLabel(it);
    Rect c = it.ctrl;
    bool hov, held;
    bool pressed = ButtonBehavior(c, id, hov, held, BF_PressOnClick);
    if (pressed && !IsPopupOpen(pid)) OpenPopupEx(pid, PK_Color, c);
    bool open = IsPopupOpen(pid);
    WState& st = WS(id);
    Anim(st.a, hov ? 1.0f : 0.0f, 18);
    AddRect(c, LerpC(s.frame, s.frameHover, st.a), s.rounding);
    AddRectStroke(c, open ? s.accent : s.border, s.rounding);
    Rect sw(c.x + 5, c.y + 5, Snap(Mn(34.0f, c.w * 0.3f)), c.h - 10);
    if (color.a < 0.999f) Checkerboard(sw, 5);
    AddRect(sw, color, 4);
    AddRectStroke(sw, Color(0, 0, 0, 0.25f), 4);
    std::string hex = ToHex(color, alpha);
    DrawTextIn(Str(hex), Rect(sw.Right() + 9, c.y, Mx(0.0f, c.Right() - sw.Right() - 14), c.h), FontMono, s.text);
    FocusRing(id, c, s.rounding);
    bool changed = false;
    if (open && BeginPopupEx(pid, PK_Color, c, 250, 250)) {
        WState& ps = WS(pid);
        float h, sat, v;
        ToHsv(color, h, sat, v);
        if (ps.init) {
            if (v <= 0.0001f) { h = ps.a; sat = ps.b; }
            else if (sat <= 0.0001f) h = ps.a;
        }
        LayoutState& L = Lay();
        float W = L.right - L.left;
        Rect sv = PlaceItem(W, 150);
        bool h1, d1;
        ButtonBehavior(sv, Combine(pid, 1), h1, d1, BF_PressOnClick | BF_NoFocus);
        if (d1) {
            sat = Clampf((g->mouse.x - sv.x) / sv.w, 0, 1);
            v = 1 - Clampf((g->mouse.y - sv.y) / sv.h, 0, 1);
            color = Hsv(h, sat, v, color.a);
            changed = true;
        }
        AddGradient(sv, Color(1, 1, 1, 1), Hsv(h, 1, 1), false, 6);
        AddGradient(sv, Color(0, 0, 0, 0), Color(0, 0, 0, 1), true, 6);
        Vec2 mk(sv.x + sat * sv.w, sv.y + (1 - v) * sv.h);
        AddCircleStroke(mk, 7, Color(0, 0, 0, 0.35f), 3.0f);
        AddCircleStroke(mk, 7, Color(1, 1, 1, 1), 2.0f);
        Rect hb = PlaceItem(W, 14);
        bool h2, d2;
        ButtonBehavior(hb, Combine(pid, 2), h2, d2, BF_PressOnClick | BF_NoFocus);
        if (d2) {
            h = Clampf((g->mouse.x - hb.x) / hb.w, 0, 0.9999f);
            color = Hsv(h, sat, v, color.a);
            changed = true;
        }
        AddClipPush(hb);
        for (int i = 0; i < 6; i++) {
            float x0 = hb.x + hb.w * i / 6.0f, x1 = hb.x + hb.w * (i + 1) / 6.0f;
            AddGradient(Rect(x0, hb.y, x1 - x0 + 0.5f, hb.h), Hsv(i / 6.0f, 1, 1), Hsv((i + 1) / 6.0f, 1, 1), false);
        }
        AddClipPop();
        float hx = hb.x + h * hb.w;
        AddRect(Rect(hx - 3, hb.y - 2, 6, hb.h + 4), Color(1, 1, 1, 1), 3);
        AddRectStroke(Rect(hx - 3, hb.y - 2, 6, hb.h + 4), Color(0, 0, 0, 0.35f), 3);
        if (alpha) {
            Rect ab = PlaceItem(W, 14);
            bool h3, d3;
            ButtonBehavior(ab, Combine(pid, 3), h3, d3, BF_PressOnClick | BF_NoFocus);
            if (d3) { color.a = Clampf((g->mouse.x - ab.x) / ab.w, 0, 1); changed = true; }
            Checkerboard(ab, 7);
            AddGradient(ab, color.WithAlpha(0), color.WithAlpha(1), false);
            float ax = ab.x + color.a * ab.w;
            AddRect(Rect(ax - 3, ab.y - 2, 6, ab.h + 4), Color(1, 1, 1, 1), 3);
            AddRectStroke(Rect(ax - 3, ab.y - 2, 6, ab.h + 4), Color(0, 0, 0, 0.35f), 3);
        }
        Rect row = PlaceItem(W, g->frameH);
        Rect prev(row.x, row.y, g->frameH * 1.6f, row.h);
        if (color.a < 0.999f) Checkerboard(prev, 5);
        AddRect(prev, color, s.rounding);
        AddRectStroke(prev, s.border, s.rounding);
        ID hexId = Combine(pid, 9);
        std::string& hb2 = g->numText[hexId];
        if (g->te.id != hexId) hb2 = ToHex(color, alpha);
        bool sub = false;
        Rect hexR(prev.Right() + 8, row.y, row.Right() - prev.Right() - 8, row.h);
        PushFont(FontMono);
        if (TextFieldCore(hexId, hexR, hb2, Str(), 0, false, sub)) {
            Color nc;
            if (ParseHex(hb2.c_str(), nc)) { if (!alpha) nc.a = color.a; color = nc; changed = true; }
        }
        PopFont();
        ps.init = true;
        ps.a = h;
        ps.b = sat;
        EndPopupEx();
    }
    g->last.id = id;
    g->last.changed = changed;
    return changed;
}

// =================================================================================
// Plots
// =================================================================================

namespace detail {
static void PlotCore(Str label, const float* v, int n, float height, float vmin, float vmax, bool bars) {
    Style& s = g->style;
    ID id = Combine(MakeID(label), 0x9107u);
    float w = ResolveWidth(AvailW(), AvailW(), true);
    Rect r = PlaceItem(w, Mx(height, 44.0f));
    AddRect(r, s.field, s.rounding);
    AddRectStroke(r, s.border, s.rounding);
    Str t = DisplayText(label);
    float headH = LineH(FontSmall) + 8;
    Rect area(r.x + 10, r.y + headH + 2, r.w - 20, r.h - headH - 10);
    if (n <= 0 || !v || area.w < 4 || area.h < 4) {
        DrawTextIn(t, Rect(r.x + 10, r.y + 4, r.w - 20, headH - 4), FontSmall, s.textDim);
        return;
    }
    float lo = vmin, hi = vmax;
    if (!(hi > lo) || !std::isfinite(lo) || !std::isfinite(hi)) {
        bool any = false;
        for (int i = 0; i < n; i++) {
            if (!std::isfinite(v[i])) continue;
            if (!any) { lo = hi = v[i]; any = true; }
            else { lo = Mn(lo, v[i]); hi = Mx(hi, v[i]); }
        }
        if (!any) { lo = 0; hi = 1; }
        if (bars) lo = Mn(lo, 0.0f);
        float range = hi - lo;
        if (range < 1e-6f) { range = fabsf(hi) > 0 ? fabsf(hi) : 1.0f; lo -= range * 0.5f; hi += range * 0.5f; }
        else { hi += range * 0.08f; if (!bars || lo < 0) lo -= range * 0.08f; }
    }
    for (int k = 1; k <= 3; k++) AddRect(Rect(area.x, Snap(area.y + area.h * k / 4.0f), area.w, Px()), s.separator.WithAlpha(0.8f));
    auto Y = [&](float val) { return area.Bottom() - (Clampf(val, lo, hi) - lo) / (hi - lo) * area.h; };  // NaN -> bottom
    Color ac = s.accent;
    bool hov = Hoverable(r, id);
    int hi_idx = -1;
    if (hov) hi_idx = (int)Clampf(floorf((g->mouse.x - area.x) / area.w * (bars ? n : (n - 1)) + (bars ? 0.0f : 0.5f)), 0, (float)(n - 1));
    if (bars) {
        float bw = area.w / n;
        float gap = bw > 4 ? Mn(2.0f, bw * 0.2f) : 0.0f;
        float y0 = Y(Mx(lo, 0.0f));
        for (int i = 0; i < n; i++) {
            float y = Y(v[i]);
            Rect br(area.x + i * bw + gap * 0.5f, Mn(y, y0), Mx(1.0f, bw - gap), fabsf(y0 - y));
            AddRect(br, i == hi_idx ? s.accentHover : ac.WithAlpha(0.85f), Mn(2.0f, bw * 0.3f));
        }
    } else {
        int maxPts = Mx(2, (int)(area.w * 1.5f));
        int stride = n > maxPts ? (n + maxPts - 1) / maxPts : 1;
        std::vector<Vec2> pts;
        pts.reserve((size_t)(n / stride + 3));
        for (int i = 0; i < n; i += stride) pts.push_back(Vec2(area.x + (n == 1 ? 0.0f : area.w * i / (n - 1)), Y(v[i])));
        if ((n - 1) % stride != 0) pts.push_back(Vec2(area.Right(), Y(v[n - 1])));
        if (pts.size() == 1) pts.push_back(Vec2(area.Right(), pts[0].y));
        std::vector<Vec2> fill(pts);
        fill.push_back(Vec2(pts.back().x, area.Bottom()));
        fill.push_back(Vec2(pts.front().x, area.Bottom()));
        AddPoly(fill.data(), (int)fill.size(), ac.WithAlpha(0.14f), true, true);
        AddPoly(pts.data(), (int)pts.size(), ac, false, false, 1.8f);
        if (hi_idx >= 0) {
            float hx = area.x + (n == 1 ? 0.0f : area.w * hi_idx / (n - 1));
            AddRect(Rect(Snap(hx), area.y, Px(), area.h), s.textDim.WithAlpha(0.5f));
            AddCircle(Vec2(hx, Y(v[hi_idx])), 4.5f, ac);
            AddCircleStroke(Vec2(hx, Y(v[hi_idx])), 4.5f, s.field, 1.5f);
        }
    }
    DrawTextIn(t, Rect(r.x + 10, r.y + 4, r.w * 0.6f, headH - 4), FontSmall, s.textDim);
    char buf[48];
    snprintf(buf, sizeof(buf), "%.4g", (double)v[hi_idx >= 0 ? hi_idx : n - 1]);
    DrawTextIn(Str(buf), Rect(r.x + r.w * 0.4f, r.y + 4, r.w * 0.6f - 10, headH - 4), FontBold, s.text, Align::Right);
    g->last.id = id;
    g->last.hovered = hov;
}
} // namespace detail

void PlotLines(Str label, const float* values, int count, float height, float min, float max) { SUI_GUARD(); detail::PlotCore(label, values, count, height, min, max, false); }
void PlotBars(Str label, const float* values, int count, float height, float min, float max) { SUI_GUARD(); detail::PlotCore(label, values, count, height, min, max, true); }

// =================================================================================
// Tables
// =================================================================================

namespace detail {

static void EndTableCell() {
    TableState& T = g->table;
    if (!T.cellOpen) return;
    if (g->layDepth > 0 && Lay().kind == LK_Cell) { FinishLine(Lay()); PopLayout(); }
    AddClipPop();
    T.cellOpen = false;
}

static void EndTableRow() {
    TableState& T = g->table;
    EndTableCell();
    if (T.row < 0) return;
    Rect r(T.x, T.rowY, T.w, T.rowH);
    if (Hoverable(r, 0) && g->mClicked[0] && !g->mouseConsumed) {
        g->mouseConsumed = true;
        g->tableClicks[T.id] = T.row;
        WantFrames(2);
    }
}

static bool BeginTableCore(Str idStr, const std::vector<Str>& cols, int flags, float height) {
    TableState& T = g->table;
    if (T.active) return false;
    Style& s = g->style;
    ID tid = MakeID(idStr);
    LayoutState& P = Lay();
    FinishLine(P);
    float x = P.left + P.indent, y = P.lineY;
    float w = Mx(40.0f, P.right - x);
    T.id = tid;
    T.cols = Mx(1, Mn((int)cols.size(), 64));
    T.flags = flags;
    T.active = true;
    T.cellOpen = false;
    T.row = -1;
    T.cell = -1;
    T.x = x;
    T.y0 = y;
    T.height = height;
    T.savedFrameH = g->frameH;
    T.rowH = s.rowHeight;
    g->frameH = s.rowHeight - 4;
    T.sort = g->tableSorts[tid];
    T.sort.changed = false;
    ContState& bodyCs = CS(Combine(tid, 0xB0D7u));
    float sb = (height != 0 && bodyCs.hadBar) ? s.scrollbarSize + 2 : 0;
    T.w = Mx(10.0f, w - sb);
    std::vector<float>& wts = g->tableWeights[tid];
    if ((int)wts.size() != T.cols) {
        wts.assign((size_t)T.cols, 1.0f);
        for (size_t i = 0; i < wts.size() && i < g->nextTableWeights.size(); i++) { float v = g->nextTableWeights[i]; wts[i] = (v > 0.05f && v < 1e6f) ? v : 0.05f; }
    }
    g->nextTableWeights.clear();
    float sum = 0;
    for (float f : wts) sum += f;
    float cx = x;
    for (int i = 0; i < T.cols; i++) {
        T.colW[i] = Snap(T.w * wts[(size_t)i] / sum);
        T.colX[i] = cx;
        cx += T.colW[i];
    }
    T.colW[T.cols - 1] = (x + T.w) - T.colX[T.cols - 1];
    float headH = (flags & TableNoHeader) ? 0.0f : Snap(s.rowHeight + 2);
    T.headH = headH;
    if (headH > 0) {
        Rect hr(x, y, w, headH);
        AddRect(hr, s.panel, s.rounding);
        AddRect(Rect(x, y + headH - Px(), w, Px()), s.border);
        for (int i = 0; i < T.cols; i++) {
            Rect cr(T.colX[i], y, T.colW[i], headH);
            Str name = i < (int)cols.size() ? cols[(size_t)i] : Str();
            bool sorted = (flags & TableSortable) && T.sort.column == i;
            if (flags & TableSortable) {
                ID hid = Combine(tid, 0x4EADu + (uint32_t)i);
                bool hov, held;
                if (ButtonBehavior(Rect(cr.x + 3, cr.y + 2, cr.w - 6, cr.h - 4), hid, hov, held, BF_NoFocus)) {
                    if (T.sort.column == i) T.sort.ascending = !T.sort.ascending;
                    else { T.sort.column = i; T.sort.ascending = true; }
                    T.sort.changed = true;
                    g->tableSorts[tid] = T.sort;
                    g->tableSorts[tid].changed = false;
                    sorted = true;
                }
                if (hov) AddRect(Rect(cr.x + 3, cr.y + 3, cr.w - 6, cr.h - 6), s.frameHover, s.rounding - 1);
            }
            float tw = Mx(0.0f, cr.w - 16 - (sorted ? 18 : 0));
            DrawTextIn(name, Rect(cr.x + 8, cr.y, tw, cr.h), FontBold, sorted ? s.text : s.textDim);
            if (sorted) {
                float iconX = Mn(cr.x + 8 + TextW(name, FontBold) + 4, cr.Right() - 18);
                AddIcon(T.sort.ascending ? Icon::ChevronUp : Icon::ChevronDown, Rect(iconX, cr.y + (cr.h - 14) * 0.5f, 14, 14), s.accent, 0, 2.2f);
            }
            if (i > 0) AddRect(Rect(Snap(cr.x), cr.y + 7, Px(), cr.h - 14), s.border);
            if ((flags & TableResizable) && i < T.cols - 1) {
                Rect dr(cr.Right() - 4, cr.y, 8, cr.h);
                ID did = Combine(tid, 0xD1Fu + (uint32_t)i);
                bool hov, held;
                ButtonBehavior(dr, did, hov, held, BF_PressOnClick | BF_NoFocus);
                if (hov || held) g->cursor = kCurSizeWE;
                if (held) {
                    float pair = T.colW[i] + T.colW[i + 1];
                    float nw = Clampf(g->mouse.x - T.colX[i], 30.0f, pair - 30.0f);
                    float wsum = wts[(size_t)i] + wts[(size_t)i + 1];
                    wts[(size_t)i] = wsum * nw / pair;
                    wts[(size_t)i + 1] = wsum - wts[(size_t)i];
                    WantFrames(1);
                }
                if (held) AddRect(Rect(Snap(cr.Right()), cr.y + 4, 2, cr.h - 8), s.accent);
            }
        }
    }
    float bodyY = y + headH;
    T.layoutDepth = g->layDepth;
    if (height < 0) {   // fill the remaining space (minus |height| - 1)
        float bottom = (P.kind == LK_Root || P.kind == LK_Popup) ? P.rect.Bottom() - P.pad : P.clip.Bottom() - P.pad;
        height = Mx(headH + 60.0f, bottom - y + height + 1);
        T.height = height;
    }
    if (height > 0) {
        T.scroll = true;
        BeginChildRect(Combine(tid, 0xB0D7u), Rect(x, bodyY, w, Mx(20.0f, height - headH)), false, 0, 0);
    } else {
        T.scroll = false;
        Rect clip = P.clip;
        LayoutState& B = PushLayout(LK_Table, Combine(tid, 0xB0D7u));
        B.clip = clip;
        B.rect = Rect(x, bodyY, w, 0);
        B.left = x;
        B.right = x + w;
        B.lineY = B.startY = bodyY;
        B.maxY = bodyY;
        B.spacingY = 0;
    }
    auto it = g->tableClicks.find(tid);
    T.clickedRow = (it != g->tableClicks.end()) ? it->second : -1;
    if (it != g->tableClicks.end()) g->tableClicks.erase(it);
    return true;
}

static void EndTableIfOpen() {
    TableState& T = g->table;
    if (!T.active) return;
    Style& s = g->style;
    EndTableRow();
    float bodyH;
    if (T.scroll) {
        bodyH = Lay().rect.h;
        EndChild();
    } else {
        LayoutState& B = Lay();
        if (B.kind == LK_Table) {
            FinishLine(B);
            bodyH = Mx(0.0f, B.maxY - B.startY);
            PopLayout();
        } else {
            bodyH = 0;
        }
    }
    float total = T.headH + bodyH;
    float fullW = T.w + ((T.scroll && CS(Combine(T.id, 0xB0D7u)).hadBar) ? s.scrollbarSize + 2 : 0);
    AddRectStroke(Rect(T.x, T.y0, fullW, total), s.border, s.rounding);
    g->frameH = T.savedFrameH;
    T.active = false;
    PlaceItem(fullW, total);
}

} // namespace detail


bool TableNextRow(bool selected) {
    SUI_GUARD(false);
    using namespace detail;
    TableState& T = g->table;
    if (!T.active) return false;
    Style& s = g->style;
    EndTableRow();
    T.row++;
    T.cell = -1;
    Rect r = PlaceItem(T.w, T.rowH);
    r.x = T.x;
    r.w = T.w;
    T.rowY = r.y;
    bool hov = Hoverable(r, 0);
    if ((T.flags & TableStriped) && (T.row % 2 == 1)) AddRect(r, s.text.WithAlpha(ThemeIsDark() ? 0.025f : 0.035f));
    if (selected) AddRect(r, s.accent.WithAlpha(0.22f));
    else if (hov) AddRect(r, s.frameHover.WithAlpha(0.8f));
    if (selected) AddRect(Rect(r.x, r.y + 4, 3, r.h - 8), s.accent, 1.5f);
    AddRect(Rect(r.x, r.Bottom() - Px(), r.w, Px()), s.separator.WithAlpha(0.7f));
    bool clicked = T.clickedRow == T.row;
    g->last = ItemInfo();
    g->last.rect = r;
    g->last.hovered = hov;
    g->last.clicked = clicked;
    g->last.rightClicked = hov && g->mClicked[1];
    return clicked;
}

void TableNextCell() {
    SUI_GUARD();
    using namespace detail;
    TableState& T = g->table;
    if (!T.active || T.row < 0) return;
    EndTableCell();
    T.cell++;
    if (T.cell >= T.cols) return;
    Rect cell(T.colX[T.cell], T.rowY, T.colW[T.cell], T.rowH);
    Rect clip = Intersect(cell, Lay().clip);
    AddClipPush(clip);
    LayoutState& C = PushLayout(LK_Cell, Combine(T.id, 0xCE11u + (uint32_t)T.cell));
    C.clip = clip;
    C.rect = cell;
    C.left = cell.x + 8;
    C.right = cell.Right() - 8;
    C.lineY = C.startY = Snap(T.rowY + (T.rowH - g->frameH) * 0.5f);
    C.lineH = g->frameH;
    C.maxY = C.startY;
    C.spacingY = 2;
    T.cellOpen = true;
}

void EndTable() { SUI_GUARD(); detail::EndTableIfOpen(); }

TableSort TableSorting() {
    if (!detail::g || !detail::g->table.active) return TableSort();
    return detail::g->table.sort;
}

// =================================================================================
// Log
// =================================================================================

namespace detail {

struct LogData {
    struct Line { Level level; double time; std::string text; };
    std::mutex mutex;
    std::deque<Line> lines;
    size_t max = 2000;
    uint64_t version = 0;
};

static void LogAdd(LogData& log, Level level, Str text) {
    SYSTEMTIME st;
    GetLocalTime(&st);
    double t = st.wHour * 3600.0 + st.wMinute * 60.0 + st.wSecond + st.wMilliseconds / 1000.0;
    {
        std::lock_guard<std::mutex> lock(log.mutex);
        size_t start = 0;
        for (size_t i = 0; i <= text.n; i++) {
            if (i == text.n || text.s[i] == '\n') {
                size_t end = i;
                if (end > start && text.s[end - 1] == '\r') end--;
                LogData::Line l;
                l.level = level;
                l.time = t;
                l.text.assign(text.s + start, end - start);
                log.lines.push_back(l);
                start = i + 1;
            }
        }
        while (log.lines.size() > log.max && !log.lines.empty()) log.lines.pop_front();
        log.version++;
    }
    Wake();
}
static void LogClear(LogData& log) {
    { std::lock_guard<std::mutex> lock(log.mutex); log.lines.clear(); log.version++; }
    Wake();
}
static size_t LogCount(LogData& log) { std::lock_guard<std::mutex> lock(log.mutex); return log.lines.size(); }
static std::string LogText(LogData& log) {
    std::lock_guard<std::mutex> lock(log.mutex);
    std::string out;
    for (const LogData::Line& l : log.lines) { out += l.text; out += '\n'; }
    return out;
}

static void LogView(Str id, LogData& log, float height, bool showTime) {
    SUI_GUARD();
    Style& s = g->style;
    ID lid = MakeID(id);
    LayoutState& P = Lay();
    float avail = AvailW();
    float w = ResolveWidth(avail, avail, true);
    if (height <= 0) {
        float bottom = (P.kind == LK_Root || P.kind == LK_Popup) ? P.rect.Bottom() - P.pad : P.clip.Bottom() - P.pad;
        height = Mx(80.0f, bottom - NextItemY(P) + height);
    }
    Rect r = PlaceItem(w, height);
    ContState& cs = CS(lid);
    WState& st = WS(lid);
    const float pad = 6;
    float lh = Snap(LineH(FontMono) + 3);
    AddRect(r, s.field, s.rounding);
    Rect clip = Intersect(r.Shrink(1), P.clip);
    AddClipPush(clip);
    int action = 0;
    std::string copyText;
    float contentH;
    {
        std::lock_guard<std::mutex> lock(log.mutex);
        size_t n = log.lines.size();
        contentH = (float)n * lh + pad * 2;
        bool atBottom = cs.scrollTarget >= cs.contentH - cs.viewH - 4;
        if ((uint32_t)st.i != (uint32_t)log.version) {
            if (atBottom) cs.scrollTarget = cs.scroll = Mx(0.0f, contentH - r.h);
            st.i = (int)(uint32_t)log.version;
        }
        float scroll = cs.scroll;
        int first = (int)ClampD(floor((scroll - pad) / lh), 0.0, (double)n);
        int last = (int)ClampD(ceil((scroll - pad + r.h) / lh) + 1.0, 0.0, (double)n);
        float timeW = showTime ? TextW("00:00:00", FontMono) + 10 : 0;
        int selected = st.open ? (int)st.d : -1;
        for (int i = first; i < last; i++) {
            const LogData::Line& line = log.lines[(size_t)i];
            float y = r.y + pad - scroll + i * lh;
            Rect lr(r.x + 2, y, r.w - 4, lh);
            if (i == selected) AddRect(lr, s.selection.WithAlpha(s.selection.a * 0.6f), 3);
            float x = r.x + pad + 2;
            if (showTime) {
                char tb[16];
                int secs = (int)line.time;
                snprintf(tb, sizeof(tb), "%02d:%02d:%02d", (secs / 3600) % 24, (secs / 60) % 60, secs % 60);
                AddTextEntry(GetText(Str(tb), FontMono), Vec2(x, y + 1), s.textDim.WithAlpha(0.8f));
                x += timeW;
            }
            Color c = line.level == Level::Info ? s.text : LevelColor(line.level);
            AddTextEntry(GetText(Str(line.text), FontMono), Vec2(x, y + 1), c);
        }
        bool hov = Hoverable(r, lid);
        if (hov && (g->mClicked[0] || g->mClicked[1])) {
            int idx = (int)floorf((g->mouse.y - r.y - pad + scroll) / lh);
            if (idx >= 0 && idx < (int)n) { st.open = true; st.d = (float)idx; }
            if (g->mClicked[0]) { g->mouseConsumed = true; g->focusId = lid; g->focusClaimed = true; g->focusVisible = false; }
        }
        if (g->focusId == lid && st.open && g->kDown[VK_CONTROL] && g->kPressedFirst['C'] && (int)st.d < (int)n)
            copyText = log.lines[(size_t)st.d].text;
        if (g->focusId == lid && g->kDown[VK_CONTROL] && g->kPressedFirst['A']) action = 2;
        if (action == 2) { for (const LogData::Line& l : log.lines) { copyText += l.text; copyText += '\n'; } }
    }
    AddClipPop();
    ScrollArea(lid, r, contentH, pad);
    AddRectStroke(r, g->focusId == lid ? s.borderStrong : s.border, s.rounding);
    if (!copyText.empty()) { SetClipboard(Str(copyText)); Toast("Copied to clipboard", Level::Success, 1.5f); }
    g->last = ItemInfo();
    g->last.id = lid;
    g->last.rect = r;
    g->last.hovered = Hoverable(r, lid);
    g->last.rightClicked = g->last.hovered && g->mClicked[1];
    if (BeginContextMenu()) {
        if (MenuItem("Copy line", "Ctrl+C", false, st.open)) action = 1;
        if (MenuItem("Copy all", "Ctrl+A")) action = 2;
        MenuSeparator();
        if (MenuItem("Clear")) action = 3;
        EndContextMenu();
    }
    if (action == 1 || action == 2) {
        std::lock_guard<std::mutex> lock(log.mutex);
        std::string text;
        if (action == 1 && st.open && (size_t)st.d < log.lines.size()) text = log.lines[(size_t)st.d].text;
        if (action == 2) for (const LogData::Line& l : log.lines) { text += l.text; text += '\n'; }
        if (!text.empty()) SetClipboard(Str(text));
    }
    if (action == 3) { LogClear(log); st.open = false; }
}

} // namespace detail

// =================================================================================
// Menus
// =================================================================================

bool BeginMenuBar() {
    SUI_GUARD(false);
    using namespace detail;
    Style& s = g->style;
    g->menubarSeen = true;
    float h = Snap(g->frameH + 4);
    if (fabsf(g->menubarH - h) > 0.1f) { g->menubarH = h; WantFrames(1); }
    Rect r(0, 0, g->viewW, h);
    g->curLayer = LayerIndex(0, 1);
    g->curLayerKey = 0;
    LayoutState& L = PushLayout(LK_MenuBar, Hash32("##menubar", 9, 0));
    L.rect = r;
    L.clip = r;
    L.left = 6;
    L.right = g->viewW - 6;
    L.lineY = L.startY = 2;
    L.horiz = 1;
    L.spacingY = 0;
    AddRect(r, s.panel);
    AddRect(Rect(0, r.Bottom() - Px(), r.w, Px()), s.separator);
    return true;
}
void EndMenuBar() {
    SUI_GUARD();
    if (detail::Lay().kind == detail::LK_MenuBar) detail::PopLayout();
    else detail::Note("EndMenuBar() without a matching BeginMenuBar() - ignored.");
}

bool BeginMenu(Str label) {
    SUI_GUARD(false);
    using namespace detail;
    Style& s = g->style;
    LayoutState& L = Lay();
    ID id = MakeID(label);
    ID pid = Combine(id, 0x3E40u);
    Str t = DisplayText(label);
    if (L.kind == LK_MenuBar) {
        float tw = TextW(t, FontBody);
        Rect r = PlaceItem(tw + 22, L.rect.h - 4);
        bool hov = Hoverable(r, id);
        bool open = IsPopupOpen(pid);
        bool anyMenu = !g->popups.empty() && g->popups[0].fromMenuBar;
        if (hov && g->mClicked[0] && !g->mouseConsumed) {
            g->mouseConsumed = true;
            g->focusClaimed = true;
            if (!open) OpenPopupEx(pid, PK_Menu, r, true);
        } else if (hov && anyMenu && !open) {
            OpenPopupEx(pid, PK_Menu, r, true);
        }
        open = IsPopupOpen(pid);
        WState& st = WS(id);
        Anim(st.a, hov ? 1.0f : 0.0f, 20);
        if (open) AddRect(r, s.frameActive, s.rounding);
        else if (st.a > 0) AddRect(r, MulA(s.frameHover, st.a), s.rounding);
        DrawTextIn(t, r, FontBody, s.text, Align::Center);
        if (open && BeginPopupEx(pid, PK_Menu, r, 200)) { Lay().spacingY = 1; return true; }
        return false;
    }
    float h = s.rowHeight;
    float w = Mx(0.0f, L.right - (L.left + L.indent));
    ReportNatural(28 + TextW(t, FontBody) + 40);
    Rect r = PlaceItem(w, h, false, true);
    bool hov = Hoverable(r, id);
    bool open = IsPopupOpen(pid);
    if (!hov && g->menuNoReopen == pid) g->menuNoReopen = 0;
    if (hov && !open && g->menuNoReopen != pid) { OpenPopupEx(pid, PK_Menu, r); open = true; }
    WState& st = WS(id);
    Anim(st.a, (hov || open) ? 1.0f : 0.0f, 22);
    if (st.a > 0) AddRect(r, MulA(s.frameHover, st.a), s.rounding - 1);
    DrawTextIn(t, Rect(r.x + 28, r.y, r.w - 52, h), FontBody, s.text);
    AddIcon(Icon::ChevronRight, Rect(r.Right() - 22, Snap(r.y + (h - 14) * 0.5f), 14, 14), s.textDim, 0, 2.0f);
    if (open && BeginPopupEx(pid, PK_Menu, r, 190)) { Lay().spacingY = 1; return true; }
    return false;
}
void EndMenu() { SUI_GUARD(); detail::EndPopupEx(); }

namespace detail {
static bool MenuItemImpl(Str label, Str shortcut, bool checked, bool enabled, bool* toggle) {
    Style& s = g->style;
    LayoutState& L = Lay();
    ID id = MakeID(label);
    Str t = DisplayText(label);
    float h = s.rowHeight;
    float tw = TextW(t, FontBody), sw = shortcut.n ? TextW(shortcut, FontBody) : 0.0f;
    ReportNatural(28 + tw + (sw > 0 ? 36 + sw : 0) + 14);
    float w = Mx(0.0f, L.right - (L.left + L.indent));
    Rect r = PlaceItem(w, h, false, true);
    if (!enabled) BeginDisabled(true);
    bool hov, held;
    bool pressed = ButtonBehavior(r, id, hov, held, BF_NoFocus);
    if (hov && (int)g->popups.size() > g->popupDepth) { g->popups.resize((size_t)g->popupDepth); WantFrames(1); }
    WState& st = WS(id);
    Anim(st.a, hov ? 1.0f : 0.0f, 24);
    if (st.a > 0) AddRect(r, MulA(held ? s.frameActive : s.frameHover, st.a), s.rounding - 1);
    bool on = toggle ? *toggle : checked;
    if (on) AddIcon(Icon::Check, Rect(r.x + 7, Snap(r.y + (h - 15) * 0.5f), 15, 15), s.accent, 0, 2.6f);
    DrawTextIn(t, Rect(r.x + 28, r.y, Mx(0.0f, r.w - 36 - (sw > 0 ? sw + 20 : 0)), h), FontBody, s.text);
    if (sw > 0) DrawTextIn(shortcut, Rect(r.Right() - sw - 12, r.y, sw, h), FontBody, s.textDim, Align::Right);
    if (!enabled) EndDisabled();
    if (pressed) {
        if (toggle) *toggle = !*toggle;
        CloseAllPopups();
    }
    return pressed;
}
} // namespace detail

bool MenuItem(Str label, Str shortcut, bool checked, bool enabled) { SUI_GUARD(false); return detail::MenuItemImpl(label, shortcut, checked, enabled, nullptr); }
bool MenuItem(Str label, Str shortcut, bool* toggle, bool enabled) { SUI_GUARD(false); return detail::MenuItemImpl(label, shortcut, false, enabled, toggle); }
void MenuSeparator() {
    SUI_GUARD();
    using namespace detail;
    LayoutState& L = Lay();
    float w = Mx(0.0f, L.right - (L.left + L.indent));
    Rect r = PlaceItem(w, 7, false, true);
    AddRect(Rect(r.x + 6, Snap(r.y + 3), r.w - 12, Px()), g->style.separator);
}


// =================================================================================
// Lists and weights arrive from the DLL interface as plain arrays
// =================================================================================

void BeginRow(const float* weights, int count) {
    SUI_GUARD();
    BeginRowImpl(count > 0 ? weights : nullptr, count > 0 ? count : 1);
}
void BeginColumns(const float* weights, int count) {
    SUI_GUARD();
    detail::BeginColumnsImpl(count > 0 ? weights : nullptr, count > 0 ? count : 1);
}
void NextTableWeights(const float* weights, int count) {
    if (detail::g) detail::g->nextTableWeights.assign(weights, weights + (weights && count > 0 ? detail::Mn(count, 64) : 0));
}
bool Combo(Str label, int& current, const std::vector<Str>& items) {
    SUI_GUARD(false);
    return detail::ComboCore(label, current, items);
}
bool Tabs(int& current, const std::vector<Str>& tabs) {
    SUI_GUARD(false);
    return detail::TabsCore(current, tabs);
}
bool Segmented(Str label, int& current, const std::vector<Str>& options) {
    SUI_GUARD(false);
    return detail::SegmentedCore(label, current, options);
}
bool BeginTable(Str id, const std::vector<Str>& columns, int flags, float height) {
    SUI_GUARD(false);
    return detail::BeginTableCore(id, columns, flags, height);
}

#undef SUI_GUARD

} // namespace sui


// #################################################################################
//                     DLL interface: C adapters and exports
// #################################################################################

namespace sui {
namespace abi {

// Calls that change the UI must come from the thread running Run(); from any other
// thread they are ignored (instead of corrupting memory). The few thread-safe calls
// (Wake, Toast, Log, Quit, SetTitle, dialogs, ...) have no check.
#define SUI_UI_ONLY(ret) if (!detail::OnUiThread()) return ret

// NaN arguments become 0, so one bad number cannot spread through the layout.
static inline float Fin(float v) { return std::isnan(v) ? 0.0f : v; }
static inline Vec2 Fin(Vec2 v) { return Vec2(Fin(v.x), Fin(v.y)); }
static inline Rect Fin(Rect r) { return Rect(Fin(r.x), Fin(r.y), Fin(r.w), Fin(r.h)); }
static inline Color Fin(Color c) { return Color(Fin(c.r), Fin(c.g), Fin(c.b), Fin(c.a)); }

static std::vector<Str> ToStrs(const void* list, int count, api::ItemFn get) {
    std::vector<Str> out;
    if (!get || count <= 0) return out;
    out.reserve((size_t)count);
    for (int i = 0; i < count; i++) {
        size_t n = 0;
        const char* s = get(list, i, &n);
        out.push_back(Str(s, s ? n : 0));
    }
    return out;
}
static std::vector<Vec2> ToVec2s(const Vec2* points, int count) {
    std::vector<Vec2> out;
    if (!points || count <= 0) return out;
    out.reserve(static_cast<size_t>(count));
    for (int i = 0; i < count; i++) out.push_back(Fin(points[i]));
    return out;
}
// Sends an edited string back to the program, if it changed.
static void WriteBack(const std::string& s, const char* old, size_t oldLen, api::StrFn set, void* user) {
    if (s.size() != oldLen || (oldLen && memcmp(s.data(), old, oldLen) != 0)) set(user, s.c_str(), s.size());
}

// Running an app
static int SUI_CALL c_Run(const api::RunConfig* rc, api::FrameFn frame, void* user) {
    Config cfg;
    if (rc) {
        cfg.title = rc->title ? rc->title : "SimplyUI";
        cfg.width = rc->width;
        cfg.height = rc->height;
        cfg.minWidth = rc->minWidth;
        cfg.minHeight = rc->minHeight;
        cfg.resizable = rc->resizable != 0;
        cfg.topmost = rc->topmost != 0;
        cfg.scroll = rc->scroll != 0;
        cfg.rememberWindow = rc->rememberWindow != 0;
        cfg.theme = static_cast<Theme>(rc->theme);
        cfg.accent = rc->accent;
        cfg.appId = rc->appId ? rc->appId : "";
        cfg.iconResource = rc->iconResource;
        if (rc->size >= offsetof(api::RunConfig, singleInstance) + sizeof(int)) {
            cfg.tray = rc->tray != 0;
            cfg.startHidden = rc->startHidden != 0;
            cfg.singleInstance = rc->singleInstance != 0;
        }
    }
    std::function<void()> fn = [frame, user]() { if (frame) frame(user); };
    try {
        return Run(cfg, fn);
    } catch (...) {
        return 1;
    }
}
static void SUI_CALL c_Quit(int exitCode) { Quit(exitCode); }
static int SUI_CALL c_CloseRequested(void) { return CloseRequested() ? 1 : 0; }
static void SUI_CALL c_CancelClose(void) { SUI_UI_ONLY(); CancelClose(); }
static void SUI_CALL c_SetTitle(const char* title, size_t title_n) { SetTitle(Str(title, title_n)); }
static void SUI_CALL c_SetWindowSize(int width, int height) { SetWindowSize(width, height); }
static void SUI_CALL c_WindowSize(Vec2* out) { *out = WindowSize(); }
static void SUI_CALL c_SetTopmost(int topmost) { SetTopmost(topmost != 0); }
static int SUI_CALL c_IsTopmost(void) { return IsTopmost() ? 1 : 0; }
static void SUI_CALL c_Minimize(void) { Minimize(); }
static void SUI_CALL c_BringToFront(void) { BringToFront(); }
static void* SUI_CALL c_NativeWindow(void) { return NativeWindow(); }
static float SUI_CALL c_DpiScale(void) { return DpiScale(); }
static double SUI_CALL c_Time(void) { return Time(); }
static float SUI_CALL c_DeltaTime(void) { return DeltaTime(); }
static uint64_t SUI_CALL c_FrameCount(void) { return FrameCount(); }
static void SUI_CALL c_RequestRedraw(void) { RequestRedraw(); }
static void SUI_CALL c_RedrawIn(float seconds) { seconds = Fin(seconds); RedrawIn(seconds); }
static void SUI_CALL c_Wake(void) { Wake(); }

// Style, themes and fonts
static Style* SUI_CALL c_GetStyle(void) { return &GetStyle(); }
static void SUI_CALL c_SetTheme(int theme) { SUI_UI_ONLY(); SetTheme(static_cast<Theme>(theme)); }
static int SUI_CALL c_GetTheme(void) { return static_cast<int>(GetTheme()); }
static int SUI_CALL c_IsDark(void) { return IsDark() ? 1 : 0; }
static void SUI_CALL c_SetAccent(const Color* accent) { SUI_UI_ONLY(); Color accent_ok = Fin(*accent); accent = &accent_ok; SetAccent(*accent); }
static void SUI_CALL c_SystemAccent(Color* out) { *out = SystemAccent(); }
static int SUI_CALL c_AddFont(const char* family, size_t family_n, float size, int bold, int italic) { SUI_UI_ONLY(0); size = Fin(size); return AddFont(Str(family, family_n), size, bold != 0, italic != 0); }
static void SUI_CALL c_PushFont(int font) { SUI_UI_ONLY(); PushFont(font); }
static void SUI_CALL c_PopFont(void) { SUI_UI_ONLY(); PopFont(); }
static float SUI_CALL c_TextWidth(const char* text, size_t text_n, int font) { SUI_UI_ONLY(0); return TextWidth(Str(text, text_n), font); }
static float SUI_CALL c_LineHeight(int font) { SUI_UI_ONLY(0); return LineHeight(font); }

// Input
static void SUI_CALL c_MousePos(Vec2* out) { *out = MousePos(); }
static int SUI_CALL c_MouseDown(int button) { return MouseDown(button) ? 1 : 0; }
static int SUI_CALL c_MouseClicked(int button) { return MouseClicked(button) ? 1 : 0; }
static int SUI_CALL c_MouseReleased(int button) { return MouseReleased(button) ? 1 : 0; }
static int SUI_CALL c_MouseDoubleClicked(int button) { return MouseDoubleClicked(button) ? 1 : 0; }
static float SUI_CALL c_MouseWheel(void) { return MouseWheel(); }
static int SUI_CALL c_KeyDown(int key) { return KeyDown(static_cast<Key>(key)) ? 1 : 0; }
static int SUI_CALL c_KeyPressed(int key, int repeat) { return KeyPressed(static_cast<Key>(key), repeat != 0) ? 1 : 0; }
static int SUI_CALL c_KeyReleased(int key) { return KeyReleased(static_cast<Key>(key)) ? 1 : 0; }
static int SUI_CALL c_CtrlDown(void) { return CtrlDown() ? 1 : 0; }
static int SUI_CALL c_ShiftDown(void) { return ShiftDown() ? 1 : 0; }
static int SUI_CALL c_AltDown(void) { return AltDown() ? 1 : 0; }
static int SUI_CALL c_Shortcut(const char* keys, size_t keys_n) { SUI_UI_ONLY(0); return Shortcut(Str(keys, keys_n)) ? 1 : 0; }
static int SUI_CALL c_GlobalHotkey(const char* keys, size_t keys_n) { SUI_UI_ONLY(0); return GlobalHotkey(Str(keys, keys_n)) ? 1 : 0; }
static int SUI_CALL c_IsEditingText(void) { return IsEditingText() ? 1 : 0; }

// Layout
static void SUI_CALL c_SameLine(float spacing) { SUI_UI_ONLY(); spacing = Fin(spacing); SameLine(spacing); }
static void SUI_CALL c_NewLine(void) { SUI_UI_ONLY(); NewLine(); }
static void SUI_CALL c_Spacing(float height) { SUI_UI_ONLY(); height = Fin(height); Spacing(height); }
static void SUI_CALL c_Spacer(float width, float height) { SUI_UI_ONLY(); width = Fin(width); height = Fin(height); Spacer(width, height); }
static void SUI_CALL c_Indent(float amount) { SUI_UI_ONLY(); amount = Fin(amount); Indent(amount); }
static void SUI_CALL c_Unindent(float amount) { SUI_UI_ONLY(); amount = Fin(amount); Unindent(amount); }
static void SUI_CALL c_NextWidth(float width) { SUI_UI_ONLY(); width = Fin(width); NextWidth(width); }
static float SUI_CALL c_AvailableWidth(void) { SUI_UI_ONLY(0); return AvailableWidth(); }
static void SUI_CALL c_BeginRow(int columns) { SUI_UI_ONLY(); BeginRow(columns); }
static void SUI_CALL c_BeginRowWeights(const float* weights, int weights_count) { SUI_UI_ONLY(); BeginRow(weights, weights_count); }
static void SUI_CALL c_EndRow(void) { SUI_UI_ONLY(); EndRow(); }
static void SUI_CALL c_BeginColumns(int columns) { SUI_UI_ONLY(); BeginColumns(columns); }
static void SUI_CALL c_BeginColumnsWeights(const float* weights, int weights_count) { SUI_UI_ONLY(); BeginColumns(weights, weights_count); }
static void SUI_CALL c_NextColumn(void) { SUI_UI_ONLY(); NextColumn(); }
static void SUI_CALL c_EndColumns(void) { SUI_UI_ONLY(); EndColumns(); }
static void SUI_CALL c_BeginHorizontal(int align) { SUI_UI_ONLY(); BeginHorizontal(static_cast<Align>(align)); }
static void SUI_CALL c_EndHorizontal(void) { SUI_UI_ONLY(); EndHorizontal(); }
static void SUI_CALL c_BeginCard(const char* title, size_t title_n) { SUI_UI_ONLY(); BeginCard(Str(title, title_n)); }
static void SUI_CALL c_EndCard(void) { SUI_UI_ONLY(); EndCard(); }
static void SUI_CALL c_BeginChild(const char* id, size_t id_n, float height, int border) { SUI_UI_ONLY(); height = Fin(height); BeginChild(Str(id, id_n), height, border != 0); }
static void SUI_CALL c_EndChild(void) { SUI_UI_ONLY(); EndChild(); }
static void SUI_CALL c_BeginFooter(void) { SUI_UI_ONLY(); BeginFooter(); }
static void SUI_CALL c_EndFooter(void) { SUI_UI_ONLY(); EndFooter(); }
static void SUI_CALL c_BeginDisabled(int disabled) { SUI_UI_ONLY(); BeginDisabled(disabled != 0); }
static void SUI_CALL c_EndDisabled(void) { SUI_UI_ONLY(); EndDisabled(); }
static void SUI_CALL c_PushIDStr(const char* id, size_t id_n) { SUI_UI_ONLY(); PushID(Str(id, id_n)); }
static void SUI_CALL c_PushIDInt(int id) { SUI_UI_ONLY(); PushID(id); }
static void SUI_CALL c_PushIDPtr(const void* ptr) { SUI_UI_ONLY(); PushID(ptr); }
static void SUI_CALL c_PopID(void) { SUI_UI_ONLY(); PopID(); }
static uint32_t SUI_CALL c_GetID(const char* label, size_t label_n) { SUI_UI_ONLY(0); return GetID(Str(label, label_n)); }
static const ItemInfo* SUI_CALL c_LastItem(void) { return &LastItem(); }
static void SUI_CALL c_Tooltip(const char* text, size_t text_n) { SUI_UI_ONLY(); Tooltip(Str(text, text_n)); }

// Text
static void SUI_CALL c_Text(const char* text, size_t text_n) { SUI_UI_ONLY(); Text(Str(text, text_n)); }
static void SUI_CALL c_TextDim(const char* text, size_t text_n) { SUI_UI_ONLY(); TextDim(Str(text, text_n)); }
static void SUI_CALL c_TextColored(const Color* color, const char* text, size_t text_n) { SUI_UI_ONLY(); Color color_ok = Fin(*color); color = &color_ok; TextColored(*color, Str(text, text_n)); }
static void SUI_CALL c_TextWrapped(const char* text, size_t text_n) { SUI_UI_ONLY(); TextWrapped(Str(text, text_n)); }
static void SUI_CALL c_Heading(const char* text, size_t text_n) { SUI_UI_ONLY(); Heading(Str(text, text_n)); }
static void SUI_CALL c_Title(const char* text, size_t text_n) { SUI_UI_ONLY(); Title(Str(text, text_n)); }
static void SUI_CALL c_LabelText(const char* label, size_t label_n, const char* value, size_t value_n) { SUI_UI_ONLY(); LabelText(Str(label, label_n), Str(value, value_n)); }
static void SUI_CALL c_BulletText(const char* text, size_t text_n) { SUI_UI_ONLY(); BulletText(Str(text, text_n)); }
static int SUI_CALL c_Link(const char* text, size_t text_n, const char* url, size_t url_n) { SUI_UI_ONLY(0); return Link(Str(text, text_n), Str(url, url_n)) ? 1 : 0; }
static void SUI_CALL c_BadgeColor(const char* text, size_t text_n, const Color* color) { SUI_UI_ONLY(); Color color_ok = Fin(*color); color = &color_ok; Badge(Str(text, text_n), *color); }
static void SUI_CALL c_BadgeLevel(const char* text, size_t text_n, int level) { SUI_UI_ONLY(); Badge(Str(text, text_n), static_cast<Level>(level)); }
static void SUI_CALL c_Separator(void) { SUI_UI_ONLY(); Separator(); }
static void SUI_CALL c_SeparatorText(const char* text, size_t text_n) { SUI_UI_ONLY(); SeparatorText(Str(text, text_n)); }

// Widgets
static int SUI_CALL c_Button(const char* label, size_t label_n) { SUI_UI_ONLY(0); return Button(Str(label, label_n)) ? 1 : 0; }
static int SUI_CALL c_ButtonIcon(const char* label, size_t label_n, int icon) { SUI_UI_ONLY(0); return Button(Str(label, label_n), static_cast<Icon>(icon)) ? 1 : 0; }
static int SUI_CALL c_PrimaryButton(const char* label, size_t label_n) { SUI_UI_ONLY(0); return PrimaryButton(Str(label, label_n)) ? 1 : 0; }
static int SUI_CALL c_DangerButton(const char* label, size_t label_n) { SUI_UI_ONLY(0); return DangerButton(Str(label, label_n)) ? 1 : 0; }
static int SUI_CALL c_IconButton(int icon, const char* tooltip, size_t tooltip_n) { SUI_UI_ONLY(0); return IconButton(static_cast<Icon>(icon), Str(tooltip, tooltip_n)) ? 1 : 0; }
static int SUI_CALL c_Checkbox(const char* label, size_t label_n, bool* value) { SUI_UI_ONLY(0); return Checkbox(Str(label, label_n), *value) ? 1 : 0; }
static int SUI_CALL c_Toggle(const char* label, size_t label_n, bool* value) { SUI_UI_ONLY(0); return Toggle(Str(label, label_n), *value) ? 1 : 0; }
static int SUI_CALL c_RadioButton(const char* label, size_t label_n, int* current, int value) { SUI_UI_ONLY(0); return RadioButton(Str(label, label_n), *current, value) ? 1 : 0; }
static int SUI_CALL c_Selectable(const char* label, size_t label_n, int selected) { SUI_UI_ONLY(0); return Selectable(Str(label, label_n), selected != 0) ? 1 : 0; }
static int SUI_CALL c_SliderFloat(const char* label, size_t label_n, float* value, float min, float max, const char* format) { SUI_UI_ONLY(0); min = Fin(min); max = Fin(max); return Slider(Str(label, label_n), *value, min, max, format) ? 1 : 0; }
static int SUI_CALL c_SliderInt(const char* label, size_t label_n, int* value, int min, int max, const char* format) { SUI_UI_ONLY(0); return Slider(Str(label, label_n), *value, min, max, format) ? 1 : 0; }
static int SUI_CALL c_DragFloat(const char* label, size_t label_n, float* value, float speed, float min, float max, const char* format) { SUI_UI_ONLY(0); speed = Fin(speed); min = Fin(min); max = Fin(max); return Drag(Str(label, label_n), *value, speed, min, max, format) ? 1 : 0; }
static int SUI_CALL c_DragInt(const char* label, size_t label_n, int* value, float speed, int min, int max) { SUI_UI_ONLY(0); speed = Fin(speed); return Drag(Str(label, label_n), *value, speed, min, max) ? 1 : 0; }
static int SUI_CALL c_InputInt(const char* label, size_t label_n, int* value, int step, int min, int max) { SUI_UI_ONLY(0); return InputInt(Str(label, label_n), *value, step, min, max) ? 1 : 0; }
static int SUI_CALL c_InputFloat(const char* label, size_t label_n, float* value, float step, float min, float max, const char* format) { SUI_UI_ONLY(0); step = Fin(step); min = Fin(min); max = Fin(max); return InputFloat(Str(label, label_n), *value, step, min, max, format) ? 1 : 0; }
static int SUI_CALL c_InputText(const char* label, size_t label_n, const char* text, size_t text_n, api::StrFn text_set, void* text_user, const char* hint, size_t hint_n, int flags) { SUI_UI_ONLY(0);
    std::string text_s(text ? text : "", text ? text_n : 0);
    bool r = InputText(Str(label, label_n), text_s, Str(hint, hint_n), flags);
    WriteBack(text_s, text, text_n, text_set, text_user);
    return r ? 1 : 0;
}
static int SUI_CALL c_InputMultiline(const char* label, size_t label_n, const char* text, size_t text_n, api::StrFn text_set, void* text_user, float height, int flags) { SUI_UI_ONLY(0); height = Fin(height);
    std::string text_s(text ? text : "", text ? text_n : 0);
    bool r = InputMultiline(Str(label, label_n), text_s, height, flags);
    WriteBack(text_s, text, text_n, text_set, text_user);
    return r ? 1 : 0;
}
static int SUI_CALL c_SearchBox(const char* id, size_t id_n, const char* text, size_t text_n, api::StrFn text_set, void* text_user, const char* hint, size_t hint_n, int flags) { SUI_UI_ONLY(0);
    std::string text_s(text ? text : "", text ? text_n : 0);
    bool r = SearchBox(Str(id, id_n), text_s, Str(hint, hint_n), flags);
    WriteBack(text_s, text, text_n, text_set, text_user);
    return r ? 1 : 0;
}
static int SUI_CALL c_Combo(const char* label, size_t label_n, int* current, const void* items, int items_count, api::ItemFn items_get) { SUI_UI_ONLY(0); return Combo(Str(label, label_n), *current, ToStrs(items, items_count, items_get)) ? 1 : 0; }
static int SUI_CALL c_ListBox(const char* label, size_t label_n, int* current, const void* items, int items_count, api::ItemFn items_get, int visibleItems) { SUI_UI_ONLY(0);
    return ListBox(Str(label, label_n), *current, ToStrs(items, items_count, items_get), visibleItems) ? 1 : 0;
}
static int SUI_CALL c_Tabs(int* current, const void* tabs, int tabs_count, api::ItemFn tabs_get) { SUI_UI_ONLY(0); return Tabs(*current, ToStrs(tabs, tabs_count, tabs_get)) ? 1 : 0; }
static int SUI_CALL c_Segmented(const char* label, size_t label_n, int* current, const void* options, int options_count, api::ItemFn options_get) { SUI_UI_ONLY(0); return Segmented(Str(label, label_n), *current, ToStrs(options, options_count, options_get)) ? 1 : 0; }
static int SUI_CALL c_CollapsingHeader(const char* label, size_t label_n, int defaultOpen) { SUI_UI_ONLY(0); return CollapsingHeader(Str(label, label_n), defaultOpen != 0) ? 1 : 0; }
static int SUI_CALL c_TreeNode(const char* label, size_t label_n, int flags) { SUI_UI_ONLY(0); return TreeNode(Str(label, label_n), flags) ? 1 : 0; }
static void SUI_CALL c_TreePop(void) { SUI_UI_ONLY(); TreePop(); }
static void SUI_CALL c_ProgressBar(float fraction, const char* overlay, size_t overlay_n) { SUI_UI_ONLY(); fraction = Fin(fraction); ProgressBar(fraction, Str(overlay, overlay_n)); }
static void SUI_CALL c_Spinner(float size) { SUI_UI_ONLY(); size = Fin(size); Spinner(size); }
static int SUI_CALL c_ColorEdit(const char* label, size_t label_n, Color* color, int alpha) { SUI_UI_ONLY(0); return ColorEdit(Str(label, label_n), *color, alpha != 0) ? 1 : 0; }
static void SUI_CALL c_PlotLines(const char* label, size_t label_n, const float* values, int count, float height, float min, float max) { SUI_UI_ONLY(); height = Fin(height); min = Fin(min); max = Fin(max); PlotLines(Str(label, label_n), values, count, height, min, max); }
static void SUI_CALL c_PlotBars(const char* label, size_t label_n, const float* values, int count, float height, float min, float max) { SUI_UI_ONLY(); height = Fin(height); min = Fin(min); max = Fin(max); PlotBars(Str(label, label_n), values, count, height, min, max); }
static void SUI_CALL c_DropZone(const char* label, size_t label_n, float height, api::StrFn add, void* add_user) { SUI_UI_ONLY(); height = Fin(height);
    std::vector<std::string> r = DropZone(Str(label, label_n), height);
    for (size_t i = 0; i < r.size(); i++) add(add_user, r[i].c_str(), r[i].size());
}

// Tables
static int SUI_CALL c_BeginTable(const char* id, size_t id_n, const void* columns, int columns_count, api::ItemFn columns_get, int flags, float height) { SUI_UI_ONLY(0); height = Fin(height); return BeginTable(Str(id, id_n), ToStrs(columns, columns_count, columns_get), flags, height) ? 1 : 0; }
static int SUI_CALL c_TableNextRow(int selected) { SUI_UI_ONLY(0); return TableNextRow(selected != 0) ? 1 : 0; }
static void SUI_CALL c_TableNextCell(void) { SUI_UI_ONLY(); TableNextCell(); }
static void SUI_CALL c_EndTable(void) { SUI_UI_ONLY(); EndTable(); }
static void SUI_CALL c_NextTableWeights(const float* weights, int weights_count) { SUI_UI_ONLY(); NextTableWeights(weights, weights_count); }
static void SUI_CALL c_TableSorting(TableSort* out) { SUI_UI_ONLY(); *out = TableSorting(); }

// Log view
static void* SUI_CALL c_LogCreate(size_t maxLines) {
    detail::LogData* log = new (std::nothrow) detail::LogData();
    if (log) log->max = maxLines;
    return log;
}
static void SUI_CALL c_LogDestroy(void* log) { delete static_cast<detail::LogData*>(log); }
static void SUI_CALL c_LogAdd(void* log, int level, const char* text, size_t text_n) {
    if (log) detail::LogAdd(*static_cast<detail::LogData*>(log), static_cast<Level>(level), Str(text, text_n));
}
static void SUI_CALL c_LogClear(void* log) { if (log) detail::LogClear(*static_cast<detail::LogData*>(log)); }
static size_t SUI_CALL c_LogCount(void* log) { return log ? detail::LogCount(*static_cast<detail::LogData*>(log)) : 0; }
static void SUI_CALL c_LogText(void* log, api::StrFn out, void* out_user) {
    std::string r = log ? detail::LogText(*static_cast<detail::LogData*>(log)) : std::string();
    out(out_user, r.c_str(), r.size());
}
static void SUI_CALL c_LogView(const char* id, size_t id_n, void* log, float height, int showTime) { SUI_UI_ONLY(); height = Fin(height);
    if (log) detail::LogView(Str(id, id_n), *static_cast<detail::LogData*>(log), height, showTime != 0);
}

// Images and custom drawing
static void SUI_CALL c_LoadTexture(const char* path, size_t path_n, Texture* out) { SUI_UI_ONLY(); *out = LoadTexture(Str(path, path_n)); }
static void SUI_CALL c_LoadTextureFromMemory(const void* data, size_t size, Texture* out) { SUI_UI_ONLY(); *out = LoadTextureFromMemory(data, size); }
static void SUI_CALL c_CreateTexture(const void* rgba, int width, int height, Texture* out) { SUI_UI_ONLY(); *out = CreateTexture(rgba, width, height); }
static void SUI_CALL c_UpdateTexture(Texture* texture, const void* rgba) { SUI_UI_ONLY(); UpdateTexture(*texture, rgba); }
static void SUI_CALL c_FreeTexture(Texture* texture) { SUI_UI_ONLY(); FreeTexture(*texture); }
static void SUI_CALL c_Image(const Texture* texture, const Vec2* size) { SUI_UI_ONLY(); Vec2 size_ok = Fin(*size); size = &size_ok; Image(*texture, *size); }
static void SUI_CALL c_BeginCanvas(float height, int border, Canvas* out) { SUI_UI_ONLY(); height = Fin(height); *out = BeginCanvas(height, border != 0); }
static void SUI_CALL c_EndCanvas(void) { SUI_UI_ONLY(); EndCanvas(); }
static void SUI_CALL c_CanvasLine(const Canvas* self, const Vec2* a, const Vec2* b, const Color* c, float thickness) { SUI_UI_ONLY(); Vec2 a_ok = Fin(*a); a = &a_ok; Vec2 b_ok = Fin(*b); b = &b_ok; Color c_ok = Fin(*c); c = &c_ok; thickness = Fin(thickness); self->Line(*a, *b, *c, thickness); }
static void SUI_CALL c_CanvasFillRect(const Canvas* self, const Rect* r, const Color* c, float rounding) { SUI_UI_ONLY(); Rect r_ok = Fin(*r); r = &r_ok; Color c_ok = Fin(*c); c = &c_ok; rounding = Fin(rounding); self->FillRect(*r, *c, rounding); }
static void SUI_CALL c_CanvasStrokeRect(const Canvas* self, const Rect* r, const Color* c, float rounding, float thickness) { SUI_UI_ONLY(); Rect r_ok = Fin(*r); r = &r_ok; Color c_ok = Fin(*c); c = &c_ok; rounding = Fin(rounding); thickness = Fin(thickness); self->StrokeRect(*r, *c, rounding, thickness); }
static void SUI_CALL c_CanvasFillCircle(const Canvas* self, const Vec2* center, float radius, const Color* c) { SUI_UI_ONLY(); Vec2 center_ok = Fin(*center); center = &center_ok; radius = Fin(radius); Color c_ok = Fin(*c); c = &c_ok; self->FillCircle(*center, radius, *c); }
static void SUI_CALL c_CanvasStrokeCircle(const Canvas* self, const Vec2* center, float radius, const Color* c, float thickness) { SUI_UI_ONLY(); Vec2 center_ok = Fin(*center); center = &center_ok; radius = Fin(radius); Color c_ok = Fin(*c); c = &c_ok; thickness = Fin(thickness); self->StrokeCircle(*center, radius, *c, thickness); }
static void SUI_CALL c_CanvasPolyline(const Canvas* self, const Vec2* points, int points_count, const Color* c, float thickness, int closed) { SUI_UI_ONLY(); Color c_ok = Fin(*c); c = &c_ok; thickness = Fin(thickness); self->Polyline(ToVec2s(points, points_count), *c, thickness, closed != 0); }
static void SUI_CALL c_CanvasFillPolygon(const Canvas* self, const Vec2* points, int points_count, const Color* c) { SUI_UI_ONLY(); Color c_ok = Fin(*c); c = &c_ok; self->FillPolygon(ToVec2s(points, points_count), *c); }
static void SUI_CALL c_CanvasBezier(const Canvas* self, const Vec2* p0, const Vec2* p1, const Vec2* p2, const Vec2* p3, const Color* c, float thickness) { SUI_UI_ONLY(); Vec2 p0_ok = Fin(*p0); p0 = &p0_ok; Vec2 p1_ok = Fin(*p1); p1 = &p1_ok; Vec2 p2_ok = Fin(*p2); p2 = &p2_ok; Vec2 p3_ok = Fin(*p3); p3 = &p3_ok; Color c_ok = Fin(*c); c = &c_ok; thickness = Fin(thickness); self->Bezier(*p0, *p1, *p2, *p3, *c, thickness); }
static void SUI_CALL c_CanvasText(const Canvas* self, const Vec2* pos, const char* text, size_t text_n, const Color* c, int font) { SUI_UI_ONLY(); Vec2 pos_ok = Fin(*pos); pos = &pos_ok; Color c_ok = Fin(*c); c = &c_ok; self->Text(*pos, Str(text, text_n), *c, font); }
static void SUI_CALL c_CanvasImage(const Canvas* self, const Texture* texture, const Rect* dest, float opacity) { SUI_UI_ONLY(); Rect dest_ok = Fin(*dest); dest = &dest_ok; opacity = Fin(opacity); self->Image(*texture, *dest, opacity); }
static void SUI_CALL c_CanvasIconAt(const Canvas* self, int icon, const Rect* r, const Color* c) { SUI_UI_ONLY(); Rect r_ok = Fin(*r); r = &r_ok; Color c_ok = Fin(*c); c = &c_ok; self->IconAt(static_cast<Icon>(icon), *r, *c); }

// Popups, menus, modals, notifications
static void SUI_CALL c_OpenPopup(const char* id, size_t id_n) { SUI_UI_ONLY(); OpenPopup(Str(id, id_n)); }
static int SUI_CALL c_BeginPopup(const char* id, size_t id_n) { SUI_UI_ONLY(0); return BeginPopup(Str(id, id_n)) ? 1 : 0; }
static void SUI_CALL c_EndPopup(void) { SUI_UI_ONLY(); EndPopup(); }
static void SUI_CALL c_ClosePopup(void) { SUI_UI_ONLY(); ClosePopup(); }
static int SUI_CALL c_BeginContextMenu(void) { SUI_UI_ONLY(0); return BeginContextMenu() ? 1 : 0; }
static void SUI_CALL c_EndContextMenu(void) { SUI_UI_ONLY(); EndContextMenu(); }
static void SUI_CALL c_OpenModal(const char* title, size_t title_n) { SUI_UI_ONLY(); OpenModal(Str(title, title_n)); }
static int SUI_CALL c_BeginModal(const char* title, size_t title_n) { SUI_UI_ONLY(0); return BeginModal(Str(title, title_n)) ? 1 : 0; }
static void SUI_CALL c_EndModal(void) { SUI_UI_ONLY(); EndModal(); }
static void SUI_CALL c_CloseModal(void) { SUI_UI_ONLY(); CloseModal(); }
static int SUI_CALL c_BeginMenuBar(void) { SUI_UI_ONLY(0); return BeginMenuBar() ? 1 : 0; }
static void SUI_CALL c_EndMenuBar(void) { SUI_UI_ONLY(); EndMenuBar(); }
static int SUI_CALL c_BeginMenu(const char* label, size_t label_n) { SUI_UI_ONLY(0); return BeginMenu(Str(label, label_n)) ? 1 : 0; }
static void SUI_CALL c_EndMenu(void) { SUI_UI_ONLY(); EndMenu(); }
static int SUI_CALL c_MenuItem(const char* label, size_t label_n, const char* shortcut, size_t shortcut_n, int checked, int enabled) { SUI_UI_ONLY(0); return MenuItem(Str(label, label_n), Str(shortcut, shortcut_n), checked != 0, enabled != 0) ? 1 : 0; }
static int SUI_CALL c_MenuItemToggle(const char* label, size_t label_n, const char* shortcut, size_t shortcut_n, bool* toggle, int enabled) { SUI_UI_ONLY(0); return MenuItem(Str(label, label_n), Str(shortcut, shortcut_n), toggle, enabled != 0) ? 1 : 0; }
static void SUI_CALL c_MenuSeparator(void) { SUI_UI_ONLY(); MenuSeparator(); }
static void SUI_CALL c_Toast(const char* text, size_t text_n, int level, float seconds) { seconds = Fin(seconds); Toast(Str(text, text_n), static_cast<Level>(level), seconds); }

// Native dialogs and system helpers
static void SUI_CALL c_OpenFileDialog(const char* filter, size_t filter_n, const char* title, size_t title_n, api::StrFn out, void* out_user) {
    std::string r = OpenFileDialog(Str(filter, filter_n), Str(title, title_n));
    out(out_user, r.c_str(), r.size());
}
static void SUI_CALL c_OpenFilesDialog(const char* filter, size_t filter_n, const char* title, size_t title_n, api::StrFn add, void* add_user) {
    std::vector<std::string> r = OpenFilesDialog(Str(filter, filter_n), Str(title, title_n));
    for (size_t i = 0; i < r.size(); i++) add(add_user, r[i].c_str(), r[i].size());
}
static void SUI_CALL c_SaveFileDialog(const char* filter, size_t filter_n, const char* defaultName, size_t defaultName_n, const char* title, size_t title_n, api::StrFn out, void* out_user) {
    std::string r = SaveFileDialog(Str(filter, filter_n), Str(defaultName, defaultName_n), Str(title, title_n));
    out(out_user, r.c_str(), r.size());
}
static void SUI_CALL c_PickFolder(const char* title, size_t title_n, api::StrFn out, void* out_user) {
    std::string r = PickFolder(Str(title, title_n));
    out(out_user, r.c_str(), r.size());
}
static void SUI_CALL c_ShowMessage(const char* title, size_t title_n, const char* text, size_t text_n, int level) { ShowMessage(Str(title, title_n), Str(text, text_n), static_cast<Level>(level)); }
static int SUI_CALL c_Confirm(const char* title, size_t title_n, const char* text, size_t text_n) { return Confirm(Str(title, title_n), Str(text, text_n)) ? 1 : 0; }
static void SUI_CALL c_OpenURL(const char* urlOrPath, size_t urlOrPath_n) { OpenURL(Str(urlOrPath, urlOrPath_n)); }
static void SUI_CALL c_ShowInExplorer(const char* path, size_t path_n) { ShowInExplorer(Str(path, path_n)); }
static void SUI_CALL c_SetClipboard(const char* text, size_t text_n) { SetClipboard(Str(text, text_n)); }
static void SUI_CALL c_GetClipboard(api::StrFn out, void* out_user) {
    std::string r = GetClipboard();
    out(out_user, r.c_str(), r.size());
}
static void SUI_CALL c_DroppedFiles(api::StrFn add, void* add_user) { SUI_UI_ONLY();
    const std::vector<std::string>& r = DroppedFiles();
    for (size_t i = 0; i < r.size(); i++) add(add_user, r[i].c_str(), r[i].size());
}
static void SUI_CALL c_ToWide(const char* utf8, size_t utf8_n, api::WStrFn out, void* out_user) {
    std::wstring r = ToWide(Str(utf8, utf8_n));
    out(out_user, r.c_str(), r.size());
}
static void SUI_CALL c_ToUtf8(const wchar_t* wide, size_t wide_n, api::StrFn out, void* out_user) {
    std::string r = ToUtf8(wide ? std::wstring(wide, wide_n) : std::wstring());
    out(out_user, r.c_str(), r.size());
}

// Settings
static void SUI_CALL c_PersistBool(const char* key, size_t key_n, bool* value) { SUI_UI_ONLY(); Persist(Str(key, key_n), *value); }
static void SUI_CALL c_PersistInt(const char* key, size_t key_n, int* value) { SUI_UI_ONLY(); Persist(Str(key, key_n), *value); }
static void SUI_CALL c_PersistFloat(const char* key, size_t key_n, float* value) { SUI_UI_ONLY(); Persist(Str(key, key_n), *value); }
static void SUI_CALL c_PersistString(const char* key, size_t key_n, const char* value, size_t value_n, api::StrFn value_set, void* value_user) { SUI_UI_ONLY();
    std::string value_s(value ? value : "", value ? value_n : 0);
    Persist(Str(key, key_n), value_s);
    WriteBack(value_s, value, value_n, value_set, value_user);
}
static void SUI_CALL c_PersistColor(const char* key, size_t key_n, Color* value) { SUI_UI_ONLY(); Persist(Str(key, key_n), *value); }
static void SUI_CALL c_SaveSettings(void) { SaveSettings(); }
static void SUI_CALL c_SettingsPath(api::StrFn out, void* out_user) {
    std::string r = SettingsPath();
    out(out_user, r.c_str(), r.size());
}
static void SUI_CALL c_HideWindow(void) { HideWindow(); }
static void SUI_CALL c_ToggleWindow(void) { ToggleWindow(); }
static void SUI_CALL c_FocusNext(void) { SUI_UI_ONLY(); FocusNext(); }
static void SUI_CALL c_TaskbarProgress(float fraction, int level) { SUI_UI_ONLY(); TaskbarProgress(fraction, static_cast<Level>(level)); }
static void SUI_CALL c_Beep(int level) { Beep(static_cast<Level>(level)); }
static void SUI_CALL c_CommandLine(api::StrFn add, void* add_user) {
    std::vector<std::string> r = CommandLine();
    for (size_t i = 0; i < r.size(); i++) add(add_user, r[i].c_str(), r[i].size());
}

static api::Table MakeTable() {
    api::Table t = {};
    t.size = (uint32_t)sizeof(t);
    t.versionNum = SIMPLYUI_VERSION_NUM;
    t.version = SIMPLYUI_VERSION;
    t.Run                   = c_Run;
    t.Quit                  = c_Quit;
    t.CloseRequested        = c_CloseRequested;
    t.CancelClose           = c_CancelClose;
    t.SetTitle              = c_SetTitle;
    t.SetWindowSize         = c_SetWindowSize;
    t.WindowSize            = c_WindowSize;
    t.SetTopmost            = c_SetTopmost;
    t.IsTopmost             = c_IsTopmost;
    t.Minimize              = c_Minimize;
    t.BringToFront          = c_BringToFront;
    t.NativeWindow          = c_NativeWindow;
    t.DpiScale              = c_DpiScale;
    t.Time                  = c_Time;
    t.DeltaTime             = c_DeltaTime;
    t.FrameCount            = c_FrameCount;
    t.RequestRedraw         = c_RequestRedraw;
    t.RedrawIn              = c_RedrawIn;
    t.Wake                  = c_Wake;
    t.GetStyle              = c_GetStyle;
    t.SetTheme              = c_SetTheme;
    t.GetTheme              = c_GetTheme;
    t.IsDark                = c_IsDark;
    t.SetAccent             = c_SetAccent;
    t.SystemAccent          = c_SystemAccent;
    t.AddFont               = c_AddFont;
    t.PushFont              = c_PushFont;
    t.PopFont               = c_PopFont;
    t.TextWidth             = c_TextWidth;
    t.LineHeight            = c_LineHeight;
    t.MousePos              = c_MousePos;
    t.MouseDown             = c_MouseDown;
    t.MouseClicked          = c_MouseClicked;
    t.MouseReleased         = c_MouseReleased;
    t.MouseDoubleClicked    = c_MouseDoubleClicked;
    t.MouseWheel            = c_MouseWheel;
    t.KeyDown               = c_KeyDown;
    t.KeyPressed            = c_KeyPressed;
    t.KeyReleased           = c_KeyReleased;
    t.CtrlDown              = c_CtrlDown;
    t.ShiftDown             = c_ShiftDown;
    t.AltDown               = c_AltDown;
    t.Shortcut              = c_Shortcut;
    t.GlobalHotkey          = c_GlobalHotkey;
    t.IsEditingText         = c_IsEditingText;
    t.SameLine              = c_SameLine;
    t.NewLine               = c_NewLine;
    t.Spacing               = c_Spacing;
    t.Spacer                = c_Spacer;
    t.Indent                = c_Indent;
    t.Unindent              = c_Unindent;
    t.NextWidth             = c_NextWidth;
    t.AvailableWidth        = c_AvailableWidth;
    t.BeginRow              = c_BeginRow;
    t.BeginRowWeights       = c_BeginRowWeights;
    t.EndRow                = c_EndRow;
    t.BeginColumns          = c_BeginColumns;
    t.BeginColumnsWeights   = c_BeginColumnsWeights;
    t.NextColumn            = c_NextColumn;
    t.EndColumns            = c_EndColumns;
    t.BeginHorizontal       = c_BeginHorizontal;
    t.EndHorizontal         = c_EndHorizontal;
    t.BeginCard             = c_BeginCard;
    t.EndCard               = c_EndCard;
    t.BeginChild            = c_BeginChild;
    t.EndChild              = c_EndChild;
    t.BeginFooter           = c_BeginFooter;
    t.EndFooter             = c_EndFooter;
    t.BeginDisabled         = c_BeginDisabled;
    t.EndDisabled           = c_EndDisabled;
    t.PushIDStr             = c_PushIDStr;
    t.PushIDInt             = c_PushIDInt;
    t.PushIDPtr             = c_PushIDPtr;
    t.PopID                 = c_PopID;
    t.GetID                 = c_GetID;
    t.LastItem              = c_LastItem;
    t.Tooltip               = c_Tooltip;
    t.Text                  = c_Text;
    t.TextDim               = c_TextDim;
    t.TextColored           = c_TextColored;
    t.TextWrapped           = c_TextWrapped;
    t.Heading               = c_Heading;
    t.Title                 = c_Title;
    t.LabelText             = c_LabelText;
    t.BulletText            = c_BulletText;
    t.Link                  = c_Link;
    t.BadgeColor            = c_BadgeColor;
    t.BadgeLevel            = c_BadgeLevel;
    t.Separator             = c_Separator;
    t.SeparatorText         = c_SeparatorText;
    t.Button                = c_Button;
    t.ButtonIcon            = c_ButtonIcon;
    t.PrimaryButton         = c_PrimaryButton;
    t.DangerButton          = c_DangerButton;
    t.IconButton            = c_IconButton;
    t.Checkbox              = c_Checkbox;
    t.Toggle                = c_Toggle;
    t.RadioButton           = c_RadioButton;
    t.Selectable            = c_Selectable;
    t.SliderFloat           = c_SliderFloat;
    t.SliderInt             = c_SliderInt;
    t.DragFloat             = c_DragFloat;
    t.DragInt               = c_DragInt;
    t.InputInt              = c_InputInt;
    t.InputFloat            = c_InputFloat;
    t.InputText             = c_InputText;
    t.InputMultiline        = c_InputMultiline;
    t.SearchBox             = c_SearchBox;
    t.Combo                 = c_Combo;
    t.ListBox               = c_ListBox;
    t.Tabs                  = c_Tabs;
    t.Segmented             = c_Segmented;
    t.CollapsingHeader      = c_CollapsingHeader;
    t.TreeNode              = c_TreeNode;
    t.TreePop               = c_TreePop;
    t.ProgressBar           = c_ProgressBar;
    t.Spinner               = c_Spinner;
    t.ColorEdit             = c_ColorEdit;
    t.PlotLines             = c_PlotLines;
    t.PlotBars              = c_PlotBars;
    t.DropZone              = c_DropZone;
    t.BeginTable            = c_BeginTable;
    t.TableNextRow          = c_TableNextRow;
    t.TableNextCell         = c_TableNextCell;
    t.EndTable              = c_EndTable;
    t.NextTableWeights      = c_NextTableWeights;
    t.TableSorting          = c_TableSorting;
    t.LogCreate             = c_LogCreate;
    t.LogDestroy            = c_LogDestroy;
    t.LogAdd                = c_LogAdd;
    t.LogClear              = c_LogClear;
    t.LogCount              = c_LogCount;
    t.LogText               = c_LogText;
    t.LogView               = c_LogView;
    t.LoadTexture           = c_LoadTexture;
    t.LoadTextureFromMemory = c_LoadTextureFromMemory;
    t.CreateTexture         = c_CreateTexture;
    t.UpdateTexture         = c_UpdateTexture;
    t.FreeTexture           = c_FreeTexture;
    t.Image                 = c_Image;
    t.BeginCanvas           = c_BeginCanvas;
    t.EndCanvas             = c_EndCanvas;
    t.CanvasLine            = c_CanvasLine;
    t.CanvasFillRect        = c_CanvasFillRect;
    t.CanvasStrokeRect      = c_CanvasStrokeRect;
    t.CanvasFillCircle      = c_CanvasFillCircle;
    t.CanvasStrokeCircle    = c_CanvasStrokeCircle;
    t.CanvasPolyline        = c_CanvasPolyline;
    t.CanvasFillPolygon     = c_CanvasFillPolygon;
    t.CanvasBezier          = c_CanvasBezier;
    t.CanvasText            = c_CanvasText;
    t.CanvasImage           = c_CanvasImage;
    t.CanvasIconAt          = c_CanvasIconAt;
    t.OpenPopup             = c_OpenPopup;
    t.BeginPopup            = c_BeginPopup;
    t.EndPopup              = c_EndPopup;
    t.ClosePopup            = c_ClosePopup;
    t.BeginContextMenu      = c_BeginContextMenu;
    t.EndContextMenu        = c_EndContextMenu;
    t.OpenModal             = c_OpenModal;
    t.BeginModal            = c_BeginModal;
    t.EndModal              = c_EndModal;
    t.CloseModal            = c_CloseModal;
    t.BeginMenuBar          = c_BeginMenuBar;
    t.EndMenuBar            = c_EndMenuBar;
    t.BeginMenu             = c_BeginMenu;
    t.EndMenu               = c_EndMenu;
    t.MenuItem              = c_MenuItem;
    t.MenuItemToggle        = c_MenuItemToggle;
    t.MenuSeparator         = c_MenuSeparator;
    t.Toast                 = c_Toast;
    t.OpenFileDialog        = c_OpenFileDialog;
    t.OpenFilesDialog       = c_OpenFilesDialog;
    t.SaveFileDialog        = c_SaveFileDialog;
    t.PickFolder            = c_PickFolder;
    t.ShowMessage           = c_ShowMessage;
    t.Confirm               = c_Confirm;
    t.OpenURL               = c_OpenURL;
    t.ShowInExplorer        = c_ShowInExplorer;
    t.SetClipboard          = c_SetClipboard;
    t.GetClipboard          = c_GetClipboard;
    t.DroppedFiles          = c_DroppedFiles;
    t.ToWide                = c_ToWide;
    t.ToUtf8                = c_ToUtf8;
    t.PersistBool           = c_PersistBool;
    t.PersistInt            = c_PersistInt;
    t.PersistFloat          = c_PersistFloat;
    t.PersistString         = c_PersistString;
    t.PersistColor          = c_PersistColor;
    t.SaveSettings          = c_SaveSettings;
    t.SettingsPath          = c_SettingsPath;
    t.HideWindow            = c_HideWindow;
    t.ToggleWindow          = c_ToggleWindow;
    t.FocusNext             = c_FocusNext;
    t.TaskbarProgress       = c_TaskbarProgress;
    t.CommandLine           = c_CommandLine;
    t.Beep                  = c_Beep;
    return t;
}

} // namespace abi
} // namespace sui

// Returns the function table, or nullptr when the program was built with a
// simplyui.h this DLL cannot serve.
extern "C" __declspec(dllexport) const sui::api::Table* SUI_CALL sui_GetApi(int abiVersion, uint32_t tableSize,
                                                                           uint32_t styleSize, uint32_t itemInfoSize,
                                                                           uint32_t canvasSize) {
    static const sui::api::Table table = sui::abi::MakeTable();
    if (abiVersion != sui::api::kAbiVersion || tableSize > sizeof(table) || styleSize != sizeof(sui::Style) ||
        itemInfoSize != sizeof(sui::ItemInfo) || canvasSize != sizeof(sui::Canvas))
        return nullptr;
    return &table;
}

extern "C" __declspec(dllexport) const char* SUI_CALL sui_Version(void) { return SIMPLYUI_VERSION; }

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
