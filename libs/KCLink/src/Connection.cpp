#include <KCLink/Connection.hpp>

#include <fcntl.h>
#include <poll.h>
#include <sys/socket.h>
#include <unistd.h>

#include <algorithm>
#include <cerrno>
#include <stdexcept>
#include <system_error>
#include <utility>

namespace kc::link {
    namespace {
        constexpr std::size_t maxChunkSize = 16U * 1024U * 1024U;
        constexpr std::size_t headerSize = 13;

        constexpr std::uint8_t flagStart = 0x01;
        constexpr std::uint8_t flagEnd = 0x02;
        constexpr std::uint8_t flagDisconnect = 0x04;

        [[noreturn]] void throwSocketError(const char* operation) {
            throw std::system_error(errno, std::generic_category(), operation);
        }

        void writeU32LE(std::uint8_t* destination, std::uint32_t value) {
            destination[0] = static_cast<std::uint8_t>(value);
            destination[1] = static_cast<std::uint8_t>(value >> 8);
            destination[2] = static_cast<std::uint8_t>(value >> 16);
            destination[3] = static_cast<std::uint8_t>(value >> 24);
        }

        void writeU64LE(std::uint8_t* destination, std::uint64_t value) {
            for (std::size_t index = 0; index < 8; ++index)
                destination[index] = static_cast<std::uint8_t>(value >> (index * 8));
        }

        std::uint32_t readU32LE(const std::uint8_t* source) {
            return static_cast<std::uint32_t>(source[0])
                 | (static_cast<std::uint32_t>(source[1]) << 8)
                 | (static_cast<std::uint32_t>(source[2]) << 16)
                 | (static_cast<std::uint32_t>(source[3]) << 24);
        }

        std::uint64_t readU64LE(const std::uint8_t* source) {
            std::uint64_t value = 0;
            for (std::size_t index = 0; index < 8; ++index)
                value |= static_cast<std::uint64_t>(source[index]) << (index * 8);
            return value;
        }

        void writeExact(int socket, const void* source, std::size_t size) {
            const auto* bytes = static_cast<const unsigned char*>(source);
            std::size_t sent = 0;
            while (sent < size) {
                const auto count = ::send(socket, bytes + sent, size - sent, MSG_NOSIGNAL);
                if (count < 0) {
                    if (errno == EINTR) continue;
                    if (errno == EAGAIN || errno == EWOULDBLOCK) {
                        pollfd event{socket, POLLOUT, 0};
                        int ready;
                        do {
                            ready = ::poll(&event, 1, -1);
                        } while (ready < 0 && errno == EINTR);
                        if (ready < 0) throwSocketError("poll(POLLOUT)");
                        continue;
                    }
                    throwSocketError("send");
                }
                if (count == 0) throw std::runtime_error("KCLink: socket closed while sending");
                sent += static_cast<std::size_t>(count);
            }
        }
    }

    Connection::Connection(int descriptor) noexcept : socket(descriptor) {
        setNonBlocking();
    }

    Connection::~Connection() { close(); }

    Connection::Connection(Connection&& other) noexcept
        : socket(other.socket),
          receiveBuffer(std::move(other.receiveBuffer)),
          receivePayload(std::move(other.receivePayload)),
          receiveExpected(other.receiveExpected),
          receiving(other.receiving),
          peerEof(other.peerEof),
          sendExpected(other.sendExpected),
          sendWritten(other.sendWritten),
          sending(other.sending),
          maxLogicalMessageSize(other.maxLogicalMessageSize) {
        other.socket = -1;
        other.resetReceiveState();
    }

    Connection& Connection::operator=(Connection&& other) noexcept {
        if (this != &other) {
            close();
            socket = other.socket;
            receiveBuffer = std::move(other.receiveBuffer);
            receivePayload = std::move(other.receivePayload);
            receiveExpected = other.receiveExpected;
            receiving = other.receiving;
            peerEof = other.peerEof;
            sendExpected = other.sendExpected;
            sendWritten = other.sendWritten;
            sending = other.sending;
            maxLogicalMessageSize = other.maxLogicalMessageSize;
            other.socket = -1;
            other.resetReceiveState();
        }
        return *this;
    }

