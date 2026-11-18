#pragma once
#include <cstdint>
#include <future>
#include <memory>
#include <mutex>
#include <type_traits>
#include <unordered_map>
#include <utility>
#include <vector>

#include "Event/EventDispatcher.hpp"
#include "Event/EventRegistry.hpp"
#include "Event/EventSubscription.hpp"
#include "Event/Events/CancellableEvent.hpp"
#include "Event/Events/Event.hpp"
#include "Event/Listener.hpp"
#include "Event/detail/EventRegistrar.hpp"
#include "Event/detail/TypeId.hpp"

namespace kc {
    // Public coordinator: registerListener / unregisterListener / callEvent.
    // Synchronous callEvent dispatches on the caller's thread and awaits
    // cross-thread handlers; callEventAsync/callEventFuture post cross-thread
    // handlers and return without waiting. Thread-safe so events can be fired
    // from worker threads (e.g. registry reload) while other threads register
    // listeners.
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

        // Registers an existing, externally-owned listener instance. The
        // EventManager keeps a shared reference for the lifetime of the
        // subscription (and of any in-flight async handler), so the caller can
        // drop its own reference without tearing the listener down mid-dispatch.
        // The handle unregisters it when destroyed.
        template<typename ListenerType>
        EventSubscription registerScopedListener(std::shared_ptr<ListenerType> listener);

        // Removes every handler + instance belonging to ListenerType.
        template<typename ListenerType>
        void unregisterListener();

        // Fires an event on the caller's thread. The event is constructed here
        // from the given ctor args, so firing sites don't have to instantiate +
        // repeat the event object (e.g. callEvent<RegistryReloadEvent>()).
        //
        // Synchronous: same-thread handlers run inline and cross-thread handlers
        // are awaited, so the effects are visible when callEvent returns.
        //
        // TODO(cancellable): decide how a cancelled CancellableEvent
        // short-circuits — stop invoking remaining handlers in the dispatcher
        // and/or abort the firing call site so it doesn't have to check
        // isCancelled() after firing.
        template<typename EventType, typename... Args>
        void callEvent(Args&&... args);

        // Fire-and-forget variant: same-thread handlers still run inline before
        // returning, but handlers bound to other threads are posted to their
        // mailbox and not awaited. Cancellable events are rejected (there is no
        // result to observe).
        template<typename EventType, typename... Args>
        void callEventAsync(Args&&... args);

        // Fire-and-forget variant that returns a completion handle. The future
        // is fulfilled once every remote thread group has finished; obtaining
        // it never blocks, and callers wait on it only if they need to.
        template<typename EventType, typename... Args>
        std::shared_future<void> callEventFuture(Args&&... args);

        EventDispatcher& getDispatcher() { return dispatcher; }
        EventRegistry& getRegistry() { return EventRegistry::get(); }

        void unregisterSubscription(uint64_t subscriptionId);

    private:
        EventManager() = default;

        // Shared registration core for an already-created listener instance;
        // returns the subscription id the handlers were registered under.
        template<typename ListenerType>
        uint64_t registerSharedFor(const std::shared_ptr<ListenerType>& listener);

        // Creates + owns the listener instance, then registers it.
        template<typename ListenerType>
        uint64_t registerShared();

        struct Registration {
            uint64_t subscriptionId = 0;
            uint64_t classId = 0;
            std::shared_ptr<Listener> instance;
        };

        std::mutex mutex;
        uint64_t nextSubscriptionId = 1;
        std::unordered_map<uint64_t, std::vector<Registration>> classes;
        EventDispatcher dispatcher;
    };

    template<typename ListenerType>
    uint64_t EventManager::registerSharedFor(const std::shared_ptr<ListenerType>& listener) {
        static_assert(std::is_base_of_v<Listener, ListenerType>,
                      "registerListener<L>() requires L to inherit kc::Listener");

        std::lock_guard<std::mutex> lock(mutex);
        const uint64_t classId = detail::typeId<ListenerType>();
        const uint64_t subscriptionId = nextSubscriptionId++;
        classes[classId].push_back(
            Registration{subscriptionId, classId, listener});

        // Shared base handle: the handler records (and the posted async lambda)
        // keep the listener alive even after it is unregistered.
        std::shared_ptr<Listener> base = listener;

        // Fold over the known event types; hook up whatever this class handles.
        detail::TypeListFor<EventRegistryTypes>::forEach([&](auto tag) {
            using EventType = typename decltype(tag)::type;
            if constexpr (detail::EventRegistrar::handles<ListenerType, EventType>()) {
                dispatcher.addHandler(EventHandler{
                    detail::typeId<EventType>(),
                    detail::EventRegistrar::priority<ListenerType, EventType>(),
                    subscriptionId,
                    base,
                    [base](Event& event) {
                        ListenerType::template kcOnEvent<ListenerType>(
                            *static_cast<ListenerType*>(base.get()), event,
                            (detail::TypeTag<EventType>*)nullptr);
                    },
                    detail::EventRegistrar::thread<ListenerType, EventType>()
                });
            }
        });

        return subscriptionId;
    }

    template<typename ListenerType>
    uint64_t EventManager::registerShared() {
        return registerSharedFor<ListenerType>(std::make_shared<ListenerType>());
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
    EventSubscription EventManager::registerScopedListener(std::shared_ptr<ListenerType> listener) {
        return EventSubscription(this, registerSharedFor<ListenerType>(listener));
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

        auto event = std::make_shared<CleanEvent>(std::forward<Args>(args)...);
        EventRegistry::get().template ensure<CleanEvent>();
        dispatcher.dispatch(event, detail::typeId<CleanEvent>());
    }

    template<typename EventType, typename... Args>
    void EventManager::callEventAsync(Args&&... args) {
        using CleanEvent = std::remove_cv_t<EventType>;
        static_assert(std::is_base_of_v<Event, CleanEvent>,
                      "callEventAsync<E>() requires E derived from kc::Event");
        static_assert(!std::is_base_of_v<CancellableEvent, CleanEvent>,
                      "callEventAsync cannot observe cancellation; use callEvent<E>() instead");

        auto event = std::make_shared<CleanEvent>(std::forward<Args>(args)...);
        EventRegistry::get().template ensure<CleanEvent>();
        dispatcher.dispatchAsync(event, detail::typeId<CleanEvent>());
    }

    template<typename EventType, typename... Args>
    std::shared_future<void> EventManager::callEventFuture(Args&&... args) {
        using CleanEvent = std::remove_cv_t<EventType>;
        static_assert(std::is_base_of_v<Event, CleanEvent>,
                      "callEventFuture<E>() requires E derived from kc::Event");
        static_assert(!std::is_base_of_v<CancellableEvent, CleanEvent>,
                      "callEventFuture cannot be used with a CancellableEvent; use callEvent instead.");

        auto event = std::make_shared<CleanEvent>(std::forward<Args>(args)...);
        EventRegistry::get().template ensure<CleanEvent>();

        auto done = std::make_shared<std::promise<void>>();
        std::shared_future<void> future = done->get_future();
        dispatcher.dispatchAsync(event, detail::typeId<CleanEvent>(), std::move(done));
        return future;
    }
}
