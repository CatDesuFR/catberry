// Developer tool: lists object IDs whose texture names contain a word. Only
// runs if "selftest-objects.txt" exists in the mod's save folder (deleted when
// it starts); the file holds the words, comma-separated. Writes "OBJDUMP" lines.
#include <Geode/modify/MenuLayer.hpp>
#include <filesystem>
#include <fstream>
#include <sstream>

using namespace geode::prelude;

class $modify(ObjDumpMenu, MenuLayer) {
    bool init() {
        if (!MenuLayer::init()) return false;
        std::error_code ec;
        auto trigger = std::filesystem::path(Mod::get()->getSaveDir().native()) / "selftest-objects.txt";
        if (!std::filesystem::exists(trigger, ec)) return true;
        std::string words;
        { std::ifstream in(trigger); std::getline(in, words); }
        std::filesystem::remove(trigger, ec);
        std::vector<std::string> want;
        std::stringstream ss(words);
        for (std::string w; std::getline(ss, w, ',');) if (!w.empty()) want.push_back(w);
        auto tb = ObjectToolbox::sharedState();
        for (int id = 1; id < 4600; id++) {
            std::string frame = tb->intKeyToFrame(id);
            if (frame.empty()) continue;
            std::string low = frame;
            std::transform(low.begin(), low.end(), low.begin(), ::tolower);
            for (auto const& w : want) if (low.find(w) != std::string::npos) { log::info("OBJDUMP {} {}", id, frame); break; }
        }
        log::info("OBJDUMP DONE");
        return true;
    }
};
