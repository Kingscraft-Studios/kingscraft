#pragma once
#include <mutex>
#include <unordered_map>
#include <vector>

#include "EventHandler.hpp"

namespace kc {
    class Event;

    class EventDispatcher {
    public:
        void addHandler(const EventHandler& handler);
        void removeSubscription(uint64_t subscriptionId);
        void clear();

        size_t handlerCount() const;
        size_t handlerCount(uint64_t eventType) const;

        void dispatch(Event& event, uint64_t eventType) const;

    private:
        mutable std::mutex mutex;
        std::unordered_map<uint64_t, std::vector<EventHandler>> handlers;
    };
}
