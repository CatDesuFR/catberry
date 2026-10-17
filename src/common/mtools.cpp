// catberry mtools - records keyboard keys, mouse buttons and wheel notches
// with their timing and plays them back (no pointer movement).

#include "gui.h"
#include "macro.h"
#include "platform.h"
#include "player.h"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <iterator>
#include <set>

namespace {

const int kStartDelaySec = 3;

const int kWidth = 364;
const int kHeight = 282;
const Rect kRecordBtn = {16, 16, 162, 44};
const Rect kPlayBtn = {186, 16, 162, 44};
const Rect kRecKeyBtn = {200, 114, 148, 30};
const Rect kPlayKeyBtn = {200, 152, 148, 30};
const Rect kSaveBtn = {244, 198, 49, 30};
const Rect kLoadBtn = {299, 198, 49, 30};

class MtoolsApp : public App {
public:
    explicit MtoolsApp(Host &host)
        : host_(host), player_(host.output(), [this] { host_.post([this] { dirty = true; }); }) {
        loadConfig();
    }
    ~MtoolsApp() override {
        player_.stop();
        saveConfig();
    }

    const char *title() const override { return "Catberry Macro Tools"; }
    int width() const override { return kWidth; }
    int height() const override { return kHeight + (host_.canRecord() ? 0 : 40); }
    bool wantsTicks() const override { return countingDown_ || recording_; }
    std::vector<int> watchedKeys() const override {
        if (capturing_) return {};
        return {recordKey_, playKey_};
    }

    void tick() override {
        if (countingDown_ && Clock::now() >= startAt_) {
            countingDown_ = false;
            startPlayback();
        }
        dirty = true;
    }

    void globalInput(const InputEvent &e) override {
        if (capturing_ || Clock::now() < ignoreHotkeysUntil_) return;
        if (e.code == recordKey_ || e.code == playKey_) {
            if (!e.down) return;
            if (e.code == recordKey_)
                toggleRecording();
            else
                togglePlayback(false);
            return;
        }
        if (!recording_) return;
        if (!isWheel(e.code)) {
            // A release whose press came before recording started (such as
            // the click on Record itself) would replay as a stray release.
            if (!e.down && !pressed_.erase(e.code)) return;
            if (e.down) pressed_.insert(e.code);
        }
        auto us = std::chrono::duration_cast<std::chrono::microseconds>(Clock::now() - recordStart_).count();
        macro_.events.push_back({us, e.code, e.down});
    }

    void focusLost() override {
        unfocusFields();
        capturing_ = nullptr;
    }

    void key(const TypedKey &press) override {
        if (capturing_) {
            if (press.code < 0 || press.modifier) return;
            int &other = capturing_ == &recordKey_ ? playKey_ : recordKey_;
            if (press.special != TypedKey::Escape && press.code != other) *capturing_ = press.code;
            capturing_ = nullptr;
            // The same key press is also on its way as global input.
            ignoreHotkeysUntil_ = Clock::now() + std::chrono::milliseconds(300);
            dirty = true;
            return;
        }
        TextField *field = repeat_.focused ? &repeat_ : name_.focused ? &name_ : nullptr;
        if (!field) return;
        TextField::Result result = field->key(press);
        if (result == TextField::Committed) unfocusFields();
        if (result != TextField::Ignored) dirty = true;
    }

