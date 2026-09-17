#pragma once
#include "Event.hpp"

namespace kc {

    // Why a player died. Extended as more danger sources are added.
    enum class DeathCause {
        Void,
    };

    // Fired exactly once when the player dies (currently: falling below the
    // void kill plane). Listeners drive UI, respawn logic, etc.
    class PlayerDeathEvent : public Event {
    public:
        explicit PlayerDeathEvent(DeathCause cause) : cause_(cause) {}
        DeathCause getCause() const { return cause_; }
    private:
        DeathCause cause_ = DeathCause::Void;
    };
}