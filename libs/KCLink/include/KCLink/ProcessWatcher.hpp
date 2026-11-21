#pragma once

#include <KCLink/Export.hpp>

#include <memory>
#include <sys/types.h>

namespace kc::link {

    namespace detail {
        struct ProcessCore;
    }

    class EventLoop;

    // Watches a child process for exit. EventLoop calls the registered
    // handler when the process terminates.
    class KCLINK_API ProcessWatcher {
    public:
        explicit ProcessWatcher(pid_t process);
        ~ProcessWatcher();

        ProcessWatcher(const ProcessWatcher&) = delete;
        ProcessWatcher& operator=(const ProcessWatcher&) = delete;

        pid_t processId() const noexcept;

    private:
        detail::ProcessCore& core() noexcept;
        const detail::ProcessCore& core() const noexcept;

        std::unique_ptr<detail::ProcessCore> core_;

        friend class EventLoop;
    };

}
