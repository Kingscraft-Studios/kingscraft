#pragma once

#include <atomic>

#include "Event/Events/ChunkLoadedEvent.hpp"
#include "Event/Events/ChunkUnloadedEvent.hpp"
#include "Event/KCEventHandler.hpp"
#include "Event/Listener.hpp"
#include "Event/detail/EventRegistrar.hpp"

namespace kc {

    // Observes the chunk pipeline: keeps a live loaded-chunk count and logs
    // unloads. Future consumers (minimap, lazy lighting, save indicators) can
    // subscribe to the same events at their own priority.
    class ChunkLifecycleListener : public Listener {
    public:
        ChunkLifecycleListener() = default;

        static int32_t getLoadedCount() { return loadedCount_.load(); }

        // Must be public: EventManager's generated dispatch lambda calls the
        // kcOnEvent helper from outside the class.
        KC_EVENT_HANDLER(ChunkLoadedEvent, onChunkLoaded, ThreadName::GameLogic)
        KC_EVENT_HANDLER(ChunkUnloadedEvent, onChunkUnloaded, ThreadName::GameLogic)

    private:
        void onChunkLoaded(ChunkLoadedEvent& event);
        void onChunkUnloaded(ChunkUnloadedEvent& event);

        static inline std::atomic<int32_t> loadedCount_{0};
    };

}