    void click(int x, int y, int button) override {
        // Played-back clicks land here too if the pointer is over the
        // window, so during playback any click just means "stop".
        if (player_.playing() || countingDown_) {
            if (button <= 3) stopPlayback();
            return;
        }
        if (button != 1) return;
        if (recording_) {
            if (kRecordBtn.contains(x, y)) toggleRecording();
            return;
        }

        unfocusFields();
        capturing_ = nullptr;
        message_.clear();
        if (kRecordBtn.contains(x, y)) {
            toggleRecording();
        } else if (kPlayBtn.contains(x, y)) {
            togglePlayback(true);
        } else if (repeat_.rect.contains(x, y)) {
            repeat_.focused = true;
        } else if (name_.rect.contains(x, y)) {
            name_.focused = true;
        } else if (kRecKeyBtn.contains(x, y)) {
            capturing_ = &recordKey_;
        } else if (kPlayKeyBtn.contains(x, y)) {
            capturing_ = &playKey_;
        } else if (kSaveBtn.contains(x, y)) {
            if (name_.text.empty())
                message_ = "Type a macro name first";
            else if (macro_.events.empty())
                message_ = "Nothing recorded to save";
            else
                message_ = saveMacro(name_.text, macro_) ? "Saved '" + name_.text + "'"
                                                         : "Could not save '" + name_.text + "'";
        } else if (kLoadBtn.contains(x, y)) {
            if (name_.text.empty())
                message_ = "Type a macro name first";
            else
                message_ = loadMacro(name_.text, macro_) ? "Loaded '" + name_.text + "'"
                                                         : "No macro named '" + name_.text + "'";
        }
        dirty = true;
    }

    void draw(Canvas &c) override {
        c.clear();
        c.button(kRecordBtn, recording_ ? "Stop recording" : "Record", recording_ ? Color::Stop : Color::Panel,
                 recording_ ? Color::Bg : Color::Text);
        if (player_.playing()) {
            c.button(kPlayBtn, "Stop", Color::Stop, Color::Bg);
        } else if (countingDown_) {
            auto left = std::chrono::duration_cast<std::chrono::milliseconds>(startAt_ - Clock::now()).count();
            c.button(kPlayBtn, "Starting in " + std::to_string(std::max(0L, static_cast<long>(left)) / 1000 + 1) + "...",
                     Color::Accent, Color::Bg);
        } else {
            c.button(kPlayBtn, "Play", Color::Go, Color::Bg);
        }

        c.label(16, repeat_.rect, "Repeat (0 = until stopped)", Color::Text);
        repeat_.draw(c);
        c.label(16, kRecKeyBtn, "Record hotkey", Color::Text);
        hotkeyButton(c, kRecKeyBtn, recordKey_);
        c.label(16, kPlayKeyBtn, "Play hotkey", Color::Text);
        hotkeyButton(c, kPlayKeyBtn, playKey_);
        c.label(16, name_.rect, "Macro name", Color::Text);
        name_.draw(c);
        c.button(kSaveBtn, "Save", Color::Panel, Color::Text);
        c.button(kLoadBtn, "Load", Color::Panel, Color::Text);

        std::string status;
        char buf[96];
        if (!message_.empty()) {
            status = message_;
        } else if (recording_) {
            std::snprintf(buf, sizeof buf, "Recording... %zu events (%s to stop)", macro_.events.size(),
                          keyName(recordKey_).c_str());
            status = buf;
        } else if (player_.playing()) {
            status = "Playing - " + keyName(playKey_) + " or click this window to stop";
        } else if (countingDown_) {
            status = "Move the pointer to where it should click";
        } else if (macro_.events.empty()) {
            status = "Nothing recorded yet";
        } else {
            std::snprintf(buf, sizeof buf, "Ready: %zu events, %.1f s", macro_.events.size(),
                          macro_.durationUs / 1e6);
            status = buf;
        }
        c.text(16, 252, status, Color::Text);
        c.text(16, 270, std::string("Backend: ") + host_.output().name() + " | records keys and clicks",
               Color::Dim);
        if (!host_.canRecord()) {
            c.text(16, 294, "Recording and hotkeys are off. To fix, run this", Color::Stop);
            c.text(16, 312, std::string("and re-login:  ") + (host_.permissionFix() ? host_.permissionFix() : ""),
                   Color::Stop);
        }
    }

private:
    void toggleRecording() {
        if (player_.playing() || countingDown_) return;
        message_.clear();
        dirty = true;
        if (!recording_) {
            if (!host_.canRecord()) {
                message_ = "Recording is not available (see below)";
                return;
            }
            macro_ = Macro();
            pressed_.clear();
            recordStart_ = Clock::now();
            recording_ = true;
            return;
        }
        recording_ = false;
        macro_.durationUs =
            std::chrono::duration_cast<std::chrono::microseconds>(Clock::now() - recordStart_).count();
        // Presses still held at the end (such as the click on Stop) have no
        // release to pair with and would stay stuck down on playback.
        for (int code : pressed_) {
            for (auto it = macro_.events.rbegin(); it != macro_.events.rend(); ++it) {
                if (it->code == code && it->down) {
                    macro_.events.erase(std::next(it).base());
                    break;
                }
            }
        }
    }

