#pragma once
#include "Event.hpp"

namespace kc {

    // Fired before a player breaks the block at (x, y, z). Carries the world
    // cell the raycast hit; the block there is resolved live by listeners.
    class BlockBreakEvent : public Event {
    public:
        BlockBreakEvent(int x, int y, int z) : x_(x), y_(y), z_(z) {}

        int getX() const { return x_; }
        int getY() const { return y_; }
        int getZ() const { return z_; }
    private:
        int x_ = 0, y_ = 0, z_ = 0;
    };
}