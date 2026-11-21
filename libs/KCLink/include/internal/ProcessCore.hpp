#pragma once

#ifndef KCLINK_INTERNAL_COMPILATION
#error "KCLink internal header: not part of the public API"
#endif

#include "internal/Unix/UniqueFd.hpp"

#include <sys/types.h>

namespace kc::link::detail {

    struct ProcessCore {
        explicit ProcessCore(pid_t process);

        pid_t processId() const noexcept;
        int handle() const noexcept;

    private:
        pid_t process = -1;
        UniqueFd handle_;
    };

}
