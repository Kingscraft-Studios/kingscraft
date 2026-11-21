#include <KCLink/ProcessWatcher.hpp>

#include <fcntl.h>
#include <sys/syscall.h>
#include <unistd.h>

#include <stdexcept>
#include <system_error>

namespace kc::link {

    ProcessWatcher::ProcessWatcher(pid_t process) : process(process) {
#ifndef SYS_pidfd_open
        throw std::runtime_error("KCLink: pidfd_open is not available on this system");
#else
        handle = static_cast<int>(::syscall(SYS_pidfd_open, process, 0));
        if (handle < 0)
            throw std::system_error(errno, std::generic_category(), "pidfd_open");
        const int flags = ::fcntl(handle, F_GETFD, 0);
        if (flags >= 0) ::fcntl(handle, F_SETFD, flags | FD_CLOEXEC);
#endif
    }

    ProcessWatcher::~ProcessWatcher() {
        if (handle >= 0) ::close(handle);
    }

    pid_t ProcessWatcher::processId() const noexcept { return process; }

    int ProcessWatcher::nativeHandle() const noexcept { return handle; }

}
