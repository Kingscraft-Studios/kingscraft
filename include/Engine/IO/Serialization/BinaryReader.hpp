#pragma once

#include <cstddef>
#include <cstdint>
#include <cstring>

namespace kc {
    // Bounds-checked little-endian reader over a raw byte range. Every read is
    // validated against the remaining length; once an overrun happens ok()
    // latches false and all later reads fail, so a truncated/corrupt buffer can
    // never read past its end.
    class BinaryReader {
    public:
        BinaryReader(const char* data, std::size_t size)
            : data_(data), size_(size) {}

        std::size_t position() const { return pos_; }
        std::size_t remaining() const { return (pos_ <= size_) ? (size_ - pos_) : 0; }
        bool ok() const { return ok_; }

        bool u8(uint8_t& v) {
            if (!ensure(1)) return false;
            v = static_cast<uint8_t>(data_[pos_++]);
            return true;
        }
        bool u16(uint16_t& v) {
            if (!ensure(2)) return false;
            v = static_cast<uint16_t>(static_cast<uint8_t>(data_[pos_]))
              | static_cast<uint16_t>(static_cast<uint8_t>(data_[pos_ + 1]) << 8);
            pos_ += 2;
            return true;
        }
        bool u32(uint32_t& v) {
            if (!ensure(4)) return false;
            v = 0;
            for (int i = 0; i < 4; ++i)
                v |= static_cast<uint32_t>(static_cast<uint8_t>(data_[pos_ + i])) << (8 * i);
            pos_ += 4;
            return true;
        }
        bool u64(uint64_t& v) {
            if (!ensure(8)) return false;
            v = 0;
            for (int i = 0; i < 8; ++i)
                v |= static_cast<uint64_t>(static_cast<uint8_t>(data_[pos_ + i])) << (8 * i);
            pos_ += 8;
            return true;
        }
        bool i32(int32_t& v) {
            uint32_t tmp = 0;
            if (!u32(tmp)) return false;
            v = static_cast<int32_t>(tmp);
            return true;
        }
        bool bytes(void* dst, std::size_t n) {
            if (!ensure(n)) return false;
            if (n != 0) std::memcpy(dst, data_ + pos_, n);
            pos_ += n;
            return true;
        }
        bool skip(std::size_t n) {
            if (!ensure(n)) return false;
            pos_ += n;
            return true;
        }

    private:
        bool ensure(std::size_t n) {
            if (!ok_ || pos_ + n > size_ || pos_ + n < pos_) {
                ok_ = false;
                return false;
            }
            return true;
        }

        const char* data_;
        std::size_t size_;
        std::size_t pos_ = 0;
        bool ok_ = true;
    };
} // namespace kc
