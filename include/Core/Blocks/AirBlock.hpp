#pragma once
#include "Block.hpp"

namespace kc {
    class AirBlock : public Block {
    public:
        AABB setCollisionBox() const override {
            return AABB(glm::vec3(0.0f), glm::vec3(0.0f));
        }

        bool isSolid() const override { return false; }
        bool isReplaceable() const override { return true; }
        int getLightAbsorption() const override { return 0; }
        float getHardness() const override { return 0.0f; }
    };
}
