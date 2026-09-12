#pragma once
#include <cstdint>
#include <memory>
#include <mutex>
#include <type_traits>
#include <unordered_map>
#include <utility>
#include <vector>

#include "Event/EventDispatcher.hpp"
#include "Event/EventRegistry.hpp"
#include "Event/EventSubscription.hpp"
#include "Event/Events/Event.hpp"
#include "Event/Listener.hpp"
#include "Event/detail/EventRegistrar.hpp"
#include "Event/detail/TypeId.hpp"

namespace kc {
    // Public coordinator: registerListener / unregisterListener / callEvent.
    // Synchronous; dispatches on the caller's thread. Thread-safe so events
    // can be fired from worker threads (e.g. registry reload) while other
    // threads register listeners.
    class EventManager {
    public:
        static EventManager& get() {
            static EventManager instance;
            return instance;
        }

        // Creates and owns the listener instance, discovers ALL its
        // KC_EVENT_HANDLER methods, and registers each into the dispatcher.
        // The registration persists until unregisterListener<L>() is called or
        // the EventManager is destroyed; no handle needs to be kept alive.
        template<typename ListenerType>
        void registerListener();

        // Scoped variant: the returned handle removes every handler for this
        // listener when it is destroyed (opt-in auto-cleanup).
        template<typename ListenerType>
        EventSubscription registerScopedListener();

        // Removes every handler + instance belonging to ListenerType.
        template<typename ListenerType>
        void unregisterListener();

        // Fires an event on the caller's thread. The event is constructed here
        // from the given ctor args, so firing sites don't have to instantiate +
        // repeat the event object (e.g. callEvent<RegistryReloadEvent>()).
        //
        // TODO(cancellable): decide how a cancelled CancellableEvent
        // short-circuits — stop invoking remaining handlers in the dispatcher
        // and/or abort the firing call site so it doesn't have to check
        // isCancelled() after firing.
        template<typename EventType, typename... Args>
        void callEvent(Args&&... args);

        EventDispatcher& getDispatcher() { return dispatcher; }
        EventRegistry& getRegistry() { return EventRegistry::get(); }

        void unregisterSubscription(uint64_t subscriptionId);

    private:
        EventManager() = default;

        // Shared registration core; returns the subscription id the handlers
        // were registered under (used by the scoped variant).
        template<typename ListenerType>
        uint64_t registerShared();

        struct Registration {
            uint64_t subscriptionId = 0;
            uint64_t classId = 0;
            std::unique_ptr<Listener> instance;
        };

        std::mutex mutex;
        uint64_t nextSubscriptionId = 1;
        std::unordered_map<uint64_t, std::vector<Registration>> classes;
        EventDispatcher dispatcher;
    };

    template<typename ListenerType>
    uint64_t EventManager::registerShared() {
        static_assert(std::is_base_of_v<Listener, ListenerType>,
                      "registerListener<L>() requires L to inherit kc::Listener");

        auto instance = std::make_unique<ListenerType>();
        ListenerType* listener = instance.get();

        std::lock_guard<std::mutex> lock(mutex);
        const uint64_t classId = detail::typeId<ListenerType>();
        const uint64_t subscriptionId = nextSubscriptionId++;
        classes[classId].push_back(
            Registration{subscriptionId, classId, std::move(instance)});

        // Fold over the known event types; hook up whatever this class handles.
        detail::TypeListFor<EventRegistryTypes>::forEach([&](auto tag) {
            using EventType = typename decltype(tag)::type;
            if constexpr (detail::EventRegistrar::handles<ListenerType, EventType>()) {
                dispatcher.addHandler(EventHandler{
                    detail::typeId<EventType>(),
                    detail::EventRegistrar::priority<ListenerType, EventType>(),
                    subscriptionId,
                    listener,
                    [listener](Event& event) {
                        ListenerType::template kcOnEvent<ListenerType>(*listener, event,
                            (detail::TypeTag<EventType>*)nullptr);
                    }
                });
            }
        });

        return subscriptionId;
    }

    template<typename ListenerType>
    void EventManager::registerListener() {
        registerShared<ListenerType>();
    }

    template<typename ListenerType>
    EventSubscription EventManager::registerScopedListener() {
        return EventSubscription(this, registerShared<ListenerType>());
    }

    template<typename ListenerType>
    void EventManager::unregisterListener() {
        static_assert(std::is_base_of_v<Listener, ListenerType>,
                      "unregisterListener<L>() requires L to inherit kc::Listener");

        const uint64_t classId = detail::typeId<ListenerType>();
        std::lock_guard<std::mutex> lock(mutex);
        auto it = classes.find(classId);
        if (it == classes.end()) return;

        for (const auto& reg : it->second)
            dispatcher.removeSubscription(reg.subscriptionId);
        classes.erase(it);
    }

    template<typename EventType, typename... Args>
    void EventManager::callEvent(Args&&... args) {
        using CleanEvent = std::remove_cv_t<EventType>;
        static_assert(std::is_base_of_v<Event, CleanEvent>,
                      "callEvent<E>() requires E derived from kc::Event");

        CleanEvent event(std::forward<Args>(args)...);
        EventRegistry::get().template ensure<CleanEvent>();
        dispatcher.dispatch(event, detail::typeId<CleanEvent>());
    }
}
