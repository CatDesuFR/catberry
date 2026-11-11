#include "Log.hpp"
#include <ctime>

namespace cmm::log {
    static constexpr size_t MAX_ENTRIES = 2000;

    static std::mutex& mutex() {
        static std::mutex m;
        return m;
    }

    static std::deque<Entry>& entries() {
        static std::deque<Entry> e;
        return e;
    }

    std::string timestamp(bool withDate) {
        std::time_t now = std::time(nullptr);
        std::tm tm{};
#ifdef GEODE_IS_WINDOWS
        localtime_s(&tm, &now);
#else
        localtime_r(&now, &tm);
#endif
        char buf[32];
        std::strftime(buf, sizeof(buf), withDate ? "%Y-%m-%d %H:%M:%S" : "%H:%M:%S", &tm);
        return buf;
    }

    void push(Level level, std::string message, bool cbf) {
        switch (level) {
            case Level::Debug: geode::log::debug("{}", message); break;
            case Level::Info: geode::log::info("{}", message); break;
            case Level::Warn: geode::log::warn("{}", message); break;
            case Level::Error: geode::log::error("{}", message); break;
        }
        std::lock_guard lock(mutex());
        auto& e = entries();
        e.push_back({timestamp(false), level, std::move(message), cbf});
        while (e.size() > MAX_ENTRIES) e.pop_front();
    }

    std::deque<Entry> snapshot() {
        std::lock_guard lock(mutex());
        return entries();
    }

    void withEntries(std::function<void(std::deque<Entry> const&)> const& fn) {
        std::lock_guard lock(mutex());
        fn(entries());
    }

    void clear() {
        std::lock_guard lock(mutex());
        entries().clear();
    }

    size_t count() {
        std::lock_guard lock(mutex());
        return entries().size();
    }
}
