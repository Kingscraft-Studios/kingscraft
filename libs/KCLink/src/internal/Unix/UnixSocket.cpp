#include "internal/Unix/UnixSocket.hpp"

#include <fcntl.h>
#include <poll.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <unistd.h>

#include <cerrno>
#include <cstring>
#include <stdexcept>
#include <system_error>
#include <utility>

namespace kc::link::detail {
    namespace {

        [[noreturn]] void throwSystemError(const char* operation) {
            throw std::system_error(errno, std::generic_category(), operation);
        }

        void removeStaleSocket(const std::string& path) {
            struct stat original{};
            if (::lstat(path.c_str(), &original) < 0) {
                if (errno == ENOENT) return;
                throwSystemError("lstat socket path");
            }
            if (!S_ISSOCK(original.st_mode))
                throw std::system_error(EEXIST, std::generic_category(),
                                        "socket path exists and is not a socket");

            UniqueFd probe(::socket(AF_UNIX, SOCK_STREAM, 0));
            if (!probe.valid()) throwSystemError("socket probe");

            sockaddr_un address{};
            address.sun_family = AF_UNIX;
            std::memcpy(address.sun_path, path.c_str(), path.size() + 1);
            const int result = ::connect(probe.get(), reinterpret_cast<sockaddr*>(&address),
                                         sizeof(address));
            const int connectError = errno;
            if (result == 0)
                throw std::system_error(EADDRINUSE, std::generic_category(),
                                        "socket path is already serving connections");
            if (connectError != ECONNREFUSED && connectError != ENOENT)
                throw std::system_error(connectError, std::generic_category(),
                                        "socket probe connect");

            struct stat current{};
            if (::lstat(path.c_str(), &current) < 0) {
                if (errno == ENOENT) return;
                throwSystemError("lstat socket path");
            }
            if (current.st_dev != original.st_dev || current.st_ino != original.st_ino)
                throw std::system_error(EADDRINUSE, std::generic_category(),
                                        "socket path changed while checking stale socket");
            if (::unlink(path.c_str()) < 0 && errno != ENOENT)
                throwSystemError("remove stale socket");
        }

        void validatePath(const std::string& path) {
            if (path.empty() || path.size() >= sizeof(sockaddr_un{}.sun_path))
                throw std::invalid_argument("KCLink: socket path is empty or too long");
        }

    }

    UniqueFd connectLocal(const std::string& socketPath) {
        validatePath(socketPath);

        UniqueFd socket(::socket(AF_UNIX, SOCK_STREAM, 0));
        if (!socket.valid()) throwSystemError("socket");

        sockaddr_un address{};
        address.sun_family = AF_UNIX;
        std::memcpy(address.sun_path, socketPath.c_str(), socketPath.size() + 1);
        if (::connect(socket.get(), reinterpret_cast<sockaddr*>(&address), sizeof(address)) < 0)
            throwSystemError("connect");

        setNonBlocking(socket);
        return socket;
    }

    Listener listenLocal(const std::string& socketPath) {
        validatePath(socketPath);
        removeStaleSocket(socketPath);

        UniqueFd socket(::socket(AF_UNIX, SOCK_STREAM, 0));
        if (!socket.valid()) throwSystemError("socket");

        sockaddr_un address{};
        address.sun_family = AF_UNIX;
        std::memcpy(address.sun_path, socketPath.c_str(), socketPath.size() + 1);
        if (::bind(socket.get(), reinterpret_cast<sockaddr*>(&address), sizeof(address)) < 0)
            throwSystemError("bind");

        struct stat info{};
        if (::lstat(socketPath.c_str(), &info) < 0) throwSystemError("lstat bound socket");

        SocketIdentity identity{
            socketPath,
            static_cast<unsigned long long>(info.st_dev),
            static_cast<unsigned long long>(info.st_ino)
        };

        if (::chmod(socketPath.c_str(), 0600) < 0) {
            const int error = errno;
            removeSocketPathIfOwned(identity);
            throw std::system_error(error, std::generic_category(), "chmod socket path");
        }

        if (::listen(socket.get(), 16) < 0) {
            const int error = errno;
            removeSocketPathIfOwned(identity);
            throw std::system_error(error, std::generic_category(), "listen");
        }

        setNonBlocking(socket);
        return Listener{std::move(socket), std::move(identity)};
    }

    std::optional<UniqueFd> acceptLocal(const UniqueFd& listener) {
        if (!listener.valid()) return std::nullopt;

        int descriptor;
        do {
            descriptor = ::accept(listener.get(), nullptr, nullptr);
        } while (descriptor < 0 && errno == EINTR);

        if (descriptor < 0) {
            if (errno == EAGAIN || errno == EWOULDBLOCK || errno == ECONNABORTED)
                return std::nullopt;
            throwSystemError("accept");
        }

        UniqueFd socket(descriptor);
        setNonBlocking(socket);
        return socket;
    }

    pid_t peerProcessId(const UniqueFd& socket) {
        if (!socket.valid()) throw std::logic_error("KCLink: peer query on a closed connection");

        ucred credentials{};
        socklen_t size = sizeof(credentials);
        if (::getsockopt(socket.get(), SOL_SOCKET, SO_PEERCRED, &credentials, &size) < 0)
            throwSystemError("getsockopt(SO_PEERCRED)");
        return credentials.pid;
    }

    void setNonBlocking(const UniqueFd& socket) noexcept {
        if (!socket.valid()) return;
        const int flags = ::fcntl(socket.get(), F_GETFL, 0);
        if (flags >= 0) ::fcntl(socket.get(), F_SETFL, flags | O_NONBLOCK);
    }

    void writeExact(const UniqueFd& socket, const void* data, std::size_t size) {
        if (!socket.valid()) throw std::logic_error("KCLink: send on a closed connection");

        const auto* bytes = static_cast<const unsigned char*>(data);
        std::size_t written = 0;
        while (written < size) {
            const auto count = ::send(socket.get(), bytes + written, size - written, MSG_NOSIGNAL);
            if (count < 0) {
                if (errno == EINTR) continue;
                if (errno == EAGAIN || errno == EWOULDBLOCK) {
                    pollfd event{socket.get(), POLLOUT, 0};
                    int ready;
                    do {
                        ready = ::poll(&event, 1, -1);
                    } while (ready < 0 && errno == EINTR);
                    if (ready < 0) throwSystemError("poll(POLLOUT)");
                    continue;
                }
                throwSystemError("send");
            }
            if (count == 0) throw std::runtime_error("KCLink: socket closed while sending");
            written += static_cast<std::size_t>(count);
        }
    }

    void closeSocket(UniqueFd& socket) noexcept {
        if (socket.valid()) {
            ::shutdown(socket.get(), SHUT_RDWR);
            socket.reset();
        }
    }

    void removeSocketPathIfOwned(const SocketIdentity& identity) noexcept {
        if (identity.path.empty()) return;

        struct stat current{};
        if (::lstat(identity.path.c_str(), &current) == 0 &&
            static_cast<unsigned long long>(current.st_dev) == identity.device &&
            static_cast<unsigned long long>(current.st_ino) == identity.inode) {
            ::unlink(identity.path.c_str());
        }
    }

}
