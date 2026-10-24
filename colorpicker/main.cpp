// Color Picker - pick any pixel on screen with a magnifier, copy HEX / RGB / HSL / HSV.
// Ctrl+Alt+C anywhere = pick.  In picker: arrows = move 1px, Enter/click = pick, Esc = cancel.
#include "../common/ui.h"
#include <cmath>
using namespace ui;

enum { ID_PICK = 100, ID_C0, ID_C1, ID_C2, ID_C3 };
static const int HOTKEY = 1, HIST = 12;
static HWND gWnd, gCopy[4];
static COLORREF gColor = RGB(124, 92, 255);
static bool gHas = false;
static std::vector<COLORREF> gHist;
static std::wstring gStatus = L"Click \x201CPick a color\x201D or press Ctrl+Alt+C anywhere";
static COLORREF gStatusColor = MUTED;

static void setStatus(const std::wstring& s, COLORREF c) { gStatus = s; gStatusColor = c; InvalidateRect(gWnd, nullptr, FALSE); }

static void hsx(COLORREF c, double& h, double& sl, double& l, double& sv, double& v) {
    double r = GetRValue(c) / 255.0, g = GetGValue(c) / 255.0, b = GetBValue(c) / 255.0;
    double mx = std::max({r, g, b}), mn = std::min({r, g, b}), d = mx - mn;
    h = 0;
    if (d > 0) {
        if (mx == r) h = 60 * fmod((g - b) / d, 6); else if (mx == g) h = 60 * ((b - r) / d + 2); else h = 60 * ((r - g) / d + 4);
        if (h < 0) h += 360;
    }
    l = (mx + mn) / 2; sl = d == 0 ? 0 : d / (1 - fabs(2 * l - 1)); v = mx; sv = mx == 0 ? 0 : d / mx;
}
static std::wstring format(int i, COLORREF c) {
    int r = GetRValue(c), g = GetGValue(c), b = GetBValue(c);
    double h, sl, l, sv, v; hsx(c, h, sl, l, sv, v);
    switch (i) {
    case 0: return fmt(L"#%02X%02X%02X", r, g, b);
    case 1: return fmt(L"rgb(%d, %d, %d)", r, g, b);
    case 2: return fmt(L"hsl(%d, %d%%, %d%%)", (int)lround(h), (int)lround(sl * 100), (int)lround(l * 100));
    default: return fmt(L"hsv(%d, %d%%, %d%%)", (int)lround(h), (int)lround(sv * 100), (int)lround(v * 100));
    }
}

// ---- history persistence ----
static std::wstring histPath() {
    wchar_t p[MAX_PATH]; DWORD n = GetEnvironmentVariableW(L"APPDATA", p, MAX_PATH);
    if (!n) return L"";
    std::wstring d = std::wstring(p) + L"\\QRClipTools"; CreateDirectoryW(d.c_str(), nullptr);
    return d + L"\\colorpicker.txt";
}
static void loadHist() {
    std::string s; if (!readFile(histPath(), s)) return;
    size_t i = 0;
    while (i + 6 <= s.size() && (int)gHist.size() < HIST) {
        unsigned v; if (sscanf(s.c_str() + i, "%6x", &v) == 1) gHist.push_back(RGB((v >> 16) & 255, (v >> 8) & 255, v & 255));
        i = s.find('\n', i); if (i == std::string::npos) break; i++;
    }
}
static void saveHist() {
    std::string s; char b[16];
    for (COLORREF c : gHist) { snprintf(b, 16, "%02X%02X%02X\n", GetRValue(c), GetGValue(c), GetBValue(c)); s += b; }
    writeFile(histPath(), s.data(), s.size());
}

static void choose(COLORREF c, bool addHist, const wchar_t* how) {
    gColor = c; gHas = true;
    for (auto b : gCopy) EnableWindow(b, TRUE);
    if (addHist) {
        gHist.erase(std::remove(gHist.begin(), gHist.end(), c), gHist.end());
        gHist.insert(gHist.begin(), c);
        if ((int)gHist.size() > HIST) gHist.resize(HIST);
        saveHist();
    }
    std::wstring hex = format(0, c);
    if (setClipText(gWnd, hex)) setStatus(L"\x2713 " + hex + L" " + how + L" \x2014 copied to clipboard", OK);
}

