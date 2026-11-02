// The windows on Windows: GDI drawing, with global input from low-level hooks.

#include "../common/gui.h"
#include "win.h"

#include <windowsx.h>

#include <atomic>
#include <cmath>
#include <cstdio>
#include <deque>
#include <memory>
#include <mutex>

namespace {

const UINT kInputMessage = WM_APP + 1;
const UINT kPostMessage = WM_APP + 2;
const UINT_PTR kTickTimer = 1;

COLORREF colorRef(Color c) {
    static const COLORREF table[] = {RGB(0x1e, 0x1e, 0x2e), RGB(0x31, 0x32, 0x44), RGB(0x58, 0x5b, 0x70),
                                     RGB(0xcd, 0xd6, 0xf4), RGB(0xa6, 0xad, 0xc8), RGB(0x89, 0xb4, 0xfa),
                                     RGB(0xa6, 0xe3, 0xa1), RGB(0xf3, 0x8b, 0xa8)};
    return table[static_cast<int>(c)];
}

// Draws into a memory DC, scaling the app's pixel coordinates for the
// display's DPI.
class GdiCanvas : public Canvas {
public:
    GdiCanvas(HDC dc, double scale, int width, int height) : dc_(dc), scale_(scale), width_(width), height_(height) {
        TEXTMETRICW tm;
        GetTextMetricsW(dc_, &tm);
        ascent_ = tm.tmAscent;
        descent_ = tm.tmDescent;
    }

    void clear() override { fill(0, 0, width_, height_, Color::Bg); }
    void text(int x, int y, const std::string &s, Color color) override { drawText(px(x), px(y), s, color); }
    void label(int x, const Rect &row, const std::string &s, Color color) override {
        drawText(px(x), baseline(row), s, color);
    }
    void button(const Rect &r, const std::string &s, Color bg, Color fg) override {
        int x = px(r.x), y = px(r.y), w = px(r.x + r.w) - x, h = px(r.y + r.h) - y;
        fill(x, y, w, h, Color::Border);
        int border = std::max(1, px(1));
        fill(x + border, y + border, w - 2 * border, h - 2 * border, bg);
        std::wstring wide = widen(s);
        SIZE size{};
        GetTextExtentPoint32W(dc_, wide.c_str(), static_cast<int>(wide.size()), &size);
        drawText(x + (w - size.cx) / 2, baseline(r), s, fg);
    }

private:
    int px(int v) const { return static_cast<int>(std::lround(v * scale_)); }
    int baseline(const Rect &r) const {
        int y = px(r.y), h = px(r.y + r.h) - y;
        return y + (h + ascent_ - descent_) / 2;
    }
    void fill(int x, int y, int w, int h, Color c) {
        SetDCBrushColor(dc_, colorRef(c));
        RECT rc = {x, y, x + w, y + h};
        FillRect(dc_, &rc, static_cast<HBRUSH>(GetStockObject(DC_BRUSH)));
    }
    void drawText(int x, int y, const std::string &s, Color color) {
        std::wstring wide = widen(s);
        SetTextColor(dc_, colorRef(color));
        TextOutW(dc_, x, y, wide.c_str(), static_cast<int>(wide.size()));
    }

    HDC dc_;
    double scale_;
    int width_, height_;
    int ascent_ = 0, descent_ = 0;
};

class WinHost : public Host {
public:
    WinHost(std::unique_ptr<Output> output, bool recordsMouse)
        : output_(std::move(output)), recordsMouse_(recordsMouse) {}

    Output &output() override { return *output_; }
    bool outputLimited() const override { return false; }
    HotkeyScope hotkeyScope() const override { return HotkeyScope::Global; }
    bool canRecord() const override { return true; }
    const char *permissionFix() const override { return nullptr; }
    void post(std::function<void()> fn) override {
        {
            std::lock_guard<std::mutex> lock(postMutex_);
            posted_.push_back(std::move(fn));
        }
        if (HWND hwnd = hwnd_) PostMessageW(hwnd, kPostMessage, 0, 0);
    }

