#pragma once
#include "Core/Blocks/Block.hpp"

namespace kc {
    class WaterBlock : public Block {
    public:
        using Block::Block;
        AABB setCollisionBox() const override { return AABB(glm::vec3(0.0f), glm::vec3(0.0f)); }
        bool isSolid() const override { return false; }
        bool isTransparent() const override { return true; }
        bool isLiquid() const override { return true; }
        bool isReplaceable() const override { return true; }
        float getHardness() const override { return 0.0f; }
    };
}