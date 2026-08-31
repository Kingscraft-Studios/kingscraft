#pragma once
#include <memory>
#include <shared_mutex>
#include <unordered_map>
#include <functional>
#include <condition_variable>
#include <chrono>
#include <stdexcept>
#include <type_traits>

#include "Bus/Message.hpp"
#include "Bus/Mailbox.hpp"

namespace kc {

class MessageBus {
public:
    static void Init();
    static void Shutdown();
    static MessageBus& Get();
    MessageBus() = default;

    void subscribe(ThreadName name, std::shared_ptr<Mailbox> mailbox);
    void unsubscribe(ThreadName name);

    bool send(ThreadName target, std::function<void()> payload);

    template<typename T>
    void request(ThreadName target, std::function<T()> work,
                 ThreadName replyTo, std::function<void(T)> callback)
    {
        auto payload = std::make_shared<std::function<void()>>(
            [work = std::move(work), replyTo, callback = std::move(callback)]() mutable {
                if constexpr (std::is_void_v<T>) {
                    work();
                    MessageBus::Get().send(replyTo, [cb = std::move(callback)]() {
                        cb();
                    });
                } else {
                    auto result = work();
                    MessageBus::Get().send(replyTo, [cb = std::move(callback), r = std::move(result)]() mutable {
                        cb(std::move(r));
                    });
                }
            }
        );
        constexpr auto kRetryDelay = std::chrono::microseconds(100);
        constexpr auto kTimeout = std::chrono::milliseconds(500);

        const auto start = std::chrono::steady_clock::now();
        while (std::chrono::steady_clock::now() - start < kTimeout) {
            if (send(target, [payload]() { (*payload)(); }))
                return;
            std::this_thread::sleep_for(kRetryDelay);
        }
        throw std::runtime_error(
            "MessageBus request timed out: target thread not responding");
    }

    void signalQuit();
    void waitForQuit();

private:

    bool send(Message msg);

    std::shared_mutex rwMutex_;
    std::unordered_map<ThreadName, std::weak_ptr<Mailbox>> subscribers_;

    std::mutex quitMutex_;
    std::condition_variable quitCV_;
    bool quitting_ = false;
};

}
