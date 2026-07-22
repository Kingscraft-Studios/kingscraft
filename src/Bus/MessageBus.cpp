#include "Bus/MessageBus.hpp"

#include "Util/LogUtils.hpp"

namespace lve {

static std::unique_ptr<MessageBus> instance = nullptr;

void MessageBus::Init() {
    if (!instance)
        instance = std::make_unique<MessageBus>();
}

void MessageBus::Shutdown() {
    instance.reset();
}

MessageBus& MessageBus::Get() {
    return *instance;
}

void MessageBus::subscribe(ThreadName name, std::shared_ptr<Mailbox> mailbox) {
    std::unique_lock lock(rwMutex_);
    subscribers_[name] = std::move(mailbox);
}

void MessageBus::unsubscribe(ThreadName name) {
    std::unique_lock lock(rwMutex_);
    subscribers_.erase(name);
}

bool MessageBus::send(Message msg) {
    std::shared_lock lock(rwMutex_);
    auto it = subscribers_.find(msg.target);
    if (it == subscribers_.end())
        return false;
    auto mailbox = it->second.lock();
    if (!mailbox)
        return false;
    mailbox->push(std::move(msg));
    return true;
}

bool MessageBus::send(ThreadName target, std::function<void()> payload) {
    Message msg{
        .target = target,
        .payload = std::move(payload)
    };
    return send(std::move(msg));
}

void MessageBus::signalQuit() {
    {
        std::lock_guard lock(quitMutex_);
        quitting_ = true;
    }
    quitCV_.notify_one();
}

void MessageBus::waitForQuit() {
    std::unique_lock lock(quitMutex_);
    quitCV_.wait(lock, [&]() { return quitting_; });
}

} // namespace lve
