#pragma once

#include "Event/Events/BlockBreakEvent.hpp"
#include "Event/Events/BlockPlaceEvent.hpp"
#include "Event/KCEventHandler.hpp"
#include "Event/Listener.hpp"
#include "Event/detail/EventRegistrar.hpp"

namespace kc {

    // Owns the player's block-interaction rules. The screen only raycasts and
    // fires the request events; this listener validates (range implied by the
    // raycast, world bounds, air, player-AABB overlap) and applies the edit.
    class PlayerBlockInteractionListener : public Listener {
    public:
        // Must be public: EventManager's generated dispatch lambda calls the
        // kcOnEvent helper from outside the class.
        KC_EVENT_HANDLER(BlockBreakEvent, onBlockBreak, ThreadName::GameLogic)
        KC_EVENT_HANDLER(BlockPlaceEvent, onBlockPlace, ThreadName::GameLogic)

    private:
        void onBlockBreak(BlockBreakEvent& event);
        void onBlockPlace(BlockPlaceEvent& event);
    };

}