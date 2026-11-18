#pragma once
#include <future>
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

        // Synchronous: same-thread handlers run inline; handlers bound to other
        // threads are grouped per thread, posted, and awaited in priority order.
        // Returns only after every handler has run (or its target was
        // unreachable/timed out).
        void dispatch(std::shared_ptr<Event> event, uint64_t eventType) const;

        // Non-blocking: same-thread handlers still run inline; handlers bound to
        // other threads are grouped per thread and posted without being awaited.
        // When `done` is provided it is fulfilled on whichever remote group
        // finishes last (immediately if there are none).
        void dispatchAsync(std::shared_ptr<Event> event, uint64_t eventType,
                           std::shared_ptr<std::promise<void>> done = nullptr) const;

    private:
        mutable std::mutex mutex;
        // Serializes same-thread handler invocation while a dispatch runs.
        // Recursive so a handler may fire (and immediately handle) nested
        // events on its own thread without deadlocking. Cross-thread handlers
        // are routed to their bound thread instead.
        mutable std::recursive_mutex invokeMutex;
        std::unordered_map<uint64_t, std::vector<EventHandler>> handlers;
    };
}
