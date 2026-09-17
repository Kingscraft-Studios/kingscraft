#include "Event/EventDispatcher.hpp"

#include <algorithm>
#include <chrono>
#include <future>
#include <stdexcept>

#include "Bus/MessageBus.hpp"
#include "Event/detail/CurrentThread.hpp"
#include "Util/LogUtils.hpp"

namespace kc {

    // Bounded wait for a handler routed to another thread. A live target drains
    // its mailbox long before this (healthy reloads take tens of ms); the
    // timeout only fires when the target thread has already exited or stalled
    // (e.g. the window closing while a reload is in flight), so ordering across
    // threads is guaranteed while targets are live, and a dead target can never
    // wedge dispatch or WorkerPool shutdown for longer than this.
    inline constexpr auto kEventRoutedHandlerTimeout = std::chrono::seconds(10);
    void EventDispatcher::addHandler(const EventHandler& handler) {
        std::lock_guard<std::mutex> lock(mutex);
        auto& list = handlers[handler.eventType];

        // Insert after all handlers of equal or higher priority (stable order),
        // so the list runs highest-priority-first.
        auto it = list.begin();
        while (it != list.end() && it->priority >= handler.priority) ++it;
        list.insert(it, handler);
    }

    void EventDispatcher::removeSubscription(uint64_t subscriptionId) {
        std::lock_guard<std::mutex> lock(mutex);
        for (auto& [type, list] : handlers) {
            list.erase(std::remove_if(list.begin(), list.end(),
                [subscriptionId](const EventHandler& h) {
                    return h.subscriptionId == subscriptionId;
                }), list.end());
        }
    }

    void EventDispatcher::clear() {
        std::lock_guard<std::mutex> lock(mutex);
        handlers.clear();
    }

    size_t EventDispatcher::handlerCount() const {
        std::lock_guard<std::mutex> lock(mutex);
        size_t total = 0;
        for (const auto& [type, list] : handlers) total += list.size();
        return total;
    }

    size_t EventDispatcher::handlerCount(uint64_t eventType) const {
        std::lock_guard<std::mutex> lock(mutex);
        auto it = handlers.find(eventType);
        return (it != handlers.end()) ? it->second.size() : 0;
    }

    void EventDispatcher::dispatch(std::shared_ptr<Event> event, uint64_t eventType) const {
        std::vector<EventHandler> snapshot;
        {
            std::lock_guard<std::mutex> lock(mutex);
            auto it = handlers.find(eventType);
            if (it == handlers.end()) return;
            // Snapshot so handlers may register/unregister mid-dispatch safely.
            snapshot = it->second;
        }

        // Handlers run in highest-priority-first order. A handler bound to
        // another thread is handed to that thread's mailbox; dispatch waits
        // (bounded by kEventRoutedHandlerTimeout) for the remote body, so order
        // across threads matches priority order and the firing thread sees the
        // effects before callEvent returns.
        //
        // The bounded wait also covers a routed handler that synchronously
        // dispatches a cross-thread event back to the thread waiting on it:
        // the wait times out instead of deadlocking forever. No such handler
        // exists today, and a live target replies long before the timeout.
        for (const auto& handler : snapshot) {
            if (handler.thread == detail::currentThread()) {
                // Serialize handler bodies; recursive so a handler may fire
                // (and immediately handle) nested events on its own thread.
                std::lock_guard<std::recursive_mutex> lock(invokeMutex);
                try {
                    handler.invoke(*event);
                } catch (const std::runtime_error& e) {
                    LogUtils::error(ThreadName::Engine, StringBuilder::build("Event Handler threw an Exception: ", e.what()));
                } catch (...) {
                    LogUtils::error(ThreadName::Engine, "Event Handler threw an unknown Exception!");
                }
                continue;
            }

            auto ack = std::make_shared<std::promise<void>>();
            std::future<void> done = ack->get_future();

            if (!MessageBus::Get().send(handler.thread, [handler, event, ack]() {
                try {
                    handler.invoke(*event);
                } catch (const std::runtime_error& e) {
                    LogUtils::error(ThreadName::Engine, StringBuilder::build("Event Handler threw an Exception: ", e.what()));
                } catch (...) {
                    LogUtils::error(ThreadName::Engine, "Event Handler threw an unknown Exception!");
                }
                ack->set_value();
            })) {
                LogUtils::error(ThreadName::Engine,
                    StringBuilder::build("Event Handler target thread unreachable: event type ", eventType));
                continue;
            }

            if (done.wait_for(kEventRoutedHandlerTimeout) != std::future_status::ready) {
                LogUtils::error(ThreadName::Engine,
                    StringBuilder::build("Event Handler timed out waiting for target thread: event type ", eventType));
                continue;
            }
        }
    }
}
