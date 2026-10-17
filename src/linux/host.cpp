// The windows on Linux: raw Xlib drawing, plus global input from evdev, or
// from key grabs when /dev/input is not readable.

#include "../common/gui.h"
#include "linux.h"

#include <X11/XKBlib.h>
#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/keysym.h>

#include <sys/eventfd.h>
#include <unistd.h>

#include <cstdio>
#include <deque>
#include <mutex>
#include <set>

namespace {

bool g_grabFailed = false;
int onXError(Display *, XErrorEvent *e) {
    if (e->error_code == BadAccess) g_grabFailed = true;
    return 0;
}

// One fixed-size, double-buffered top-level window.
class XCanvas : public Canvas {
public:
    XCanvas(Display *dpy, const char *title, int width, int height)
        : dpy_(dpy), width_(width), height_(height) {
        int screen = DefaultScreen(dpy_);
        const unsigned int rgb[] = {0x1e1e2e, 0x313244, 0x585b70, 0xcdd6f4, 0xa6adc8, 0x89b4fa, 0xa6e3a1, 0xf38ba8};
        for (int i = 0; i < 8; ++i) colors_[i] = allocColor(rgb[i]);

        win_ = XCreateSimpleWindow(dpy_, DefaultRootWindow(dpy_), 0, 0, width_, height_, 0, 0, pixel(Color::Bg));
        XStoreName(dpy_, win_, title);
        XClassHint cls = {const_cast<char *>("catberry"), const_cast<char *>("Catberry")};
        XSetClassHint(dpy_, win_, &cls);
        XSizeHints size{};
        size.flags = PMinSize | PMaxSize;
        size.min_width = size.max_width = width_;
        size.min_height = size.max_height = height_;
        XSetWMNormalHints(dpy_, win_, &size);
        wmDelete_ = XInternAtom(dpy_, "WM_DELETE_WINDOW", False);
        XSetWMProtocols(dpy_, win_, &wmDelete_, 1);
        XSelectInput(dpy_, win_, ExposureMask | KeyPressMask | ButtonPressMask | FocusChangeMask);

        buffer_ = XCreatePixmap(dpy_, win_, width_, height_, DefaultDepth(dpy_, screen));
        gc_ = XCreateGC(dpy_, win_, 0, nullptr);
        for (const char *name : {"-*-dejavu sans-medium-r-normal--14-*-*-*-*-*-iso10646-1",
                                 "-*-helvetica-medium-r-normal--14-*-*-*-*-*-*-*", "9x15", "fixed"}) {
            if ((font_ = XLoadQueryFont(dpy_, name))) break;
        }
        if (font_) XSetFont(dpy_, gc_, font_->fid);
        XMapWindow(dpy_, win_);
    }
    ~XCanvas() override {
        if (font_) XFreeFont(dpy_, font_);
        XFreeGC(dpy_, gc_);
        XFreePixmap(dpy_, buffer_);
        XDestroyWindow(dpy_, win_);
    }

    Window window() const { return win_; }
    bool isCloseRequest(const XEvent &ev) const {
        return ev.type == ClientMessage && static_cast<Atom>(ev.xclient.data.l[0]) == wmDelete_;
    }
    void present() {
        XCopyArea(dpy_, buffer_, win_, gc_, 0, 0, width_, height_, 0, 0);
        XFlush(dpy_);
    }

