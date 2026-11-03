// The Windows point picker: a see-through top-most window over the whole
// virtual screen (all monitors), drawn with GDI. Same keys as on Linux.
#include "../common/picker.h"

#include "win.h"

#include <cstdio>
#include <string>
#include <vector>

namespace {

struct Picker {
    std::vector<QuickPoint> points;
    ClickType type = ClickType::Left;
    bool done = false, saved = false;
};

Picker *g_picker = nullptr;

COLORREF typeColor(ClickType t) {
    switch (t) {
    case ClickType::Right: return RGB(0xf3, 0x8b, 0xa8);
    case ClickType::Middle: return RGB(0xf9, 0xe2, 0xaf);
    case ClickType::Double: return RGB(0xa6, 0xe3, 0xa1);
    default: return RGB(0x89, 0xb4, 0xfa);
    }
}

const wchar_t *typeLabel(ClickType t) {
    switch (t) {
    case ClickType::Right: return L"Right";
    case ClickType::Middle: return L"Middle";
    case ClickType::Double: return L"Double";
    default: return L"Left";
    }
}

void paint(HWND hwnd) {
    PAINTSTRUCT ps;
    HDC screen = BeginPaint(hwnd, &ps);
    RECT r;
    GetClientRect(hwnd, &r);
    // Drawn off screen, then copied: no flicker.
    HDC dc = CreateCompatibleDC(screen);
    HBITMAP bmp = CreateCompatibleBitmap(screen, r.right, r.bottom);
    HGDIOBJ oldBmp = SelectObject(dc, bmp);
    HBRUSH bg = CreateSolidBrush(RGB(0x1e, 0x1e, 0x2e));
    FillRect(dc, &r, bg);
    DeleteObject(bg);
    SetBkMode(dc, TRANSPARENT);

    const std::vector<QuickPoint> &pts = g_picker->points;
    HPEN line = CreatePen(PS_DASH, 1, RGB(0xcd, 0xd6, 0xf4));
    HGDIOBJ oldPen = SelectObject(dc, line);
    for (size_t i = 1; i < pts.size(); ++i) {
        MoveToEx(dc, pts[i - 1].x, pts[i - 1].y, nullptr);
        LineTo(dc, pts[i].x, pts[i].y);
    }
    SelectObject(dc, oldPen);
    DeleteObject(line);

    HFONT font = CreateFontW(-15, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS,
                             CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Segoe UI");
    HGDIOBJ oldFont = SelectObject(dc, font);
    HPEN edge = CreatePen(PS_SOLID, 2, RGB(0x1e, 0x1e, 0x2e));
    oldPen = SelectObject(dc, edge);
    for (size_t i = 0; i < pts.size(); ++i) {
        HBRUSH fill = CreateSolidBrush(typeColor(pts[i].type));
        HGDIOBJ oldBrush = SelectObject(dc, fill);
        Ellipse(dc, pts[i].x - 13, pts[i].y - 13, pts[i].x + 13, pts[i].y + 13);
        SelectObject(dc, oldBrush);
        DeleteObject(fill);
        RECT num{pts[i].x - 13, pts[i].y - 13, pts[i].x + 13, pts[i].y + 13};
        SetTextColor(dc, RGB(0x1e, 0x1e, 0x2e));
        std::wstring n = std::to_wstring(i + 1);
        DrawTextW(dc, n.c_str(), -1, &num, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    }
    SelectObject(dc, oldPen);
    DeleteObject(edge);

    // The bar, on the primary monitor (its top-left is 0,0 on the screen).
    POINT origin{0, 0};
    ScreenToClient(hwnd, &origin);
    int primaryW = GetSystemMetrics(SM_CXSCREEN);
    wchar_t text[256];
    std::swprintf(text, 256,
                  L"Click: add a point (%ls)  ·  L / R / M / D: click type  ·  Backspace: undo  ·  "
                  L"Enter: save (%u)  ·  Esc: cancel",
                  typeLabel(g_picker->type), static_cast<unsigned>(pts.size()));
    RECT measure{0, 0, primaryW - 80, 0};
    DrawTextW(dc, text, -1, &measure, DT_CALCRECT | DT_WORDBREAK | DT_CENTER);
    int w = measure.right + 40, h = measure.bottom + 20;
    RECT box{origin.x + (primaryW - w) / 2, origin.y + 16, origin.x + (primaryW + w) / 2, origin.y + 16 + h};
    HBRUSH panel = CreateSolidBrush(RGB(0x18, 0x18, 0x25));
    HPEN border = CreatePen(PS_SOLID, 2, typeColor(g_picker->type));
    HGDIOBJ oldBrush = SelectObject(dc, panel);
    oldPen = SelectObject(dc, border);
    RoundRect(dc, box.left, box.top, box.right, box.bottom, 16, 16);
    SelectObject(dc, oldBrush);
    SelectObject(dc, oldPen);
    DeleteObject(panel);
    DeleteObject(border);
    SetTextColor(dc, RGB(0xcd, 0xd6, 0xf4));
    RECT inner{box.left + 20, box.top + 10, box.right - 20, box.bottom - 10};
    DrawTextW(dc, text, -1, &inner, DT_WORDBREAK | DT_CENTER);
    SelectObject(dc, oldFont);
    DeleteObject(font);

    BitBlt(screen, 0, 0, r.right, r.bottom, dc, 0, 0, SRCCOPY);
    SelectObject(dc, oldBmp);
    DeleteObject(bmp);
    DeleteDC(dc);
    EndPaint(hwnd, &ps);
}

LRESULT CALLBACK pickerProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
    case WM_PAINT: paint(hwnd); return 0;
    case WM_ERASEBKGND: return 1;
    case WM_SETCURSOR: SetCursor(LoadCursor(nullptr, IDC_CROSS)); return TRUE;
    case WM_LBUTTONDOWN:
        g_picker->points.push_back({static_cast<short>(LOWORD(lp)), static_cast<short>(HIWORD(lp)), g_picker->type});
        InvalidateRect(hwnd, nullptr, FALSE);
        return 0;
    case WM_KEYDOWN:
        switch (wp) {
        case 'L': g_picker->type = ClickType::Left; break;
        case 'R': g_picker->type = ClickType::Right; break;
        case 'M': g_picker->type = ClickType::Middle; break;
        case 'D': g_picker->type = ClickType::Double; break;
        case VK_BACK:
        case VK_DELETE:
            if (!g_picker->points.empty()) g_picker->points.pop_back();
            break;
        case VK_RETURN:
            if (!g_picker->points.empty()) {
                g_picker->saved = g_picker->done = true;
                DestroyWindow(hwnd);
            }
            return 0;
        case VK_ESCAPE:
            g_picker->done = true;
            DestroyWindow(hwnd);
            return 0;
        default: return 0;
        }
        InvalidateRect(hwnd, nullptr, FALSE);
        return 0;
    case WM_CLOSE:
        g_picker->done = true;
        DestroyWindow(hwnd);
        return 0;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

} // namespace

bool pickPoints(QuickSet &set) {
    // Real pixels on scaled displays, the same ones the clicks use.
    SetProcessDPIAware();
    int x = GetSystemMetrics(SM_XVIRTUALSCREEN), y = GetSystemMetrics(SM_YVIRTUALSCREEN);
    int w = GetSystemMetrics(SM_CXVIRTUALSCREEN), h = GetSystemMetrics(SM_CYVIRTUALSCREEN);
    Picker picker;
    for (const QuickPoint &p : set.points) picker.points.push_back(scaledPoint(p, set.desktopW, set.desktopH, w, h));
    g_picker = &picker;

    HINSTANCE inst = GetModuleHandleW(nullptr);
    WNDCLASSW wc{};
    wc.lpfnWndProc = pickerProc;
    wc.hInstance = inst;
    wc.lpszClassName = L"CatberryQuickClickPicker";
    RegisterClassW(&wc);
    HWND hwnd = CreateWindowExW(WS_EX_LAYERED | WS_EX_TOPMOST | WS_EX_TOOLWINDOW, wc.lpszClassName,
                                L"Catberry quick click: pick points", WS_POPUP, x, y, w, h, nullptr, nullptr, inst,
                                nullptr);
    if (!hwnd) {
        std::fprintf(stderr, "catberry: cannot open the point picker\n");
        g_picker = nullptr;
        return false;
    }
    SetLayeredWindowAttributes(hwnd, 0, 170, LWA_ALPHA); // see-through
    ShowWindow(hwnd, SW_SHOW);
    SetForegroundWindow(hwnd);
    SetFocus(hwnd);
    MSG msg;
    while (!picker.done && GetMessageW(&msg, nullptr, 0, 0) > 0) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
    g_picker = nullptr;
    if (!picker.saved) return false;
    set.points = picker.points;
    set.desktopW = w;
    set.desktopH = h;
    return true;
}
