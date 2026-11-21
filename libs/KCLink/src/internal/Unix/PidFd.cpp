#include "internal/Unix/PidFd.hpp"

#include <fcntl.h>
#include <sys/syscall.h>
#include <unistd.h>

#include <cerrno>
#include <stdexcept>
#include <system_error>

namespace kc::link::detail {

    UniqueFd openProcessHandle(pid_t process) {
#ifndef SYS_pidfd_open
        throw std::runtime_error("KCLink: pidfd_open is not available on this system");
#else
        UniqueFd handle(static_cast<int>(::syscall(SYS_pidfd_open, process, 0)));
        if (!handle.valid()) throw std::system_error(errno, std::generic_category(), "pidfd_open");

        const int flags = ::fcntl(handle.get(), F_GETFD, 0);
        if (flags >= 0) ::fcntl(handle.get(), F_SETFD, flags | FD_CLOEXEC);
        return handle;
#endif
    }

}
