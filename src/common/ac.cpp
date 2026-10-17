// catberry ac - autoclicker window.

#include "clicker.h"
#include "gui.h"
#include "platform.h"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <fstream>

namespace {

const int kStartDelaySec = 3;

const int kTargetCount = 5;
const char *const kTargetNames[kTargetCount] = {"Left", "Middle", "Right", "Scroll up", "Scroll down"};
const int kTargetCodes[kTargetCount] = {keycode::MouseLeft, keycode::MouseMiddle, keycode::MouseRight,
                                        keycode::WheelUp, keycode::WheelDown};

const int kWidth = 364;
const int kHeight = 358;
const Rect kMinusBtn = {200, 16, 30, 30};
const Rect kPlusBtn = {318, 16, 30, 30};
const Rect kTargetBtns[kTargetCount] = {
    {16, 84, 106, 30}, {129, 84, 106, 30}, {242, 84, 106, 30}, {16, 121, 162, 30}, {186, 121, 162, 30}};
const Rect kHotkeyBtn = {200, 167, 148, 30};
const Rect kToggleBtn = {16, 213, 332, 44};
const Rect kTestArea = {16, 265, 332, 44};

class AcApp : public App {
public:
    explicit AcApp(Host &host)
        : host_(host), clicker_(host.output()),
          showFix_(host.permissionFix() && host.hotkeyScope() != HotkeyScope::Global) {
        loadConfig();
        cps_.text = formatCps(clicker_.cps());
    }
    ~AcApp() override { saveConfig(); }

    const char *title() const override { return "Catberry Autoclicker"; }
    int width() const override { return kWidth; }
    int height() const override { return kHeight + (showFix_ ? 44 : 0); }
    bool wantsTicks() const override { return countingDown_ || clicker_.running(); }
    std::vector<int> watchedKeys() const override {
        // While rebinding, the current key must reach our window instead.
        if (capturing_) return {};
        return {hotkey_};
    }

    void tick() override {
        if (countingDown_ && Clock::now() >= startAt_) {
            countingDown_ = false;
            start();
        }
        // Keeps the countdown and the click-test readout moving.
        dirty = true;
    }

    void globalInput(const InputEvent &ev) override {
        if (ev.down && ev.code == hotkey_ && !capturing_ && Clock::now() >= ignoreHotkeyUntil_) toggleNow();
    }

    void focusLost() override {
        if (cps_.focused) commitCps();
        if (capturing_) {
            capturing_ = false;
            dirty = true;
        }
    }

    void key(const TypedKey &press) override {
        if (capturing_) {
            if (press.code < 0 || press.modifier) return;
            if (press.special != TypedKey::Escape) hotkey_ = press.code;
            capturing_ = false;
            // The same key press may still be on its way as global input.
            ignoreHotkeyUntil_ = Clock::now() + std::chrono::milliseconds(300);
            dirty = true;
            return;
        }
        if (!cps_.focused) return;
        switch (cps_.key(press)) {
        case TextField::Committed:
            commitCps();
            break;
        case TextField::Changed: {
            double value = std::strtod(cps_.text.c_str(), nullptr);
            if (value > 0) clicker_.setCps(value);
            dirty = true;
            break;
        }
        case TextField::Ignored:
            break;
        }
    }

    void click(int x, int y, int button) override {
        bool running = clicker_.running();
        if (running && kTestArea.contains(x, y)) {
            // Our own clicks arriving back through the whole input stack.
            if (testCount_++ == 0) testFirst_ = Clock::now();
            testLast_ = Clock::now();
            return;
        }
        if (button > 3) return; // wheel
        // While clicking, our own synthetic clicks land here too if the
        // pointer is over the window, so any other click just means "stop".
        if (running || countingDown_) {
            stop();
            return;
        }
        if (button != 1) return;

        if (cps_.focused && !cps_.rect.contains(x, y)) commitCps();
        if (!kHotkeyBtn.contains(x, y)) capturing_ = false;

        if (cps_.rect.contains(x, y)) {
            cps_.focused = true;
        } else if (kMinusBtn.contains(x, y)) {
            stepCps(-1);
        } else if (kPlusBtn.contains(x, y)) {
            stepCps(1);
        } else if (kHotkeyBtn.contains(x, y)) {
            capturing_ = !capturing_;
        } else if (kToggleBtn.contains(x, y)) {
            // Delayed, so the pointer can be moved off this window first.
            countingDown_ = true;
            startAt_ = Clock::now() + std::chrono::seconds(kStartDelaySec);
        } else {
            for (int i = 0; i < kTargetCount; ++i)
                if (kTargetBtns[i].contains(x, y)) clicker_.setCode(kTargetCodes[i]);
        }
        dirty = true;
    }

