#pragma once

#ifndef KCLINK_INTERNAL_COMPILATION
#error "KCLink internal header: not part of the public API"
#endif

#include "internal/Unix/Framing.hpp"
#include "internal/Unix/UniqueFd.hpp"

#include <KCLink/Message.hpp>

#include <cstdint>
#include <span>
#include <sys/types.h>
#include <vector>

namespace kc::link::detail {

    struct ConnectionCore {
        explicit ConnectionCore(UniqueFd socket) noexcept;

        int handle() const noexcept;
        bool isOpen() const noexcept;
        pid_t peerProcessId() const;

        void sendMessage(const Message& message);
        void beginMessage(std::uint64_t totalPayloadSize);
        void appendChunk(std::span<const std::uint8_t> chunk);
        void endMessage();
        bool isSendingMessage() const noexcept;

        void setMaxMessageSize(std::uint64_t size) noexcept;
        std::uint64_t maxMessageSize() const noexcept;

        void disconnect();
        void abort() noexcept;
        ReceiveStatus tryReceive(Message& message);

        UniqueFd socket;

        std::vector<std::uint8_t> receiveBuffer;
        std::vector<std::uint8_t> receivePayload;
        std::uint64_t receiveExpected = 0;
        bool receiving = false;
        bool peerEof = false;

        std::uint64_t sendExpected = 0;
        std::uint64_t sendWritten = 0;
        bool sending = false;

        std::uint64_t maxLogicalMessageSize = defaultMaxMessageSize;

    private:
        void writeFrame(std::uint8_t flags, std::uint64_t totalLength,
                        const std::uint8_t* data, std::size_t size);
        void resetReceiveState() noexcept;
    };

}