// ---------------- picker overlay ----------------
struct Picker { Shot* s; HDC back; HBITMAP backBmp; POINT p; bool done = false, ok = false; RECT magRect{}; };
static const int MAGN = 11, ZOOM = 12;
static RECT magFor(Picker* pk, POINT p) {  // p in local coords
    int sz = S(MAGN * ZOOM), lab = S(34), off = S(22);
    int x = p.x + off, y = p.y + off;
    // keep inside the monitor under the cursor
    POINT sp = {p.x + pk->s->x, p.y + pk->s->y};
    MONITORINFO mi = {sizeof(mi)}; GetMonitorInfoW(MonitorFromPoint(sp, MONITOR_DEFAULTTONEAREST), &mi);
    RECT m = mi.rcMonitor; OffsetRect(&m, -pk->s->x, -pk->s->y);
    if (x + sz > m.right) x = p.x - off - sz;
    if (y + sz + lab > m.bottom) y = p.y - off - sz - lab;
    return RECT{x - 3, y - 3, x + sz + 3, y + sz + lab + 3};
}
static void drawMag(Picker* pk, HDC dc) {
    RECT r = pk->magRect;
    int x = r.left + 3, y = r.top + 3, sz = S(MAGN * ZOOM), lab = S(34);
    int z = sz / MAGN;
    fillRect(dc, r, ACCENT);
    SetStretchBltMode(dc, COLORONCOLOR);
    StretchBlt(dc, x, y, z * MAGN, z * MAGN, pk->s->dc, pk->p.x - MAGN / 2, pk->p.y - MAGN / 2, MAGN, MAGN, SRCCOPY);
    // grid
    HPEN grid = CreatePen(PS_SOLID, 1, RGB(60, 60, 60)); HGDIOBJ op = SelectObject(dc, grid);
    for (int i = 1; i < MAGN; i++) {
        MoveToEx(dc, x + i * z, y, nullptr); LineTo(dc, x + i * z, y + z * MAGN);
        MoveToEx(dc, x, y + i * z, nullptr); LineTo(dc, x + z * MAGN, y + i * z);
    }
    SelectObject(dc, op); DeleteObject(grid);
    RECT c = {x + (MAGN / 2) * z - 1, y + (MAGN / 2) * z - 1, x + (MAGN / 2 + 1) * z + 1, y + (MAGN / 2 + 1) * z + 1};
    HBRUSH w = CreateSolidBrush(RGB(255, 255, 255)), k = CreateSolidBrush(RGB(0, 0, 0));
    FrameRect(dc, &c, k); InflateRect(&c, -1, -1); FrameRect(dc, &c, w); DeleteObject(w); DeleteObject(k);
    // label
    uint32_t px = pk->s->at(pk->p.x + pk->s->x, pk->p.y + pk->s->y);
    COLORREF col = RGB((px >> 16) & 255, (px >> 8) & 255, px & 255);
    RECT lr = {x, y + z * MAGN, r.right - 3, y + z * MAGN + lab};
    fillRect(dc, lr, CARD);
    RECT sw = {lr.left + S(8), lr.top + S(8), lr.left + S(26), lr.top + S(26)};
    fillRect(dc, sw, col);
    RECT tr = lr; tr.left = sw.right + S(8);
    text(dc, format(0, col), tr, fBtn, TEXT, LEFT1);
}
static LRESULT CALLBACK pickProc(HWND h, UINT m, WPARAM w, LPARAM l) {
    auto* pk = (Picker*)GetWindowLongPtrW(h, GWLP_USERDATA);
    auto moveTo = [&](POINT np) {
        RECT old = pk->magRect; pk->p = np; pk->magRect = magFor(pk, np);
        InvalidateRect(h, &old, FALSE); InvalidateRect(h, &pk->magRect, FALSE);
    };
    switch (m) {
    case WM_ERASEBKGND: return 1;
    case WM_PAINT: {
        PAINTSTRUCT ps; HDC dc = BeginPaint(h, &ps); RECT p = ps.rcPaint;
        BitBlt(pk->back, p.left, p.top, p.right - p.left, p.bottom - p.top, pk->s->dc, p.left, p.top, SRCCOPY);
        drawMag(pk, pk->back);
        BitBlt(dc, p.left, p.top, p.right - p.left, p.bottom - p.top, pk->back, p.left, p.top, SRCCOPY);
        EndPaint(h, &ps); return 0;
    }
    case WM_MOUSEMOVE: moveTo(POINT{GET_X_LPARAM(l), GET_Y_LPARAM(l)}); return 0;
    case WM_LBUTTONDOWN: pk->p = POINT{GET_X_LPARAM(l), GET_Y_LPARAM(l)}; pk->ok = pk->done = true; return 0;
    case WM_RBUTTONDOWN: pk->done = true; return 0;
    case WM_KEYDOWN: {
        int dx = 0, dy = 0, st = (GetKeyState(VK_SHIFT) & 0x8000) ? 10 : 1;
        if (w == VK_ESCAPE) pk->done = true;
        else if (w == VK_RETURN || w == VK_SPACE) pk->ok = pk->done = true;
        else if (w == VK_LEFT) dx = -st; else if (w == VK_RIGHT) dx = st; else if (w == VK_UP) dy = -st; else if (w == VK_DOWN) dy = st;
        if (dx || dy) { POINT np = {pk->p.x + dx, pk->p.y + dy}; SetCursorPos(np.x + pk->s->x, np.y + pk->s->y); moveTo(np); }
        return 0;
    }
    }
    return DefWindowProcW(h, m, w, l);
}
static void pick() {
    static bool reg = false;
    if (!reg) {
        WNDCLASSEXW wc = {sizeof(wc)}; wc.lpfnWndProc = pickProc; wc.hInstance = inst;
        wc.hCursor = LoadCursor(nullptr, IDC_CROSS); wc.lpszClassName = L"ColorPickOverlay";
        RegisterClassExW(&wc); reg = true;
    }
    Shot s;
    if (!hideAndCapture(gWnd, s)) { bringToFront(gWnd); setStatus(L"Screen capture failed", ERR); return; }
    Picker pk; pk.s = &s;
    HDC scr = GetDC(nullptr);
    pk.backBmp = CreateCompatibleBitmap(scr, s.w, s.h); pk.back = CreateCompatibleDC(scr); SelectObject(pk.back, pk.backBmp);
    ReleaseDC(nullptr, scr);
    POINT cp; GetCursorPos(&cp); pk.p = POINT{cp.x - s.x, cp.y - s.y}; pk.magRect = magFor(&pk, pk.p);
    HWND h = CreateWindowExW(WS_EX_TOPMOST | WS_EX_TOOLWINDOW, L"ColorPickOverlay", L"", WS_POPUP, s.x, s.y, s.w, s.h,
                             nullptr, nullptr, inst, nullptr);
    SetWindowLongPtrW(h, GWLP_USERDATA, (LONG_PTR)&pk);
    ShowWindow(h, SW_SHOW); SetForegroundWindow(h); SetFocus(h);
    MSG m;
    while (!pk.done && GetMessageW(&m, nullptr, 0, 0)) { TranslateMessage(&m); DispatchMessageW(&m); }
    DestroyWindow(h);
    uint32_t px = s.at(pk.p.x + s.x, pk.p.y + s.y);
    DeleteDC(pk.back); DeleteObject(pk.backBmp); s.free();
    bringToFront(gWnd);
    if (pk.ok) choose(RGB((px >> 16) & 255, (px >> 8) & 255, px & 255), true, L"picked");
    else setStatus(L"Cancelled", MUTED);
}

