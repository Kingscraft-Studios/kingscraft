#include "internal/ConnectionCore.hpp"

#include "internal/Unix/Framing.hpp"
#include "internal/Unix/UnixSocket.hpp"

#include <sys/socket.h>
#include <unistd.h>

#include <algorithm>
#include <cerrno>
#include <stdexcept>
#include <utility>

namespace kc::link::detail {

    ConnectionCore::ConnectionCore(UniqueFd descriptor) noexcept : socket(std::move(descriptor)) {
        setNonBlocking(socket);
    }

    int ConnectionCore::handle() const noexcept { return socket.get(); }

    bool ConnectionCore::isOpen() const noexcept { return socket.valid(); }

    pid_t ConnectionCore::peerProcessId() const { return kc::link::detail::peerProcessId(socket); }

    void ConnectionCore::setMaxMessageSize(std::uint64_t size) noexcept {
        maxLogicalMessageSize = size;
    }

    std::uint64_t ConnectionCore::maxMessageSize() const noexcept {
        return maxLogicalMessageSize;
    }

    void ConnectionCore::resetReceiveState() noexcept {
        receiveBuffer.clear();
        receivePayload.clear();
        receiveExpected = 0;
        receiving = false;
        peerEof = false;
    }

    void ConnectionCore::writeFrame(std::uint8_t flags, std::uint64_t totalLength,
                                    const std::uint8_t* data, std::size_t size) {
        if (!isOpen()) throw std::logic_error("KCLink: send on a closed connection");
        if (size > maxChunkSize) throw std::length_error("KCLink: frame exceeds the chunk limit");

        std::uint8_t header[frameHeaderSize];
        encodeFrameHeader(header, flags, size, totalLength);
        writeExact(socket, header, sizeof(header));
        if (size != 0) writeExact(socket, data, size);
    }

    void ConnectionCore::sendMessage(const Message& message) {
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

    void ConnectionCore::beginMessage(std::uint64_t totalPayloadSize) {
        if (!isOpen()) throw std::logic_error("KCLink: send on a closed connection");
        if (sending) throw std::logic_error("KCLink: a chunked message is already in progress");
        if (totalPayloadSize > maxLogicalMessageSize)
            throw std::length_error("KCLink: message exceeds the configured size limit");

        writeFrame(flagStart, totalPayloadSize, nullptr, 0);
        sending = true;
        sendExpected = totalPayloadSize;
        sendWritten = 0;
    }

    void ConnectionCore::appendChunk(std::span<const std::uint8_t> chunk) {
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

    void ConnectionCore::endMessage() {
        if (!sending) throw std::logic_error("KCLink: no chunked message in progress");
        if (sendWritten != sendExpected)
            throw std::length_error("KCLink: chunked message is shorter than announced");

        writeFrame(flagEnd, sendExpected, nullptr, 0);
        sending = false;
        sendExpected = 0;
        sendWritten = 0;
    }

    bool ConnectionCore::isSendingMessage() const noexcept { return sending; }

    void ConnectionCore::disconnect() {
        if (!isOpen()) return;
        if (sending) {
            abort();
            return;
        }

        try {
            writeFrame(flagDisconnect, 0, nullptr, 0);
        } catch (...) {
            // Best effort: the peer may already be gone (e.g. it was killed).
        }
        abort();
    }

    void ConnectionCore::abort() noexcept {
        closeSocket(socket);
        resetReceiveState();
        sending = false;
        sendExpected = 0;
        sendWritten = 0;
    }

    ReceiveStatus ConnectionCore::tryReceive(Message& message) {
        if (!isOpen()) return ReceiveStatus::PeerLost;

        if (!peerEof) {
            std::uint8_t chunk[64 * 1024];
            while (true) {
                const auto count = ::recv(socket.get(), chunk, sizeof(chunk), 0);
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

        // Parse before acting on EOF so a buffered DISCONNECT frame is still
        // reported as a graceful close.
        while (receiveBuffer.size() >= frameHeaderSize) {
            const FrameHeader header = decodeFrameHeader(receiveBuffer.data());

            if (header.chunkLength > maxChunkSize) {
                abort();
                throw std::length_error("KCLink: received frame exceeds the chunk limit");
            }
            if (receiveBuffer.size() < frameHeaderSize + header.chunkLength) break;

            const std::uint8_t* framePayload = receiveBuffer.data() + frameHeaderSize;

            if (header.flags & flagDisconnect) {
                resetReceiveState();
                return ReceiveStatus::GracefulClose;
            }

            const bool start = (header.flags & flagStart) != 0;
            const bool end = (header.flags & flagEnd) != 0;

            if (start) {
                if (receiving) {
                    abort();
                    throw std::runtime_error("KCLink: received START while a message is in progress");
                }
                if (header.totalLength > maxLogicalMessageSize) {
                    abort();
                    throw std::length_error("KCLink: received message exceeds the size limit");
                }
                receiving = true;
                receiveExpected = header.totalLength;
                receivePayload.clear();
                receivePayload.reserve(static_cast<std::size_t>(header.totalLength));
            } else if (!receiving) {
                abort();
                throw std::runtime_error("KCLink: received a frame without START");
            } else if (header.totalLength != receiveExpected) {
                abort();
                throw std::runtime_error("KCLink: inconsistent total length across chunks");
            }

            receivePayload.insert(receivePayload.end(), framePayload,
                                  framePayload + header.chunkLength);
            receiveBuffer.erase(receiveBuffer.begin(),
                                receiveBuffer.begin()
                                    + static_cast<std::ptrdiff_t>(frameHeaderSize + header.chunkLength));

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
            resetReceiveState();
            return ReceiveStatus::PeerLost;
        }

        return ReceiveStatus::WouldBlock;
    }

}