    void draw(Canvas &c) override {
        c.clear();
        c.label(16, cps_.rect, "Clicks per second", Color::Text);
        c.button(kMinusBtn, "-", Color::Panel, Color::Text);
        cps_.draw(c);
        c.button(kPlusBtn, "+", Color::Panel, Color::Text);

        c.text(16, 76, "Click with", Color::Text);
        for (int i = 0; i < kTargetCount; ++i) {
            bool selected = clicker_.code() == kTargetCodes[i];
            c.button(kTargetBtns[i], kTargetNames[i], selected ? Color::Accent : Color::Panel,
                     selected ? Color::Bg : Color::Text);
        }

        c.label(16, kHotkeyBtn, "Start/stop hotkey", Color::Text);
        c.button(kHotkeyBtn, capturing_ ? "Press a key..." : keyName(hotkey_), capturing_ ? Color::Accent : Color::Panel,
                 capturing_ ? Color::Bg : Color::Text);

        std::string status;
        if (clicker_.running()) {
            c.button(kToggleBtn, "Stop", Color::Stop, Color::Bg);
            status = "Running - " + keyName(hotkey_) + " or click this window to stop";
        } else if (countingDown_) {
            auto left = std::chrono::duration_cast<std::chrono::milliseconds>(startAt_ - Clock::now()).count();
            c.button(kToggleBtn,
                     "Starting in " + std::to_string(std::max(0L, static_cast<long>(left)) / 1000 + 1) +
                         "... (click to cancel)",
                     Color::Accent, Color::Bg);
            status = "Move the pointer to where it should click";
        } else {
            c.button(kToggleBtn, "Start", Color::Go, Color::Bg);
            status = "Stopped";
        }
        c.button(kTestArea, testText(), Color::Bg, Color::Dim);
        c.text(16, 330, status, Color::Text);

        std::string info = std::string("Backend: ") + host_.output().name();
        if (host_.outputLimited()) info += " (X11 windows only)";
        switch (host_.hotkeyScope()) {
        case HotkeyScope::Global: info += " | hotkey: global"; break;
        case HotkeyScope::X11WindowsOnly: info += " | hotkey: X11 windows only"; break;
        case HotkeyScope::Taken: info += " | hotkey: taken by another app"; break;
        }
        c.text(16, 348, info, Color::Dim);
        if (showFix_) {
            c.text(16, 372, "Hotkey is not global yet. To fix, run this and", Color::Stop);
            c.text(16, 390, std::string("re-login:  ") + host_.permissionFix(), Color::Stop);
        }
    }

private:
    void start() {
        testCount_ = 0;
        clicker_.setRunning(true);
        dirty = true;
    }

    void stop() {
        countingDown_ = false;
        clicker_.setRunning(false);
        dirty = true;
    }

    void toggleNow() {
        if (clicker_.running() || countingDown_)
            stop();
        else
            start();
    }

    void commitCps() {
        cps_.focused = false;
        cps_.text = formatCps(clicker_.cps());
        dirty = true;
    }

    void stepCps(double delta) {
        clicker_.setCps(clicker_.cps() + delta);
        commitCps();
    }

    std::string testText() const {
        if (testCount_ == 0) return "Click test: start, then hover here";
        char buf[64];
        double seconds = std::chrono::duration<double>(testLast_ - testFirst_).count();
        if (testCount_ < 2 || seconds <= 0)
            std::snprintf(buf, sizeof buf, "Click test: %d received", testCount_);
        else
            std::snprintf(buf, sizeof buf, "Click test: %.1f CPS received (%d)", (testCount_ - 1) / seconds,
                          testCount_);
        return buf;
    }

    static std::string formatCps(double cps) {
        char buf[32];
        std::snprintf(buf, sizeof buf, "%g", cps);
        return buf;
    }

    void loadConfig() {
        std::ifstream in(configDir() / "ac.conf");
        // Location used by the standalone catberryacgui (ac 1.0.1 and older).
        if (!in) in.open(configDir().parent_path() / "catberryacgui.conf");
        std::string line;
        while (std::getline(in, line)) {
            if (!line.empty() && line.back() == '\r') line.pop_back();
            size_t eq = line.find('=');
            if (eq == std::string::npos) continue;
            std::string key = line.substr(0, eq), value = line.substr(eq + 1);
            double number = std::strtod(value.c_str(), nullptr);
            if (key == "cps" && number > 0) {
                clicker_.setCps(number);
            } else if (key == "target" && number >= 0 && number < kTargetCount) {
                clicker_.setCode(kTargetCodes[static_cast<int>(number)]);
            } else if (key == "toggle_key" && parseKey(value) >= 0) {
                hotkey_ = parseKey(value);
            } else if (key == "hotkey" && number >= 9 && number <= 255) {
                // Older Linux builds stored an X keycode, which is the key
                // code plus 8.
                hotkey_ = static_cast<int>(number) - 8;
            }
        }
    }

    void saveConfig() {
        int target = 0;
        for (int i = 0; i < kTargetCount; ++i)
            if (kTargetCodes[i] == clicker_.code()) target = i;
        std::ofstream out(configDir() / "ac.conf");
        out << "cps=" << formatCps(clicker_.cps()) << "\n"
            << "target=" << target << "\n"
            << "toggle_key=" << keyName(hotkey_) << "\n";
    }

    Host &host_;
    Clicker clicker_;
    bool showFix_;

    int hotkey_ = keycode::F6;
    bool capturing_ = false;
    Clock::time_point ignoreHotkeyUntil_{};

    TextField cps_{{234, 16, 80, 30}, TextField::Decimal, 7, "", false};
    bool countingDown_ = false;
    Clock::time_point startAt_{};
    int testCount_ = 0;
    Clock::time_point testFirst_{}, testLast_{};
};

} // namespace

int acGui(int argc, char **argv) {
    GuiOptions options;
    for (int i = 0; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg.rfind("--backend=", 0) == 0) {
            options.backend = arg.substr(10);
        } else {
            std::fprintf(stderr, "catberry ac gui: unknown option '%s'\n", arg.c_str());
            return 2;
        }
    }
    return runGui([](Host &host) { return std::unique_ptr<App>(new AcApp(host)); }, options);
}
