#pragma once

#ifndef KCLINK_INTERNAL_COMPILATION
#error "KCLink internal header: not part of the public API"
#endif

#include "internal/Unix/UniqueFd.hpp"
#include "internal/Unix/UnixSocket.hpp"

#include <optional>
#include <string>

namespace kc::link::detail {

    struct ServerCore {
        ServerCore() noexcept;
        ~ServerCore();

        void start(const std::string& socketPath);
        bool isRunning() const noexcept;
        void stop() noexcept;
        std::optional<UniqueFd> tryAcceptFd();
        int handle() const noexcept;

    private:
        UniqueFd socket;
        SocketIdentity identity;
    };

}
