#pragma once

#include <atomic>
#include <condition_variable>
#include <future>
#include <mutex>

namespace kc {
    class Registries {
    public:
        static void build();
        static bool isBuilt() { return built_.load(); }
        static void waitForBuild();

        // Clears + rebuilds the block registry on a WorkerPool worker, firing
        // RegistryReloadPreEvent before the wipe and RegistryReloadEvent after
        // the rebuild (the point where mods re-register their entries). Returns
        // a future completed when the reload finishes. Concurrent reloads
        // serialize on buildMutex_.
        static std::shared_future<void> reload();

    private:
        static std::atomic<bool> built_;
        static std::mutex mutex_;
        static std::condition_variable cv_;
        static std::mutex buildMutex_;
    };
}