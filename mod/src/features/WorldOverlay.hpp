#pragma once

#include <Geode/Geode.hpp>

// GD 2.2 doesn't draw plain nodes (like CCDrawNode) that are added to its
// object layer, so anything we draw "in the level" goes on the PlayLayer
// instead and is lined up with the level every frame.
namespace cmm::overlay {
    // Make `overlay` (a child of `root`) draw in the same space as `source`.
    inline void syncTo(cocos2d::CCNode* overlay, cocos2d::CCNode* source, cocos2d::CCNode* root) {
        if (!overlay || !source || !root) return;
        auto toWorld = source->nodeToWorldTransform();
        auto fromWorld = root->worldToNodeTransform();
        overlay->setPosition({0, 0});
        overlay->setScale(1.f);
        overlay->setRotation(0.f);
        overlay->setAnchorPoint({0, 0});
        overlay->setAdditionalTransform(cocos2d::CCAffineTransformConcat(toWorld, fromWorld));
    }

    // Draws another node's own geometry (not its children) in this node's space.
    class Proxy : public cocos2d::CCNode {
    public:
        cocos2d::CCNode* target = nullptr;  // owned by the same PlayLayer

        static Proxy* create() {
            auto ret = new Proxy();
            if (ret->init()) {
                ret->autorelease();
                return ret;
            }
            delete ret;
            return nullptr;
        }

        void draw() override {
            if (target) target->draw();
        }
    };
}
