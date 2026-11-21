#pragma once

#include <KCLink/Export.hpp>
#include <KCLink/Message.hpp>

#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <sys/types.h>

namespace kc::link {

    namespace detail {
        struct ConnectionCore;
    }

    // A movable logical message stream. The caller owns the connection;
    // EventLoop drives incoming messages. Large messages are handled
    // transparently, so senders and receivers work in whole Message units.
    class KCLINK_API Connection {
    public:
        Connection() noexcept;
        ~Connection();

        Connection(const Connection&) = delete;
        Connection& operator=(const Connection&) = delete;
        Connection(Connection&& other) noexcept;
        Connection& operator=(Connection&& other) noexcept;

        bool isOpen() const noexcept;
        pid_t peerProcessId() const;

        void sendMessage(const Message& message);

        // Streaming form for payloads that should not be held in memory all
        // at once. Only one logical message may be in flight at a time.
        void beginMessage(std::uint64_t totalPayloadSize);
        void appendChunk(std::span<const std::uint8_t> chunk);
        void endMessage();
        bool isSendingMessage() const noexcept;

        void setMaxMessageSize(std::uint64_t size) noexcept;
        std::uint64_t maxMessageSize() const noexcept;

        // Graceful close sends an explicit control message first.
        void disconnect();

        // Immediate non-graceful close.
        void abort() noexcept;

    private:
        explicit Connection(std::unique_ptr<detail::ConnectionCore> core) noexcept;

        detail::ConnectionCore& core() noexcept;
        const detail::ConnectionCore& core() const noexcept;

        std::unique_ptr<detail::ConnectionCore> core_;

        friend class Client;
        friend class EventLoop;
    };

}
