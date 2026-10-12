#pragma once
#include <cstdint>
#include <functional>
#include <memory>

#include "Bus/Message.hpp"
#include "Event/EventPriority.hpp"

namespace kc {

    class Listener;
    class Event;

    struct EventHandler {
        uint64_t eventType = 0;
        EventPriority priority = EventPriority::Normal;
        uint64_t subscriptionId = 0;
        // Shared so an in-flight async (posted) handler keeps its listener alive
        // even if the subscription is removed before the remote thread runs it.
        std::shared_ptr<Listener> listener;
        std::function<void(Event&)> invoke;
        ThreadName thread = ThreadName::GameLogic;
    };
}
