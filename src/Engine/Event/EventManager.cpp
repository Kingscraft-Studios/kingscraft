#include "Event/EventManager.hpp"

#include <iterator>

namespace kc {

    void EventManager::unregisterSubscription(uint64_t subscriptionId) {
        std::lock_guard<std::mutex> lock(mutex);
        for (auto it = classes.begin(); it != classes.end();) {
            auto& regs = it->second;
            for (auto r = regs.begin(); r != regs.end();) {
                if (r->subscriptionId == subscriptionId) {
                    dispatcher.removeSubscription(subscriptionId);
                    r = regs.erase(r);
                } else {
                    ++r;
                }
            }
            it = (regs.empty()) ? classes.erase(it) : std::next(it);
        }
    }

} // namespace kc