// ---------------- main window ----------------
static const wchar_t* LABELS[4] = {L"HEX", L"RGB", L"HSL", L"HSV"};
static RECT histRect(int i) { return R(44 + i * 46, 440, 38, 38); }

static void paint(HDC dc, RECT) {
    header(dc, L"Color Picker", L"click anywhere on screen   \x2192   HEX  \x00B7  RGB  \x00B7  HSL", 210);
    fillRound(dc, R(24, 70, 260, 336), S(20), CARD);
    if (gHas) fillRound(dc, R(44, 90, 220, 210), S(16), gColor, BORDER);
    else {
        fillRound(dc, R(44, 90, 220, 210), S(16), EMPTY);
        text(dc, L"No color yet", R(44, 90, 220, 210), fBody, MUTED, CENTER1);
    }
    text(dc, L"or press Ctrl+Alt+C anywhere", R(44, 372, 220, 22), fSmall, MUTED, CENTER1);

    fillRound(dc, R(300, 70, 296, 336), S(20), CARD);
    for (int i = 0; i < 4; i++) {
        int y = 92 + i * 58;
        text(dc, LABELS[i], R(318, y, 46, 42), fBtn, MUTED, LEFT1);
        fillRound(dc, R(362, y, 152, 42), S(12), FIELD, BORDER);
        text(dc, gHas ? format(i, gColor) : L"\x2014", R(372, y, 136, 42), i == 0 ? fBtnBold : fSmall, gHas ? TEXT : MUTED, LEFT1);
    }
    text(dc, L"HEX is copied automatically after each pick.\nShift+Arrows move 10 px in the picker.", R(318, 330, 262, 60), fSmall, MUTED, DT_LEFT | DT_WORDBREAK);

    fillRound(dc, R(24, 420, 572, 72), S(20), CARD);
    if (gHist.empty()) text(dc, L"Recent colors show up here", R(44, 420, 520, 72), fSmall, MUTED, LEFT1);
    for (int i = 0; i < (int)gHist.size(); i++) {
        RECT r = histRect(i);
        fillRound(dc, r, S(10), gHist[i], gHist[i] == gColor && gHas ? RGB(255, 255, 255) : BORDER);
    }
    text(dc, gStatus, R(26, 500, 570, 22), fSmall, gStatusColor, LEFT1);
}

