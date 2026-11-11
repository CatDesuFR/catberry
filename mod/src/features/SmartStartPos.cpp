// Smart StartPos: sets every start position to what the player would really
// have there (gamemode, speed, size, gravity, mirror, dual), worked out from
// the portals before it. Fixes start positions that were placed with the
// wrong settings.
#include <core/Hacks.hpp>
#include <core/Log.hpp>
#include <Geode/modify/PlayLayer.hpp>
#include <algorithm>

using namespace geode::prelude;

namespace {
    enum class Kind { Mode, Speed, Mini, Gravity, Mirror, Dual };

    struct Portal {
        float x;
        Kind kind;
        int value;
    };

    // Object ID -> what it sets.
    bool classify(int id, Kind& kind, int& value) {
        switch (id) {
            // gamemodes (LevelSettingsObject::m_startMode values)
            case 12: kind = Kind::Mode; value = 0; return true;    // cube
            case 13: kind = Kind::Mode; value = 1; return true;    // ship
            case 47: kind = Kind::Mode; value = 2; return true;    // ball
            case 111: kind = Kind::Mode; value = 3; return true;   // ufo
            case 660: kind = Kind::Mode; value = 4; return true;   // wave
            case 745: kind = Kind::Mode; value = 5; return true;   // robot
            case 1331: kind = Kind::Mode; value = 6; return true;  // spider
            case 1933: kind = Kind::Mode; value = 7; return true;  // swing
            // speed (Speed enum values)
            case 200: kind = Kind::Speed; value = static_cast<int>(Speed::Slow); return true;
            case 201: kind = Kind::Speed; value = static_cast<int>(Speed::Normal); return true;
            case 202: kind = Kind::Speed; value = static_cast<int>(Speed::Fast); return true;
            case 203: kind = Kind::Speed; value = static_cast<int>(Speed::Faster); return true;
            case 1334: kind = Kind::Speed; value = static_cast<int>(Speed::Fastest); return true;
            // size
            case 101: kind = Kind::Mini; value = 1; return true;
            case 99: kind = Kind::Mini; value = 0; return true;
            // gravity: normal, flipped, toggle
            case 10: kind = Kind::Gravity; value = 0; return true;
            case 11: kind = Kind::Gravity; value = 1; return true;
            case 2926: kind = Kind::Gravity; value = 2; return true;
            // mirror
            case 45: kind = Kind::Mirror; value = 1; return true;
            case 46: kind = Kind::Mirror; value = 0; return true;
            // dual
            case 286: kind = Kind::Dual; value = 1; return true;
            case 287: kind = Kind::Dual; value = 0; return true;
            default: return false;
        }
    }
}

class $modify(SmartStartPosLayer, PlayLayer) {
    struct Fields {
        std::vector<Portal> portals;
        std::vector<Ref<StartPosObject>> starts;
    };

    void addObject(GameObject* object) {
        PlayLayer::addObject(object);
        if (!object) return;
        Kind kind;
        int value;
        if (object->m_objectID == 31) {
            m_fields->starts.push_back(static_cast<StartPosObject*>(object));
        } else if (classify(object->m_objectID, kind, value)) {
            m_fields->portals.push_back({object->getPositionX(), kind, value});
        }
    }

    void applySmartStartPos() {
        auto f = m_fields.self();
        if (f->starts.empty() || !m_levelSettings) return;
        std::sort(f->portals.begin(), f->portals.end(), [](auto const& a, auto const& b) { return a.x < b.x; });

        int changed = 0;
        for (auto& start : f->starts) {
            auto settings = start->m_startSettings;
            if (!settings) continue;
            // Start from the level's own start settings, then walk the portals.
            int mode = m_levelSettings->m_startMode;
            int speed = static_cast<int>(m_levelSettings->m_startSpeed);
            bool mini = m_levelSettings->m_startMini;
            bool flipped = m_levelSettings->m_isFlipped;
            bool mirror = m_levelSettings->m_mirrorMode;
            bool dual = m_levelSettings->m_startDual;
            float sx = start->getPositionX();
            for (auto const& p : f->portals) {
                if (p.x >= sx) break;
                switch (p.kind) {
                    case Kind::Mode: mode = p.value; break;
                    case Kind::Speed: speed = p.value; break;
                    case Kind::Mini: mini = p.value; break;
                    case Kind::Gravity: flipped = p.value == 2 ? !flipped : p.value == 1; break;
                    case Kind::Mirror: mirror = p.value; break;
                    case Kind::Dual: dual = p.value; break;
                }
            }
            if (settings->m_startMode != mode || static_cast<int>(settings->m_startSpeed) != speed || settings->m_startMini != mini
                || settings->m_isFlipped != flipped || settings->m_mirrorMode != mirror || settings->m_startDual != dual) {
                changed++;
            }
            settings->m_startMode = mode;
            settings->m_startSpeed = static_cast<Speed>(speed);
            settings->m_startMini = mini;
            settings->m_isFlipped = flipped;
            settings->m_mirrorMode = mirror;
            settings->m_startDual = dual;
        }
        if (changed) cmm::log::info("Smart StartPos: fixed {} of {} start positions", changed, f->starts.size());
    }

    void setupHasCompleted() {
        if (cmm::on("smartstartpos")) applySmartStartPos();
        PlayLayer::setupHasCompleted();
    }
};