    void Connection::setNonBlocking() noexcept {
        if (socket < 0) return;
        const int flags = ::fcntl(socket, F_GETFL, 0);
        if (flags >= 0) ::fcntl(socket, F_SETFL, flags | O_NONBLOCK);
    }

    int Connection::nativeHandle() const noexcept { return socket; }

    bool Connection::isOpen() const noexcept { return socket >= 0; }

    pid_t Connection::peerProcessId() const {
        if (!isOpen()) throw std::logic_error("KCLink: peer query on a closed connection");

        ucred credentials{};
        socklen_t size = sizeof(credentials);
        if (::getsockopt(socket, SOL_SOCKET, SO_PEERCRED, &credentials, &size) < 0)
            throwSocketError("getsockopt(SO_PEERCRED)");
        return credentials.pid;
    }

    void Connection::setMaxMessageSize(std::uint64_t size) noexcept {
        maxLogicalMessageSize = size;
    }

    std::uint64_t Connection::maxMessageSize() const noexcept {
        return maxLogicalMessageSize;
    }

    void Connection::resetReceiveState() noexcept {
        receiveBuffer.clear();
        receivePayload.clear();
        receiveExpected = 0;
        receiving = false;
        peerEof = false;
    }

    void Connection::writeFrame(std::uint8_t flags, std::uint64_t totalLength,
                                const std::uint8_t* data, std::size_t size) {
        if (!isOpen()) throw std::logic_error("KCLink: send on a closed connection");
        if (size > maxChunkSize) throw std::length_error("KCLink: frame exceeds the chunk limit");

        std::uint8_t header[headerSize];
        writeU32LE(header, static_cast<std::uint32_t>(size));
        header[4] = flags;
        writeU64LE(header + 5, totalLength);
        writeExact(socket, header, sizeof(header));
        if (size != 0) writeExact(socket, data, size);
    }

    void Connection::send(const Message& message) {
        if (!isOpen()) throw std::logic_error("KCLink: send on a closed connection");
        if (sending) throw std::logic_error("KCLink: a chunked message is already in progress");

        const auto& payload = message.payload();
        const std::uint64_t total = payload.size();
        if (total > maxLogicalMessageSize)
            throw std::length_error("KCLink: message exceeds the configured size limit");

        if (total == 0) {
            writeFrame(flagStart | flagEnd, 0, nullptr, 0);
            return;
        }

        std::size_t offset = 0;
        while (offset < total) {
            const std::size_t chunk = std::min(maxChunkSize, static_cast<std::size_t>(total - offset));
            std::uint8_t flags = 0;
            if (offset == 0) flags |= flagStart;
            if (offset + chunk == total) flags |= flagEnd;
            writeFrame(flags, total, payload.data() + offset, chunk);
            offset += chunk;
        }
    }

    void Connection::beginMessage(std::uint64_t totalPayloadSize) {
        if (!isOpen()) throw std::logic_error("KCLink: send on a closed connection");
        if (sending) throw std::logic_error("KCLink: a chunked message is already in progress");
        if (totalPayloadSize > maxLogicalMessageSize)
            throw std::length_error("KCLink: message exceeds the configured size limit");

        writeFrame(flagStart, totalPayloadSize, nullptr, 0);
        sending = true;
        sendExpected = totalPayloadSize;
        sendWritten = 0;
    }

    void Connection::sendChunk(std::span<const std::uint8_t> chunk) {
        if (!sending) throw std::logic_error("KCLink: no chunked message in progress");
        if (chunk.empty()) return;
        if (sendWritten + chunk.size() > sendExpected)
            throw std::length_error("KCLink: chunked message exceeds its announced size");

        std::size_t offset = 0;
        while (offset < chunk.size()) {
            const std::size_t size = std::min(maxChunkSize, chunk.size() - offset);
            writeFrame(0, sendExpected, chunk.data() + offset, size);
            offset += size;
            sendWritten += size;
        }
    }

