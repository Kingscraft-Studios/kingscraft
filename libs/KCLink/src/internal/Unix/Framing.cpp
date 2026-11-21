#include "internal/Unix/Framing.hpp"
#include "internal/Unix/Endian.hpp"

namespace kc::link::detail {

    void encodeFrameHeader(std::uint8_t* destination, std::uint8_t flags,
                           std::size_t chunkLength, std::uint64_t totalLength) noexcept {
        writeU32LE(destination, static_cast<std::uint32_t>(chunkLength));
        destination[4] = flags;
        writeU64LE(destination + 5, totalLength);
    }

    FrameHeader decodeFrameHeader(const std::uint8_t* source) noexcept {
        FrameHeader header;
        header.chunkLength = readU32LE(source);
        header.flags = source[4];
        header.totalLength = readU64LE(source + 5);
        return header;
    }

}
