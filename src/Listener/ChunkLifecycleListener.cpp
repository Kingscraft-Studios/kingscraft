#include "Listener/ChunkLifecycleListener.hpp"

#include <cstdint>

namespace kc {

    void ChunkLifecycleListener::onChunkLoaded(ChunkLoadedEvent& event) {
        loadedCount_.fetch_add(1);
        (void)event;
    }

    void ChunkLifecycleListener::onChunkUnloaded(ChunkUnloadedEvent& event) {
        loadedCount_.fetch_sub(1);
        (void)event;
    }

}