    void Connection::endMessage() {
        if (!sending) throw std::logic_error("KCLink: no chunked message in progress");
        if (sendWritten != sendExpected)
            throw std::length_error("KCLink: chunked message is shorter than announced");

        writeFrame(flagEnd, sendExpected, nullptr, 0);
        sending = false;
        sendExpected = 0;
        sendWritten = 0;
    }

    bool Connection::isSendingMessage() const noexcept { return sending; }

    void Connection::disconnect() {
        if (!isOpen()) return;
        if (sending) {
            // A graceful frame cannot be interleaved into a logical message.
            close();
            return;
        }
        try {
            writeFrame(flagDisconnect, 0, nullptr, 0);
        } catch (...) {
            // Best effort: the peer may already be gone (e.g. it was killed).
        }
        close();
    }

    ReceiveStatus Connection::tryReceive(Message& message) {
        if (!isOpen()) return ReceiveStatus::PeerLost;

        if (!peerEof) {
            std::uint8_t chunk[64 * 1024];
            while (true) {
                const auto count = ::recv(socket, chunk, sizeof(chunk), 0);
                if (count > 0) {
                    receiveBuffer.insert(receiveBuffer.end(), chunk, chunk + count);
                    continue;
                }
                if (count == 0) {
                    peerEof = true;
                    break;
                }
                if (errno == EINTR) continue;
                if (errno == EAGAIN || errno == EWOULDBLOCK) break;
                peerEof = true;
                break;
            }
        }

        // Parse before acting on EOF so that a buffered DISCONNECT frame is
        // still reported as a graceful close.
        while (receiveBuffer.size() >= headerSize) {
            const std::uint32_t chunkLength = readU32LE(receiveBuffer.data());
            const std::uint8_t flags = receiveBuffer[4];
            const std::uint64_t totalLength = readU64LE(receiveBuffer.data() + 5);

            if (chunkLength > maxChunkSize) {
                close();
                throw std::length_error("KCLink: received frame exceeds the chunk limit");
            }
            if (receiveBuffer.size() < headerSize + chunkLength) break;

            const std::uint8_t* framePayload = receiveBuffer.data() + headerSize;

            if (flags & flagDisconnect) {
                resetReceiveState();
                return ReceiveStatus::GracefulClose;
            }

            const bool start = (flags & flagStart) != 0;
            const bool end = (flags & flagEnd) != 0;

            if (start) {
                if (receiving) {
                    close();
                    throw std::runtime_error("KCLink: received START while a message is in progress");
                }
                if (totalLength > maxLogicalMessageSize) {
                    close();
                    throw std::length_error("KCLink: received message exceeds the size limit");
                }
                receiving = true;
                receiveExpected = totalLength;
                receivePayload.clear();
                receivePayload.reserve(static_cast<std::size_t>(totalLength));
            } else if (!receiving) {
                close();
                throw std::runtime_error("KCLink: received a frame without START");
            } else if (totalLength != receiveExpected) {
                close();
                throw std::runtime_error("KCLink: inconsistent total length across chunks");
            }

            receivePayload.insert(receivePayload.end(), framePayload, framePayload + chunkLength);
            receiveBuffer.erase(receiveBuffer.begin(),
                                receiveBuffer.begin() + static_cast<std::ptrdiff_t>(headerSize + chunkLength));

            if (end) {
                if (receivePayload.size() != receiveExpected) {
                    resetReceiveState();
                    throw std::runtime_error("KCLink: message ended before its announced size");
                }
                message = Message(std::move(receivePayload));
                resetReceiveState();
                return ReceiveStatus::Message;
            }
        }

        if (peerEof) {
            // Nothing complete left to deliver; any buffered bytes are a
            // truncated message and the peer is gone.
            resetReceiveState();
            return ReceiveStatus::PeerLost;
        }

        return ReceiveStatus::WouldBlock;
    }

    void Connection::close() noexcept {
        if (socket >= 0) {
            ::shutdown(socket, SHUT_RDWR);
            ::close(socket);
            socket = -1;
        }
        resetReceiveState();
        sending = false;
        sendExpected = 0;
        sendWritten = 0;
    }

}
