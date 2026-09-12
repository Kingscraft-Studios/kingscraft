#include "Core/Registries.hpp"

#include "Bus/MessageBus.hpp"
#include "Bus/Mailbox.hpp"
#include "Core/Blocks/Blocks.hpp"
#include "Core/Registry.hpp"
#include "Core/WorkerPool.hpp"
#include "Event/EventManager.hpp"
#include "Util/LogUtils.hpp"

#include <chrono>
#include <stdexcept>

namespace kc {
    std::atomic<bool> Registries::built_{false};
    std::mutex Registries::mutex_;
    std::condition_variable Registries::cv_;
    std::mutex Registries::buildMutex_;

    void Registries::build() {
        auto mailbox = std::make_shared<Mailbox>();
        MessageBus::Get().subscribe(ThreadName::Registry, mailbox);

        int pending = 0;
        Blocks::registerBlocks(pending);

        while (pending > 0) {
            Message msg;
            if (mailbox->pop_for(msg, std::chrono::milliseconds(100))) {
                if (msg.payload) msg.payload();
            }
        }

        MessageBus::Get().unsubscribe(ThreadName::Registry);

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

    std::shared_future<void> Registries::reload() {
        return WorkerPool::get().startWorker([]() {
            // Serializes overlapping reloads: build() re-subscribes the
            // ThreadName::Registry mailbox key, so two wholly concurrent
            // reloads must never interleave.
            std::lock_guard<std::mutex> lock(buildMutex_);

            const auto started = std::chrono::steady_clock::now();
            LogUtils::info(ThreadName::Registry, "Registry reload starting...");

            try {
                EventManager::get().callEvent<RegistryReloadPreEvent>();

                Registry<Block>::getRegistry().clear();
                build();

                EventManager::get().callEvent<RegistryReloadEvent>();

                const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
                    std::chrono::steady_clock::now() - started).count();
                LogUtils::info(ThreadName::Registry,
                    StringBuilder::build("Registry reload completed in ", elapsed, "ms."));
            } catch (const std::runtime_error& e) {
                LogUtils::error(ThreadName::Registry,
                    StringBuilder::build("Registry reload failed: ", e.what()));
            } catch (...) {
                LogUtils::error(ThreadName::Registry, "Registry reload failed with an unknown exception.");
            }
        });
    }
} // namespace kc