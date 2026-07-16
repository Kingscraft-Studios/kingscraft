#include "Threads/InputThread.hpp"

#include "Bus/MessageBus.hpp"
#include "Util/LogUtils.hpp"

namespace lve {
    void InputThread::Init(WindowCreateInfo info) {
        window.emplace(info.width, info.height, info.name);
        mailbox = std::make_shared<Mailbox>();
        MessageBus::Get().subscribe(ThreadName::Input, mailbox);
        running = true;
    }

    void InputThread::run() {
        while (running) {
            Message msg;
            while (mailbox->try_pop(msg)) {
                if (msg.payload) {
                    try {
                        msg.payload();
                    } catch (std::runtime_error& e) {
                        LogUtils::error(ThreadName::Input, StringBuilder::build("Input Thread Exception: ", e.what()));
                    }
                }
            }
        }
    }

    void InputThread::Shutdown() {
        running = false;
        mailbox->stop();
    }
}
