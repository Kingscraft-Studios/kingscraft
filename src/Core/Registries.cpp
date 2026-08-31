#include "Core/Registries.hpp"

#include "Core/Blocks/Blocks.hpp"
#include "Bus/MessageBus.hpp"
#include "Bus/Mailbox.hpp"

namespace kc {
    std::atomic<bool> Registries::built_{false};
    std::mutex Registries::mutex_;
    std::condition_variable Registries::cv_;

    void Registries::build() {
        auto mailbox = std::make_shared<Mailbox>();
        MessageBus::Get().subscribe(ThreadName::Registration, mailbox);

        int pending = 0;
        Blocks::registerBlocks(pending);

        while (pending > 0) {
            Message msg;
            if (mailbox->pop_for(msg, std::chrono::milliseconds(100))) {
                if (msg.payload) msg.payload();
            }
        }

        MessageBus::Get().unsubscribe(ThreadName::Registration);

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