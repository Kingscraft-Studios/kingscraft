#pragma once

#include <KCLink/Connection.hpp>

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
        bool isRunning() const noexcept;
        void stop() noexcept;

    private:
        int socket = -1;
        std::string connectionPath;
    };

}
