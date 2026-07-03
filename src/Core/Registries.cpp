#include "Core/Registries.hpp"

#include "Core/Blocks/Blocks.hpp"

namespace lve {
    std::atomic<bool> Registries::built_{false};
    std::mutex Registries::mutex_;
    std::condition_variable Registries::cv_;

    void Registries::build() {
        Blocks::registerBlocks();
        {
            std::lock_guard<std::mutex> lock(mutex_);
            built_.store(true);
        }
        cv_.notify_all();
    }

    void Registries::waitForBuild() {
        std::unique_lock<std::mutex> lock(mutex_);
        cv_.wait(lock, [] { return built_.load(); });
    }
}
