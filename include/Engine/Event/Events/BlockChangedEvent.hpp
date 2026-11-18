#pragma once
#include <utility>

#include "Event.hpp"
#include "Core/RegistryKey.hpp"

namespace kc {

    class Block;

    // Fired after any block mutation has been applied (world cell updated,
    // cache + unsaved-chunk tracking updated). Covers break, place, and every
    // future edit path since World::setBlock is the single mutation point.
    class BlockChangedEvent : public Event {
    public:
        BlockChangedEvent(int x, int y, int z, RegistryKey<Block> previous,
                          RegistryKey<Block> current)
            : x_(x), y_(y), z_(z), previous_(std::move(previous)),
              current_(std::move(current)) {}

        int getX() const { return x_; }
        int getY() const { return y_; }
        int getZ() const { return z_; }
        const RegistryKey<Block>& getPrevious() const { return previous_; }
        const RegistryKey<Block>& getCurrent() const { return current_; }
    private:
        int x_ = 0, y_ = 0, z_ = 0;
        RegistryKey<Block> previous_;
        RegistryKey<Block> current_;
    };
}