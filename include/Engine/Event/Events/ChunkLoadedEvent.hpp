#pragma once
#include "Event.hpp"

namespace kc {

    // Fired on the GameLogic thread once a chunk finishes loading and is
    // installed into the live world (async pipeline install, or the initial
    // 3x3 / boundary synchronous load).
    class ChunkLoadedEvent : public Event {
    public:
        ChunkLoadedEvent(int gridX, int gridZ) : gridX_(gridX), gridZ_(gridZ) {}
        int getGridX() const { return gridX_; }
        int getGridZ() const { return gridZ_; }
    private:
        int gridX_ = 0, gridZ_ = 0;
    };
}