    void clear() override {
        XSetForeground(dpy_, gc_, pixel(Color::Bg));
        XFillRectangle(dpy_, buffer_, gc_, 0, 0, width_, height_);
    }
    void text(int x, int y, const std::string &s, Color color) override {
        XSetForeground(dpy_, gc_, pixel(color));
        XDrawString(dpy_, buffer_, gc_, x, y, s.c_str(), static_cast<int>(s.size()));
    }
    void label(int x, const Rect &row, const std::string &s, Color color) override { text(x, baseline(row), s, color); }
    void button(const Rect &r, const std::string &s, Color bg, Color fg) override {
        XSetForeground(dpy_, gc_, pixel(bg));
        XFillRectangle(dpy_, buffer_, gc_, r.x, r.y, r.w, r.h);
        XSetForeground(dpy_, gc_, pixel(Color::Border));
        XDrawRectangle(dpy_, buffer_, gc_, r.x, r.y, r.w - 1, r.h - 1);
        int textWidth = font_ ? XTextWidth(font_, s.c_str(), static_cast<int>(s.size())) : 0;
        text(r.x + (r.w - textWidth) / 2, baseline(r), s, fg);
    }

private:
    unsigned long pixel(Color c) const { return colors_[static_cast<int>(c)]; }
    unsigned long allocColor(unsigned int rgb) {
        XColor c{};
        c.red = ((rgb >> 16) & 0xff) * 257;
        c.green = ((rgb >> 8) & 0xff) * 257;
        c.blue = (rgb & 0xff) * 257;
        XAllocColor(dpy_, DefaultColormap(dpy_, DefaultScreen(dpy_)), &c);
        return c.pixel;
    }
    int baseline(const Rect &r) const {
        int ascent = font_ ? font_->ascent : 10, descent = font_ ? font_->descent : 2;
        return r.y + (r.h + ascent - descent) / 2;
    }

    Display *dpy_;
    int width_, height_;
    Window win_ = 0;
    Pixmap buffer_ = 0;
    GC gc_ = nullptr;
    XFontStruct *font_ = nullptr;
    Atom wmDelete_ = 0;
    unsigned long colors_[8];
};

class XHost : public Host {
public:
    XHost(Display *dpy, std::unique_ptr<Output> output)
        : dpy_(dpy), root_(DefaultRootWindow(dpy)), output_(std::move(output)), wayland_(isWayland()),
          wakeFd_(eventfd(0, EFD_NONBLOCK | EFD_CLOEXEC)) {}
    ~XHost() override { close(wakeFd_); }

    Output &output() override { return *output_; }
    bool outputLimited() const override { return wayland_ && std::string(output_->name()) == "XTest"; }
    HotkeyScope hotkeyScope() const override {
        if (evdev_.available()) return HotkeyScope::Global;
        if (g_grabFailed) return HotkeyScope::Taken;
        // On Wayland a key grab only fires while an XWayland window has focus.
        return wayland_ ? HotkeyScope::X11WindowsOnly : HotkeyScope::Global;
    }
    bool canRecord() const override { return evdev_.available(); }
    const char *permissionFix() const override { return evdev_.available() ? nullptr : kInputGroupCommand; }
    void post(std::function<void()> fn) override {
        {
            std::lock_guard<std::mutex> lock(postMutex_);
            posted_.push_back(std::move(fn));
        }
        uint64_t one = 1;
        if (write(wakeFd_, &one, sizeof one) < 0) {
        }
    }

    void run(App &app) {
        XCanvas canvas(dpy_, app.title(), app.width(), app.height());
        // Holding a grabbed hotkey must not toggle over and over.
        XkbSetDetectableAutoRepeat(dpy_, True, nullptr);
        XSetErrorHandler(onXError);
        auto lastTick = Clock::now(), lastRescan = Clock::now();
        bool quit = false;

        while (!quit) {
            while (XPending(dpy_)) {
                XEvent ev;
                XNextEvent(dpy_, &ev);
                if (canvas.isCloseRequest(ev))
                    quit = true;
                else
                    handle(app, canvas, ev);
            }
            if (quit) break;

            auto now = Clock::now();
            if (app.wantsTicks() && now - lastTick >= std::chrono::milliseconds(100)) {
                lastTick = now;
                app.tick();
            }
            syncGrabs(app.watchedKeys());
            if (app.dirty) {
                app.dirty = false;
                app.draw(canvas);
                canvas.present();
            }

            std::vector<pollfd> fds;
            fds.push_back({ConnectionNumber(dpy_), POLLIN, 0});
            fds.push_back({wakeFd_, POLLIN, 0});
            evdev_.addPollFds(fds);
            int timeout = -1;
            if (app.wantsTicks())
                timeout = 100;
            else if (evdev_.available())
                timeout = 3000;
            poll(fds.data(), fds.size(), timeout);

            uint64_t count;
            if (read(wakeFd_, &count, sizeof count) > 0) runPosted();
            for (const InputEvent &e : evdev_.read()) app.globalInput(e);
            if (evdev_.available() && Clock::now() - lastRescan >= std::chrono::seconds(3)) {
                lastRescan = Clock::now();
                evdev_.rescan();
            }
        }
        syncGrabs({});
    }

private:
    void runPosted() {
        std::deque<std::function<void()>> todo;
        {
            std::lock_guard<std::mutex> lock(postMutex_);
            todo.swap(posted_);
        }
        for (auto &fn : todo) fn();
    }