static LRESULT CALLBACK wndProc(HWND h, UINT m, WPARAM w, LPARAM l) {
    switch (m) {
    case WM_CREATE:
        gWnd = h;
        button(h, ID_PICK, L"Pick a color", R(44, 316, 220, 48), PRIMARY);
        for (int i = 0; i < 4; i++) { gCopy[i] = button(h, ID_C0 + i, L"Copy", R(522, 92 + i * 58, 58, 42)); EnableWindow(gCopy[i], FALSE); }
        loadHist();
        if (!RegisterHotKey(h, HOTKEY, MOD_CONTROL | MOD_ALT | MOD_NOREPEAT, 'C'))
            setStatus(L"Ctrl+Alt+C is used by another app \x2014 use the button instead", WARN);
        return 0;
    case WM_HOTKEY: if (w == HOTKEY) pick(); return 0;
    case WM_COMMAND: {
        int id = LOWORD(w);
        if (id == ID_PICK) pick();
        else if (id >= ID_C0 && id <= ID_C3) {
            std::wstring v = format(id - ID_C0, gColor);
            if (setClipText(h, v)) { flash(gCopy[id - ID_C0], L"\x2713"); setStatus(L"\x2713 Copied " + v, OK); }
        }
        return 0;
    }
    case WM_LBUTTONDOWN: {
        POINT p = {GET_X_LPARAM(l), GET_Y_LPARAM(l)};
        for (int i = 0; i < (int)gHist.size(); i++) { RECT r = histRect(i); if (PtInRect(&r, p)) { choose(gHist[i], false, L"selected"); InvalidateRect(h, nullptr, FALSE); } }
        return 0;
    }
    case WM_SETCURSOR: {
        POINT p; GetCursorPos(&p); ScreenToClient(h, &p);
        for (int i = 0; i < (int)gHist.size(); i++) { RECT r = histRect(i); if (PtInRect(&r, p)) { SetCursor(LoadCursor(nullptr, IDC_HAND)); return TRUE; } }
        break;
    }
    case WM_DRAWITEM: drawButton((DRAWITEMSTRUCT*)l); return TRUE;
    case WM_ERASEBKGND: return 1;
    case WM_PAINT: paintBuffered(h, paint); return 0;
    case WM_DESTROY: UnregisterHotKey(h, HOTKEY); PostQuitMessage(0); return 0;
    }
    return DefWindowProcW(h, m, w, l);
}

int WINAPI wWinMain(HINSTANCE inst, HINSTANCE, LPWSTR, int show) {
    init(inst);
    HWND h = createMain(L"ColorPickerWnd", L"Color Picker", 620, 530, wndProc);
    ShowWindow(h, show); UpdateWindow(h);
    return loop([&](MSG& m) {
        if (m.message == WM_KEYDOWN && m.wParam == VK_SPACE && GetForegroundWindow() == h) { pick(); return true; }
        return false;
    });
}
