#include "Bus/MessageBus.hpp"

#include "Util/LogUtils.hpp"

namespace kc {

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
    if (!mailbox) {
        log(LogLevel::WARN, "Unable to send message to target thread. Target isn't subscribed yet!");
        return false;
    }
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

void MessageBus::log(LogLevel level, const std::string& line) {
    // FIXME: If log() is called BEFORE the Engine Subs to Mailbox the Log is Discarded!
    std::shared_lock lock(rwMutex_);
    auto it = subscribers_.find(ThreadName::Engine);
    if (it == subscribers_.end())
        return;
    auto mailbox = it->second.lock();

    // No need to Check if Mailbox Exists
    mailbox->push({ThreadName::Engine, [level, line]() {
        Logger::Get().log(level, ThreadName::MessageBus, line);
    }});
}
} // namespace kc
