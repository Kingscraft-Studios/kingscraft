#include <KCLink/Server.hpp>

#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <unistd.h>
#include <poll.h>

#include <algorithm>
#include <cerrno>
#include <cstring>
#include <limits>
#include <stdexcept>
#include <system_error>

namespace kc::link {
    namespace {
        void removeStaleSocket(const std::string& path) {
            struct stat original{};
            if (::lstat(path.c_str(), &original) < 0) {
                if (errno == ENOENT) return;
                throw std::system_error(errno, std::generic_category(), "lstat socket path");
            }
            if (!S_ISSOCK(original.st_mode))
                throw std::system_error(EEXIST, std::generic_category(),
                                        "socket path exists and is not a socket");

            const int probe = ::socket(AF_UNIX, SOCK_STREAM, 0);
            if (probe < 0) throw std::system_error(errno, std::generic_category(), "socket probe");

            sockaddr_un address{};
            address.sun_family = AF_UNIX;
            std::memcpy(address.sun_path, path.c_str(), path.size() + 1);
            const int result = ::connect(probe, reinterpret_cast<sockaddr*>(&address), sizeof(address));
            const int connectError = errno;
            ::close(probe);
            if (result == 0)
                throw std::system_error(EADDRINUSE, std::generic_category(),
                                        "socket path is already serving connections");
            if (connectError != ECONNREFUSED && connectError != ENOENT)
                throw std::system_error(connectError, std::generic_category(), "socket probe connect");

            struct stat current{};
            if (::lstat(path.c_str(), &current) < 0) {
                if (errno == ENOENT) return;
                throw std::system_error(errno, std::generic_category(), "lstat socket path");
            }
            if (current.st_dev != original.st_dev || current.st_ino != original.st_ino)
                throw std::system_error(EADDRINUSE, std::generic_category(),
                                        "socket path changed while checking stale socket");
            if (::unlink(path.c_str()) < 0 && errno != ENOENT)
                throw std::system_error(errno, std::generic_category(), "remove stale socket");
        }

        void unlinkIfOwned(const std::string& path, unsigned long long device,
                           unsigned long long inode) noexcept {
            struct stat current{};
            if (::lstat(path.c_str(), &current) == 0 &&
                static_cast<unsigned long long>(current.st_dev) == device &&
                static_cast<unsigned long long>(current.st_ino) == inode) {
                ::unlink(path.c_str());
            }
        }
    }

    Server::~Server() { stop(); }

    void Server::start(const std::string& socketPath) {
        if (isRunning()) throw std::logic_error("KCLink: server is already running");
        if (socketPath.empty() || socketPath.size() >= sizeof(sockaddr_un{}.sun_path))
            throw std::invalid_argument("KCLink: socket path is empty or too long");

        removeStaleSocket(socketPath);

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
        struct stat socketInfo{};
        if (::lstat(socketPath.c_str(), &socketInfo) < 0) {
            const int error = errno;
            ::close(descriptor);
            throw std::system_error(error, std::generic_category(), "lstat bound socket");
        }
        const auto device = static_cast<unsigned long long>(socketInfo.st_dev);
        const auto inode = static_cast<unsigned long long>(socketInfo.st_ino);
        if (::chmod(socketPath.c_str(), 0600) < 0) {
            const int error = errno;
            ::close(descriptor);
            unlinkIfOwned(socketPath, device, inode);
            throw std::system_error(error, std::generic_category(), "chmod socket path");
        }
        if (::listen(descriptor, 16) < 0) {
            const int error = errno;
            ::close(descriptor);
            unlinkIfOwned(socketPath, device, inode);
            throw std::system_error(error, std::generic_category(), "listen");
        }

        socket = descriptor;
        connectionPath = socketPath;
        pathDevice = device;
        pathInode = inode;
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

    std::optional<Connection> Server::acceptFor(std::chrono::milliseconds timeout) {
        if (!isRunning()) throw std::logic_error("KCLink: accept on a stopped server");

        const auto count = std::clamp<long long>(timeout.count(), 0, std::numeric_limits<int>::max());
        pollfd event{socket, POLLIN, 0};
        int ready;
        do {
            ready = ::poll(&event, 1, static_cast<int>(count));
        } while (ready < 0 && errno == EINTR);
        if (ready < 0) throw std::system_error(errno, std::generic_category(), "poll");
        if (ready == 0) return std::nullopt;

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
            unlinkIfOwned(connectionPath, pathDevice, pathInode);
            connectionPath.clear();
            pathDevice = 0;
            pathInode = 0;
        }
    }

}
