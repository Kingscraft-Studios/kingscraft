#pragma once
#include "Event.hpp"

namespace kc {

    // Fired on the GameLogic thread when a chunk starts unloading (before
    // deferral to the pending-cleanup ring / renderer GPU teardown).
    class ChunkUnloadedEvent : public Event {
    public:
        ChunkUnloadedEvent(int gridX, int gridZ) : gridX_(gridX), gridZ_(gridZ) {}
        int getGridX() const { return gridX_; }
        int getGridZ() const { return gridZ_; }
    private:
        int gridX_ = 0, gridZ_ = 0;
    };
}