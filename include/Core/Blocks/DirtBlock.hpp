#pragma once
#include "Core/Blocks/Block.hpp"

namespace lve {
    class DirtBlock : public Block {
    public:
        using Block::Block;
        float getHardness() const override { return 0.5f; }
    };
}
