#pragma once

#ifndef KCLINK_INTERNAL_COMPILATION
#error "KCLink internal header: not part of the public API"
#endif

#include "internal/Unix/UniqueFd.hpp"

#include <sys/types.h>

namespace kc::link::detail {

    UniqueFd openProcessHandle(pid_t process);

}
