#pragma once

#include <KCLink/Connection.hpp>

#include <optional>
#include <string>

namespace kc::link {

    class EventLoop;

    // A Unix-domain listening socket. Lifecycle (start/stop) is public; the
    // accept operation is driven by EventLoop.
    class Server {
    public:
        Server() noexcept = default;
        ~Server();

        Server(const Server&) = delete;
        Server& operator=(const Server&) = delete;

        void start(const std::string& socketPath);
        bool isRunning() const noexcept;
        void stop() noexcept;

    private:
        std::optional<Connection> tryAccept();
        int nativeHandle() const noexcept;

        int socket = -1;
        std::string connectionPath;
        unsigned long long pathDevice = 0;
        unsigned long long pathInode = 0;

        friend class EventLoop;
    };

}
