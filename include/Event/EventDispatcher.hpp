#pragma once
#include <memory>
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

        void dispatch(std::shared_ptr<Event> event, uint64_t eventType) const;

    private:
        mutable std::mutex mutex;
        // Serializes same-thread handler invocation while a dispatch runs.
        // Recursive so a handler may fire (and immediately handle) nested
        // events on its own thread without deadlocking. Cross-thread handlers
        // are routed to their bound thread and awaited via a handshake instead.
        mutable std::recursive_mutex invokeMutex;
        std::unordered_map<uint64_t, std::vector<EventHandler>> handlers;
    };
}
