#include <KCLink/Server.hpp>

#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>

#include <cerrno>
#include <cstring>
#include <stdexcept>
#include <system_error>

namespace kc::link {

    Server::~Server() { stop(); }

    void Server::start(const std::string& socketPath) {
        if (isRunning()) throw std::logic_error("KCLink: server is already running");
        if (socketPath.empty() || socketPath.size() >= sizeof(sockaddr_un{}.sun_path))
            throw std::invalid_argument("KCLink: socket path is empty or too long");

        const int descriptor = ::socket(AF_UNIX, SOCK_STREAM, 0);
        if (descriptor < 0) throw std::system_error(errno, std::generic_category(), "socket");

        sockaddr_un address{};
        address.sun_family = AF_UNIX;
        std::memcpy(address.sun_path, socketPath.c_str(), socketPath.size() + 1);
        if (::bind(descriptor, reinterpret_cast<sockaddr*>(&address), sizeof(address)) < 0) {
            const int error = errno;
            ::close(descriptor);
            throw std::system_error(error, std::generic_category(), "bind");
        }
        if (::listen(descriptor, 16) < 0) {
            const int error = errno;
            ::close(descriptor);
            ::unlink(socketPath.c_str());
            throw std::system_error(error, std::generic_category(), "listen");
        }

        socket = descriptor;
        connectionPath = socketPath;
    }

    Connection Server::accept() {
        if (!isRunning()) throw std::logic_error("KCLink: accept on a stopped server");

        int descriptor;
        do {
            descriptor = ::accept(socket, nullptr, nullptr);
        } while (descriptor < 0 && errno == EINTR);

        if (descriptor < 0) throw std::system_error(errno, std::generic_category(), "accept");
        return Connection(descriptor);
    }

    bool Server::isRunning() const noexcept { return socket >= 0; }

    void Server::stop() noexcept {
        if (socket >= 0) {
            ::shutdown(socket, SHUT_RDWR);
            ::close(socket);
            socket = -1;
        }
        if (!connectionPath.empty()) {
            ::unlink(connectionPath.c_str());
            connectionPath.clear();
        }
    }

}
