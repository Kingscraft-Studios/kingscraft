#include "Event/EventRegistry.hpp"

namespace kc {
    EventRegistry& EventRegistry::get() {
        static EventRegistry instance;
        return instance;
    }

    void EventRegistry::ensureId(uint64_t typeId) {
        std::lock_guard<std::mutex> lock(mutex);
        known.insert(typeId);
    }
}
