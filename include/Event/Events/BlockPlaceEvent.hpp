#pragma once
#include <utility>

#include "Event.hpp"
#include "Core/RegistryKey.hpp"

namespace kc {

    class Block;

    // Fired before a player places the hotbar-selected block at (x, y, z).
    // face is the raycast hit face pointing from the hit cell toward the
    // placement cell. replaced is whatever currently occupies the target cell.
    class BlockPlaceEvent : public Event {
    public:
        BlockPlaceEvent(int x, int y, int z, RegistryKey<Block> block,
                        RegistryKey<Block> replaced, int face)
            : x_(x), y_(y), z_(z), block_(std::move(block)),
              replaced_(std::move(replaced)), face_(face) {}

        int getX() const { return x_; }
        int getY() const { return y_; }
        int getZ() const { return z_; }
        int getFace() const { return face_; }
        const RegistryKey<Block>& getBlock() const { return block_; }
        const RegistryKey<Block>& getReplaced() const { return replaced_; }
    private:
        int x_ = 0, y_ = 0, z_ = 0;
        RegistryKey<Block> block_;
        RegistryKey<Block> replaced_;
        int face_ = 0;
    };
}