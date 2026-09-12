#pragma once
#include "Event.hpp"

namespace kc {

    // Fired on the GameLogic thread after the renderer has finished rebuilding
    // the texture array and patching per-block texture offsets. Dependent
    // game-side systems (world remesh, hotbar icons) listen to this so their
    // work never races the renderer's mid-reload state.
    class RegistryReloadPostEvent : public Event {
    public:
    };
}