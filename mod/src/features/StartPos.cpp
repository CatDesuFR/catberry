#include "StartPos.hpp"
#include <core/Hacks.hpp>
#include <core/Log.hpp>
#include <Geode/modify/PlayLayer.hpp>
#include <algorithm>

using namespace geode::prelude;

class $modify(SPSwitcherLayer, PlayLayer) {
    struct Fields {
        std::vector<Ref<StartPosObject>> startPositions;
        int index = -1;
        Ref<CCLabelBMFont> label;
        float labelTimer = 0.f;
    };

    bool init(GJGameLevel* level, bool dontCreateObjects) {
        if (!PlayLayer::init(level, dontCreateObjects)) return false;

        auto f = m_fields.self();
        std::sort(f->startPositions.begin(), f->startPositions.end(), [](auto const& a, auto const& b) {
            return a->getPositionX() < b->getPositionX();
        });

        f->index = -1;
        for (size_t i = 0; i < f->startPositions.size(); i++) {
            if (f->startPositions[i] == m_startPosObject) f->index = static_cast<int>(i);
        }

        auto win = CCDirector::get()->getWinSize();
        auto label = CCLabelBMFont::create("", "bigFont.fnt");
        label->setScale(0.5f);
        label->setOpacity(0);
        label->setPosition({win.width / 2.f, 20.f});
        label->setZOrder(1000);
        this->addChild(label);
        f->label = label;

        // On-screen arrows next to the label.
        if (cmm::on("startposswitcher") && cmm::val<bool>("startposswitcher.arrows", true) && !f->startPositions.empty()) {
            auto left = CCSprite::createWithSpriteFrameName("GJ_arrow_01_001.png");
            auto right = CCSprite::createWithSpriteFrameName("GJ_arrow_01_001.png");
            if (left && right) {
                left->setScale(0.45f);
                right->setScale(0.45f);
                right->setFlipX(true);
                auto prev = CCMenuItemSpriteExtra::create(left, this, menu_selector(SPSwitcherLayer::onPrevStart));
                auto next = CCMenuItemSpriteExtra::create(right, this, menu_selector(SPSwitcherLayer::onNextStart));
                prev->setPosition({-70.f, 0.f});
                next->setPosition({70.f, 0.f});
                auto menu = CCMenu::create();
                menu->addChild(prev);
                menu->addChild(next);
                menu->setPosition({win.width / 2.f, 20.f});
                menu->setID("cats-mod-menu-startpos-arrows");
                this->addChild(menu, 1000);
            }
        }
        return true;
    }

    void onPrevStart(CCObject*) { switchTo(m_fields->index - 1); }
    void onNextStart(CCObject*) { switchTo(m_fields->index + 1); }

    void addObject(GameObject* object) {
        PlayLayer::addObject(object);
        if (object && object->m_objectID == 31) {
            m_fields->startPositions.push_back(static_cast<StartPosObject*>(object));
        }
    }

    void postUpdate(float dt) {
        PlayLayer::postUpdate(dt);
        auto f = m_fields.self();
        if (!f->label) return;
        if (f->labelTimer > 0.f) {
            f->labelTimer -= dt;
            float alpha = std::clamp(f->labelTimer / 0.5f, 0.f, 1.f);
            f->label->setOpacity(static_cast<GLubyte>(alpha * 255));
        }
    }

    void switchTo(int index) {
        auto f = m_fields.self();
        int count = static_cast<int>(f->startPositions.size());
        if (count == 0) return;

        // -1 is "start of level"; wrap around both ends.
        if (index < -1) index = count - 1;
        if (index >= count) index = -1;
        f->index = index;

        StartPosObject* obj = index < 0 ? nullptr : f->startPositions[index].data();
        if (m_isPracticeMode) this->removeAllCheckpoints();
        this->setActiveStartPosObject(obj);
        this->resetLevel();
        this->startMusic();

        f->label->setString(fmt::format("{}/{}", index + 1, count).c_str());
        f->label->setOpacity(255);
        f->labelTimer = 1.5f;
        cmm::log::info("StartPos switcher: {}/{}", index + 1, count);
    }
};

namespace cmm::startpos {
    static SPSwitcherLayer* layer() {
        return static_cast<SPSwitcherLayer*>(PlayLayer::get());
    }

    int currentIndex() {
        auto l = layer();
        return l ? l->m_fields->index : -1;
    }

    int count() {
        auto l = layer();
        return l ? static_cast<int>(l->m_fields->startPositions.size()) : 0;
    }

    void step(PlayLayer* pl, int direction) {
        if (!pl || !cmm::on("startposswitcher")) return;
        auto l = static_cast<SPSwitcherLayer*>(pl);
        l->switchTo(l->m_fields->index + direction);
    }
}
