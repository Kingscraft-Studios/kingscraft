#pragma once

#include <KCLink/Message.hpp>

#include <cstddef>
#include <cstdint>
#include <span>
#include <sys/types.h>
#include <vector>

namespace kc::link {

    // Result of consuming whatever bytes are currently available on a
    // connection. A Message is only returned once an entire logical message
    // has been reassembled from its frames.
    enum class ReceiveStatus {
        Message,
        GracefulClose,
        PeerLost,
        WouldBlock
    };

    // A framed, non-blocking stream. A Connection is movable but not copyable.
    // Calls on the same Connection must be externally synchronized.
    //
    // Wire format (little-endian):
    //   [uint32 chunkLength][uint8 flags][uint64 totalLength][chunk payload]
    // flags: START=1, END=2, DISCONNECT=4. A logical message may span many
    // frames; KCLink splits and reassembles them transparently.
    class Connection {
    public:
        Connection() noexcept = default;
        ~Connection();

        Connection(const Connection&) = delete;
        Connection& operator=(const Connection&) = delete;
        Connection(Connection&& other) noexcept;
        Connection& operator=(Connection&& other) noexcept;

        bool isOpen() const noexcept;
        pid_t peerProcessId() const;

        // Sends a complete logical message, chunking it automatically when it
        // exceeds the 16 MiB per-frame limit.
        void send(const Message& message);

        // Streaming alternative for large messages that should not be held in
        // memory all at once. Only one message may be in flight at a time.
        void beginMessage(std::uint64_t totalPayloadSize);
        void sendChunk(std::span<const std::uint8_t> chunk);
        void endMessage();
        bool isSendingMessage() const noexcept;

        // Upper bound for a single logical message (default 1 GiB).
        void setMaxMessageSize(std::uint64_t size) noexcept;
        std::uint64_t maxMessageSize() const noexcept;

        // Sends a graceful-disconnect frame, then closes the socket.
        void disconnect();

        void close() noexcept;

    private:
        explicit Connection(int socket) noexcept;

        ReceiveStatus tryReceive(Message& message);
        void writeFrame(std::uint8_t flags, std::uint64_t totalLength,
                        const std::uint8_t* data, std::size_t size);
        void setNonBlocking() noexcept;
        void resetReceiveState() noexcept;
        int nativeHandle() const noexcept;

        int socket = -1;

        std::vector<std::uint8_t> receiveBuffer;
        std::vector<std::uint8_t> receivePayload;
        std::uint64_t receiveExpected = 0;
        bool receiving = false;
        bool peerEof = false;

        std::uint64_t sendExpected = 0;
        std::uint64_t sendWritten = 0;
        bool sending = false;

        std::uint64_t maxLogicalMessageSize = 1024ULL * 1024ULL * 1024ULL;

        friend class Client;
        friend class Server;
        friend class EventLoop;
    };

}
