#pragma once

#include <KCLink/Connection.hpp>

#include <chrono>
#include <optional>
#include <string>

namespace kc::link {

    class Server {
    public:
        Server() noexcept = default;
        ~Server();

        Server(const Server&) = delete;
        Server& operator=(const Server&) = delete;

        void start(const std::string& socketPath);
        Connection accept();
        std::optional<Connection> acceptFor(std::chrono::milliseconds timeout);
        bool isRunning() const noexcept;
        void stop() noexcept;

    private:
        int socket = -1;
        std::string connectionPath;
        unsigned long long pathDevice = 0;
        unsigned long long pathInode = 0;
    };

}
