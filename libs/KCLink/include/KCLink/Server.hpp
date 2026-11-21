#pragma once

#include <KCLink/Export.hpp>

#include <cstdint>
#include <memory>
#include <string>

namespace kc::link {

    namespace detail {
        struct ServerCore;
    }

    class EventLoop;

    // A listening endpoint. Lifecycle is public; accepting and dispatching
    // connections is driven by EventLoop.
    class KCLINK_API Server {
    public:
        Server() noexcept;
        ~Server();

        Server(const Server&) = delete;
        Server& operator=(const Server&) = delete;

        void start(const std::string& socketPath);
        bool isRunning() const noexcept;
        void stop() noexcept;

    private:
        detail::ServerCore& core() noexcept;
        const detail::ServerCore& core() const noexcept;

        std::unique_ptr<detail::ServerCore> core_;

        friend class EventLoop;
    };

}
