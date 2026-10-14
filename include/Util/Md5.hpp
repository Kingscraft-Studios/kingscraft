#pragma once

#include <array>
#include <cstdint>
#include <cstring>
#include <string>

namespace kc {

    // Minimal RFC 1321 MD5, needed only because vanilla's
    // RandomSupport.seedFromHashOf(String) hashes noise seed names with MD5 and
    // reads the digest as two little-endian 64-bit words. Every octavalue seed in
    // the density-function graph ("octave_0" ... "octave_31") goes through this,
    // so the digest has to be byte-exact.
    class Md5 {
    public:
        static std::array<uint8_t, 16> hash(const std::string& input) {
            Md5 ctx;
            ctx.update(reinterpret_cast<const uint8_t*>(input.data()), input.size());
            return ctx.finish();
        }

        static std::array<uint8_t, 16> hash(const uint8_t* data, size_t len) {
            Md5 ctx;
            ctx.update(data, len);
            return ctx.finish();
        }

    private:
        void update(const uint8_t* data, size_t len) {
            totalBits_ += static_cast<uint64_t>(len) * 8;
            size_t offset = 0;
            if (bufferLen_ > 0) {
                const size_t need = 64 - bufferLen_;
                const size_t take = len < need ? len : need;
                std::memcpy(buffer_.data() + bufferLen_, data, take);
                bufferLen_ += take;
                offset += take;
                if (bufferLen_ == 64) {
                    transform(buffer_.data());
                    bufferLen_ = 0;
                }
            }
            for (; offset + 64 <= len; offset += 64) transform(data + offset);
            if (offset < len) {
                std::memcpy(buffer_.data() + bufferLen_, data + offset, len - offset);
                bufferLen_ += len - offset;
            }
        }

        std::array<uint8_t, 16> finish() {
            const uint64_t bitCount = totalBits_;
            uint8_t pad = 0x80;
            update(&pad, 1);
            totalBits_ = bitCount;  // padding is not part of the message length
            pad = 0x00;
            while (bufferLen_ != 56) {
                update(&pad, 1);
                totalBits_ = bitCount;
            }
            uint8_t lengthBytes[8];
            for (int i = 0; i < 8; ++i) lengthBytes[i] = static_cast<uint8_t>(bitCount >> (8 * i));
            update(lengthBytes, 8);

            std::array<uint8_t, 16> digest{};
            for (int i = 0; i < 4; ++i) {
                digest[static_cast<size_t>(4 * i + 0)] = static_cast<uint8_t>(state_[i] >> 0);
                digest[static_cast<size_t>(4 * i + 1)] = static_cast<uint8_t>(state_[i] >> 8);
                digest[static_cast<size_t>(4 * i + 2)] = static_cast<uint8_t>(state_[i] >> 16);
                digest[static_cast<size_t>(4 * i + 3)] = static_cast<uint8_t>(state_[i] >> 24);
            }
            return digest;
        }

        static uint32_t rotateLeft(uint32_t x, int c) { return (x << c) | (x >> (32 - c)); }

        void transform(const uint8_t* chunk) {
            static const uint32_t K[64] = {
                0xd76aa478u, 0xe8c7b756u, 0x242070dbu, 0xc1bdceeeu, 0xf57c0fafu, 0x4787c62au,
                0xa8304613u, 0xfd469501u, 0x698098d8u, 0x8b44f7afu, 0xffff5bb1u, 0x895cd7beu,
                0x6b901122u, 0xfd987193u, 0xa679438eu, 0x49b40821u, 0xf61e2562u, 0xc040b340u,
                0x265e5a51u, 0xe9b6c7aau, 0xd62f105du, 0x02441453u, 0xd8a1e681u, 0xe7d3fbc8u,
                0x21e1cde6u, 0xc33707d6u, 0xf4d50d87u, 0x455a14edu, 0xa9e3e905u, 0xfcefa3f8u,
                0x676f02d9u, 0x8d2a4c8au, 0xfffa3942u, 0x8771f681u, 0x6d9d6122u, 0xfde5380cu,
                0xa4beea44u, 0x4bdecfa9u, 0xf6bb4b60u, 0xbebfbc70u, 0x289b7ec6u, 0xeaa127fau,
                0xd4ef3085u, 0x04881d05u, 0xd9d4d039u, 0xe6db99e5u, 0x1fa27cf8u, 0xc4ac5665u,
                0xf4292244u, 0x432aff97u, 0xab9423a7u, 0xfc93a039u, 0x655b59c3u, 0x8f0ccc92u,
                0xffeff47du, 0x85845dd1u, 0x6fa87e4fu, 0xfe2ce6e0u, 0xa3014314u, 0x4e0811a1u,
                0xf7537e82u, 0xbd3af235u, 0x2ad7d2bbu, 0xeb86d391u};
            static const int S[64] = {7, 12, 17, 22, 7, 12, 17, 22, 7, 12, 17, 22, 7, 12, 17, 22,
                                      5, 9,  14, 20, 5, 9,  14, 20, 5, 9,  14, 20, 5, 9,  14, 20,
                                      4, 11, 16, 23, 4, 11, 16, 23, 4, 11, 16, 23, 4, 11, 16, 23,
                                      6, 10, 15, 21, 6, 10, 15, 21, 6, 10, 15, 21, 6, 10, 15, 21};

            uint32_t m[16];
            for (int i = 0; i < 16; ++i) {
                m[i] = static_cast<uint32_t>(chunk[4 * i]) |
                       (static_cast<uint32_t>(chunk[4 * i + 1]) << 8) |
                       (static_cast<uint32_t>(chunk[4 * i + 2]) << 16) |
                       (static_cast<uint32_t>(chunk[4 * i + 3]) << 24);
            }

            uint32_t a = state_[0], b = state_[1], c = state_[2], d = state_[3];
            for (int i = 0; i < 64; ++i) {
                uint32_t f;
                int g;
                if (i < 16) {
                    f = (b & c) | (~b & d);
                    g = i;
                } else if (i < 32) {
                    f = (d & b) | (~d & c);
                    g = (5 * i + 1) % 16;
                } else if (i < 48) {
                    f = b ^ c ^ d;
                    g = (3 * i + 5) % 16;
                } else {
                    f = c ^ (b | ~d);
                    g = (7 * i) % 16;
                }
                const uint32_t tmp = d;
                d = c;
                c = b;
                const uint32_t sum = a + f + K[i] + m[g];
                b = b + rotateLeft(sum, S[i]);
                a = tmp;
            }
            state_[0] += a;
            state_[1] += b;
            state_[2] += c;
            state_[3] += d;
        }

        std::array<uint32_t, 4> state_ = {0x67452301u, 0xefcdab89u, 0x98badcfeu, 0x10325476u};
        std::array<uint8_t, 64> buffer_{};
        size_t bufferLen_ = 0;
        uint64_t totalBits_ = 0;
    };

}
