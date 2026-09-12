#pragma once
#include "Event.hpp"

namespace kc {

    // Fired after a registry reload has cleared and rebuilt its contents.
    // Acts as the "post" phase: this is where mods re-register their entries
    // that were wiped by the reload.
    class RegistryReloadEvent : public Event {
    public:
    };
}