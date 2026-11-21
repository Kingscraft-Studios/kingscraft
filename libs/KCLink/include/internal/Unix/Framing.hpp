#pragma once

#ifndef KCLINK_INTERNAL_COMPILATION
#error "KCLink internal header: not part of the public API"
#endif

#include <cstddef>
#include <cstdint>

namespace kc::link::detail {

    enum class ReceiveStatus {
        Message,
        GracefulClose,
        PeerLost,
        WouldBlock
    };

    constexpr std::size_t maxChunkSize = 16U * 1024U * 1024U;
    constexpr std::size_t frameHeaderSize = 13;
    constexpr std::uint64_t defaultMaxMessageSize = 1024ULL * 1024ULL * 1024ULL;

    constexpr std::uint8_t flagStart = 0x01;
    constexpr std::uint8_t flagEnd = 0x02;
    constexpr std::uint8_t flagDisconnect = 0x04;

    struct FrameHeader {
        std::uint32_t chunkLength = 0;
        std::uint8_t flags = 0;
        std::uint64_t totalLength = 0;
    };

    void encodeFrameHeader(std::uint8_t* destination, std::uint8_t flags,
                           std::size_t chunkLength, std::uint64_t totalLength) noexcept;
    FrameHeader decodeFrameHeader(const std::uint8_t* source) noexcept;

}
