#pragma once

#include <Geode/Geode.hpp>
#include <deque>
#include <functional>
#include <mutex>
#include <string>

// Logger for Catberry's Mod Menu. Everything logged here shows in the live console
// and is also forwarded to Geode's normal log.
namespace cmm::log {
    enum class Level { Debug, Info, Warn, Error };

    struct Entry {
        std::string time;
        Level level;
        std::string message;
        bool cbf = false;  // a Click Before Frames "SAVED" line
    };

    void push(Level level, std::string message, bool cbf = false);
    // A Click Before Frames save line. These are what the console shows by default.
    inline void cbf(std::string message) { push(Level::Info, std::move(message), true); }
    std::deque<Entry> snapshot();
    // Runs fn with the entries locked (no copy).
    void withEntries(std::function<void(std::deque<Entry> const&)> const& fn);
    void clear();
    size_t count();

    std::string timestamp(bool withDate);

    template <class... Args>
    void debug(fmt::format_string<Args...> f, Args&&... args) { push(Level::Debug, fmt::format(f, std::forward<Args>(args)...)); }
    template <class... Args>
    void info(fmt::format_string<Args...> f, Args&&... args) { push(Level::Info, fmt::format(f, std::forward<Args>(args)...)); }
    template <class... Args>
    void warn(fmt::format_string<Args...> f, Args&&... args) { push(Level::Warn, fmt::format(f, std::forward<Args>(args)...)); }
    template <class... Args>
    void error(fmt::format_string<Args...> f, Args&&... args) { push(Level::Error, fmt::format(f, std::forward<Args>(args)...)); }
}
