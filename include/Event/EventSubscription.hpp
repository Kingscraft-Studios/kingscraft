#pragma once

#include <cstdint>

namespace kc {

    class EventManager;

    // RAII handle for a listener registration group. Destructing it
    // unregisters every handler that registration created.
    class EventSubscription {
    public:
        EventSubscription() = default;
        EventSubscription(EventManager* manager, uint64_t subscriptionId)
            : manager(manager), subscriptionId(subscriptionId) {}

        ~EventSubscription();

        EventSubscription(const EventSubscription&) = delete;
        EventSubscription& operator=(const EventSubscription&) = delete;

        EventSubscription(EventSubscription&& other) noexcept
            : manager(other.manager), subscriptionId(other.subscriptionId) {
            other.manager = nullptr;
            other.subscriptionId = 0;
        }

        EventSubscription& operator =(EventSubscription&& other) noexcept {
            unsubscribe();
            manager = other.manager;
            subscriptionId = other.subscriptionId;
            other.manager = nullptr;
            other.subscriptionId = 0;
            return *this;
        }

        bool valid() const { return manager != nullptr; }
        void unsubscribe();

    private:
        EventManager* manager = nullptr;
        uint64_t subscriptionId = 0;
    };

}