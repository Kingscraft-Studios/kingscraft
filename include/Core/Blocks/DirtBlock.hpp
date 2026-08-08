#pragma once
#include "Core/Blocks/Block.hpp"

namespace lve {
    class DirtBlock : public Block {
    public:
        using Block::Block;
        AABB setCollisionBox() const override { return AABB(glm::vec3(0.0f), glm::vec3(1.0f)); }
        float getHardness() const override { return 0.5f; }
    };
}
