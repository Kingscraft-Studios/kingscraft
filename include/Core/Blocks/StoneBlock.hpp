#pragma once
#include "Core/Blocks/Block.hpp"

namespace lve {
    class StoneBlock : public Block {
    public:
        using Block::Block;
        float getHardness() const override { return 1.5f; }
    };
}
