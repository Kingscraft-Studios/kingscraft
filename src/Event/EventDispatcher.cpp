#include "Event/EventDispatcher.hpp"

#include <algorithm>
#include <chrono>
#include <future>
#include <stdexcept>

#include "Bus/MessageBus.hpp"
#include "Event/detail/CurrentThread.hpp"
#include "Util/LogUtils.hpp"
#include "Util/StringUtils.hpp"

namespace kc {

    // Bounded wait for a handler routed to another thread. A live target drains
    // its mailbox long before this (healthy reloads take tens of ms); the
    // timeout only fires when the target thread has already exited or stalled
    // (e.g. the window closing while a reload is in flight), so ordering across
    // threads is guaranteed while targets are live, and a dead target can never
    // wedge dispatch or WorkerPool shutdown for longer than this.
    inline constexpr auto eventRoutedHandlerTimeout = std::chrono::seconds(10);

    namespace {

        // Handlers bound to one thread, in priority order.
        struct ThreadGroup {
            ThreadName thread;
            std::vector<EventHandler> handlers;
        };

        // Collapse the priority-ordered handler snapshot into per-thread groups.
        // Groups appear in the order their highest-priority handler appears, and
        // handlers inside a group keep priority order, so batching preserves the
        // same ordering as one-hop-per-handler dispatch.
        std::vector<ThreadGroup> groupByThread(const std::vector<EventHandler>& snapshot) {
            std::vector<ThreadGroup> groups;
            for (const auto& handler : snapshot) {
                auto it = std::find_if(groups.begin(), groups.end(),
                    [&](const ThreadGroup& g) { return g.thread == handler.thread; });
                if (it == groups.end()) {
                    groups.push_back(ThreadGroup{handler.thread, {handler}});
                } else {
                    it->handlers.push_back(handler);
                }
            }
            return groups;
        }

        void invokeHandler(const EventHandler& handler, Event& event) {
            try {
                handler.invoke(event);
            } catch (const std::runtime_error& e) {
                LogUtils::error(ThreadName::Engine, StringUtils::build("Event Handler threw an Exception: ", e.what()));
            } catch (...) {
                LogUtils::error(ThreadName::Engine, "Event Handler threw an unknown Exception!");
            }
        }

    }

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

        // Handlers run in highest-priority-first order. Handlers sharing a
        // thread are run in one hop; a group bound to another thread is handed
        // to that thread's mailbox and awaited (bounded by
        // eventRoutedHandlerTimeout), so order across threads matches priority
        // order and the firing thread sees the effects before callEvent returns.
        //
        // The bounded wait also covers a routed handler that synchronously
        // dispatches a cross-thread event back to the thread waiting on it:
        // the wait times out instead of deadlocking forever. No such handler
        // exists today, and a live target replies long before the timeout.
        for (const auto& group : groupByThread(snapshot)) {
            if (group.thread == detail::currentThread()) {
                // Serialize handler bodies; recursive so a handler may fire
                // (and immediately handle) nested events on its own thread.
                std::lock_guard<std::recursive_mutex> lock(invokeMutex);
                for (const auto& handler : group.handlers) invokeHandler(handler, *event);
                continue;
            }

            auto ack = std::make_shared<std::promise<void>>();
            std::future<void> done = ack->get_future();

            if (!MessageBus::Get().send(group.thread, [group, event, ack]() {
                for (const auto& handler : group.handlers) invokeHandler(handler, *event);
                ack->set_value();
            })) {
                LogUtils::error(ThreadName::Engine,
                    StringUtils::build("Event Handler target thread unreachable: event type ", eventType));
                continue;
            }

            if (done.wait_for(eventRoutedHandlerTimeout) != std::future_status::ready) {
                LogUtils::error(ThreadName::Engine,
                    StringUtils::build("Event Handler timed out waiting for target thread: event type ", eventType));
                continue;
            }
        }
    }

    void EventDispatcher::dispatchAsync(std::shared_ptr<Event> event, uint64_t eventType,
                                        std::shared_ptr<std::promise<void>> done) const {
        std::vector<EventHandler> snapshot;
        {
            std::lock_guard<std::mutex> lock(mutex);
            auto it = handlers.find(eventType);
            if (it == handlers.end()) {
                if (done) done->set_value();
                return;
            }
            // Snapshot so handlers may register/unregister mid-dispatch safely.
            snapshot = it->second;
        }

        auto groups = groupByThread(snapshot);
        const int remoteCount = static_cast<int>(std::count_if(groups.begin(), groups.end(),
            [](const ThreadGroup& g) { return g.thread != detail::currentThread(); }));

        // Shared completion state. The event is held here (and by every posted
        // group lambda) so it outlives the firing call; each remote group
        // decrements `remaining`, and the last one fulfils `done`. Listeners are
        // kept alive transitively through the EventHandler shared_ptrs.
        auto remaining = std::make_shared<std::atomic<int>>(remoteCount);
        auto complete = [remaining, done]() {
            if (remaining->fetch_sub(1, std::memory_order_acq_rel) == 1 && done) {
                done->set_value();
            }
        };

        for (const auto& group : groups) {
            if (group.thread == detail::currentThread()) {
                std::lock_guard<std::recursive_mutex> lock(invokeMutex);
                for (const auto& handler : group.handlers) invokeHandler(handler, *event);
                continue;
            }

            if (!MessageBus::Get().send(group.thread, [group, event, complete]() {
                for (const auto& handler : group.handlers) invokeHandler(handler, *event);
                complete();
            })) {
                LogUtils::error(ThreadName::Engine,
                    StringUtils::build("Event Handler target thread unreachable: event type ", eventType));
                complete();
            }
        }

        // No remote groups: completion is immediate (all handlers were inline).
        if (remoteCount == 0 && done) done->set_value();
    }
}
