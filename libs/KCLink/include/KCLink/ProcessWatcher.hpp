#pragma once

#include <sys/types.h>

namespace kc::link {

    class EventLoop;

    // Watches a child process for exit without exposing any OS handle.
    // EventLoop calls the registered handler when the process terminates.
    class ProcessWatcher {
    public:
        explicit ProcessWatcher(pid_t process);
        ~ProcessWatcher();

        ProcessWatcher(const ProcessWatcher&) = delete;
        ProcessWatcher& operator=(const ProcessWatcher&) = delete;

        pid_t processId() const noexcept;

    private:
        int nativeHandle() const noexcept;

        pid_t process = -1;
        int handle = -1;

        friend class EventLoop;
    };

}
