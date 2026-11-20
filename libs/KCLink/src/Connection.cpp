#include <KCLink/Connection.hpp>

#include <arpa/inet.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <unistd.h>

#include <cerrno>
#include <stdexcept>
#include <system_error>
#include <utility>

namespace kc::link {
    namespace {
        constexpr std::uint32_t maxMessageSize = 16U * 1024U * 1024U;

        [[noreturn]] void throwSocketError(const char* operation) {
            throw std::system_error(errno, std::generic_category(), operation);
        }

        bool readExact(int socket, void* destination, std::size_t size,
                       bool allowCleanEof = false) {
            auto* bytes = static_cast<unsigned char*>(destination);
            std::size_t received = 0;
            while (received < size) {
                const auto count = ::recv(socket, bytes + received, size - received, 0);
                if (count == 0) {
                    if (allowCleanEof && received == 0) return false;
                    throw std::runtime_error("KCLink: peer disconnected during a message");
                }
                if (count < 0) {
                    if (errno == EINTR) continue;
                    throwSocketError("recv");
                }
                received += static_cast<std::size_t>(count);
            }
            return true;
        }

        void writeExact(int socket, const void* source, std::size_t size) {
            const auto* bytes = static_cast<const unsigned char*>(source);
            std::size_t sent = 0;
            while (sent < size) {
                const auto count = ::send(socket, bytes + sent, size - sent, MSG_NOSIGNAL);
                if (count < 0) {
                    if (errno == EINTR) continue;
                    throwSocketError("send");
                }
                if (count == 0) throw std::runtime_error("KCLink: socket closed while sending");
                sent += static_cast<std::size_t>(count);
            }
        }
    }

    Connection::Connection(int descriptor) noexcept : socket(descriptor) {}
    Connection::~Connection() { close(); }

    Connection::Connection(Connection&& other) noexcept : socket(other.socket) {
        other.socket = -1;
    }

    Connection& Connection::operator=(Connection&& other) noexcept {
        if (this != &other) {
            close();
            socket = other.socket;
            other.socket = -1;
        }
        return *this;
    }

    bool Connection::isOpen() const noexcept { return socket >= 0; }

    pid_t Connection::peerProcessId() const {
        if (!isOpen()) throw std::logic_error("KCLink: peer query on a closed connection");

        ucred credentials{};
        socklen_t size = sizeof(credentials);
        if (::getsockopt(socket, SOL_SOCKET, SO_PEERCRED, &credentials, &size) < 0)
            throwSocketError("getsockopt(SO_PEERCRED)");
        return credentials.pid;
    }

    void Connection::setReceiveTimeout(std::chrono::milliseconds timeout) {
        if (!isOpen()) throw std::logic_error("KCLink: timeout set on a closed connection");
        if (timeout.count() < 0) throw std::invalid_argument("KCLink: receive timeout cannot be negative");

        timeval value{};
        value.tv_sec = static_cast<time_t>(timeout.count() / 1000);
        value.tv_usec = static_cast<suseconds_t>((timeout.count() % 1000) * 1000);
        if (::setsockopt(socket, SOL_SOCKET, SO_RCVTIMEO, &value, sizeof(value)) < 0)
            throwSocketError("setsockopt(SO_RCVTIMEO)");
    }

    void Connection::send(const Message& message) {
        if (!isOpen()) throw std::logic_error("KCLink: send on a closed connection");
        const auto& payload = message.payload();
        if (payload.size() > maxMessageSize)
            throw std::length_error("KCLink: message exceeds the 16 MiB limit");

        const auto size = htonl(static_cast<std::uint32_t>(payload.size()));
        writeExact(socket, &size, sizeof(size));
        if (!payload.empty()) writeExact(socket, payload.data(), payload.size());
    }

    bool Connection::receive(Message& message) {
        if (!isOpen()) throw std::logic_error("KCLink: receive on a closed connection");

        std::uint32_t networkSize = 0;
        if (!readExact(socket, &networkSize, sizeof(networkSize), true)) return false;
        const auto size = ntohl(networkSize);
        if (size > maxMessageSize) throw std::length_error("KCLink: received message exceeds the 16 MiB limit");

        std::vector<std::uint8_t> payload(size);
        if (size != 0) readExact(socket, payload.data(), payload.size());
        message = Message(std::move(payload));
        return true;
    }

    void Connection::close() noexcept {
        if (socket >= 0) {
            ::shutdown(socket, SHUT_RDWR);
            ::close(socket);
            socket = -1;
        }
    }

}