    void startPlayback() {
        player_.start(macro_, std::atoi(repeat_.text.c_str()));
        dirty = true;
    }

    void stopPlayback() {
        countingDown_ = false;
        player_.stop();
        dirty = true;
    }

    void togglePlayback(bool delayed) {
        if (recording_) return;
        message_.clear();
        dirty = true;
        if (player_.playing() || countingDown_) {
            stopPlayback();
        } else if (macro_.events.empty()) {
            message_ = "Nothing to play - record or load a macro first";
        } else if (delayed) {
            // Gives time to move the pointer off this window first.
            countingDown_ = true;
            startAt_ = Clock::now() + std::chrono::seconds(kStartDelaySec);
        } else {
            startPlayback();
        }
    }

    void unfocusFields() {
        if (repeat_.focused && repeat_.text.empty()) repeat_.text = "1";
        repeat_.focused = name_.focused = false;
        dirty = true;
    }

    void hotkeyButton(Canvas &c, const Rect &r, const int &key) const {
        bool active = capturing_ == &key;
        c.button(r, active ? "Press a key..." : keyName(key), active ? Color::Accent : Color::Panel,
                 active ? Color::Bg : Color::Text);
    }

    void loadConfig() {
        std::ifstream in(configDir() / "mtools.conf");
        std::string line;
        while (std::getline(in, line)) {
            if (!line.empty() && line.back() == '\r') line.pop_back();
            size_t eq = line.find('=');
            if (eq == std::string::npos) continue;
            std::string key = line.substr(0, eq), value = line.substr(eq + 1);
            if (key == "repeat" && std::atoi(value.c_str()) >= 0)
                repeat_.text = std::to_string(std::atoi(value.c_str()));
            else if (key == "record_hotkey" && parseKey(value) > 0)
                recordKey_ = parseKey(value);
            else if (key == "play_hotkey" && parseKey(value) > 0)
                playKey_ = parseKey(value);
            else if (key == "name" && validName(value))
                name_.text = value.substr(0, name_.maxLen);
        }
    }

    void saveConfig() {
        std::ofstream out(configDir() / "mtools.conf");
        out << "repeat=" << std::atoi(repeat_.text.c_str()) << "\n"
            << "record_hotkey=" << keyName(recordKey_) << "\n"
            << "play_hotkey=" << keyName(playKey_) << "\n"
            << "name=" << name_.text << "\n";
    }

    Host &host_;
    Player player_;

    Macro macro_;
    bool recording_ = false;
    Clock::time_point recordStart_{};
    std::set<int> pressed_;

    int recordKey_ = keycode::F8;
    int playKey_ = keycode::F9;
    int *capturing_ = nullptr; // which hotkey is being rebound, if any
    Clock::time_point ignoreHotkeysUntil_{};

    TextField repeat_{{268, 76, 80, 30}, TextField::Integer, 5, "1", false};
    TextField name_{{110, 198, 128, 30}, TextField::Name, 14, "", false};
    bool countingDown_ = false;
    Clock::time_point startAt_{};
    std::string message_;
};

} // namespace

int mtoolsGui() {
    GuiOptions options;
    options.keyboardOutput = true;
    options.recordsMouse = true;
    return runGui([](Host &host) { return std::unique_ptr<App>(new MtoolsApp(host)); }, options);
}
