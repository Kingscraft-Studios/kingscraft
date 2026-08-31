#pragma once

#include <memory>
#include <atomic>

#include "Bus/Mailbox.hpp"

namespace kc {

class ResourceLoader {
public:
    void run();
    void stop();

private:
    std::shared_ptr<Mailbox> mailbox_;
    std::atomic<bool> running_{true};
};

} // namespace kc
