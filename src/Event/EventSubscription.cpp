#include "Event/EventSubscription.hpp"
#include "Event/EventManager.hpp"

namespace kc {
    EventSubscription::~EventSubscription() {
        unsubscribe();
    }

    void EventSubscription::unsubscribe() {
        if (manager) {
            manager->unregisterSubscription(subscriptionId);
            manager = nullptr;
            subscriptionId = 0;
        }
    }
}
