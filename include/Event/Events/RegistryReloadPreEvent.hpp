#pragma once
#include "Event.hpp"

namespace kc {

    // Fired before a registry reload wipes and rebuilds its contents. Listeners
    // can run pre-reload cleanup here (e.g. drop cached lookups).
    class RegistryReloadPreEvent : public Event {
    public:
    };
}