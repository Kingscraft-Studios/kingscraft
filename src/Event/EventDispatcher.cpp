#include "Event/EventDispatcher.hpp"

#include <algorithm>
#include <stdexcept>

#include "Util/LogUtils.hpp"

namespace kc {
    void EventDispatcher::addHandler(const EventHandler& handler) {
        std::lock_guard<std::mutex> lock(mutex);
        auto& list = handlers[handler.eventType];

        // Insert after all handlers of equal/lower priority (stable order).
        auto it = list.begin();
        while (it != list.end() && it->priority <= handler.priority) ++it;
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

    void EventDispatcher::dispatch(Event& event, uint64_t eventType) const {
        std::vector<EventHandler> snapshot;
        {
            std::lock_guard<std::mutex> lock(mutex);
            auto it = handlers.find(eventType);
            if (it == handlers.end()) return;
            // Snapshot so handlers may register/unregister mid-dispatch safely.
            snapshot = it->second;
        }

        for (const auto& handler : snapshot) {
            try {
                handler.invoke(event);
            } catch (const std::runtime_error& e) {
                LogUtils::error(ThreadName::Engine, StringBuilder::build("Event Handler threw an Exception: ", e.what()));
            } catch (...) {
                LogUtils::error(ThreadName::Engine, "Event Handler threw an unknown Exception!");
            }
        }
    }
}
