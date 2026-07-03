#pragma once

#include <atomic>
#include <mutex>
#include <condition_variable>

namespace lve {
    class Registries {
    public:
        static void build();
        static bool isBuilt() { return built_.load(); }
        static void waitForBuild();
    private:
        static std::atomic<bool> built_;
        static std::mutex mutex_;
        static std::condition_variable cv_;
    };
}
