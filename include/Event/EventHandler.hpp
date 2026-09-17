#pragma once
#include <cstdint>
#include <functional>

#include "Bus/Message.hpp"
#include "Event/EventPriority.hpp"

namespace kc {

    class Listener;
    class Event;

    struct EventHandler {
        uint64_t eventType = 0;
        EventPriority priority = EventPriority::Normal;
        uint64_t subscriptionId = 0;
        Listener* listener = nullptr;
        std::function<void(Event&)> invoke;
        ThreadName thread = ThreadName::GameLogic;
    };
}