    void handle(App &app, XCanvas &canvas, XEvent &ev) {
        switch (ev.type) {
        case Expose:
            app.dirty = true;
            break;
        case FocusOut:
            app.focusLost();
            break;
        case KeyPress:
            if (ev.xkey.window == root_) {
                // Delivered by a passive grab. X keycodes are key codes + 8.
                int code = static_cast<int>(ev.xkey.keycode) - 8;
                if (grabDown_.insert(code).second) app.globalInput({code, true});
            } else if (ev.xkey.window == canvas.window()) {
                app.key(toTypedKey(ev.xkey));
            }
            break;
        case KeyRelease:
            if (ev.xkey.window == root_) {
                int code = static_cast<int>(ev.xkey.keycode) - 8;
                if (grabDown_.erase(code)) app.globalInput({code, false});
            }
            break;
        case ButtonPress:
            app.click(ev.xbutton.x, ev.xbutton.y, static_cast<int>(ev.xbutton.button));
            break;
        }
    }

    static TypedKey toTypedKey(XKeyEvent &key) {
        char chars[8];
        KeySym sym = NoSymbol;
        int len = XLookupString(&key, chars, sizeof chars, &sym, nullptr);
        TypedKey press;
        press.code = static_cast<int>(key.keycode) - 8;
        press.modifier = IsModifierKey(sym);
        switch (sym) {
        case XK_Return:
        case XK_KP_Enter: press.special = TypedKey::Enter; break;
        case XK_Escape: press.special = TypedKey::Escape; break;
        case XK_BackSpace: press.special = TypedKey::Backspace; break;
        case XK_Tab: press.special = TypedKey::Tab; break;
        default:
            if (len == 1 && chars[0] >= 0x20 && chars[0] < 0x7f) press.ch = chars[0];
        }
        return press;
    }

    // Without evdev, the keys the app watches are grabbed on the root window.
    void syncGrabs(const std::vector<int> &wanted) {
        std::set<int> keys;
        if (!evdev_.available())
            for (int code : wanted)
                if (code > 0 && code < 248) keys.insert(code);
        if (keys == grabbed_) return;
        for (int code : grabbed_) XUngrabKey(dpy_, code + 8, AnyModifier, root_);
        g_grabFailed = false;
        grabDown_.clear();
        for (int code : keys) {
            // One grab per NumLock/CapsLock combination, or the key stops
            // working whenever one of them is on.
            for (unsigned int mods : {0u, (unsigned)LockMask, (unsigned)Mod2Mask, (unsigned)(LockMask | Mod2Mask)})
                XGrabKey(dpy_, code + 8, mods, root_, False, GrabModeAsync, GrabModeAsync);
        }
        XSync(dpy_, False);
        grabbed_ = keys;
    }

    Display *dpy_;
    Window root_;
    std::unique_ptr<Output> output_;
    bool wayland_;
    EvdevInput evdev_;
    int wakeFd_;
    std::mutex postMutex_;
    std::deque<std::function<void()>> posted_;
    std::set<int> grabbed_, grabDown_;
};

} // namespace

int runGui(const AppFactory &make, const GuiOptions &options) {
    Display *dpy = XOpenDisplay(nullptr);
    if (!dpy) {
        std::fprintf(stderr, "catberry: cannot open X display (is DISPLAY set?)\n");
        return 1;
    }
    std::string error;
    std::unique_ptr<Output> output = createOutput(options.backend, options.keyboardOutput, error);
    if (!output) {
        std::fprintf(stderr, "catberry: %s\n", error.c_str());
        XCloseDisplay(dpy);
        return 1;
    }
    {
        XHost host(dpy, std::move(output));
        std::unique_ptr<App> app = make(host);
        host.run(*app);
    }
    XCloseDisplay(dpy);
    return 0;
}