    int run(App &app) {
        app_ = &app;
        // Sharp text on scaled displays: we scale our own drawing instead of
        // letting Windows stretch a blurry bitmap.
        using SetContextFn = BOOL(WINAPI *)(HANDLE);
        HMODULE user32 = GetModuleHandleW(L"user32.dll");
        auto setContext = reinterpret_cast<SetContextFn>(
            reinterpret_cast<void *>(GetProcAddress(user32, "SetProcessDpiAwarenessContext")));
        if (!setContext || !setContext(reinterpret_cast<HANDLE>(-2))) // DPI_AWARENESS_CONTEXT_SYSTEM_AWARE
            SetProcessDPIAware();
        HDC screen = GetDC(nullptr);
        scale_ = GetDeviceCaps(screen, LOGPIXELSX) / 96.0;
        ReleaseDC(nullptr, screen);

        HINSTANCE self = GetModuleHandleW(nullptr);
        WNDCLASSEXW cls{};
        cls.cbSize = sizeof cls;
        cls.lpfnWndProc = wndProc;
        cls.hInstance = self;
        cls.hCursor = LoadCursorW(nullptr, reinterpret_cast<LPCWSTR>(IDC_ARROW));
        cls.hIcon = LoadIconW(nullptr, reinterpret_cast<LPCWSTR>(IDI_APPLICATION));
        cls.lpszClassName = L"catberry";
        RegisterClassExW(&cls);

        DWORD style = WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX;
        RECT rc = {0, 0, px(app.width()), px(app.height())};
        AdjustWindowRectEx(&rc, style, FALSE, 0);
        font_ = CreateFontW(-px(14), 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS,
                            CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_SWISS, L"Segoe UI");
        HWND hwnd = CreateWindowExW(0, L"catberry", widen(app.title()).c_str(), style, CW_USEDEFAULT, CW_USEDEFAULT,
                                    rc.right - rc.left, rc.bottom - rc.top, nullptr, nullptr, self, this);
        if (!hwnd) {
            std::fprintf(stderr, "catberry: cannot create the window\n");
            return 1;
        }
        hooks_.reset(new HookInput(recordsMouse_, hwnd, kInputMessage));
        if (!hooks_->ok()) std::fprintf(stderr, "catberry: cannot watch the keyboard, hotkeys will not work\n");
        SetTimer(hwnd, kTickTimer, 100, nullptr);
        ShowWindow(hwnd, SW_SHOW);
        SetForegroundWindow(hwnd);
        if (!posted_.empty()) PostMessageW(hwnd, kPostMessage, 0, 0);

        MSG msg;
        while (GetMessageW(&msg, nullptr, 0, 0) > 0) {
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
        hooks_.reset();
        DeleteObject(font_);
        return 0;
    }

private:
    int px(int v) const { return static_cast<int>(std::lround(v * scale_)); }
    int unscale(int v) const { return static_cast<int>(v / scale_); }

    static LRESULT CALLBACK wndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
        if (msg == WM_NCCREATE) {
            auto *host = static_cast<WinHost *>(reinterpret_cast<CREATESTRUCTW *>(lParam)->lpCreateParams);
            host->hwnd_ = hwnd;
            SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(host));
        }
        auto *host = reinterpret_cast<WinHost *>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
        if (!host) return DefWindowProcW(hwnd, msg, wParam, lParam);
        LRESULT result = host->handle(hwnd, msg, wParam, lParam);
        if (host->app_ && host->app_->dirty && msg != WM_PAINT) InvalidateRect(hwnd, nullptr, FALSE);
        return result;
    }

