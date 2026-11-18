#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

namespace kc {
    // Explicit little-endian writer. Appends to the vector handed in; the
    // caller owns the buffer. Every multi-byte field is emitted byte by byte so
    // the on-disk layout never depends on host endianness or struct padding —
    // raw struct dumps are never written.
    class BinaryWriter {
    public:
        explicit BinaryWriter(std::vector<char>& out) : out_(out) {}

        void u8(uint8_t v) {
            out_.push_back(static_cast<char>(v & 0xFFu));
        }
        void u16(uint16_t v) {
            out_.push_back(static_cast<char>(v & 0xFFu));
            out_.push_back(static_cast<char>((v >> 8) & 0xFFu));
        }
        void u32(uint32_t v) {
            for (int i = 0; i < 4; ++i)
                out_.push_back(static_cast<char>((v >> (8 * i)) & 0xFFu));
        }
        void u64(uint64_t v) {
            for (int i = 0; i < 8; ++i)
                out_.push_back(static_cast<char>((v >> (8 * i)) & 0xFFu));
        }
        void i32(int32_t v) { u32(static_cast<uint32_t>(v)); }

        void bytes(const void* data, std::size_t n) {
            if (n == 0) return;
            const char* p = static_cast<const char*>(data);
            out_.insert(out_.end(), p, p + n);
        }
        void magic(const char* m, std::size_t n) { bytes(m, n); }

        std::size_t size() const { return out_.size(); }

    private:
        std::vector<char>& out_;
    };
} // namespace kc
