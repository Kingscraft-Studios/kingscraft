#pragma once

#ifndef KCLINK_INTERNAL_COMPILATION
#error "KCLink internal header: not part of the public API"
#endif

#include "internal/Unix/UniqueFd.hpp"

#include <cstddef>
#include <optional>
#include <string>
#include <sys/types.h>

namespace kc::link::detail {

    struct SocketIdentity {
        std::string path;
        unsigned long long device = 0;
        unsigned long long inode = 0;
    };

    struct Listener {
        UniqueFd socket;
        SocketIdentity identity;
    };

    UniqueFd connectLocal(const std::string& socketPath);
    Listener listenLocal(const std::string& socketPath);
    std::optional<UniqueFd> acceptLocal(const UniqueFd& listener);
    pid_t peerProcessId(const UniqueFd& socket);
    void setNonBlocking(const UniqueFd& socket) noexcept;
    void writeExact(const UniqueFd& socket, const void* data, std::size_t size);
    void closeSocket(UniqueFd& socket) noexcept;
    void removeSocketPathIfOwned(const SocketIdentity& identity) noexcept;

}