    LRESULT handle(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
        App &app = *app_;
        switch (msg) {
        case WM_PAINT:
            paint(hwnd);
            return 0;
        case WM_ERASEBKGND:
            return 1;
        case WM_LBUTTONDOWN:
        case WM_MBUTTONDOWN:
        case WM_RBUTTONDOWN: {
            int button = msg == WM_LBUTTONDOWN ? 1 : msg == WM_MBUTTONDOWN ? 2 : 3;
            app.click(unscale(GET_X_LPARAM(lParam)), unscale(GET_Y_LPARAM(lParam)), button);
            return 0;
        }
        case WM_MOUSEWHEEL: {
            POINT pt = {GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)};
            ScreenToClient(hwnd, &pt);
            app.click(unscale(pt.x), unscale(pt.y), GET_WHEEL_DELTA_WPARAM(wParam) > 0 ? 4 : 5);
            return 0;
        }
        case WM_KEYDOWN:
        case WM_SYSKEYDOWN: {
            TypedKey press;
            unsigned scan = (lParam >> 16) & 0xff;
            bool extended = (lParam >> 24) & 1;
            press.code = codeFromWindowsKey(static_cast<unsigned>(wParam), scan, extended);
            switch (wParam) {
            case VK_RETURN: press.special = TypedKey::Enter; break;
            case VK_ESCAPE: press.special = TypedKey::Escape; break;
            case VK_BACK: press.special = TypedKey::Backspace; break;
            case VK_TAB: press.special = TypedKey::Tab; break;
            case VK_SHIFT:
            case VK_CONTROL:
            case VK_MENU:
            case VK_LWIN:
            case VK_RWIN:
            case VK_CAPITAL:
            case VK_NUMLOCK: press.modifier = true; break;
            }
            app.key(press);
            // Keep Alt+F4 and friends working; plain F10 must not open the
            // system menu.
            bool altHeld = (lParam >> 29) & 1;
            if (msg == WM_SYSKEYDOWN && altHeld) return DefWindowProcW(hwnd, msg, wParam, lParam);
            return 0;
        }
        case WM_CHAR:
            if (wParam >= 0x20 && wParam < 0x7f) {
                TypedKey press;
                press.ch = static_cast<char>(wParam);
                app.key(press);
            }
            return 0;
        case WM_KILLFOCUS:
            app.focusLost();
            return 0;
        case WM_TIMER:
            if (app.wantsTicks()) app.tick();
            return 0;
        case kInputMessage:
            for (const InputEvent &e : hooks_->take()) app.globalInput(e);
            return 0;
        case kPostMessage: {
            std::deque<std::function<void()>> todo;
            {
                std::lock_guard<std::mutex> lock(postMutex_);
                todo.swap(posted_);
            }
            for (auto &fn : todo) fn();
            return 0;
        }
        case WM_CLOSE:
            DestroyWindow(hwnd);
            return 0;
        case WM_DESTROY:
            KillTimer(hwnd, kTickTimer);
            hwnd_ = nullptr;
            PostQuitMessage(0);
            return 0;
        }
        return DefWindowProcW(hwnd, msg, wParam, lParam);
    }

    void paint(HWND hwnd) {
        PAINTSTRUCT ps;
        HDC dc = BeginPaint(hwnd, &ps);
        RECT rc;
        GetClientRect(hwnd, &rc);
        HDC mem = CreateCompatibleDC(dc);
        HBITMAP bitmap = CreateCompatibleBitmap(dc, rc.right, rc.bottom);
        HGDIOBJ oldBitmap = SelectObject(mem, bitmap);
        HGDIOBJ oldFont = SelectObject(mem, font_);
        SetBkMode(mem, TRANSPARENT);
        SetTextAlign(mem, TA_BASELINE | TA_LEFT);
        {
            GdiCanvas canvas(mem, scale_, rc.right, rc.bottom);
            app_->dirty = false;
            app_->draw(canvas);
        }
        BitBlt(dc, 0, 0, rc.right, rc.bottom, mem, 0, 0, SRCCOPY);
        SelectObject(mem, oldFont);
        SelectObject(mem, oldBitmap);
        DeleteObject(bitmap);
        DeleteDC(mem);
        EndPaint(hwnd, &ps);
    }

    std::unique_ptr<Output> output_;
    bool recordsMouse_;
    App *app_ = nullptr;
    std::atomic<HWND> hwnd_{nullptr};
    HFONT font_ = nullptr;
    double scale_ = 1.0;
    std::unique_ptr<HookInput> hooks_;
    std::mutex postMutex_;
    std::deque<std::function<void()>> posted_;
};

} // namespace

int runGui(const AppFactory &make, const GuiOptions &options) {
    std::string error;
    std::unique_ptr<Output> output = createOutput(options.backend, options.keyboardOutput, error);
    if (!output) {
        std::fprintf(stderr, "catberry: %s\n", error.c_str());
        return 1;
    }
    FineTimer timer;
    WinHost host(std::move(output), options.recordsMouse);
    std::unique_ptr<App> app = make(host);
    return host.run(*app);
}
