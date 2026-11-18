#pragma once
#include "Event.hpp"

namespace kc {

    // Fired when the player is returned to life (respawn button). Listeners
    // undo whatever PlayerDeathEvent did (re-capture cursor, hide overlay).
    class PlayerRespawnEvent : public Event {
    public:
    };
}