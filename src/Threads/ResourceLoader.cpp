#include "Threads/ResourceLoader.hpp"
#include "Bus/MessageBus.hpp"
#include "Bus/Message.hpp"
#include "Threads/Logger.hpp"

#include <chrono>
#include <exception>

#include "Util/LogUtils.hpp"

namespace kc {

void ResourceLoader::run() {
    mailbox_ = std::make_shared<Mailbox>();
    MessageBus::Get().subscribe(ThreadName::Resource, mailbox_);

    while (running_) {
        Message msg;
        while (mailbox_->pop_for(msg, std::chrono::milliseconds(100))) {
            if (msg.payload) {
                try {
                    msg.payload();
                } catch (const std::exception& e) {
                    LogUtils::error(ThreadName::Resource, StringBuilder::build("Resource thread exception: ", e.what()));
                } catch (...) {
                    LogUtils::error(ThreadName::Resource, "Resource thread caught unknown exception");
                }
            }
        }
    }

    MessageBus::Get().unsubscribe(ThreadName::Resource);
}

void ResourceLoader::stop() {
    running_ = false;
    mailbox_->stop();
}

} // namespace kc
