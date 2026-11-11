#define WINVER 0x0601
#define _WIN32_WINNT 0x0601
#define UNICODE
#define _UNICODE
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#define _CRT_SECURE_NO_WARNINGS

#include <windows.h>
#include <windowsx.h>
#include <shellapi.h>
#include <richedit.h>
#include <commctrl.h>
#include <dwmapi.h>
#include <uxtheme.h>

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <sstream>
#include <string>
#include <unordered_set>
#include <vector>

#pragma comment(lib, "comctl32.lib")
#pragma comment(lib, "ole32.lib")
#pragma comment(lib, "uuid.lib")
#pragma comment(lib, "dwmapi.lib")
#pragma comment(lib, "uxtheme.lib")
#pragma comment(lib, "msimg32.lib")

namespace {

constexpr UINT WM_TRAY = WM_APP + 1;
constexpr UINT WM_SHOW_EXISTING = WM_APP + 2;

constexpr int ID_NEW = 1;
constexpr int ID_SHOW = 2;
constexpr int ID_HIDE = 3;
constexpr int ID_EXIT = 4;
constexpr int ID_BOLD = 10;
constexpr int ID_CLOSE_BTN = 20;
constexpr int ID_COLOR0 = 40;
constexpr int ID_FILTER_ALL = 100;
constexpr int ID_FILTER_BASE = 101;
constexpr int ID_NOTE_LIST = 200;
constexpr int ID_SHOW_NEW = 201;
constexpr int ID_SHOW_OPEN = 202;
constexpr int ID_SHOW_DELETE = 203;
constexpr int IDC_EDIT = 300;
constexpr int IDC_CHECKHOST = 301;
constexpr int ID_ROW_CHECK0 = 2000;
constexpr int ID_ROW_EDIT0 = 3000;

constexpr int kTitleH = 34;
constexpr int kPad = 4;
constexpr int kColorBarH = 34;
constexpr int kMinW = 220;
constexpr int kMinH = 180;
constexpr int kCloseSize = 22;
constexpr int kEdge = 5;
constexpr int kScrollW = 4;
constexpr LONG kBodyPt = 16;
constexpr UINT_PTR kSaveTimer = 42;
constexpr UINT_PTR kScrollTimer = 43;
constexpr UINT_PTR kCheckLayoutTimer = 45;
constexpr int kColorCount = 5;

enum class NoteMode { Text, Checklist };

struct NotePalette {
    COLORREF top;
    COLORREF bottom;
    COLORREF title;
    COLORREF text;
    COLORREF thumb;
    COLORREF dot;
};

// yellow (default), red, green, blue, gray — full-window gradients
const NotePalette kPalettes[kColorCount] = {
    { RGB(255, 242, 170), RGB(255, 210, 85),  RGB(255, 236, 150), RGB(40, 30, 8),   RGB(200, 160, 50), RGB(255, 200, 40) },
    { RGB(255, 220, 215), RGB(245, 155, 145), RGB(255, 210, 205), RGB(60, 20, 16),  RGB(200, 100, 90), RGB(230, 80, 70) },
    { RGB(225, 245, 215), RGB(155, 205, 140), RGB(215, 238, 205), RGB(20, 45, 20),  RGB(100, 160, 90), RGB(80, 170, 70) },
    { RGB(215, 230, 250), RGB(145, 175, 225), RGB(205, 222, 248), RGB(16, 30, 60),  RGB(90, 130, 200), RGB(70, 120, 210) },
    { RGB(240, 240, 240), RGB(185, 185, 185), RGB(232, 232, 232), RGB(30, 30, 30),  RGB(140, 140, 140), RGB(120, 120, 120) },
};

struct CheckItem {
    std::wstring text;
    bool done = false;
    HWND row{};
    HWND check{};
    HWND edit{};
};

struct Note {
    std::wstring id;
    std::wstring path;
    HWND hwnd{};
    HWND edit{};
    HWND closeBtn{};
    HWND checkHost{};   // clipped viewport
    HWND checkBody{};   // tall content panel (smooth scroll via one MoveWindow)
    HWND scrollRail{};
    WNDPROC editOld{};
    NoteMode mode = NoteMode::Text;
    std::vector<CheckItem> items;
    std::vector<std::wstring> tags;
    bool hidden = false;
    int wheelAcc = 0;
    int colorIdx = 0;
    int checkContentH = 0; // pixel height of checklist body
    HBRUSH paperBrush{};
};

HINSTANCE g_inst;
HWND g_tray;
HWND g_browser;
HICON g_icon;
HBRUSH g_paper;
HBRUSH g_titleBrush;
HBRUSH g_scrollThumb;
HFONT g_font;
HFONT g_fontBold;
HFONT g_uiFont;
HACCEL g_accel;
std::vector<Note> g_notes;
std::vector<std::wstring> g_allTags;
std::wstring g_activeFilter;
bool g_suppressSave = false;
bool g_busy = false;
bool g_quitting = false;
UINT g_taskbarCreated = 0;
int g_nextId = 1;
int g_bodyFontDpi = 0;
std::wstring g_exeDir;

// #region agent log
inline void AgentLog(const char* hyp, const char* loc, const char* msg, const std::string& dataJson = "{}") {
    FILE* f = fopen("C:\\Users\\micha\\Desktop\\Notes\\debug-ddd9ed.log", "a");
    if (!f) return;
    long long ts = (long long)GetTickCount64();
    fprintf(f,
        "{\"sessionId\":\"ddd9ed\",\"hypothesisId\":\"%s\",\"location\":\"%s\",\"message\":\"%s\",\"data\":%s,\"timestamp\":%lld}\n",
        hyp, loc, msg, dataJson.c_str(), ts);
    fclose(f);
}
static int g_dbgIsBoldCalls = 0;
static int g_dbgSerializeCalls = 0;
static int g_dbgEnChange = 0;
// #endregion

std::wstring Utf8ToWide(const std::string& s) {
    if (s.empty()) return {};
    int n = MultiByteToWideChar(CP_UTF8, 0, s.data(), (int)s.size(), nullptr, 0);
    std::wstring w(n, 0);
    MultiByteToWideChar(CP_UTF8, 0, s.data(), (int)s.size(), &w[0], n);
    return w;
}

std::string WideToUtf8(const std::wstring& w) {
    if (w.empty()) return {};
    int n = WideCharToMultiByte(CP_UTF8, 0, w.data(), (int)w.size(), nullptr, 0, nullptr, nullptr);
    std::string s(n, 0);
    WideCharToMultiByte(CP_UTF8, 0, w.data(), (int)w.size(), &s[0], n, nullptr, nullptr);
    return s;
}

std::wstring ToLower(std::wstring s) {
    for (auto& c : s) c = (wchar_t)towlower(c);
    return s;
}

std::wstring ExeDir() {
    wchar_t buf[MAX_PATH]{};
    GetModuleFileNameW(nullptr, buf, MAX_PATH);
    std::wstring path = buf;
    size_t slash = path.find_last_of(L"\\/");
    if (slash != std::wstring::npos) path.resize(slash + 1);
    return path;
}

std::wstring NewNoteId() {
    for (;;) {
        wchar_t name[64];
        swprintf(name, 64, L"note-%03d", g_nextId++);
        if (GetFileAttributesW((g_exeDir + name + L".md").c_str()) == INVALID_FILE_ATTRIBUTES)
            return name;
    }
}

std::wstring CtrlText(HWND h) {
    int len = GetWindowTextLengthW(h);
    if (len <= 0) return {};
    std::wstring t((size_t)len + 1, 0);
    GetWindowTextW(h, &t[0], len + 1);
    t.resize((size_t)len);
    return t;
}

Note* FindNote(HWND hwnd) {
    for (Note& n : g_notes) if (n.hwnd == hwnd) return &n;
    return nullptr;
}

Note* FindNoteByEdit(HWND edit) {
    for (Note& n : g_notes) if (n.edit == edit) return &n;
    return nullptr;
}

Note* FindNoteByRowEdit(HWND edit) {
    for (Note& n : g_notes)
        for (auto& it : n.items)
            if (it.edit == edit) return &n;
    return nullptr;
}

void RefreshBrowser();
void SaveNote(Note& note);
void ScheduleSave(Note& note);
void LayoutNote(HWND hwnd);
void LayoutChecklist(Note& note, bool refreshRail = true);
void EnsureCheckBody(Note& note);
void ApplyChecklistScroll(Note& note);
void ScheduleScrollRailRefresh(Note& note);
void BuildChecklistUi(Note& note);
void SyncChecklistModel(Note& note);
void ConvertNoteToChecklist(Note& note);
std::wstring SerializeNote(Note& note);
void ApplyDefaultCharFormat(HWND edit);
void SyncEditBgToNoteGradient(HWND edit, const Note& note);
void PaintNoteGradientSlice(HDC dc, HWND hwnd, const Note& note);
bool IsRichEditHwnd(HWND hwnd);
void PaintTransparentRichEdit(HWND edit, HDC hdc, Note* note);
bool ForwardMouseToEdit(HWND parent, HWND edit, UINT msg, WPARAM wp, LPARAM lp);
void RefreshScrollRail(Note& note);
HWND ScrollTarget(const Note& note);

std::vector<std::wstring> ParseTags(const std::wstring& md) {
    std::vector<std::wstring> tags;
    size_t end = md.find(L'\n');
    std::wstring first = end == std::wstring::npos ? md : md.substr(0, end);
    size_t i = 0;
    while (i < first.size()) {
        while (i < first.size() && iswspace(first[i])) ++i;
        if (i >= first.size()) break;
        if (first[i] == L'#') {
            size_t start = ++i;
            while (i < first.size() && !iswspace(first[i]) && first[i] != L'#') ++i;
            if (i > start) {
                auto tag = ToLower(first.substr(start, i - start));
                if (std::find(tags.begin(), tags.end(), tag) == tags.end()) tags.push_back(tag);
            }
        } else break;
    }
    return tags;
}

void RebuildTagIndex() {
    std::unordered_set<std::wstring> seen;
    g_allTags.clear();
    for (const Note& n : g_notes)
        for (const auto& t : n.tags)
            if (seen.insert(t).second) g_allTags.push_back(t);
    std::sort(g_allTags.begin(), g_allTags.end());
}

int WindowDpi(HWND hwnd) {
    using Fn = UINT(WINAPI*)(HWND);
    static Fn fn = reinterpret_cast<Fn>(GetProcAddress(GetModuleHandleW(L"user32"), "GetDpiForWindow"));
    if (fn && hwnd) return (int)fn(hwnd);
    HDC dc = GetDC(hwnd);
    int dpi = dc ? GetDeviceCaps(dc, LOGPIXELSY) : 96;
    if (dc) ReleaseDC(hwnd, dc);
    return dpi > 0 ? dpi : 96;
}

int BodyFontPx(int dpi) {
    return MulDiv((int)kBodyPt, dpi, 72);
}

LONG BodyTwips(int dpi) {
    // pixels → twips so Rich Edit matches HFONT pixel height
    return MulDiv(BodyFontPx(dpi), 1440, dpi);
}

void RecreateBodyFont(int dpi) {
    const int wantPx = BodyFontPx(dpi);
    if (!g_font || g_bodyFontDpi != dpi) {
        HFONT neu = CreateFontW(-wantPx, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
            DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
            DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");
        HFONT neuB = CreateFontW(-wantPx, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE,
            DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
            DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");
        HFONT old = g_font;
        HFONT oldB = g_fontBold;
        g_font = neu;
        g_fontBold = neuB;
        g_bodyFontDpi = dpi;
        if (old) DeleteObject(old);
        if (oldB) DeleteObject(oldB);
    }
    for (Note& n : g_notes) {
        if (n.edit) {
            SendMessageW(n.edit, WM_SETFONT, (WPARAM)g_font, TRUE);
            ApplyDefaultCharFormat(n.edit);
        }
        for (auto& it : n.items) {
            if (it.edit) {
                SendMessageW(it.edit, WM_SETFONT, (WPARAM)g_font, TRUE);
                if (IsRichEditHwnd(it.edit)) ApplyDefaultCharFormat(it.edit);
            }
        }
    }
    // #region agent log
    {
        char data[160];
        snprintf(data, sizeof(data),
            "{\"dpi\":%d,\"wantPx\":%d,\"notes\":%d,\"run\":\"post-fix7\"}",
            dpi, wantPx, (int)g_notes.size());
        AgentLog("A", "RecreateBodyFont", "font_applied", data);
    }
    // #endregion
}

void ApplyDefaultCharFormat(HWND edit) {
    HWND root = GetAncestor(edit, GA_ROOT);
    Note* note = root ? FindNote(root) : FindNoteByEdit(edit);
    if (!note) note = FindNoteByRowEdit(edit);
    int dpi = WindowDpi(note && note->hwnd ? note->hwnd : edit);
    const NotePalette& pal = kPalettes[note ? (std::max)(0, (std::min)(note->colorIdx, kColorCount - 1)) : 0];
    CHARFORMAT2W cf{};
    cf.cbSize = sizeof(cf);
    cf.dwMask = CFM_COLOR | CFM_FACE | CFM_SIZE | CFM_BOLD;
    cf.dwEffects = 0;
    cf.crTextColor = pal.text;
    cf.yHeight = BodyTwips(dpi);
    lstrcpynW(cf.szFaceName, L"Segoe UI", LF_FACESIZE);
    SendMessageW(edit, EM_SETCHARFORMAT, SCF_ALL, (LPARAM)&cf);
    // Background is painted by us (gradient); RichEdit default fill is never shown.
    SendMessageW(edit, EM_SETBKGNDCOLOR, 0, pal.top);
}

void FillGradientRect(HDC dc, const RECT& rc, COLORREF top, COLORREF bottom) {
    TRIVERTEX verts[2]{};
    verts[0].x = rc.left;
    verts[0].y = rc.top;
    verts[0].Red = (COLOR16)(GetRValue(top) << 8);
    verts[0].Green = (COLOR16)(GetGValue(top) << 8);
    verts[0].Blue = (COLOR16)(GetBValue(top) << 8);
    verts[1].x = rc.right;
    verts[1].y = rc.bottom;
    verts[1].Red = (COLOR16)(GetRValue(bottom) << 8);
    verts[1].Green = (COLOR16)(GetGValue(bottom) << 8);
    verts[1].Blue = (COLOR16)(GetBValue(bottom) << 8);
    GRADIENT_RECT gr{0, 1};
    GradientFill(dc, verts, 2, &gr, 1, GRADIENT_FILL_RECT_V);
}

COLORREF LerpColor(COLORREF a, COLORREF b, float t) {
    if (t < 0.f) t = 0.f;
    if (t > 1.f) t = 1.f;
    auto ch = [&](int ca, int cb) { return (int)(ca + (cb - ca) * t + 0.5f); };
    return RGB(ch(GetRValue(a), GetRValue(b)), ch(GetGValue(a), GetGValue(b)), ch(GetBValue(a), GetBValue(b)));
}

const NotePalette& NotePal(const Note& note);
bool IsRichEditHwnd(HWND hwnd);

COLORREF NoteColorAtY(const Note& note, int yInNote) {
    if (!note.hwnd) return NotePal(note).top;
    RECT nr{};
    GetClientRect(note.hwnd, &nr);
    int h = (std::max)(1, (int)nr.bottom);
    const NotePalette& pal = NotePal(note);
    return LerpColor(pal.top, pal.bottom, (float)yInNote / (float)h);
}

// Paint the same full-note gradient, clipped to this child (no solid "card" over the fade).
void PaintNoteGradientSlice(HDC dc, HWND hwnd, const Note& note) {
    RECT cr{};
    GetClientRect(hwnd, &cr);
    if (!note.hwnd) {
        const NotePalette& pal = NotePal(note);
        FillGradientRect(dc, cr, pal.top, pal.bottom);
        return;
    }
    RECT nr{};
    GetClientRect(note.hwnd, &nr);
    int noteH = (std::max)(1, (int)nr.bottom);
    POINT tl{0, 0};
    POINT br{cr.right, cr.bottom};
    MapWindowPoints(hwnd, note.hwnd, &tl, 1);
    MapWindowPoints(hwnd, note.hwnd, &br, 1);
    const NotePalette& pal = NotePal(note);
    COLORREF c0 = LerpColor(pal.top, pal.bottom, (float)tl.y / (float)noteH);
    COLORREF c1 = LerpColor(pal.top, pal.bottom, (float)br.y / (float)noteH);
    FillGradientRect(dc, cr, c0, c1);
}

void SyncEditBgToNoteGradient(HWND edit, const Note& note) {
    if (!edit || !note.hwnd || !IsWindow(edit)) return;
    // Keep a dummy bk color; real pixels come from PaintTransparentRichEdit.
    SendMessageW(edit, EM_SETBKGNDCOLOR, 0, NotePal(note).top);
    InvalidateRect(edit, nullptr, FALSE);
}

bool ForwardMouseToEdit(HWND parent, HWND edit, UINT msg, WPARAM wp, LPARAM lp) {
    if (!parent || !edit || !IsWindow(edit)) return false;
    POINT pt{GET_X_LPARAM(lp), GET_Y_LPARAM(lp)};
    RECT er{};
    GetWindowRect(edit, &er);
    MapWindowPoints(nullptr, parent, (POINT*)&er, 2);
    if (!PtInRect(&er, pt)) return false;
    POINT ep = pt;
    MapWindowPoints(parent, edit, &ep, 1);
    if (msg == WM_LBUTTONDOWN || msg == WM_LBUTTONDBLCLK)
        SetFocus(edit);
    SendMessageW(edit, msg, wp, MAKELPARAM(ep.x, ep.y));
    return true;
}

int RichEditLineHeight(HWND edit, int dpi) {
    HDC dc = GetDC(edit);
    HFONT old = g_font ? (HFONT)SelectObject(dc, g_font) : nullptr;
    TEXTMETRICW tm{};
    GetTextMetricsW(dc, &tm);
    if (old) SelectObject(dc, old);
    ReleaseDC(edit, dc);
    int h = tm.tmHeight + tm.tmExternalLeading;
    return h > 0 ? h : BodyFontPx(dpi);
}

void PaintTransparentRichEdit(HWND edit, HDC hdc, Note* note) {
    RECT rc{};
    GetClientRect(edit, &rc);
    if (note) PaintNoteGradientSlice(hdc, edit, *note);
    else FillRect(hdc, &rc, g_paper);

    int dpi = WindowDpi(note && note->hwnd ? note->hwnd : edit);
    int lineH = RichEditLineHeight(edit, dpi);

    POINT sp{};
    SendMessageW(edit, EM_GETSCROLLPOS, 0, (LPARAM)&sp);

    // Selection highlight (FORMATRANGE does not draw the caret selection).
    CHARRANGE sel{};
    SendMessageW(edit, EM_EXGETSEL, 0, (LPARAM)&sel);
    if (sel.cpMin != sel.cpMax) {
        LONG cp = (std::min)(sel.cpMin, sel.cpMax);
        LONG end = (std::max)(sel.cpMin, sel.cpMax);
        HBRUSH br = CreateSolidBrush(RGB(150, 185, 230));
        while (cp < end) {
            int line = (int)SendMessageW(edit, EM_LINEFROMCHAR, (WPARAM)cp, 0);
            int lineIdx = (int)SendMessageW(edit, EM_LINEINDEX, (WPARAM)line, 0);
            int lineLen = (int)SendMessageW(edit, EM_LINELENGTH, (WPARAM)cp, 0);
            LONG lineEnd = (LONG)lineIdx + lineLen;
            LONG segEnd = (std::min)(end, lineEnd);
            LRESULT xy0 = SendMessageW(edit, EM_POSFROMCHAR, (WPARAM)cp, 0);
            int x0 = (short)LOWORD(xy0) - sp.x;
            int y0 = (short)HIWORD(xy0) - sp.y;
            int x1 = rc.right;
            if (segEnd < GetWindowTextLengthW(edit)) {
                LRESULT xy1 = SendMessageW(edit, EM_POSFROMCHAR, (WPARAM)segEnd, 0);
                if (xy1 != (LRESULT)-1) {
                    x1 = (short)LOWORD(xy1) - sp.x;
                    if (x1 <= x0) x1 = rc.right;
                }
            }
            RECT sr{x0, y0, x1, y0 + lineH};
            FillRect(hdc, &sr, br);
            if (segEnd <= cp) break;
            cp = segEnd;
            if (cp < end && cp == lineEnd) {
                // Advance past CR/LF
                wchar_t buf[3]{};
                TEXTRANGEW tr{};
                tr.chrg.cpMin = cp;
                tr.chrg.cpMax = (std::min)(cp + 2, end);
                tr.lpstrText = buf;
                SendMessageW(edit, EM_GETTEXTRANGE, 0, (LPARAM)&tr);
                if (buf[0] == L'\r') ++cp;
                if (buf[0] == L'\n' || buf[1] == L'\n') ++cp;
            }
        }
        DeleteObject(br);
    }

    // Text (and bold runs) on top of the gradient — no solid RichEdit fill.
    FORMATRANGE fr{};
    fr.hdc = hdc;
    fr.hdcTarget = hdc;
    int pageR = (std::max)(1, (int)rc.right + (int)sp.x);
    int pageB = (std::max)(1, (int)rc.bottom + (int)sp.y);
    fr.rc.left = MulDiv(sp.x, 1440, dpi);
    fr.rc.top = MulDiv(sp.y, 1440, dpi);
    fr.rc.right = MulDiv(pageR, 1440, dpi);
    fr.rc.bottom = MulDiv(pageB, 1440, dpi);
    fr.rcPage = fr.rc;
    fr.chrg.cpMin = 0;
    fr.chrg.cpMax = -1;
    SendMessageW(edit, EM_FORMATRANGE, TRUE, (LPARAM)&fr);
    SendMessageW(edit, EM_FORMATRANGE, FALSE, 0);

    if (GetFocus() == edit) {
        CHARRANGE cr{};
        SendMessageW(edit, EM_EXGETSEL, 0, (LPARAM)&cr);
        LONG cp = cr.cpMax;
        int cx = 0, cy = 0;
        int len = GetWindowTextLengthW(edit);
        if (len == 0) {
            cx = 0;
            cy = 0;
        } else if (cp >= len) {
            LRESULT xy = SendMessageW(edit, EM_POSFROMCHAR, (WPARAM)(len - 1), 0);
            cx = (short)LOWORD(xy);
            cy = (short)HIWORD(xy);
            SIZE sz{};
            HDC mdc = GetDC(edit);
            HFONT old = g_font ? (HFONT)SelectObject(mdc, g_font) : nullptr;
            wchar_t last[2] = {0, 0};
            TEXTRANGEW tr{};
            tr.chrg.cpMin = len - 1;
            tr.chrg.cpMax = len;
            tr.lpstrText = last;
            SendMessageW(edit, EM_GETTEXTRANGE, 0, (LPARAM)&tr);
            if (last[0] && last[0] != L'\r' && last[0] != L'\n')
                GetTextExtentPoint32W(mdc, last, 1, &sz);
            if (old) SelectObject(mdc, old);
            ReleaseDC(edit, mdc);
            cx += sz.cx;
        } else {
            LRESULT xy = SendMessageW(edit, EM_POSFROMCHAR, (WPARAM)cp, 0);
            cx = (short)LOWORD(xy);
            cy = (short)HIWORD(xy);
        }
        cx -= sp.x;
        cy -= sp.y;
        if (!GetPropW(edit, L"ownCaret")) {
            CreateCaret(edit, nullptr, (std::max)(1, MulDiv(1, dpi, 96)), lineH);
            SetPropW(edit, L"ownCaret", (HANDLE)1);
        }
        SetCaretPos(cx, cy);
        ShowCaret(edit);
    }
}

const NotePalette& NotePal(const Note& note) {
    int i = note.colorIdx;
    if (i < 0 || i >= kColorCount) i = 0;
    return kPalettes[i];
}

void ApplyNoteTheme(Note& note) {
    const NotePalette& pal = NotePal(note);
    if (note.paperBrush) { DeleteObject(note.paperBrush); note.paperBrush = nullptr; }
    note.paperBrush = CreateSolidBrush(pal.top);
    auto themeEdit = [&](HWND edit) {
        if (!edit) return;
        SyncEditBgToNoteGradient(edit, note);
        CHARFORMAT2W def{};
        def.cbSize = sizeof(def);
        def.dwMask = CFM_COLOR;
        def.crTextColor = pal.text;
        SendMessageW(edit, EM_SETCHARFORMAT, SCF_DEFAULT, (LPARAM)&def);
    };
    themeEdit(note.edit);
    for (auto& it : note.items) themeEdit(it.edit);
    if (note.hwnd) {
        InvalidateRect(note.hwnd, nullptr, TRUE);
        if (note.closeBtn) InvalidateRect(note.closeBtn, nullptr, TRUE);
        if (note.checkHost) InvalidateRect(note.checkHost, nullptr, TRUE);
        if (note.checkBody) InvalidateRect(note.checkBody, nullptr, TRUE);
        if (note.scrollRail) InvalidateRect(note.scrollRail, nullptr, TRUE);
    }
}

void CollapseCaret(HWND edit) {
    CHARRANGE cr{};
    SendMessageW(edit, EM_EXGETSEL, 0, (LPARAM)&cr);
    if (cr.cpMin != cr.cpMax) {
        cr.cpMax = cr.cpMin;
        SendMessageW(edit, EM_EXSETSEL, 0, (LPARAM)&cr);
    }
    SendMessageW(edit, EM_SCROLLCARET, 0, 0);
}

RECT ColorDotRect(const RECT& client, int index, int dpi) {
    int barTop = client.bottom - kColorBarH;
    int dot = MulDiv(14, dpi, 96);
    int gap = MulDiv(16, dpi, 96);
    int total = kColorCount * dot + (kColorCount - 1) * gap;
    int x0 = (client.right - total) / 2;
    int y0 = barTop + (kColorBarH - dot) / 2;
    RECT r{x0 + index * (dot + gap), y0, x0 + index * (dot + gap) + dot, y0 + dot};
    return r;
}

int NoteRowH(int dpi) {
    // Font pixel height + generous leading so row edits never clip/overlap.
    return BodyFontPx(dpi) + MulDiv(24, dpi, 96);
}

int NoteCheckSize(int dpi) {
    return (std::max)(MulDiv(22, dpi, 96), BodyFontPx(dpi) + MulDiv(4, dpi, 96));
}

bool IsRichEditHwnd(HWND hwnd);
void HideSystemScrollBar(HWND hwnd) {
    // ShowScrollBar(FALSE) zeroes Rich Edit GetScrollInfo — hide via EM only.
    if (IsRichEditHwnd(hwnd)) {
        SendMessageW(hwnd, EM_SHOWSCROLLBAR, SB_VERT, FALSE);
        SendMessageW(hwnd, EM_SHOWSCROLLBAR, SB_HORZ, FALSE);
        return;
    }
    ShowScrollBar(hwnd, SB_VERT, FALSE);
    ShowScrollBar(hwnd, SB_HORZ, FALSE);
}

HWND ScrollTarget(const Note& note) {
    if (note.mode == NoteMode::Checklist && note.checkHost && IsWindow(note.checkHost))
        return note.checkHost;
    return note.edit;
}

Note* FindNoteByScrollTarget(HWND target) {
    for (Note& n : g_notes) {
        if (n.edit == target || n.checkHost == target) return &n;
    }
    return nullptr;
}

bool IsRichEditHwnd(HWND hwnd) {
    wchar_t cls[64]{};
    GetClassNameW(hwnd, cls, 64);
    for (wchar_t* p = cls; *p; ++p) *p = (wchar_t)towlower(*p);
    // Msftedit reports "RICHEDIT50W"
    return wcsstr(cls, L"richedit") != nullptr;
}

bool ReadVertScroll(HWND target, SCROLLINFO* out) {
    if (!target || !out) return false;
    ZeroMemory(out, sizeof(*out));
    out->cbSize = sizeof(*out);
    out->fMask = SIF_ALL;
    GetScrollInfo(target, SB_VERT, out);
    if (out->nPage > 0 && out->nMax >= (int)out->nPage)
        return true;

    if (!IsRichEditHwnd(target)) return false;

    RECT rc{};
    GetClientRect(target, &rc);
    int viewH = rc.bottom - rc.top;
    if (viewH <= 0) return false;

    POINT sp{};
    SendMessageW(target, EM_GETSCROLLPOS, 0, (LPARAM)&sp);

    int contentH = viewH;
    int len = GetWindowTextLengthW(target);
    if (len > 0) {
        LRESULT xy = SendMessageW(target, EM_POSFROMCHAR, (WPARAM)(len - 1), 0);
        if (xy != (LRESULT)-1) {
            int y = (short)HIWORD(xy);
            int lineH = BodyFontPx(WindowDpi(target)) + 4;
            HDC dc = GetDC(target);
            if (dc) {
                HFONT old = g_font ? (HFONT)SelectObject(dc, g_font) : nullptr;
                TEXTMETRICW tm{};
                if (GetTextMetricsW(dc, &tm)) lineH = tm.tmHeight + tm.tmExternalLeading;
                if (old) SelectObject(dc, old);
                ReleaseDC(target, dc);
            }
            contentH = sp.y + y + lineH + 8;
        }
    }

    out->nMin = 0;
    out->nMax = (std::max)(viewH, contentH) - 1;
    out->nPage = (UINT)viewH;
    out->nPos = sp.y;
    out->nTrackPos = sp.y;
    return out->nMax >= (int)out->nPage;
}

void SetVertScrollPos(HWND target, int pos) {
    SCROLLINFO si{};
    if (!ReadVertScroll(target, &si)) return;
    int maxPos = si.nMax - (int)si.nPage + 1;
    if (maxPos < si.nMin) maxPos = si.nMin;
    if (pos < si.nMin) pos = si.nMin;
    if (pos > maxPos) pos = maxPos;
    if (pos == si.nPos) return;

    // #region agent log
    {
        static DWORD lastLog;
        DWORD now = GetTickCount();
        if (now - lastLog > 200) {
            lastLog = now;
            char data[160];
            snprintf(data, sizeof(data),
                "{\"rich\":%d,\"from\":%d,\"to\":%d,\"page\":%u,\"max\":%d,\"run\":\"post-fix8\"}",
                IsRichEditHwnd(target) ? 1 : 0, si.nPos, pos, si.nPage, si.nMax);
            AgentLog("H", "SetVertScrollPos", "scroll", data);
        }
    }
    // #endregion

    if (IsRichEditHwnd(target)) {
        POINT pt{0, pos};
        SendMessageW(target, EM_SETSCROLLPOS, 0, (LPARAM)&pt);
        if (Note* note = FindNoteByScrollTarget(target))
            ScheduleScrollRailRefresh(*note);
        return;
    }

    si.fMask = SIF_POS;
    si.nPos = pos;
    SetScrollInfo(target, SB_VERT, &si, TRUE);
    if (Note* note = FindNoteByScrollTarget(target)) {
        ApplyChecklistScroll(*note);
        ScheduleScrollRailRefresh(*note);
    }
}

bool ThumbRectFor(HWND target, int railH, RECT* thumbOut) {
    if (!target || railH <= 0) return false;
    SCROLLINFO si{};
    if (!ReadVertScroll(target, &si)) return false;
    int range = si.nMax - si.nMin + 1;
    if (range <= (int)si.nPage || si.nPage == 0) return false;
    int thumbH = (std::max)(MulDiv(24, WindowDpi(target), 96), MulDiv(railH, (int)si.nPage, range));
    thumbH = (std::min)(thumbH, railH);
    int travel = railH - thumbH;
    int span = range - (int)si.nPage;
    int thumbY = span > 0 ? MulDiv(travel, si.nPos - si.nMin, span) : 0;
    thumbOut->left = 0;
    thumbOut->right = kScrollW;
    thumbOut->top = thumbY;
    thumbOut->bottom = thumbY + thumbH;
    // #region agent log
    {
        static DWORD lastLog;
        DWORD now = GetTickCount();
        if (now - lastLog > 800) {
            lastLog = now;
            char data[200];
            snprintf(data, sizeof(data),
                "{\"rich\":%d,\"page\":%u,\"max\":%d,\"pos\":%d,\"thumbH\":%d,\"run\":\"post-fix8\"}",
                IsRichEditHwnd(target) ? 1 : 0, si.nPage, si.nMax, si.nPos, thumbH);
            AgentLog("E", "ThumbRectFor", "ok", data);
        }
    }
    // #endregion
    return true;
}

void RefreshScrollRail(Note& note) {
    if (!note.scrollRail || !IsWindow(note.scrollRail)) return;
    SetWindowLongPtrW(note.scrollRail, GWLP_USERDATA, (LONG_PTR)ScrollTarget(note));
    InvalidateRect(note.scrollRail, nullptr, FALSE);
}

void ScheduleScrollRailRefresh(Note& note) {
    if (!note.hwnd) return;
    SetTimer(note.hwnd, kScrollTimer, 8, nullptr); // ~120Hz rail chrome; content already scrolled
}

void EnsureCheckBody(Note& note) {
    if (!note.checkHost) return;
    if (note.checkBody && IsWindow(note.checkBody)) return;
    note.checkBody = CreateWindowExW(0, L"StickyCheckBody", L"",
        WS_CHILD | WS_VISIBLE,
        0, 0, 10, 10, note.checkHost, nullptr, g_inst, nullptr);
}

// Moving a parent of a focused EDIT leaves ghost carets; hide/restore around moves.
struct ChecklistCaretGuard {
    HWND edit = nullptr;
    DWORD selStart = 0, selEnd = 0;
    explicit ChecklistCaretGuard(HWND body) {
        HWND focus = GetFocus();
        if (!focus || !body || !IsChild(body, focus)) return;
        edit = focus;
        SendMessageW(edit, EM_GETSEL, (WPARAM)&selStart, (LPARAM)&selEnd);
        HideCaret(edit);
    }
    ~ChecklistCaretGuard() {
        if (!edit || !IsWindow(edit)) return;
        ShowCaret(edit);
        SendMessageW(edit, EM_SETSEL, selStart, selEnd);
    }
};

void ApplyChecklistScroll(Note& note) {
    if (!note.checkHost || !note.checkBody) return;
    RECT host{};
    GetClientRect(note.checkHost, &host);
    int viewH = (std::max)(1, (int)host.bottom);
    int contentH = (std::max)(viewH, note.checkContentH);
    SCROLLINFO si{sizeof(si), SIF_POS};
    GetScrollInfo(note.checkHost, SB_VERT, &si);
    int maxPos = (std::max)(0, contentH - viewH);
    if (si.nPos > maxPos) {
        si.fMask = SIF_POS;
        si.nPos = maxPos;
        SetScrollInfo(note.checkHost, SB_VERT, &si, TRUE);
    }
    ChecklistCaretGuard caret(note.checkBody);
    MoveWindow(note.checkBody, 0, -si.nPos, host.right, contentH, FALSE);
    for (auto& it : note.items) {
        if (it.edit) SyncEditBgToNoteGradient(it.edit, note);
    }
    RedrawWindow(note.checkHost, nullptr, nullptr,
        RDW_INVALIDATE | RDW_ERASE | RDW_ALLCHILDREN);
}

LRESULT CALLBACK ScrollRailProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    HWND target = (HWND)GetWindowLongPtrW(hwnd, GWLP_USERDATA);
    Note* note = FindNote(GetParent(hwnd));
    HBRUSH paper = (note && note->paperBrush) ? note->paperBrush : g_paper;
    COLORREF thumbColor = note ? NotePal(*note).thumb : RGB(214, 170, 48);
    switch (msg) {
    case WM_ERASEBKGND: {
        if (note) {
            PaintNoteGradientSlice((HDC)wp, hwnd, *note);
            return 1;
        }
        RECT rc{};
        GetClientRect(hwnd, &rc);
        FillRect((HDC)wp, &rc, paper);
        return 1;
    }
    case WM_PAINT: {
        PAINTSTRUCT ps;
        HDC dc = BeginPaint(hwnd, &ps);
        RECT rc{};
        GetClientRect(hwnd, &rc);
        if (note) PaintNoteGradientSlice(dc, hwnd, *note);
        else FillRect(dc, &rc, paper);
        RECT thumb{};
        bool hasThumb = ThumbRectFor(target, rc.bottom, &thumb);
        if (hasThumb) {
            HBRUSH th = CreateSolidBrush(thumbColor);
            FillRect(dc, &thumb, th);
            DeleteObject(th);
        }
        // #region agent log
        {
            static DWORD lastLog;
            DWORD now = GetTickCount();
            if (now - lastLog > 500) {
                lastLog = now;
                SCROLLINFO si{};
                if (target) ReadVertScroll(target, &si);
                char data[220];
                snprintf(data, sizeof(data),
                    "{\"railW\":%d,\"railH\":%d,\"hasThumb\":%d,\"thumbH\":%d,\"page\":%u,\"max\":%d,\"rich\":%d,\"run\":\"post-fix8\"}",
                    rc.right, rc.bottom, hasThumb ? 1 : 0, hasThumb ? (thumb.bottom - thumb.top) : 0,
                    si.nPage, si.nMax, target && IsRichEditHwnd(target) ? 1 : 0);
                AgentLog("D", "ScrollRailProc", "paint", data);
            }
        }
        // #endregion
        EndPaint(hwnd, &ps);
        return 0;
    }
    case WM_LBUTTONDOWN: {
        if (!target) return 0;
        RECT rc{}, thumb{};
        GetClientRect(hwnd, &rc);
        int y = GET_Y_LPARAM(lp);
        if (!ThumbRectFor(target, rc.bottom, &thumb)) return 0;
        SCROLLINFO si{};
        ReadVertScroll(target, &si);
        int thumbH = thumb.bottom - thumb.top;
        if (y < thumb.top || y >= thumb.bottom) {
            int range = si.nMax - si.nMin + 1;
            int span = range - (int)si.nPage;
            int travel = rc.bottom - thumbH;
            if (travel > 0 && span > 0)
                SetVertScrollPos(target, si.nMin + MulDiv(y, span, travel));
        }
        SetCapture(hwnd);
        SetPropW(hwnd, L"drag", (HANDLE)(INT_PTR)(y - thumb.top));
        SetPropW(hwnd, L"dragging", (HANDLE)1);
        InvalidateRect(hwnd, nullptr, FALSE);
        return 0;
    }
    case WM_MOUSEMOVE:
        if (GetCapture() == hwnd && GetPropW(hwnd, L"dragging") && target) {
            RECT rc{}, thumb{};
            GetClientRect(hwnd, &rc);
            if (!ThumbRectFor(target, rc.bottom, &thumb)) return 0;
            int offset = (int)(INT_PTR)GetPropW(hwnd, L"drag");
            int y = GET_Y_LPARAM(lp) - offset;
            int thumbH = thumb.bottom - thumb.top;
            int travel = rc.bottom - thumbH;
            SCROLLINFO si{};
            if (!ReadVertScroll(target, &si)) return 0;
            int range = si.nMax - si.nMin + 1;
            int span = range - (int)si.nPage;
            if (travel > 0 && span > 0) {
                if (y < 0) y = 0;
                if (y > travel) y = travel;
                SetVertScrollPos(target, si.nMin + MulDiv(y, span, travel));
                InvalidateRect(hwnd, nullptr, FALSE);
            }
        }
        return 0;
    case WM_LBUTTONUP:
        if (GetCapture() == hwnd) ReleaseCapture();
        RemovePropW(hwnd, L"dragging");
        RemovePropW(hwnd, L"drag");
        return 0;
    case WM_MOUSEWHEEL:
        if (target) {
            SendMessageW(target, WM_MOUSEWHEEL, wp, lp);
            InvalidateRect(hwnd, nullptr, FALSE);
        }
        return 0;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

constexpr wchar_t kBulletChar = 0x2022;  // •

bool IsBoldAt(HWND edit, LONG cp) {
    // #region agent log
    ++g_dbgIsBoldCalls;
    // #endregion
    CHARRANGE old{};
    SendMessageW(edit, EM_EXGETSEL, 0, (LPARAM)&old);
    CHARRANGE one{cp, cp + 1};
    SendMessageW(edit, EM_EXSETSEL, 0, (LPARAM)&one);
    CHARFORMAT2W cf{};
    cf.cbSize = sizeof(cf);
    SendMessageW(edit, EM_GETCHARFORMAT, SCF_SELECTION, (LPARAM)&cf);
    SendMessageW(edit, EM_EXSETSEL, 0, (LPARAM)&old);
    return (cf.dwEffects & CFE_BOLD) != 0;
}

struct RedrawFreeze {
    HWND hwnd;
    explicit RedrawFreeze(HWND h) : hwnd(h) {
        if (hwnd) SendMessageW(hwnd, WM_SETREDRAW, FALSE, 0);
    }
    ~RedrawFreeze() {
        if (hwnd) {
            SendMessageW(hwnd, WM_SETREDRAW, TRUE, 0);
            InvalidateRect(hwnd, nullptr, FALSE);
            // No UpdateWindow — avoids forcing a sync paint stall.
        }
    }
};

void ToggleBold(HWND edit);
WORD GetParaNumbering(HWND edit);
void AppendMdStyled(HWND edit, const std::wstring& text, LONG a, LONG b, std::wstring& out);

void ToggleBold(HWND edit) {
    if (!edit) return;
    if (!IsRichEditHwnd(edit)) return;
    CHARRANGE cr{};
    SendMessageW(edit, EM_EXGETSEL, 0, (LPARAM)&cr);
    CHARFORMAT2W cf{};
    cf.cbSize = sizeof(cf);
    SendMessageW(edit, EM_GETCHARFORMAT, SCF_SELECTION, (LPARAM)&cf);
    bool turnOn = !(cf.dwMask & CFM_BOLD) || !(cf.dwEffects & CFE_BOLD);
    cf.cbSize = sizeof(cf);
    cf.dwMask = CFM_BOLD;
    cf.dwEffects = turnOn ? CFE_BOLD : 0;
    SendMessageW(edit, EM_SETCHARFORMAT, SCF_SELECTION, (LPARAM)&cf);
    if (cr.cpMin == cr.cpMax) {
        SendMessageW(edit, EM_EXSETSEL, 0, (LPARAM)&cr);
        SendMessageW(edit, EM_SCROLLCARET, 0, 0);
    }
}

// Fast bold emit: binary-split on mixed CHARFORMAT runs (log N messages, not N).
bool BoldOfRange(HWND edit, LONG a, LONG b, bool* boldOut) {
    if (a >= b) { *boldOut = false; return true; }
    CHARRANGE cr{a, b};
    SendMessageW(edit, EM_EXSETSEL, 0, (LPARAM)&cr);
    CHARFORMAT2W cf{};
    cf.cbSize = sizeof(cf);
    SendMessageW(edit, EM_GETCHARFORMAT, SCF_SELECTION, (LPARAM)&cf);
    if (!(cf.dwMask & CFM_BOLD)) return false;
    *boldOut = (cf.dwEffects & CFE_BOLD) != 0;
    return true;
}

void AppendMdStyled(HWND edit, const std::wstring& text, LONG a, LONG b, std::wstring& out) {
    if (a >= b) return;
    bool bold = false;
    if (BoldOfRange(edit, a, b, &bold)) {
        if (bold) out += L"**";
        out.append(text, (size_t)a, (size_t)(b - a));
        if (bold) out += L"**";
        return;
    }
    LONG mid = a + (b - a) / 2;
    if (mid <= a) mid = a + 1;
    AppendMdStyled(edit, text, a, mid, out);
    AppendMdStyled(edit, text, mid, b, out);
}

std::wstring RichEditToMarkdown(HWND edit) {
    // #region agent log
    int boldBefore = g_dbgIsBoldCalls;
    DWORD t0 = GetTickCount();
    // #endregion
    CHARRANGE oldSel{};
    SendMessageW(edit, EM_EXGETSEL, 0, (LPARAM)&oldSel);
    SendMessageW(edit, WM_SETREDRAW, FALSE, 0);

    std::wstring text = CtrlText(edit);
    std::wstring out;
    LONG total = (LONG)text.size();
    LONG i = 0;
    int autoNum = 1;
    int formatQueries = 0;

    while (i <= total) {
        LONG lineEnd = i;
        while (lineEnd < total && text[(size_t)lineEnd] != L'\n' && text[(size_t)lineEnd] != L'\r')
            ++lineEnd;

        CHARRANGE at{i, i};
        SendMessageW(edit, EM_EXSETSEL, 0, (LPARAM)&at);
        WORD num = GetParaNumbering(edit);
        ++formatQueries;

        LONG content = i;
        if (num == PFN_BULLET) out += L"- ";
        else if (num == 2) {
            wchar_t buf[32];
            swprintf(buf, 32, L"%d. ", autoNum++);
            out += buf;
        } else if (content < lineEnd && text[(size_t)content] == kBulletChar &&
                   content + 1 < lineEnd && text[(size_t)content + 1] == L' ') {
            out += L"- ";
            content += 2;
            autoNum = 1;
        } else {
            autoNum = 1;
        }

        AppendMdStyled(edit, text, content, lineEnd, out);
        formatQueries += 2; // approx; real count varies

        if (lineEnd < total) {
            out.push_back(L'\n');
            if (text[(size_t)lineEnd] == L'\r' && lineEnd + 1 < total && text[(size_t)lineEnd + 1] == L'\n')
                i = lineEnd + 2;
            else
                i = lineEnd + 1;
        } else break;
    }

    SendMessageW(edit, EM_EXSETSEL, 0, (LPARAM)&oldSel);
    SendMessageW(edit, WM_SETREDRAW, TRUE, 0);
    // No UpdateWindow — keep typing/scroll instant; next paint is enough.
    // #region agent log
    {
        char data[220];
        snprintf(data, sizeof(data),
            "{\"chars\":%ld,\"isBoldCalls\":%d,\"ms\":%lu,\"serializeN\":%d,\"run\":\"post-fix10\"}",
            total, g_dbgIsBoldCalls - boldBefore, (unsigned long)(GetTickCount() - t0), ++g_dbgSerializeCalls);
        AgentLog("A", "RichEditToMarkdown", "serialize_walk", data);
    }
    // #endregion
    return out;
}

void SetParaBullet(HWND edit) {
    PARAFORMAT2 pf{};
    pf.cbSize = sizeof(pf);
    pf.dwMask = PFM_NUMBERING | PFM_OFFSET | PFM_STARTINDENT;
    pf.wNumbering = PFN_BULLET;
    pf.dxOffset = 360;
    pf.dxStartIndent = 360;
    SendMessageW(edit, EM_SETPARAFORMAT, 0, (LPARAM)&pf);
}

void SetParaNumber(HWND edit, int start) {
    PARAFORMAT2 pf{};
    pf.cbSize = sizeof(pf);
    pf.dwMask = PFM_NUMBERING | PFM_NUMBERINGSTART | PFM_NUMBERINGSTYLE |
                PFM_OFFSET | PFM_STARTINDENT;
    pf.wNumbering = 2;
    pf.wNumberingStart = (WORD)(start > 0 ? start : 1);
    pf.wNumberingStyle = PFNS_PERIOD;
    pf.dxOffset = 360;
    pf.dxStartIndent = 360;
    SendMessageW(edit, EM_SETPARAFORMAT, 0, (LPARAM)&pf);
}

// Continue an existing numbered list — do not force wNumberingStart (lets RE renumber).
void SetParaNumberContinue(HWND edit) {
    PARAFORMAT2 pf{};
    pf.cbSize = sizeof(pf);
    pf.dwMask = PFM_NUMBERING | PFM_NUMBERINGSTYLE | PFM_OFFSET | PFM_STARTINDENT;
    pf.wNumbering = 2;
    pf.wNumberingStyle = PFNS_PERIOD;
    pf.dxOffset = 360;
    pf.dxStartIndent = 360;
    SendMessageW(edit, EM_SETPARAFORMAT, 0, (LPARAM)&pf);
}

void ClearParaList(HWND edit) {
    PARAFORMAT2 pf{};
    pf.cbSize = sizeof(pf);
    pf.dwMask = PFM_NUMBERING | PFM_NUMBERINGSTART | PFM_NUMBERINGSTYLE |
                PFM_OFFSET | PFM_STARTINDENT;
    pf.wNumbering = 0;
    pf.wNumberingStart = 0;
    pf.dxOffset = 0;
    pf.dxStartIndent = 0;
    SendMessageW(edit, EM_SETPARAFORMAT, 0, (LPARAM)&pf);
}

WORD GetParaNumbering(HWND edit) {
    PARAFORMAT2 pf{};
    pf.cbSize = sizeof(pf);
    SendMessageW(edit, EM_GETPARAFORMAT, 0, (LPARAM)&pf);
    return pf.wNumbering;
}

int GetParaNumberingStart(HWND edit) {
    PARAFORMAT2 pf{};
    pf.cbSize = sizeof(pf);
    SendMessageW(edit, EM_GETPARAFORMAT, 0, (LPARAM)&pf);
    return (int)pf.wNumberingStart;
}

WORD NumberingAtCp(HWND edit, LONG cp) {
    CHARRANGE old{};
    SendMessageW(edit, EM_EXGETSEL, 0, (LPARAM)&old);
    CHARRANGE at{cp, cp};
    SendMessageW(edit, EM_EXSETSEL, 0, (LPARAM)&at);
    WORD num = GetParaNumbering(edit);
    SendMessageW(edit, EM_EXSETSEL, 0, (LPARAM)&old);
    return num;
}

// Re-apply numbering so only the first item of a contiguous run has a start value.
void NormalizeNumberedRun(HWND edit, LONG anyCpInRun) {
    SendMessageW(edit, WM_SETREDRAW, FALSE, 0);
    LONG line = (LONG)SendMessageW(edit, EM_LINEFROMCHAR, (WPARAM)anyCpInRun, 0);
    LONG first = line;
    while (first > 0) {
        LONG prevStart = (LONG)SendMessageW(edit, EM_LINEINDEX, (WPARAM)(first - 1), 0);
        if (NumberingAtCp(edit, prevStart) != 2) break;
        --first;
    }
    LONG lineCount = (LONG)SendMessageW(edit, EM_GETLINECOUNT, 0, 0);
    LONG last = first;
    while (last + 1 < lineCount) {
        LONG nextStart = (LONG)SendMessageW(edit, EM_LINEINDEX, (WPARAM)(last + 1), 0);
        if (NumberingAtCp(edit, nextStart) != 2) break;
        ++last;
    }
    CHARRANGE old{};
    SendMessageW(edit, EM_EXGETSEL, 0, (LPARAM)&old);
    for (LONG L = first; L <= last; ++L) {
        LONG cp = (LONG)SendMessageW(edit, EM_LINEINDEX, (WPARAM)L, 0);
        CHARRANGE at{cp, cp};
        SendMessageW(edit, EM_EXSETSEL, 0, (LPARAM)&at);
        if (L == first) {
            int start = GetParaNumberingStart(edit);
            if (start <= 0) start = 1;
            SetParaNumber(edit, start);
        } else {
            SetParaNumberContinue(edit);
        }
    }
    SendMessageW(edit, EM_EXSETSEL, 0, (LPARAM)&old);
    SendMessageW(edit, WM_SETREDRAW, TRUE, 0);
    InvalidateRect(edit, nullptr, FALSE);
    // #region agent log
    {
        char data[96];
        snprintf(data, sizeof(data),
            "{\"first\":%ld,\"last\":%ld,\"run\":\"post-fix10\"}", first, last);
        AgentLog("N", "NormalizeNumberedRun", "renumber", data);
    }
    // #endregion
}

// Read current line using Rich Edit line APIs (EM indices ≠ GetWindowText when \r vs \r\n).
bool GetEditLinePrefix(HWND edit, LONG caret, LONG* lineStartOut, std::wstring* prefixOut) {
    if (caret < 0) return false;
    LONG line = (LONG)SendMessageW(edit, EM_LINEFROMCHAR, (WPARAM)caret, 0);
    LONG lineStart = (LONG)SendMessageW(edit, EM_LINEINDEX, (WPARAM)line, 0);
    if (lineStart < 0) return false;
    LONG lineLen = (LONG)SendMessageW(edit, EM_LINELENGTH, (WPARAM)lineStart, 0);
    if (lineLen < 0) lineLen = 0;
    // EM_GETLINE: first WORD of buffer is buffer size in TCHARs
    std::wstring buf(static_cast<size_t>(lineLen) + 2, 0);
    *(WORD*)buf.data() = (WORD)(lineLen + 1);
    LONG got = (LONG)SendMessageW(edit, EM_GETLINE, (WPARAM)line, (LPARAM)buf.data());
    if (got < 0) got = 0;
    buf.resize(static_cast<size_t>(got));
    while (!buf.empty() && (buf.back() == L'\r' || buf.back() == L'\n')) buf.pop_back();
    LONG take = caret - lineStart;
    if (take < 0) take = 0;
    if (take > (LONG)buf.size()) take = (LONG)buf.size();
    *lineStartOut = lineStart;
    *prefixOut = buf.substr(0, static_cast<size_t>(take));
    return true;
}

// Keep-style: fire once when trigger is complete.
enum class RuleResult { None, ToChecklist };

RuleResult TryInputRules(HWND edit) {
    CHARRANGE cr{};
    SendMessageW(edit, EM_EXGETSEL, 0, (LPARAM)&cr);
    LONG caret = cr.cpMin;
    LONG lineStart = 0;
    std::wstring prefix;
    if (!GetEditLinePrefix(edit, caret, &lineStart, &prefix)) return RuleResult::None;

    // #region agent log
    {
        char data[128];
        snprintf(data, sizeof(data),
            "{\"caret\":%ld,\"lineStart\":%ld,\"prefixLen\":%d,\"run\":\"post-fix10\"}",
            caret, lineStart, (int)prefix.size());
        AgentLog("H", "TryInputRules", "line_prefix", data);
    }
    // #endregion

    auto eat = [&](LONG n) {
        g_busy = true;
        CHARRANGE sel{lineStart, lineStart + n};
        SendMessageW(edit, EM_EXSETSEL, 0, (LPARAM)&sel);
        SendMessageW(edit, EM_REPLACESEL, TRUE, (LPARAM)L"");
        g_busy = false;
    };

    if (prefix.size() >= 2 && prefix.substr(prefix.size() - 2) == L"[]")
        return RuleResult::ToChecklist;

    if (prefix == L"- ") {
        g_busy = true;
        CHARRANGE sel{lineStart, lineStart + 2};
        SendMessageW(edit, EM_EXSETSEL, 0, (LPARAM)&sel);
        wchar_t rep[3] = {kBulletChar, L' ', 0};
        SendMessageW(edit, EM_REPLACESEL, TRUE, (LPARAM)rep);
        g_busy = false;
        // #region agent log
        AgentLog("G", "TryInputRules", "ul_bullet_insert", "{\"run\":\"post-fix3\"}");
        // #endregion
        return RuleResult::None;
    }

    if (prefix.size() >= 3 && prefix.back() == L' ') {
        size_t dot = prefix.find(L'.');
        if (dot != std::wstring::npos && dot > 0 && dot + 2 == prefix.size()) {
            bool ok = true;
            for (size_t i = 0; i < dot; ++i) if (!iswdigit(prefix[i])) { ok = false; break; }
            if (ok) {
                int start = _wtoi(prefix.c_str());
                eat((LONG)prefix.size());
                SetParaNumber(edit, start);
                return RuleResult::None;
            }
        }
    }
    return RuleResult::None;
}

void LoadMarkdownToRichEdit(HWND edit, const std::wstring& md) {
    g_busy = true;
    SetWindowTextW(edit, L"");
    ApplyDefaultCharFormat(edit);

    struct Span { LONG a, b; };
    struct Para { LONG cp; WORD num; int start; };
    std::vector<Span> bolds;
    std::vector<Para> paras;
    std::wstring display;

    size_t i = 0;
    while (i <= md.size()) {
        size_t lineEnd = md.find(L'\n', i);
        std::wstring line = md.substr(i, lineEnd == std::wstring::npos ? std::wstring::npos : lineEnd - i);
        if (!line.empty() && line.back() == L'\r') line.pop_back();

        WORD numbering = 0;
        int numStart = 1;
        std::wstring body = line;
        if (body.rfind(L"- ", 0) == 0 && body.rfind(L"- [", 0) != 0) {
            // Display as glyph bullet, not PFN_BULLET
            display.push_back(kBulletChar);
            display.push_back(L' ');
            body = body.substr(2);
        } else {
            size_t dot = body.find(L". ");
            if (dot != std::wstring::npos && dot > 0) {
                bool dig = true;
                for (size_t k = 0; k < dot; ++k) if (!iswdigit(body[k])) { dig = false; break; }
                if (dig) {
                    numbering = 2;
                    numStart = _wtoi(body.c_str());
                    body = body.substr(dot + 2);
                }
            }
        }

        LONG lineCp = (LONG)display.size();
        if (numbering) paras.push_back({lineCp, numbering, numStart});

        size_t j = 0;
        while (j < body.size()) {
            if (j + 1 < body.size() && body[j] == L'*' && body[j + 1] == L'*') {
                size_t close = body.find(L"**", j + 2);
                if (close != std::wstring::npos) {
                    LONG a = (LONG)display.size();
                    display.append(body, j + 2, close - (j + 2));
                    bolds.push_back({a, (LONG)display.size()});
                    j = close + 2;
                    continue;
                }
            }
            display.push_back(body[j++]);
        }
        if (lineEnd != std::wstring::npos) display.push_back(L'\n');
        if (lineEnd == std::wstring::npos) break;
        i = lineEnd + 1;
    }

    SetWindowTextW(edit, display.c_str());
    ApplyDefaultCharFormat(edit);
    for (size_t pi = 0; pi < paras.size(); ++pi) {
        auto& p = paras[pi];
        CHARRANGE cr{p.cp, p.cp};
        SendMessageW(edit, EM_EXSETSEL, 0, (LPARAM)&cr);
        if (p.num == PFN_BULLET) {
            SetParaBullet(edit);
            continue;
        }
        bool firstOfRun = true;
        if (pi > 0 && paras[pi - 1].num == 2) {
            LONG prevCp = paras[pi - 1].cp;
            LONG prevLineEnd = prevCp;
            while (prevLineEnd < (LONG)display.size() && display[(size_t)prevLineEnd] != L'\n')
                ++prevLineEnd;
            // Contiguous numbered lines → continue (no forced start).
            if (prevLineEnd + 1 == p.cp) firstOfRun = false;
        }
        if (firstOfRun) SetParaNumber(edit, p.start);
        else SetParaNumberContinue(edit);
    }
    for (auto& b : bolds) {
        CHARRANGE cr{b.a, b.b};
        SendMessageW(edit, EM_EXSETSEL, 0, (LPARAM)&cr);
        CHARFORMAT2W cf{};
        cf.cbSize = sizeof(cf);
        cf.dwMask = CFM_BOLD;
        cf.dwEffects = CFE_BOLD;
        SendMessageW(edit, EM_SETCHARFORMAT, SCF_SELECTION, (LPARAM)&cf);
    }
    CHARRANGE end{(LONG)display.size(), (LONG)display.size()};
    SendMessageW(edit, EM_EXSETSEL, 0, (LPARAM)&end);
    g_busy = false;
}

bool LooksLikeChecklist(const std::wstring& md) {
    size_t i = 0;
    int checks = 0, lines = 0;
    while (i <= md.size()) {
        size_t e = md.find(L'\n', i);
        std::wstring line = md.substr(i, e == std::wstring::npos ? std::wstring::npos : e - i);
        if (!line.empty() && line.back() == L'\r') line.pop_back();
        if (!line.empty()) {
            ++lines;
            if (line.rfind(L"- [ ]", 0) == 0 || line.rfind(L"- [x]", 0) == 0 ||
                line.rfind(L"- [X]", 0) == 0 || line.rfind(L"- []", 0) == 0)
                ++checks;
        }
        if (e == std::wstring::npos) break;
        i = e + 1;
    }
    return checks > 0 && checks * 2 >= lines;
}

std::vector<CheckItem> ParseChecklistMd(const std::wstring& md) {
    std::vector<CheckItem> items;
    size_t i = 0;
    while (i <= md.size()) {
        size_t e = md.find(L'\n', i);
        std::wstring line = md.substr(i, e == std::wstring::npos ? std::wstring::npos : e - i);
        if (!line.empty() && line.back() == L'\r') line.pop_back();
        CheckItem it;
        bool got = false;
        if (line.rfind(L"- [x] ", 0) == 0 || line.rfind(L"- [X] ", 0) == 0) {
            it.done = true; it.text = line.substr(6); got = true;
        } else if (line.rfind(L"- [ ] ", 0) == 0) {
            it.text = line.substr(6); got = true;
        } else if (line.rfind(L"- [] ", 0) == 0) {
            it.text = line.substr(5); got = true;
        } else if (line.rfind(L"- [x]", 0) == 0 || line.rfind(L"- [X]", 0) == 0) {
            it.done = true; it.text = line.size() > 5 ? line.substr(5) : L""; got = true;
            if (!it.text.empty() && it.text[0] == L' ') it.text.erase(0, 1);
        } else if (line.rfind(L"- [ ]", 0) == 0 || line.rfind(L"- []", 0) == 0) {
            size_t skip = line.rfind(L"- []", 0) == 0 ? 4 : 5;
            it.text = line.size() > skip ? line.substr(skip) : L"";
            if (!it.text.empty() && it.text[0] == L' ') it.text.erase(0, 1);
            got = true;
        } else if (!line.empty()) {
            it.text = line; got = true;
        }
        if (got) items.push_back(std::move(it));
        if (e == std::wstring::npos) break;
        i = e + 1;
    }
    if (items.empty()) items.push_back({});
    return items;
}

std::wstring ChecklistItemToMd(HWND edit) {
    if (!edit) return {};
    if (!IsRichEditHwnd(edit)) return CtrlText(edit);
    CHARRANGE oldSel{};
    SendMessageW(edit, EM_EXGETSEL, 0, (LPARAM)&oldSel);
    SendMessageW(edit, WM_SETREDRAW, FALSE, 0);
    std::wstring text = CtrlText(edit);
    std::wstring out;
    AppendMdStyled(edit, text, 0, (LONG)text.size(), out);
    SendMessageW(edit, EM_EXSETSEL, 0, (LPARAM)&oldSel);
    SendMessageW(edit, WM_SETREDRAW, TRUE, 0);
    return out;
}

void LoadBoldMarkdown(HWND edit, const std::wstring& md) {
    if (!edit || !IsRichEditHwnd(edit)) {
        if (edit) SetWindowTextW(edit, md.c_str());
        return;
    }
    bool wasBusy = g_busy;
    g_busy = true;
    SetWindowTextW(edit, L"");
    ApplyDefaultCharFormat(edit);
    struct Span { LONG a, b; };
    std::vector<Span> bolds;
    std::wstring display;
    size_t j = 0;
    while (j < md.size()) {
        if (j + 1 < md.size() && md[j] == L'*' && md[j + 1] == L'*') {
            size_t close = md.find(L"**", j + 2);
            if (close != std::wstring::npos) {
                LONG a = (LONG)display.size();
                display.append(md, j + 2, close - (j + 2));
                bolds.push_back({a, (LONG)display.size()});
                j = close + 2;
                continue;
            }
        }
        display.push_back(md[j++]);
    }
    SetWindowTextW(edit, display.c_str());
    ApplyDefaultCharFormat(edit);
    for (auto& b : bolds) {
        CHARRANGE cr{b.a, b.b};
        SendMessageW(edit, EM_EXSETSEL, 0, (LPARAM)&cr);
        CHARFORMAT2W cf{};
        cf.cbSize = sizeof(cf);
        cf.dwMask = CFM_BOLD;
        cf.dwEffects = CFE_BOLD;
        SendMessageW(edit, EM_SETCHARFORMAT, SCF_SELECTION, (LPARAM)&cf);
    }
    CHARRANGE z{(LONG)display.size(), (LONG)display.size()};
    SendMessageW(edit, EM_EXSETSEL, 0, (LPARAM)&z);
    g_busy = wasBusy;
}

void SyncChecklistModel(Note& note) {
    for (auto& it : note.items) {
        if (it.edit) it.text = ChecklistItemToMd(it.edit);
    }
}

void PaintCheckBox(DRAWITEMSTRUCT* dis, bool done, int dpi, HBRUSH /*paper*/) {
    Note* note = nullptr;
    HWND row = GetParent(dis->hwndItem);
    if (row) {
        HWND body = GetParent(row);
        for (Note& n : g_notes) if (n.checkBody == body) { note = &n; break; }
    }
    // Transparent fill — draw the same gradient slice so no solid chip sits on the note.
    if (note) PaintNoteGradientSlice(dis->hDC, dis->hwndItem, *note);
    else FillRect(dis->hDC, &dis->rcItem, g_paper);
    int box = NoteCheckSize(dpi);
    int x = dis->rcItem.left + (dis->rcItem.right - dis->rcItem.left - box) / 2;
    int y = dis->rcItem.top + (dis->rcItem.bottom - dis->rcItem.top - box) / 2;
    RECT br{x, y, x + box, y + box};
    HBRUSH border = CreateSolidBrush(RGB(160, 120, 30));
    FrameRect(dis->hDC, &br, border);
    DeleteObject(border);
    if (done) {
        HPEN pen = CreatePen(PS_SOLID, (std::max)(2, MulDiv(2, dpi, 96)), RGB(90, 60, 10));
        HGDIOBJ old = SelectObject(dis->hDC, pen);
        int m = box / 5;
        MoveToEx(dis->hDC, br.left + m, br.top + box / 2, nullptr);
        LineTo(dis->hDC, br.left + box / 2 - 1, br.bottom - m - 1);
        LineTo(dis->hDC, br.right - m, br.top + m + 1);
        SelectObject(dis->hDC, old);
        DeleteObject(pen);
    }
    if (dis->itemState & ODS_FOCUS) {
        RECT fr = br;
        InflateRect(&fr, 2, 2);
        DrawFocusRect(dis->hDC, &fr);
    }
}

std::wstring ChecklistToMd(const Note& note) {
    std::wstring out;
    for (const auto& it : note.items) {
        out += it.done ? L"- [x] " : L"- [ ] ";
        out += it.text;
        out += L'\n';
    }
    return out;
}

std::wstring SerializeNote(Note& note) {
    // #region agent log
    ++g_dbgSerializeCalls;
    // #endregion
    if (note.mode == NoteMode::Checklist) {
        SyncChecklistModel(note);
        return ChecklistToMd(note);
    }
    return RichEditToMarkdown(note.edit);
}

int MeasureChecklistEditH(HWND edit, int textW, int dpi) {
    int minH = BodyFontPx(dpi) + MulDiv(10, dpi, 96);
    if (!edit || textW <= 0) return minH;

    // Force width first so wrap metrics match the laid-out row.
    RECT wr{}, cr{};
    GetWindowRect(edit, &wr);
    GetClientRect(edit, &cr);
    int borderX = (wr.right - wr.left) - (cr.right - cr.left);
    int borderY = (wr.bottom - wr.top) - (cr.bottom - cr.top);
    int wantW = textW + borderX;
    int curH = (std::max)(minH + borderY, (int)(wr.bottom - wr.top));
    if ((int)(wr.right - wr.left) != wantW) {
        SetWindowPos(edit, nullptr, 0, 0, wantW, curH,
            SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE | SWP_NOREDRAW);
    }

    std::wstring text = CtrlText(edit);
    // DrawText wrap height (handles soft wrap + hard newlines).
    HDC dc = GetDC(edit);
    HFONT useFont = g_font;
    HFONT old = useFont ? (HFONT)SelectObject(dc, useFont) : nullptr;
    TEXTMETRICW tm{};
    GetTextMetricsW(dc, &tm);
    RECT calc{0, 0, textW, 0};
    std::wstring forDt = text;
    // Normalize CRLF → LF so DrawText doesn't double-break.
    std::wstring norm;
    norm.reserve(forDt.size());
    for (size_t i = 0; i < forDt.size(); ++i) {
        if (forDt[i] == L'\r') {
            if (i + 1 < forDt.size() && forDt[i + 1] == L'\n') continue;
            norm.push_back(L'\n');
        } else {
            norm.push_back(forDt[i]);
        }
    }
    forDt.swap(norm);
    if (!forDt.empty())
        DrawTextW(dc, forDt.c_str(), -1, &calc, DT_CALCRECT | DT_WORDBREAK | DT_EDITCONTROL | DT_NOPREFIX);
    int byDraw = (int)(calc.bottom - calc.top);
    int lines = (int)SendMessageW(edit, EM_GETLINECOUNT, 0, 0);
    if (lines < 1) lines = 1;
    int lineH = tm.tmHeight + tm.tmExternalLeading;
    if (lineH < 1) lineH = BodyFontPx(dpi);
    int byLines = lines * lineH;
    if (old) SelectObject(dc, old);
    ReleaseDC(edit, dc);
    int content = (std::max)(byDraw, byLines);
    if (content < lineH) content = lineH;
    return (std::max)(minH, content + MulDiv(8, dpi, 96));
}

void LayoutChecklist(Note& note, bool refreshRail) {
    if (!note.checkHost) return;
    EnsureCheckBody(note);
    int dpi = WindowDpi(note.hwnd);
    int box = NoteCheckSize(dpi);
    int textLift = MulDiv(3, dpi, 96);
    int gap = MulDiv(8, dpi, 96);
    RECT host{};
    GetClientRect(note.checkHost, &host);
    int w = (std::max)(40, (int)host.right);
    int textX = 4 + box + 8;
    int textW = (std::max)(20, w - textX - 4);
    int y = 4;

    ChecklistCaretGuard caret(note.checkBody);
    for (size_t i = 0; i < note.items.size(); ++i) {
        auto& it = note.items[i];
        if (!it.row || !it.check || !it.edit) continue;
        int editH = MeasureChecklistEditH(it.edit, textW, dpi);
        int rowH = textLift + editH + gap;
        int boxY = textLift + (std::max)(0, (BodyFontPx(dpi) - box) / 2);
        MoveWindow(it.row, 0, y, w, rowH, FALSE);
        MoveWindow(it.check, 4, boxY, box, box, FALSE);
        MoveWindow(it.edit, textX, textLift, textW, editH, FALSE);
        y += rowH;
    }

    int viewH = (std::max)(1, (int)host.bottom);
    note.checkContentH = y + 4;
    int contentH = (std::max)(viewH, note.checkContentH);

    SCROLLINFO si{sizeof(si), SIF_POS};
    GetScrollInfo(note.checkHost, SB_VERT, &si);
    si.cbSize = sizeof(si);
    si.fMask = SIF_RANGE | SIF_PAGE | SIF_POS;
    si.nMin = 0;
    si.nMax = (std::max)(0, contentH - 1);
    si.nPage = (UINT)viewH;
    int maxPos = (std::max)(0, contentH - viewH);
    if (si.nPos > maxPos) si.nPos = maxPos;
    SetScrollInfo(note.checkHost, SB_VERT, &si, TRUE);

    MoveWindow(note.checkBody, 0, -si.nPos, w, contentH, FALSE);
    for (auto& it : note.items) {
        if (it.edit) SyncEditBgToNoteGradient(it.edit, note);
    }
    HideSystemScrollBar(note.checkHost);
    RedrawWindow(note.checkHost, nullptr, nullptr,
        RDW_INVALIDATE | RDW_ERASE | RDW_ALLCHILDREN);
    if (refreshRail) RefreshScrollRail(note);
}

bool ChecklistEditHeightChanged(Note& note, HWND edit) {
    if (!edit || !note.checkHost) return false;
    int dpi = WindowDpi(note.hwnd);
    int box = NoteCheckSize(dpi);
    RECT host{};
    GetClientRect(note.checkHost, &host);
    int textW = (std::max)(20, (int)host.right - 4 - box - 8 - 4);
    RECT er{};
    GetClientRect(edit, &er);
    return MeasureChecklistEditH(edit, textW, dpi) != er.bottom;
}

void ScheduleChecklistLayout(Note& note) {
    if (!note.hwnd) return;
    SetTimer(note.hwnd, kCheckLayoutTimer, 16, nullptr);
}

LRESULT CALLBACK RowEditProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp, UINT_PTR, DWORD_PTR);
LRESULT CALLBACK CheckRowProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp);

CheckItem CreateChecklistRowControls(Note& note, const CheckItem& model, int dpi) {
    EnsureCheckBody(note);
    int box = NoteCheckSize(dpi);
    CheckItem it = model;
    it.row = CreateWindowExW(0, L"StickyCheckRow", L"",
        WS_CHILD | WS_VISIBLE,
        0, 0, 100, NoteRowH(dpi), note.checkBody, nullptr, g_inst, nullptr);
    it.check = CreateWindowExW(0, L"BUTTON", L"",
        WS_CHILD | WS_VISIBLE | BS_OWNERDRAW,
        0, 0, box, box, it.row,
        (HMENU)(INT_PTR)ID_ROW_CHECK0, g_inst, nullptr);
    // RichEdit: real bold runs, soft wrap, Shift+Enter newlines.
    it.edit = CreateWindowExW(0, MSFTEDIT_CLASS, L"",
        WS_CHILD | WS_VISIBLE | ES_MULTILINE | ES_WANTRETURN,
        0, 0, 100, NoteRowH(dpi) - 4, it.row,
        (HMENU)(INT_PTR)ID_ROW_EDIT0, g_inst, nullptr);
    SendMessageW(it.edit, WM_SETFONT, (WPARAM)g_font, TRUE);
    SendMessageW(it.edit, EM_SETEVENTMASK, 0, ENM_CHANGE | ENM_SELCHANGE | ENM_SCROLL | ENM_SCROLLEVENTS);
    HideSystemScrollBar(it.edit);
    ApplyDefaultCharFormat(it.edit);
    SyncEditBgToNoteGradient(it.edit, note);
    if (!it.text.empty()) LoadBoldMarkdown(it.edit, it.text);
    SyncEditBgToNoteGradient(it.edit, note);
    SetWindowSubclass(it.edit, RowEditProc, 1, 0);
    return it;
}

void InsertChecklistRow(Note& note, size_t afterIdx) {
    SyncChecklistModel(note);
    if (!note.checkHost) {
        note.items.insert(note.items.begin() + (ptrdiff_t)afterIdx + 1, CheckItem{});
        BuildChecklistUi(note);
        return;
    }
    std::wstring tail;
    if (afterIdx < note.items.size() && note.items[afterIdx].edit) {
        HWND ed = note.items[afterIdx].edit;
        CHARRANGE cr{};
        SendMessageW(ed, EM_EXGETSEL, 0, (LPARAM)&cr);
        LONG cut = (std::min)(cr.cpMin, cr.cpMax);
        LONG len = GetWindowTextLengthW(ed);
        if (cut < len) {
            // Plain split for caret index; keep markdown on head via resync after truncate.
            std::wstring plain = CtrlText(ed);
            if (cut < (LONG)plain.size()) {
                tail = plain.substr((size_t)cut);
                CHARRANGE del{cut, len};
                SendMessageW(ed, EM_EXSETSEL, 0, (LPARAM)&del);
                SendMessageW(ed, EM_REPLACESEL, TRUE, (LPARAM)L"");
            }
            note.items[afterIdx].text = ChecklistItemToMd(ed);
            // Tail is plain — if head had bold only before cut, tail is unstyled (OK).
        }
    }
    int dpi = WindowDpi(note.hwnd);
    CheckItem model{};
    model.text = std::move(tail);
    CheckItem created = CreateChecklistRowControls(note, model, dpi);
    size_t at = afterIdx + 1;
    if (at > note.items.size()) at = note.items.size();
    note.items.insert(note.items.begin() + (ptrdiff_t)at, created);
    LayoutChecklist(note, true);
    SetFocus(created.edit);
    CHARRANGE z{0, 0};
    SendMessageW(created.edit, EM_EXSETSEL, 0, (LPARAM)&z);
    SendMessageW(created.edit, EM_SCROLLCARET, 0, 0);
    RedrawWindow(note.checkHost, nullptr, nullptr,
        RDW_INVALIDATE | RDW_ERASE | RDW_ALLCHILDREN | RDW_UPDATENOW);
}

void RemoveChecklistRow(Note& note, size_t idx) {
    SyncChecklistModel(note);
    if (idx >= note.items.size() || note.items.size() <= 1) return;
    if (note.items[idx].row) DestroyWindow(note.items[idx].row);
    else {
        if (note.items[idx].check) DestroyWindow(note.items[idx].check);
        if (note.items[idx].edit) DestroyWindow(note.items[idx].edit);
    }
    note.items.erase(note.items.begin() + (ptrdiff_t)idx);
    LayoutChecklist(note, true);
    // #region agent log
    {
        char data[96];
        snprintf(data, sizeof(data),
            "{\"idx\":%d,\"items\":%d,\"rebuild\":0,\"run\":\"post-fix10\"}",
            (int)idx, (int)note.items.size());
        AgentLog("F", "RemoveChecklistRow", "incremental", data);
    }
    // #endregion
}

void BuildChecklistUi(Note& note) {
    SyncChecklistModel(note);
    std::vector<CheckItem> model = note.items;
    for (auto& m : model) { m.row = nullptr; m.check = nullptr; m.edit = nullptr; }
    if (model.empty()) model.push_back({});

    if (!note.checkHost) {
        note.checkHost = CreateWindowExW(0, L"StickyCheckHost", L"",
            WS_CHILD | WS_VISIBLE | WS_VSCROLL,
            0, 0, 100, 100, note.hwnd, (HMENU)(INT_PTR)IDC_CHECKHOST, g_inst, nullptr);
        HideSystemScrollBar(note.checkHost);
    } else {
        for (HWND c = GetWindow(note.checkHost, GW_CHILD); c; ) {
            HWND n = GetWindow(c, GW_HWNDNEXT);
            DestroyWindow(c);
            c = n;
        }
        note.checkBody = nullptr;
    }
    EnsureCheckBody(note);

    int dpi = WindowDpi(note.hwnd);
    RecreateBodyFont(dpi);

    note.items.clear();
    for (size_t i = 0; i < model.size(); ++i) {
        note.items.push_back(CreateChecklistRowControls(note, model[i], dpi));
    }

    note.mode = NoteMode::Checklist;
    ShowWindow(note.edit, SW_HIDE);
    ShowWindow(note.checkHost, SW_SHOW);
    LayoutNote(note.hwnd);
    LayoutChecklist(note);
    // #region agent log
    {
        char data[160];
        snprintf(data, sizeof(data),
            "{\"dpi\":%d,\"fontPx\":%d,\"rowH\":%d,\"items\":%d,\"run\":\"post-fix10\"}",
            dpi, BodyFontPx(dpi), NoteRowH(dpi), (int)note.items.size());
        AgentLog("B", "BuildChecklistUi", "metrics", data);
    }
    // #endregion
}

void ConvertNoteToChecklist(Note& note) {
    // Build items from current text lines (strip a trailing/inline [])
    std::wstring md = RichEditToMarkdown(note.edit);
    // Remove checklist trigger markers
    for (;;) {
        size_t p = md.find(L"[]");
        if (p == std::wstring::npos) break;
        md.erase(p, 2);
    }
    while (!md.empty() && (md.back() == L'\n' || md.back() == L' ')) md.pop_back();

    std::vector<CheckItem> items;
    size_t i = 0;
    while (i <= md.size()) {
        size_t e = md.find(L'\n', i);
        std::wstring line = md.substr(i, e == std::wstring::npos ? std::wstring::npos : e - i);
        if (!line.empty() && line.back() == L'\r') line.pop_back();
        // strip list prefixes from markdown
        if (line.rfind(L"- ", 0) == 0) line = line.substr(2);
        else {
            size_t dot = line.find(L". ");
            if (dot != std::wstring::npos && dot > 0) {
                bool dig = true;
                for (size_t k = 0; k < dot; ++k) if (!iswdigit(line[k])) { dig = false; break; }
                if (dig) line = line.substr(dot + 2);
            }
        }
        while (!line.empty() && line[0] == L' ') line.erase(0, 1);
        if (!line.empty() || items.empty()) {
            CheckItem it;
            it.text = line;
            items.push_back(std::move(it));
        }
        if (e == std::wstring::npos) break;
        i = e + 1;
    }
    if (items.empty()) items.push_back({});
    // If last item empty from typing [] alone, keep one empty focused row
    note.items = std::move(items);
    BuildChecklistUi(note);
    if (!note.items.empty()) {
        SetFocus(note.items.back().edit);
        SendMessageW(note.items.back().edit, EM_SETSEL, 0, -1);
    }
}

LRESULT CALLBACK RowEditProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp, UINT_PTR, DWORD_PTR) {
    Note* note = FindNoteByRowEdit(hwnd);
    switch (msg) {
    case WM_MOUSEACTIVATE:
        return MA_ACTIVATE;
    case WM_ERASEBKGND:
        return 1;
    case WM_PAINT: {
        PAINTSTRUCT ps;
        HDC hdc = BeginPaint(hwnd, &ps);
        PaintTransparentRichEdit(hwnd, hdc, note);
        EndPaint(hwnd, &ps);
        return 0;
    }
    case WM_SETFOCUS:
        InvalidateRect(hwnd, nullptr, FALSE);
        break;
    case WM_KILLFOCUS:
        if (GetPropW(hwnd, L"ownCaret")) {
            DestroyCaret();
            RemovePropW(hwnd, L"ownCaret");
        }
        InvalidateRect(hwnd, nullptr, FALSE);
        if (note) {
            SyncChecklistModel(*note);
            if (note->hwnd) KillTimer(note->hwnd, kSaveTimer);
            SaveNote(*note);
            note->tags = ParseTags(SerializeNote(*note));
            RECT title{};
            GetClientRect(note->hwnd, &title);
            title.bottom = kTitleH;
            InvalidateRect(note->hwnd, &title, FALSE);
            RebuildTagIndex();
            RefreshBrowser();
        }
        break;
    case WM_MOUSEWHEEL:
        if (note && note->checkHost)
            return SendMessageW(note->checkHost, msg, wp, lp);
        break;
    case WM_CHAR:
        if (wp == 2) {
            ToggleBold(hwnd);
            if (note) ScheduleSave(*note);
            InvalidateRect(hwnd, nullptr, FALSE);
            return 0;
        }
        if (wp == '\r' || wp == '\n') return 0;
        break;
    case WM_KEYDOWN:
        if (!note) break;
        if (wp == VK_RETURN) {
            if (GetKeyState(VK_SHIFT) & 0x8000) {
                SendMessageW(hwnd, EM_REPLACESEL, TRUE, (LPARAM)L"\r\n");
                ScheduleSave(*note);
                LayoutChecklist(*note, true);
                SendMessageW(hwnd, EM_SCROLLCARET, 0, 0);
                InvalidateRect(hwnd, nullptr, FALSE);
                return 0;
            }
            size_t idx = 0;
            for (; idx < note->items.size(); ++idx)
                if (note->items[idx].edit == hwnd) break;
            InsertChecklistRow(*note, idx);
            ScheduleSave(*note);
            return 0;
        }
        if (wp == VK_BACK) {
            size_t idx = 0;
            for (; idx < note->items.size(); ++idx)
                if (note->items[idx].edit == hwnd) break;
            CHARRANGE cr{};
            SendMessageW(hwnd, EM_EXGETSEL, 0, (LPARAM)&cr);
            if (idx < note->items.size() && cr.cpMin == 0 && cr.cpMax == 0 &&
                GetWindowTextLengthW(hwnd) == 0 && note->items.size() > 1) {
                size_t f = idx > 0 ? idx - 1 : 0;
                RemoveChecklistRow(*note, idx);
                if (f >= note->items.size()) f = note->items.size() - 1;
                if (f < note->items.size()) {
                    SetFocus(note->items[f].edit);
                    int len = GetWindowTextLengthW(note->items[f].edit);
                    CHARRANGE end{(LONG)len, (LONG)len};
                    SendMessageW(note->items[f].edit, EM_EXSETSEL, 0, (LPARAM)&end);
                }
                ScheduleSave(*note);
                return 0;
            }
        }
        break;
    }
    LRESULT r = DefSubclassProc(hwnd, msg, wp, lp);
    // After typing/selection changes, repaint gradient+text.
    if (msg == WM_KEYUP || msg == WM_LBUTTONUP || (msg == WM_MOUSEMOVE && (wp & MK_LBUTTON)))
        InvalidateRect(hwnd, nullptr, FALSE);
    return r;
}

LRESULT CALLBACK CheckRowProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    HWND body = GetParent(hwnd);
    Note* note = nullptr;
    for (Note& n : g_notes) if (n.checkBody == body) { note = &n; break; }
    switch (msg) {
    case WM_DRAWITEM: {
        auto* dis = (DRAWITEMSTRUCT*)lp;
        if (!note || dis->CtlType != ODT_BUTTON) break;
        bool done = false;
        for (auto& it : note->items)
            if (it.check == dis->hwndItem) { done = it.done; break; }
        PaintCheckBox(dis, done, WindowDpi(note->hwnd ? note->hwnd : hwnd),
            note && note->paperBrush ? note->paperBrush : g_paper);
        return TRUE;
    }
    case WM_COMMAND:
        if (note && HIWORD(wp) == BN_CLICKED) {
            HWND btn = (HWND)lp;
            for (auto& it : note->items) {
                if (it.check == btn) {
                    it.done = !it.done;
                    InvalidateRect(it.check, nullptr, FALSE);
                    break;
                }
            }
            SyncChecklistModel(*note);
            SaveNote(*note);
            return 0;
        }
        if (note && HIWORD(wp) == EN_CHANGE) {
            ScheduleSave(*note);
            HWND edit = (HWND)lp;
            InvalidateRect(edit, nullptr, FALSE);
            if (ChecklistEditHeightChanged(*note, edit))
                LayoutChecklist(*note, true);
            return 0;
        }
        break;
    case WM_NOTIFY: {
        auto* hdr = (NMHDR*)lp;
        if (hdr && hdr->code == EN_SELCHANGE) {
            InvalidateRect(hdr->hwndFrom, nullptr, FALSE);
            return 0;
        }
        break;
    }
    case WM_ERASEBKGND: {
        if (note) {
            PaintNoteGradientSlice((HDC)wp, hwnd, *note);
            return 1;
        }
        RECT rc{};
        GetClientRect(hwnd, &rc);
        HBRUSH br = g_paper;
        FillRect((HDC)wp, &rc, br);
        return 1;
    }
    case WM_CTLCOLOREDIT:
    case WM_CTLCOLORBTN: {
        HDC dc = (HDC)wp;
        const NotePalette& pal = note ? NotePal(*note) : kPalettes[0];
        SetBkMode(dc, TRANSPARENT);
        SetTextColor(dc, pal.text);
        return (LRESULT)GetStockObject(NULL_BRUSH);
    }
    case WM_MOUSEWHEEL:
        if (note && note->checkHost)
            return SendMessageW(note->checkHost, msg, wp, lp);
        break;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

LRESULT CALLBACK CheckBodyProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    Note* note = nullptr;
    for (Note& n : g_notes) if (n.checkBody == hwnd) { note = &n; break; }
    switch (msg) {
    case WM_ERASEBKGND: {
        if (note) {
            PaintNoteGradientSlice((HDC)wp, hwnd, *note);
            return 1;
        }
        RECT rc{};
        GetClientRect(hwnd, &rc);
        FillRect((HDC)wp, &rc, g_paper);
        return 1;
    }
    case WM_MOUSEWHEEL:
        if (note && note->checkHost)
            return SendMessageW(note->checkHost, msg, wp, lp);
        break;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

LRESULT CALLBACK CheckHostProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    Note* note = nullptr;
    for (Note& n : g_notes) if (n.checkHost == hwnd) { note = &n; break; }
    switch (msg) {
    case WM_VSCROLL: {
        SCROLLINFO si{};
        if (!ReadVertScroll(hwnd, &si)) return 0;
        int pos = si.nPos;
        switch (LOWORD(wp)) {
        case SB_LINEUP: pos -= NoteRowH(WindowDpi(hwnd)); break;
        case SB_LINEDOWN: pos += NoteRowH(WindowDpi(hwnd)); break;
        case SB_PAGEUP: pos -= (int)si.nPage; break;
        case SB_PAGEDOWN: pos += (int)si.nPage; break;
        case SB_THUMBPOSITION:
        case SB_THUMBTRACK: pos = (int)HIWORD(wp); break;
        }
        SetVertScrollPos(hwnd, pos);
        return 0;
    }
    case WM_MOUSEWHEEL: {
        int delta = GET_WHEEL_DELTA_WPARAM(wp);
        SCROLLINFO si{};
        if (!ReadVertScroll(hwnd, &si)) return 0;
        UINT lines = 3;
        SystemParametersInfoW(SPI_GETWHEELSCROLLLINES, 0, &lines, 0);
        if (lines == 0) lines = 3;
        int linePx = (std::max)(12, NoteRowH(WindowDpi(hwnd)) / 3);
        int acc = (note ? note->wheelAcc : 0) + delta;
        int pixels = 0;
        if (lines >= WHEEL_PAGESCROLL)
            pixels = (acc / WHEEL_DELTA) * (int)si.nPage;
        else
            pixels = (acc / WHEEL_DELTA) * (int)lines * linePx;
        acc %= WHEEL_DELTA;
        if (note) note->wheelAcc = acc;
        if (pixels != 0) SetVertScrollPos(hwnd, si.nPos - pixels);
        return 0;
    }
    case WM_ERASEBKGND: {
        if (note) {
            PaintNoteGradientSlice((HDC)wp, hwnd, *note);
            return 1;
        }
        RECT rc{};
        GetClientRect(hwnd, &rc);
        FillRect((HDC)wp, &rc, g_paper);
        return 1;
    }
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

void LayoutNote(HWND hwnd) {
    Note* note = FindNote(hwnd);
    if (!note) return;
    RECT rc{};
    GetClientRect(hwnd, &rc);
    int w = rc.right;
    int h = rc.bottom;
    MoveWindow(note->closeBtn, w - kCloseSize - 8, (kTitleH - kCloseSize) / 2, kCloseSize, kCloseSize, TRUE);
    int top = kTitleH + 2;
    int ch = (std::max)(0, h - top - kColorBarH);
    int contentW = (std::max)(0, w - kPad * 2 - kScrollW - 2);
    if (note->mode == NoteMode::Checklist && note->checkHost) {
        MoveWindow(note->checkHost, kPad, top, contentW, ch, TRUE);
        LayoutChecklist(*note);
    } else if (note->edit) {
        MoveWindow(note->edit, kPad, top, contentW, ch, TRUE);
        HideSystemScrollBar(note->edit);
        SyncEditBgToNoteGradient(note->edit, *note);
    }
    if (note->scrollRail) {
        MoveWindow(note->scrollRail, w - kPad - kScrollW, top, kScrollW, ch, TRUE);
        SetWindowPos(note->scrollRail, HWND_TOP, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE);
        RefreshScrollRail(*note);
    }
}

void SaveNote(Note& note) {
    if (g_suppressSave || g_busy || !note.hwnd) return;
    // Never walk Rich Edit selection while the user is typing — that caused caret jumps /
    // ghost carets when editing inside lists. Defer until focus leaves the edit.
    HWND focus = GetFocus();
    if (note.mode == NoteMode::Text && note.edit && focus == note.edit) {
        SetTimer(note.hwnd, kSaveTimer, 1200, nullptr);
        return;
    }
    if (note.mode == NoteMode::Checklist && focus) {
        for (const auto& it : note.items) {
            if (it.edit == focus) {
                SetTimer(note.hwnd, kSaveTimer, 1200, nullptr);
                return;
            }
        }
    }
    // #region agent log
    AgentLog("D", "SaveNote", "full_save", "{\"run\":\"post-fix12\"}");
    // #endregion
    RECT r{};
    GetWindowRect(note.hwnd, &r);
    g_busy = true;
    CHARRANGE keep{};
    if (note.edit) SendMessageW(note.edit, EM_EXGETSEL, 0, (LPARAM)&keep);
    std::wstring body = SerializeNote(note);
    if (note.edit) SendMessageW(note.edit, EM_EXSETSEL, 0, (LPARAM)&keep);
    g_busy = false;
    note.tags = ParseTags(body);

    std::ostringstream out;
    out << "<!-- traynotes x=" << r.left
        << " y=" << r.top
        << " w=" << (r.right - r.left)
        << " h=" << (r.bottom - r.top)
        << " hidden=" << (note.hidden ? 1 : 0)
        << " mode=" << (note.mode == NoteMode::Checklist ? "check" : "text")
        << " color=" << note.colorIdx
        << " -->\n";
    out << WideToUtf8(body);
    if (!body.empty() && body.back() != L'\n') out << "\n";

    FILE* f = _wfopen(note.path.c_str(), L"wb");
    if (!f) return;
    std::string utf = out.str();
    fwrite(utf.data(), 1, utf.size(), f);
    fclose(f);
}

void ScheduleSave(Note& note) {
    if (!note.hwnd) return;
    SetTimer(note.hwnd, kSaveTimer, 400, nullptr);
}

void SaveAll() {
    for (Note& n : g_notes) {
        if (n.hwnd) KillTimer(n.hwnd, kSaveTimer);
        SaveNote(n);
    }
}

void PlaceOnScreen(int& x, int& y, int& w, int& h) {
    if (w < kMinW) w = 300;
    if (h < kMinH) h = 240;
    RECT work{};
    SystemParametersInfoW(SPI_GETWORKAREA, 0, &work, 0);
    if (x < work.left - w + 40 || x > work.right - 40 || y < work.top || y > work.bottom - 40) {
        int step = (int)g_notes.size() * 28;
        x = work.left + 80 + step;
        y = work.top + 80 + step;
    }
}

void StyleFrame(HWND hwnd) {
    // DWMWA_BORDER_COLOR = 34, DWMWA_COLOR_NONE = 0xFFFFFFFE
    const COLORREF none = 0xFFFFFFFE;
    DwmSetWindowAttribute(hwnd, 34, &none, sizeof(none));
    // DWMWA_NCRENDERING_POLICY = 2, DWMNCRP_DISABLED = 1
    const int policy = 1;
    DwmSetWindowAttribute(hwnd, 2, &policy, sizeof(policy));
}

void HideNote(Note& note) {
    if (note.hwnd) KillTimer(note.hwnd, kSaveTimer);
    note.hidden = true;
    ShowWindow(note.hwnd, SW_HIDE);
    SaveNote(note);
    RefreshBrowser();
}

void ShowNote(Note& note) {
    note.hidden = false;
    ShowWindow(note.hwnd, SW_SHOW);
    SetWindowPos(note.hwnd, HWND_NOTOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_SHOWWINDOW);
    SetForegroundWindow(note.hwnd);
    SaveNote(note);
    RefreshBrowser();
}

void HideAllNotes() {
    for (Note& n : g_notes) {
        n.hidden = true;
        ShowWindow(n.hwnd, SW_HIDE);
        SaveNote(n);
    }
    RefreshBrowser();
}

LRESULT CALLBACK EditProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    Note* note = FindNoteByEdit(hwnd);
    WNDPROC old = note ? note->editOld : DefWindowProcW;
    if (msg == WM_MOUSEACTIVATE)
        return MA_ACTIVATE;
    if (msg == WM_ERASEBKGND)
        return 1;
    if (msg == WM_PAINT) {
        PAINTSTRUCT ps;
        HDC hdc = BeginPaint(hwnd, &ps);
        PaintTransparentRichEdit(hwnd, hdc, note);
        EndPaint(hwnd, &ps);
        return 0;
    }
    if (msg == WM_SETFOCUS) {
        InvalidateRect(hwnd, nullptr, FALSE);
    }
    if (msg == WM_CHAR && wp == 2) {
        ToggleBold(hwnd);
        if (note) ScheduleSave(*note);
        InvalidateRect(hwnd, nullptr, FALSE);
        return 0;
    }
    if (msg == WM_MOUSEWHEEL) {
        LRESULT r = CallWindowProcW(old, hwnd, msg, wp, lp);
        if (note) ScheduleScrollRailRefresh(*note);
        InvalidateRect(hwnd, nullptr, FALSE);
        return r;
    }
    if (msg == WM_KILLFOCUS) {
        if (GetPropW(hwnd, L"ownCaret")) {
            DestroyCaret();
            RemovePropW(hwnd, L"ownCaret");
        }
        // Flush deferred save now that selection walks are safe.
        if (note) {
            KillTimer(note->hwnd, kSaveTimer);
            SaveNote(*note);
        }
        InvalidateRect(hwnd, nullptr, FALSE);
        return CallWindowProcW(old, hwnd, msg, wp, lp);
    }
    if (msg == WM_KEYDOWN && wp == VK_RETURN) {
        // Inspect current paragraph BEFORE Enter — avoid NumberingAtCp selection thrash.
        CHARRANGE before{};
        SendMessageW(hwnd, EM_EXGETSEL, 0, (LPARAM)&before);
        LONG line = (LONG)SendMessageW(hwnd, EM_LINEFROMCHAR, (WPARAM)before.cpMin, 0);
        LONG lineStart = (LONG)SendMessageW(hwnd, EM_LINEINDEX, (WPARAM)line, 0);
        LONG lineLen = (LONG)SendMessageW(hwnd, EM_LINELENGTH, (WPARAM)lineStart, 0);
        WORD num = GetParaNumbering(hwnd);
        std::wstring cur(static_cast<size_t>(lineLen) + 2, 0);
        *(WORD*)cur.data() = (WORD)(lineLen + 1);
        LONG got = (LONG)SendMessageW(hwnd, EM_GETLINE, (WPARAM)line, (LPARAM)cur.data());
        if (got < 0) got = 0;
        cur.resize(static_cast<size_t>(got));
        while (!cur.empty() && (cur.back() == L'\r' || cur.back() == L'\n')) cur.pop_back();

        bool emptyGlyph = cur.rfind(std::wstring(1, kBulletChar) + L" ", 0) == 0 && cur.size() == 2;
        bool emptyGlyphOnly = cur == std::wstring(1, kBulletChar);
        bool emptyListPara = (num == 2 || num == PFN_BULLET) && lineLen == 0;

        if (emptyGlyph || emptyGlyphOnly || emptyListPara) {
            // Double Enter on empty item → exit list (plain paragraph).
            g_busy = true;
            ClearParaList(hwnd);
            if (emptyGlyph || emptyGlyphOnly) {
                CHARRANGE sel{lineStart, lineStart + (LONG)cur.size()};
                SendMessageW(hwnd, EM_EXSETSEL, 0, (LPARAM)&sel);
                SendMessageW(hwnd, EM_REPLACESEL, TRUE, (LPARAM)L"");
            }
            g_busy = false;
            LRESULT r = CallWindowProcW(old, hwnd, msg, wp, lp);
            AgentLog("P", "EditProc", "list_exit", "{\"run\":\"post-fix12\"}");
            return r;
        }

        LRESULT r = CallWindowProcW(old, hwnd, msg, wp, lp);

        // Glyph bullets need the • prefix re-inserted; numbered paras continue natively.
        if (cur.rfind(std::wstring(1, kBulletChar) + L" ", 0) == 0) {
            g_busy = true;
            wchar_t rep[3] = {kBulletChar, L' ', 0};
            SendMessageW(hwnd, EM_REPLACESEL, TRUE, (LPARAM)rep);
            g_busy = false;
            AgentLog("G", "EditProc", "ul_continue", "{\"run\":\"post-fix12\"}");
        } else if (num == 2) {
            // Ensure new para continues numbering — do not renorm the whole run (caret corruption).
            SetParaNumberContinue(hwnd);
            AgentLog("N", "EditProc", "ol_continue_native", "{\"run\":\"post-fix12\"}");
        }
        return r;
    }
    LRESULT r = CallWindowProcW(old, hwnd, msg, wp, lp);
    if (msg == WM_CHAR || msg == WM_KEYUP || msg == WM_LBUTTONUP || msg == WM_LBUTTONDOWN ||
        (msg == WM_MOUSEMOVE && (wp & MK_LBUTTON)))
        InvalidateRect(hwnd, nullptr, FALSE);
    return r;
}

LRESULT CALLBACK CloseBtnProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
    case WM_PAINT: {
        PAINTSTRUCT ps;
        HDC dc = BeginPaint(hwnd, &ps);
        RECT rc{};
        GetClientRect(hwnd, &rc);
        bool hover = (bool)GetWindowLongPtrW(hwnd, GWLP_USERDATA);
        Note* note = FindNote(GetParent(hwnd));
        const NotePalette& pal = note ? NotePal(*note) : kPalettes[0];
        if (note) PaintNoteGradientSlice(dc, hwnd, *note);
        else {
            FillRect(dc, &rc, g_paper);
        }
        int cx = (rc.left + rc.right) / 2;
        int cy = (rc.top + rc.bottom) / 2;
        int r = (std::min)(rc.right - rc.left, rc.bottom - rc.top) / 2 - 1;
        HPEN pen = CreatePen(PS_SOLID, 1, pal.text);
        HGDIOBJ oldPen = SelectObject(dc, pen);
        SelectObject(dc, GetStockObject(NULL_BRUSH));
        Ellipse(dc, cx - r, cy - r, cx + r, cy + r);
        if (hover) {
            HPEN xpen = CreatePen(PS_SOLID, 2, pal.text);
            SelectObject(dc, xpen);
            int d = r / 2;
            MoveToEx(dc, cx - d, cy - d, nullptr);
            LineTo(dc, cx + d, cy + d);
            MoveToEx(dc, cx + d, cy - d, nullptr);
            LineTo(dc, cx - d, cy + d);
            SelectObject(dc, pen);
            DeleteObject(xpen);
        }
        SelectObject(dc, oldPen);
        DeleteObject(pen);
        EndPaint(hwnd, &ps);
        return 0;
    }
    case WM_ERASEBKGND: return 1;
    case WM_MOUSEMOVE:
        if (!GetWindowLongPtrW(hwnd, GWLP_USERDATA)) {
            SetWindowLongPtrW(hwnd, GWLP_USERDATA, 1);
            TRACKMOUSEEVENT tme{sizeof(tme), TME_LEAVE, hwnd, 0};
            TrackMouseEvent(&tme);
            InvalidateRect(hwnd, nullptr, FALSE);
        }
        return 0;
    case WM_MOUSELEAVE:
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, 0);
        InvalidateRect(hwnd, nullptr, FALSE);
        return 0;
    case WM_LBUTTONUP:
        PostMessageW(GetParent(hwnd), WM_COMMAND, MAKEWPARAM(ID_CLOSE_BTN, BN_CLICKED), 0);
        return 0;
    case WM_SETCURSOR:
        SetCursor(LoadCursorW(nullptr, IDC_HAND));
        return TRUE;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

LRESULT CALLBACK NoteProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
    case WM_MOUSEACTIVATE:
        // Tool-window notes: don't eat the activating click so caret lands immediately.
        return MA_ACTIVATE;
    case WM_CREATE: {
        auto* cs = (CREATESTRUCTW*)lp;
        auto* note = (Note*)cs->lpCreateParams;
        note->hwnd = hwnd;
        note->closeBtn = CreateWindowExW(0, L"StickyCloseBtn", L"", WS_CHILD | WS_VISIBLE,
            0, 0, kCloseSize, kCloseSize, hwnd, (HMENU)(INT_PTR)ID_CLOSE_BTN, g_inst, nullptr);
        note->edit = CreateWindowExW(0, MSFTEDIT_CLASS, L"",
            WS_CHILD | WS_VISIBLE | WS_VSCROLL | ES_MULTILINE | ES_AUTOVSCROLL,
            0, 0, 0, 0, hwnd, (HMENU)(INT_PTR)IDC_EDIT, g_inst, nullptr);
        HideSystemScrollBar(note->edit);
        int dpi = WindowDpi(hwnd);
        RecreateBodyFont(dpi);
        SendMessageW(note->edit, WM_SETFONT, (WPARAM)g_font, TRUE);
        SendMessageW(note->edit, EM_SETEVENTMASK, 0, ENM_CHANGE | ENM_SELCHANGE | ENM_SCROLL | ENM_SCROLLEVENTS);
        ApplyDefaultCharFormat(note->edit);
        SyncEditBgToNoteGradient(note->edit, *note);
        note->editOld = (WNDPROC)SetWindowLongPtrW(note->edit, GWLP_WNDPROC, (LONG_PTR)EditProc);
        note->scrollRail = CreateWindowExW(0, L"StickyScrollRail", L"",
            WS_CHILD | WS_VISIBLE,
            0, 0, kScrollW, 10, hwnd, nullptr, g_inst, nullptr);
        SetWindowLongPtrW(note->scrollRail, GWLP_USERDATA, (LONG_PTR)note->edit);
        StyleFrame(hwnd);
        return 0;
    }
    case WM_SIZE:
        LayoutNote(hwnd);
        return 0;
    case WM_NCCALCSIZE:
        if (wp) {
            // No reserved NC band — avoids the light “focus” frame around the note.
            return 0;
        }
        break;
    case WM_NCPAINT:
        return 0;
    case WM_NCACTIVATE:
        // Suppress default NC active/inactive focus cue; client paints the chrome.
        return TRUE;
    case WM_ERASEBKGND: {
        Note* note = FindNote(hwnd);
        RECT rc{};
        GetClientRect(hwnd, &rc);
        const NotePalette& pal = note ? NotePal(*note) : kPalettes[0];
        FillGradientRect((HDC)wp, rc, pal.top, pal.bottom);
        return 1;
    }
    case WM_PAINT: {
        PAINTSTRUCT ps;
        HDC dc = BeginPaint(hwnd, &ps);
        RECT rc{};
        GetClientRect(hwnd, &rc);
        Note* note = FindNote(hwnd);
        const NotePalette& pal = note ? NotePal(*note) : kPalettes[0];
        FillGradientRect(dc, rc, pal.top, pal.bottom);
        SetBkMode(dc, TRANSPARENT);
        SetTextColor(dc, pal.text);
        HFONT oldF = (HFONT)SelectObject(dc, g_uiFont);
        std::wstring label = note && note->mode == NoteMode::Checklist ? L"Checklist" : L"Sticky";
        if (note && !note->tags.empty()) {
            label = L"#";
            label += note->tags[0];
            if (note->tags.size() > 1) label += L" …";
        }
        RECT textRc{10, 0, rc.right - kCloseSize - 14, kTitleH};
        DrawTextW(dc, label.c_str(), -1, &textRc, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
        SelectObject(dc, oldF);
        int dpi = WindowDpi(hwnd);
        for (int i = 0; i < kColorCount; ++i) {
            RECT d = ColorDotRect(rc, i, dpi);
            HBRUSH br = CreateSolidBrush(kPalettes[i].dot);
            HGDIOBJ oldBr = SelectObject(dc, br);
            HPEN dp = CreatePen(PS_SOLID, (note && note->colorIdx == i) ? 2 : 1,
                (note && note->colorIdx == i) ? pal.text : RGB(0, 0, 0));
            HGDIOBJ oldPen2 = SelectObject(dc, note && note->colorIdx == i ? dp : GetStockObject(NULL_PEN));
            Ellipse(dc, d.left, d.top, d.right, d.bottom);
            SelectObject(dc, oldPen2);
            SelectObject(dc, oldBr);
            DeleteObject(br);
            DeleteObject(dp);
        }
        EndPaint(hwnd, &ps);
        return 0;
    }
    case WM_LBUTTONDOWN: {
        Note* note = FindNote(hwnd);
        if (!note) break;
        POINT pt{GET_X_LPARAM(lp), GET_Y_LPARAM(lp)};
        RECT rc{};
        GetClientRect(hwnd, &rc);
        int dpi = WindowDpi(hwnd);
        for (int i = 0; i < kColorCount; ++i) {
            RECT d = ColorDotRect(rc, i, dpi);
            InflateRect(&d, 4, 4);
            if (PtInRect(&d, pt)) {
                note->colorIdx = i;
                ApplyNoteTheme(*note);
                ScheduleSave(*note);
                // #region agent log
                {
                    char data[64];
                    snprintf(data, sizeof(data), "{\"color\":%d,\"run\":\"post-fix11\"}", i);
                    AgentLog("Q", "NoteProc", "color_pick", data);
                }
                // #endregion
                return 0;
            }
        }
        break;
    }
    case WM_NCHITTEST: {
        POINT pt{GET_X_LPARAM(lp), GET_Y_LPARAM(lp)};
        ScreenToClient(hwnd, &pt);
        RECT rc{};
        GetClientRect(hwnd, &rc);
        bool L = pt.x < kEdge, R = pt.x >= rc.right - kEdge;
        bool T = pt.y < kEdge, B = pt.y >= rc.bottom - kEdge;
        if (T && L) return HTTOPLEFT;
        if (T && R) return HTTOPRIGHT;
        if (B && L) return HTBOTTOMLEFT;
        if (B && R) return HTBOTTOMRIGHT;
        if (L) return HTLEFT;
        if (R) return HTRIGHT;
        if (T) return HTTOP;
        if (B) return HTBOTTOM;
        if (pt.y < kTitleH) {
            Note* note = FindNote(hwnd);
            if (note && note->closeBtn) {
                RECT c{};
                GetWindowRect(note->closeBtn, &c);
                POINT sp{GET_X_LPARAM(lp), GET_Y_LPARAM(lp)};
                if (PtInRect(&c, sp)) return HTCLIENT;
            }
            return HTCAPTION;
        }
        return HTCLIENT;
    }
    case WM_NOTIFY: {
        auto* hdr = (NMHDR*)lp;
        if (hdr && hdr->code == EN_SELCHANGE) {
            InvalidateRect(hdr->hwndFrom, nullptr, FALSE);
            return 0;
        }
        break;
    }
    case WM_COMMAND: {
        Note* note = FindNote(hwnd);
        if (!note) return 0;
        if (HIWORD(wp) == EN_VSCROLL || HIWORD(wp) == EN_HSCROLL) {
            ScheduleScrollRailRefresh(*note);
            return 0;
        }
        if (HIWORD(wp) == EN_CHANGE && LOWORD(wp) == IDC_EDIT) {
            if (g_busy || g_suppressSave) return 0;
            InvalidateRect(note->edit, nullptr, FALSE);
            // #region agent log
            ++g_dbgEnChange;
            int serBefore = g_dbgSerializeCalls;
            int boldBefore = g_dbgIsBoldCalls;
            DWORD t0 = GetTickCount();
            // #endregion
            RuleResult rr = TryInputRules(note->edit);
            // #region agent log
            {
                char data[160];
                snprintf(data, sizeof(data),
                    "{\"rule\":%d,\"enChangeN\":%d,\"run\":\"post-fix\"}",
                    (int)rr, g_dbgEnChange);
                AgentLog("E", "NoteProc:EN_CHANGE", "after_input_rules", data);
            }
            // #endregion
            if (rr == RuleResult::ToChecklist) {
                ConvertNoteToChecklist(*note);
                SaveNote(*note);
                RebuildTagIndex();
                RefreshBrowser();
                InvalidateRect(hwnd, nullptr, FALSE);
                return 0;
            }
            // Lightweight per-keystroke path: plain text for tags only — no RichEditToMarkdown.
            auto oldTags = note->tags;
            note->tags = ParseTags(CtrlText(note->edit));
            bool tagsChanged = oldTags != note->tags;
            if (tagsChanged) {
                RECT title{0, 0, 0, kTitleH};
                GetClientRect(hwnd, &title);
                title.bottom = kTitleH;
                InvalidateRect(hwnd, &title, FALSE);
                RebuildTagIndex();
                RefreshBrowser();
                // #region agent log
                AgentLog("B", "NoteProc:EN_CHANGE", "invalidate_title_only",
                    "{\"called\":1,\"run\":\"post-fix\"}");
                // #endregion
            }
            ScheduleSave(*note);
            if (note->mode == NoteMode::Text) RefreshScrollRail(*note);
            // #region agent log
            {
                char data[220];
                snprintf(data, sizeof(data),
                    "{\"serializeDelta\":%d,\"isBoldDelta\":%d,\"ms\":%lu,\"enChangeN\":%d,\"tagsChanged\":%d,\"run\":\"post-fix\"}",
                    g_dbgSerializeCalls - serBefore,
                    g_dbgIsBoldCalls - boldBefore,
                    (unsigned long)(GetTickCount() - t0),
                    g_dbgEnChange,
                    tagsChanged ? 1 : 0);
                AgentLog("D", "NoteProc:EN_CHANGE", "keystroke_cost", data);
            }
            // #endregion
            return 0;
        }
        if (LOWORD(wp) == ID_CLOSE_BTN) HideNote(*note);
        if (LOWORD(wp) == ID_BOLD) {
            HWND focus = GetFocus();
            if (note->mode == NoteMode::Text && note->edit) {
                ToggleBold(note->edit);
                SetFocus(note->edit);
                ScheduleSave(*note);
            } else if (note->mode == NoteMode::Checklist) {
                for (auto& it : note->items) {
                    if (it.edit == focus) {
                        ToggleBold(it.edit);
                        ScheduleSave(*note);
                        break;
                    }
                }
            }
        }
        return 0;
    }
    case WM_TIMER:
        if (wp == kSaveTimer) {
            KillTimer(hwnd, kSaveTimer);
            if (Note* n = FindNote(hwnd)) SaveNote(*n);
            return 0;
        }
        if (wp == kScrollTimer) {
            KillTimer(hwnd, kScrollTimer);
            if (Note* n = FindNote(hwnd)) RefreshScrollRail(*n);
            return 0;
        }
        if (wp == kCheckLayoutTimer) {
            KillTimer(hwnd, kCheckLayoutTimer);
            if (Note* n = FindNote(hwnd))
                LayoutChecklist(*n, true);
            return 0;
        }
        break;
    case WM_EXITSIZEMOVE:
        if (Note* n = FindNote(hwnd)) {
            KillTimer(hwnd, kSaveTimer);
            SaveNote(*n);
        }
        return 0;
    case WM_GETMINMAXINFO: {
        auto* info = (MINMAXINFO*)lp;
        info->ptMinTrackSize = {kMinW, kMinH};
        return 0;
    }
    case WM_CLOSE:
        if (Note* n = FindNote(hwnd)) HideNote(*n);
        return 0;
    case WM_DESTROY:
        if (!g_quitting) {
            for (auto& n : g_notes)
                if (n.hwnd == hwnd) {
                    if (n.paperBrush) { DeleteObject(n.paperBrush); n.paperBrush = nullptr; }
                    n.hwnd = nullptr;
                    break;
                }
        }
        return 0;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

void CreateNoteWindow(Note& note, int x, int y, int w, int h, const std::wstring& md, bool show) {
    PlaceOnScreen(x, y, w, h);
    HWND hwnd = CreateWindowExW(
        WS_EX_TOOLWINDOW,
        L"StickyNote", L"Sticky",
        WS_POPUP | WS_THICKFRAME,
        x, y, w, h, nullptr, nullptr, g_inst, &note);
    if (!hwnd) return;
    // #region agent log
    {
        char data[128];
        snprintf(data, sizeof(data),
            "{\"topmost\":0,\"dpi\":%d,\"fontPx\":%d,\"twips\":%ld,\"run\":\"post-fix5\"}",
            WindowDpi(hwnd), BodyFontPx(WindowDpi(hwnd)), (long)BodyTwips(WindowDpi(hwnd)));
        AgentLog("K", "CreateNoteWindow", "zorder_font", data);
    }
    // #endregion
    StyleFrame(hwnd);
    g_suppressSave = true;
    if (note.mode == NoteMode::Checklist || LooksLikeChecklist(md)) {
        note.items = ParseChecklistMd(md);
        BuildChecklistUi(note);
    } else {
        note.mode = NoteMode::Text;
        LoadMarkdownToRichEdit(note.edit, md);
        note.tags = ParseTags(md);
    }
    note.hidden = !show;
    g_suppressSave = false;
    ApplyNoteTheme(note);
    LayoutNote(hwnd);
    ShowWindow(hwnd, show ? SW_SHOW : SW_HIDE);
    RefreshScrollRail(note);
    if (show) {
        UpdateWindow(hwnd);
        SetForegroundWindow(hwnd);
        if (note.mode == NoteMode::Text) SetFocus(note.edit);
        else if (!note.items.empty()) SetFocus(note.items[0].edit);
    }
}

void NewNote() {
    std::wstring id = NewNoteId();
    Note note;
    note.id = id;
    note.path = g_exeDir + id + L".md";
    g_notes.push_back(note);
    CreateNoteWindow(g_notes.back(), 0, 0, 300, 240, L"#inbox\n", true);
    SaveNote(g_notes.back());
    RebuildTagIndex();
    RefreshBrowser();
}

bool ParseMeta(const std::string& line, int& x, int& y, int& w, int& h, bool& hidden, bool& checklist, int& color) {
    if (line.find("<!-- traynotes") == std::string::npos) return false;
    auto grab = [&](const char* key, int& out) {
        size_t p = line.find(key);
        if (p == std::string::npos) return;
        out = atoi(line.c_str() + p + strlen(key));
    };
    grab("x=", x); grab("y=", y); grab("w=", w); grab("h=", h);
    size_t p = line.find("hidden=");
    if (p != std::string::npos) hidden = atoi(line.c_str() + p + 7) != 0;
    checklist = line.find("mode=check") != std::string::npos;
    int col = 0;
    grab("color=", col);
    if (col < 0 || col >= kColorCount) col = 0;
    color = col;
    return true;
}

void LoadNotes() {
    WIN32_FIND_DATAW fd{};
    HANDLE h = FindFirstFileW((g_exeDir + L"*.md").c_str(), &fd);
    if (h == INVALID_HANDLE_VALUE) return;
    do {
        if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) continue;
        std::wstring name = fd.cFileName;
        std::wstring path = g_exeDir + name;
        FILE* fp = _wfopen(path.c_str(), L"rb");
        if (!fp) continue;
        std::string raw;
        char buf[4096];
        while (size_t n = fread(buf, 1, sizeof(buf), fp)) raw.append(buf, n);
        fclose(fp);

        int x = 0, y = 0, w = 300, h = 240;
        bool hidden = false, checklist = false;
        int color = 0;
        std::string body = raw;
        size_t nl = raw.find('\n');
        if (nl != std::string::npos) {
            std::string first = raw.substr(0, nl);
            if (ParseMeta(first, x, y, w, h, hidden, checklist, color)) body = raw.substr(nl + 1);
        }
        while (!body.empty() && (body.back() == '\n' || body.back() == '\r')) body.pop_back();

        std::wstring id = name;
        if (id.size() > 3 && id.substr(id.size() - 3) == L".md") id.resize(id.size() - 3);
        if (id.rfind(L"note-", 0) == 0) {
            int num = _wtoi(id.c_str() + 5);
            if (num >= g_nextId) g_nextId = num + 1;
        }

        Note note;
        note.id = id;
        note.path = path;
        note.hidden = hidden;
        note.colorIdx = color;
        if (checklist) note.mode = NoteMode::Checklist;
        g_notes.push_back(note);
        CreateNoteWindow(g_notes.back(), x, y, w, h, Utf8ToWide(body), !hidden);
    } while (FindNextFileW(h, &fd));
    FindClose(h);
    RebuildTagIndex();
}

void FillNoteList(HWND list) {
    SendMessageW(list, LB_RESETCONTENT, 0, 0);
    int added = 0;
    for (size_t i = 0; i < g_notes.size(); ++i) {
        const Note& n = g_notes[i];
        if (!g_activeFilter.empty()) {
            bool ok = false;
            for (const auto& t : n.tags) if (t == g_activeFilter) { ok = true; break; }
            if (!ok) continue;
        }
        std::wstring label = n.id;
        if (n.mode == NoteMode::Checklist) label += L" ☑";
        if (!n.tags.empty()) {
            label += L"  [";
            for (size_t t = 0; t < n.tags.size(); ++t) {
                if (t) label += L" ";
                label += L"#";
                label += n.tags[t];
            }
            label += L"]";
        }
        if (n.hidden) label += L"  (hidden)";
        int idx = (int)SendMessageW(list, LB_ADDSTRING, 0, (LPARAM)label.c_str());
        SendMessageW(list, LB_SETITEMDATA, idx, (LPARAM)i);
        ++added;
    }
    // #region agent log
    {
        char data[128];
        snprintf(data, sizeof(data),
            "{\"notes\":%d,\"added\":%d,\"listHwnd\":%llu,\"run\":\"post-fix3\"}",
            (int)g_notes.size(), added, (unsigned long long)(ULONG_PTR)list);
        AgentLog("I", "FillNoteList", "filled", data);
    }
    // #endregion
}

void RebuildFilterBar(HWND browser) {
    for (HWND child = GetWindow(browser, GW_CHILD); child; ) {
        HWND next = GetWindow(child, GW_HWNDNEXT);
        int id = GetDlgCtrlID(child);
        // Only destroy tag filter chips — never the list or action buttons (ids >= 200).
        if (id == ID_FILTER_ALL || (id >= ID_FILTER_BASE && id < ID_NOTE_LIST))
            DestroyWindow(child);
        child = next;
    }
    int x = 12, y = 12;
    auto add = [&](int id, const wchar_t* text, bool active) {
        HWND b = CreateWindowExW(0, L"BUTTON", text,
            WS_CHILD | WS_VISIBLE | (active ? BS_DEFPUSHBUTTON : BS_PUSHBUTTON),
            x, y, 0, 24, browser, (HMENU)(INT_PTR)id, g_inst, nullptr);
        SendMessageW(b, WM_SETFONT, (WPARAM)g_uiFont, TRUE);
        SIZE sz{};
        HDC dc = GetDC(b);
        HFONT old = (HFONT)SelectObject(dc, g_uiFont);
        GetTextExtentPoint32W(dc, text, (int)wcslen(text), &sz);
        SelectObject(dc, old);
        ReleaseDC(b, dc);
        SetWindowPos(b, nullptr, x, y, sz.cx + 18, 24, SWP_NOZORDER);
        x += sz.cx + 24;
    };
    add(ID_FILTER_ALL, L"All", g_activeFilter.empty());
    for (size_t i = 0; i < g_allTags.size(); ++i) {
        std::wstring label = L"#" + g_allTags[i];
        add(ID_FILTER_BASE + (int)i, label.c_str(), g_activeFilter == g_allTags[i]);
    }
}

void RefreshBrowser() {
    // #region agent log
    AgentLog("C", "RefreshBrowser", "rebuild_browser",
        g_browser && IsWindow(g_browser) ? "{\"visible\":1}" : "{\"visible\":0}");
    // #endregion
    if (!g_browser || !IsWindow(g_browser)) return;
    RebuildFilterBar(g_browser);
    HWND list = GetDlgItem(g_browser, ID_NOTE_LIST);
    if (list) FillNoteList(list);
}

void OpenBrowser() {
    if (g_browser && IsWindow(g_browser)) {
        ShowWindow(g_browser, SW_SHOW);
        SetForegroundWindow(g_browser);
        RefreshBrowser();
        return;
    }
    g_browser = CreateWindowExW(WS_EX_TOOLWINDOW, L"NotesBrowser", L"All notes",
        WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_THICKFRAME | WS_MINIMIZEBOX,
        120, 120, 520, 420, nullptr, nullptr, g_inst, nullptr);
    ShowWindow(g_browser, SW_SHOW);
    RefreshBrowser();
}

LRESULT CALLBACK BrowserProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
    case WM_CREATE: {
        CreateWindowExW(WS_EX_CLIENTEDGE, L"LISTBOX", L"",
            WS_CHILD | WS_VISIBLE | WS_VSCROLL | LBS_NOTIFY | LBS_NOINTEGRALHEIGHT,
            12, 48, 480, 280, hwnd, (HMENU)(INT_PTR)ID_NOTE_LIST, g_inst, nullptr);
        auto mk = [&](int id, const wchar_t* text, int x) {
            HWND b = CreateWindowExW(0, L"BUTTON", text, WS_CHILD | WS_VISIBLE,
                x, 340, 100, 28, hwnd, (HMENU)(INT_PTR)id, g_inst, nullptr);
            SendMessageW(b, WM_SETFONT, (WPARAM)g_uiFont, TRUE);
        };
        mk(ID_SHOW_OPEN, L"Show", 12);
        mk(ID_SHOW_NEW, L"New", 122);
        mk(ID_SHOW_DELETE, L"Delete", 232);
        SendMessageW(GetDlgItem(hwnd, ID_NOTE_LIST), WM_SETFONT, (WPARAM)g_font, TRUE);
        return 0;
    }
    case WM_SIZE: {
        RECT rc{};
        GetClientRect(hwnd, &rc);
        MoveWindow(GetDlgItem(hwnd, ID_NOTE_LIST), 12, 48, rc.right - 24, rc.bottom - 100, TRUE);
        int y = rc.bottom - 40;
        MoveWindow(GetDlgItem(hwnd, ID_SHOW_OPEN), 12, y, 100, 28, TRUE);
        MoveWindow(GetDlgItem(hwnd, ID_SHOW_NEW), 122, y, 100, 28, TRUE);
        MoveWindow(GetDlgItem(hwnd, ID_SHOW_DELETE), 232, y, 100, 28, TRUE);
        RebuildFilterBar(hwnd);
        return 0;
    }
    case WM_COMMAND: {
        int id = LOWORD(wp);
        // #region agent log
        {
            char data[96];
            snprintf(data, sizeof(data), "{\"id\":%d,\"notify\":%d,\"run\":\"post-fix4\"}",
                id, (int)HIWORD(wp));
            AgentLog("J", "BrowserProc:WM_COMMAND", "cmd", data);
        }
        // #endregion
        if (id == ID_FILTER_ALL) { g_activeFilter.clear(); RefreshBrowser(); return 0; }
        // Filter chips only: 101 .. 199 (must NOT include Show/New/Delete at 201+)
        if (id >= ID_FILTER_BASE && id < ID_NOTE_LIST) {
            size_t idx = (size_t)(id - ID_FILTER_BASE);
            if (idx < g_allTags.size()) { g_activeFilter = g_allTags[idx]; RefreshBrowser(); }
            return 0;
        }
        HWND list = GetDlgItem(hwnd, ID_NOTE_LIST);
        int sel = (int)SendMessageW(list, LB_GETCURSEL, 0, 0);
        size_t noteIndex = sel == LB_ERR ? (size_t)-1 : (size_t)SendMessageW(list, LB_GETITEMDATA, sel, 0);
        auto selected = [&]() -> Note* {
            return noteIndex < g_notes.size() ? &g_notes[noteIndex] : nullptr;
        };
        if (id == ID_SHOW_NEW) { NewNote(); return 0; }
        if (id == ID_SHOW_OPEN || (id == ID_NOTE_LIST && HIWORD(wp) == LBN_DBLCLK)) {
            if (Note* n = selected()) ShowNote(*n);
            return 0;
        }
        if (id == ID_SHOW_DELETE) {
            if (Note* n = selected()) {
                std::wstring doomedId = n->id;
                if (!n->path.empty()) DeleteFileW(n->path.c_str());
                HWND doomed = n->hwnd;
                for (size_t i = 0; i < g_notes.size(); ++i)
                    if (g_notes[i].id == doomedId) { g_notes.erase(g_notes.begin() + (ptrdiff_t)i); break; }
                if (doomed) {
                    bool prev = g_quitting;
                    g_quitting = true;
                    DestroyWindow(doomed);
                    g_quitting = prev;
                }
                RebuildTagIndex();
                RefreshBrowser();
            }
            return 0;
        }
        return 0;
    }
    case WM_CLOSE:
        ShowWindow(hwnd, SW_HIDE);
        return 0;
    case WM_DESTROY:
        if (hwnd == g_browser) g_browser = nullptr;
        return 0;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

HICON CreateTrayIcon() {
    const int S = 32;
    HDC screen = GetDC(nullptr);
    HDC dc = CreateCompatibleDC(screen);
    HBITMAP color = CreateCompatibleBitmap(screen, S, S);
    HGDIOBJ oldBmp = SelectObject(dc, color);
    RECT rc{0, 0, S, S};
    HBRUSH paper = CreateSolidBrush(RGB(255, 214, 74));
    FillRect(dc, &rc, paper);
    DeleteObject(paper);
    HPEN border = CreatePen(PS_SOLID, 1, RGB(168, 124, 12));
    HGDIOBJ oldPen = SelectObject(dc, border);
    SelectObject(dc, GetStockObject(NULL_BRUSH));
    Rectangle(dc, 1, 1, S - 1, S - 1);
    HPEN ink = CreatePen(PS_SOLID, 2, RGB(92, 64, 12));
    SelectObject(dc, ink);
    MoveToEx(dc, 7, 12, nullptr); LineTo(dc, 24, 12);
    MoveToEx(dc, 7, 18, nullptr); LineTo(dc, 20, 18);
    MoveToEx(dc, 7, 24, nullptr); LineTo(dc, 16, 24);
    SelectObject(dc, oldPen);
    SelectObject(dc, oldBmp);
    DeleteObject(border);
    DeleteObject(ink);
    DeleteDC(dc);
    ReleaseDC(nullptr, screen);
    BYTE maskBits[(32 * 32) / 8] = {};
    HBITMAP mask = CreateBitmap(S, S, 1, 1, maskBits);
    ICONINFO ii{};
    ii.fIcon = TRUE;
    ii.hbmColor = color;
    ii.hbmMask = mask;
    HICON icon = CreateIconIndirect(&ii);
    DeleteObject(color);
    DeleteObject(mask);
    return icon;
}

void AddTrayIcon() {
    NOTIFYICONDATAW nid{};
    nid.cbSize = sizeof(nid);
    nid.hWnd = g_tray;
    nid.uID = 1;
    nid.uFlags = NIF_MESSAGE | NIF_ICON | NIF_TIP;
    nid.uCallbackMessage = WM_TRAY;
    nid.hIcon = g_icon;
    lstrcpynW(nid.szTip, L"Sticky Notes", 128);
    Shell_NotifyIconW(NIM_ADD, &nid);
}

void RemoveTrayIcon() {
    NOTIFYICONDATAW nid{};
    nid.cbSize = sizeof(nid);
    nid.hWnd = g_tray;
    nid.uID = 1;
    Shell_NotifyIconW(NIM_DELETE, &nid);
}

void ShowTrayMenu() {
    HMENU menu = CreatePopupMenu();
    AppendMenuW(menu, MF_STRING, ID_NEW, L"New note\tCtrl+N");
    AppendMenuW(menu, MF_STRING, ID_SHOW, L"Show notes…");
    AppendMenuW(menu, MF_STRING, ID_HIDE, L"Hide all");
    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(menu, MF_STRING, ID_EXIT, L"Exit");
    POINT pt{};
    GetCursorPos(&pt);
    SetForegroundWindow(g_tray);
    TrackPopupMenu(menu, TPM_RIGHTBUTTON | TPM_BOTTOMALIGN, pt.x, pt.y, 0, g_tray, nullptr);
    PostMessageW(g_tray, WM_NULL, 0, 0);
    DestroyMenu(menu);
}

void QuitApp() {
    SaveAll();
    g_quitting = true;
    std::vector<HWND> hwnds;
    for (const Note& n : g_notes) if (n.hwnd) hwnds.push_back(n.hwnd);
    for (HWND h : hwnds) DestroyWindow(h);
    g_notes.clear();
    if (g_browser) DestroyWindow(g_browser);
    RemoveTrayIcon();
    DestroyWindow(g_tray);
}

LRESULT CALLBACK TrayProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    if (msg == g_taskbarCreated) { AddTrayIcon(); return 0; }
    switch (msg) {
    case WM_TRAY:
        if (lp == WM_LBUTTONUP) OpenBrowser();
        else if (lp == WM_LBUTTONDBLCLK) NewNote();
        else if (lp == WM_RBUTTONUP) ShowTrayMenu();
        return 0;
    case WM_SHOW_EXISTING:
        OpenBrowser();
        return 0;
    case WM_COMMAND:
        switch (LOWORD(wp)) {
        case ID_NEW: NewNote(); break;
        case ID_SHOW: OpenBrowser(); break;
        case ID_HIDE: HideAllNotes(); break;
        case ID_EXIT: QuitApp(); break;
        case ID_BOLD: {
            HWND focus = GetFocus();
            for (Note& n : g_notes) {
                if (n.mode == NoteMode::Text && n.edit &&
                    (n.edit == focus || IsChild(n.hwnd, focus))) {
                    ToggleBold(n.edit);
                    ScheduleSave(n);
                    break;
                }
                if (n.mode == NoteMode::Checklist) {
                    bool hit = false;
                    for (auto& it : n.items) {
                        if (it.edit == focus) {
                            ToggleBold(it.edit);
                            ScheduleSave(n);
                            hit = true;
                            break;
                        }
                    }
                    if (hit) break;
                }
            }
            break;
        }
        }
        return 0;
    case WM_ENDSESSION:
        SaveAll();
        return 0;
    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

}  // namespace

int WINAPI wWinMain(HINSTANCE inst, HINSTANCE, PWSTR, int) {
    HWND existing = FindWindowW(L"TrayNotesHost", nullptr);
    if (existing) {
        PostMessageW(existing, WM_SHOW_EXISTING, 0, 0);
        return 0;
    }

    g_inst = inst;
    g_exeDir = ExeDir();
    SetProcessDPIAware();
    INITCOMMONCONTROLSEX icc{sizeof(icc), ICC_STANDARD_CLASSES};
    InitCommonControlsEx(&icc);
    LoadLibraryW(L"Msftedit.dll");

    g_paper = CreateSolidBrush(RGB(255, 236, 150));
    g_titleBrush = CreateSolidBrush(RGB(255, 214, 74));
    g_scrollThumb = CreateSolidBrush(RGB(214, 170, 48));
    RecreateBodyFont(WindowDpi(nullptr));
    g_uiFont = CreateFontW(-13, 0, 0, 0, FW_SEMIBOLD, FALSE, FALSE, FALSE,
        DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
        DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");
    g_icon = CreateTrayIcon();
    g_taskbarCreated = RegisterWindowMessageW(L"TaskbarCreated");

    ACCEL accel[] = {
        {FCONTROL | FVIRTKEY, 'N', ID_NEW},
        {FCONTROL | FVIRTKEY, 'B', ID_BOLD},
    };
    g_accel = CreateAcceleratorTableW(accel, 2);

    WNDCLASSW closeCls{};
    closeCls.lpfnWndProc = CloseBtnProc;
    closeCls.hInstance = inst;
    closeCls.hCursor = LoadCursorW(nullptr, IDC_HAND);
    closeCls.lpszClassName = L"StickyCloseBtn";
    RegisterClassW(&closeCls);

    WNDCLASSW scrollCls{};
    scrollCls.lpfnWndProc = ScrollRailProc;
    scrollCls.hInstance = inst;
    scrollCls.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    scrollCls.hbrBackground = g_paper;
    scrollCls.lpszClassName = L"StickyScrollRail";
    RegisterClassW(&scrollCls);

    WNDCLASSW hostCls{};
    hostCls.lpfnWndProc = CheckHostProc;
    hostCls.hInstance = inst;
    hostCls.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    hostCls.hbrBackground = g_paper;
    hostCls.lpszClassName = L"StickyCheckHost";
    RegisterClassW(&hostCls);

    WNDCLASSW bodyCls{};
    bodyCls.lpfnWndProc = CheckBodyProc;
    bodyCls.hInstance = inst;
    bodyCls.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    bodyCls.hbrBackground = g_paper;
    bodyCls.lpszClassName = L"StickyCheckBody";
    RegisterClassW(&bodyCls);

    WNDCLASSW rowCls{};
    rowCls.lpfnWndProc = CheckRowProc;
    rowCls.hInstance = inst;
    rowCls.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    rowCls.hbrBackground = g_paper;
    rowCls.lpszClassName = L"StickyCheckRow";
    RegisterClassW(&rowCls);

    WNDCLASSW noteCls{};
    noteCls.lpfnWndProc = NoteProc;
    noteCls.hInstance = inst;
    noteCls.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    noteCls.hbrBackground = g_paper;
    noteCls.lpszClassName = L"StickyNote";
    noteCls.style = CS_DBLCLKS;
    RegisterClassW(&noteCls);

    WNDCLASSW browserCls{};
    browserCls.lpfnWndProc = BrowserProc;
    browserCls.hInstance = inst;
    browserCls.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    browserCls.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
    browserCls.lpszClassName = L"NotesBrowser";
    RegisterClassW(&browserCls);

    WNDCLASSW trayCls{};
    trayCls.lpfnWndProc = TrayProc;
    trayCls.hInstance = inst;
    trayCls.lpszClassName = L"TrayNotesHost";
    RegisterClassW(&trayCls);

    g_tray = CreateWindowExW(0, L"TrayNotesHost", L"Sticky Notes", WS_POPUP,
        0, 0, 0, 0, nullptr, nullptr, inst, nullptr);
    AddTrayIcon();
    LoadNotes();
    if (g_notes.empty()) NewNote();

    MSG msg;
    while (GetMessageW(&msg, nullptr, 0, 0)) {
        HWND accelTarget = g_tray;
        if (msg.hwnd) {
            HWND root = GetAncestor(msg.hwnd, GA_ROOT);
            if (root && FindNote(root)) accelTarget = root;
        }
        if (!TranslateAcceleratorW(accelTarget, g_accel, &msg)) {
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
    }

    RemoveTrayIcon();
    DestroyAcceleratorTable(g_accel);
    DestroyIcon(g_icon);
    DeleteObject(g_font);
    if (g_fontBold) DeleteObject(g_fontBold);
    DeleteObject(g_uiFont);
    DeleteObject(g_paper);
    DeleteObject(g_titleBrush);
    DeleteObject(g_scrollThumb);
    return 0;
}
