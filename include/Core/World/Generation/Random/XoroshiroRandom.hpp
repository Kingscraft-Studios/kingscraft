#pragma once

#include <cstdint>
#include <memory>
#include <string>

#include "Core/World/Generation/Random/Md5.hpp"
#include "Core/World/Generation/Random/RandomSource.hpp"

namespace kc {

    inline int64_t mixStafford13(int64_t z) {
        uint64_t u = static_cast<uint64_t>(z);
        u = (u ^ (u >> 30)) * 0xbf58476d1ce4e5b9ULL;
        u = (u ^ (u >> 27)) * 0x94d049bb133111ebULL;
        u ^= u >> 31;
        return static_cast<int64_t>(u);
    }

    // RandomSupport (net.minecraft.world.level.levelgen.RandomSupport).
    struct Seed128bit {
        int64_t lo = 0;
        int64_t hi = 0;

        Seed128bit xorWith(int64_t otherLo, int64_t otherHi) const {
            return Seed128bit{lo ^ otherLo, hi ^ otherHi};
        }
        Seed128bit mixed() const {
            return Seed128bit{mixStafford13(lo), mixStafford13(hi)};
        }
    };

    inline Seed128bit upgradeSeedTo128bitUnmixed(int64_t legacySeed) {
        const int64_t lowBits = legacySeed ^ 7640891576956012809LL;
        const int64_t highBits = lowBits + -7046029254386353131LL;
        return Seed128bit{lowBits, highBits};
    }

    inline Seed128bit upgradeSeedTo128bit(int64_t legacySeed) {
        return upgradeSeedTo128bitUnmixed(legacySeed).mixed();
    }

    // MD5 of the UTF-8 name, read as two little-endian 64-bit words.
    inline Seed128bit seedFromHashOf(const std::string& input) {
        const auto digest = Md5::hash(input);
        auto le64 = [&](size_t offset) {
            uint64_t v = 0;
            for (size_t i = 0; i < 8; ++i) v |= static_cast<uint64_t>(digest[offset + i]) << (8 * i);
            return static_cast<int64_t>(v);
        };
        return Seed128bit{le64(0), le64(8)};
    }

    // Xoroshiro128PlusPlus: the state machine behind the modern worldgen RNG.
    class Xoroshiro128PlusPlus {
    public:
        static constexpr int64_t kGoldenRatio64 = -7046029254386353131LL;
        static constexpr int64_t kSilverRatio64 = 7640891576956012809LL;

        Xoroshiro128PlusPlus(int64_t lo, int64_t hi) : lo_(lo), hi_(hi) {
            // An all-zero state is a fixed point, so vanilla substitutes a constant.
            if ((lo_ | hi_) == 0) {
                lo_ = kGoldenRatio64;
                hi_ = kSilverRatio64;
            }
        }
        explicit Xoroshiro128PlusPlus(Seed128bit seed)
            : Xoroshiro128PlusPlus(seed.lo, seed.hi) {}

        int64_t nextLong() {
            const int64_t s0 = lo_;
            const int64_t s1 = hi_;
            const int64_t result = static_cast<int64_t>(static_cast<uint64_t>(
                                              rotl(static_cast<uint64_t>(s0) + static_cast<uint64_t>(s1), 17))) +
                                  s0;
            const uint64_t xored = static_cast<uint64_t>(s1) ^ static_cast<uint64_t>(s0);
            lo_ = static_cast<int64_t>(rotl(static_cast<uint64_t>(s0), 49) ^ xored ^ (xored << 21));
            hi_ = static_cast<int64_t>(rotl(xored, 28));
            return result;
        }

    private:
        static uint64_t rotl(uint64_t x, int k) { return (x << k) | (x >> (64 - k)); }

        int64_t lo_;
        int64_t hi_;
    };

    // XoroshiroRandomSource. This is what the Overworld actually uses:
    // data/minecraft/worldgen/noise_settings/overworld.json sets
    // "legacy_random_source": false, so the noise permutation tables are drawn
    // from this generator and not from the 48-bit LCG. The Nether, End, Caves and
    // Floating Islands settings all set it to true and do use LegacyRandomSource.
    class XoroshiroRandom final : public RandomSource {
    public:
        explicit XoroshiroRandom(int64_t seed) : rng_(upgradeSeedTo128bit(seed)) {}
        XoroshiroRandom(int64_t lo, int64_t hi) : rng_(lo, hi) {}
        explicit XoroshiroRandom(Seed128bit seed) : rng_(seed) {}
        ~XoroshiroRandom() override = default;

        int32_t next(int bits) override {
            return static_cast<int32_t>(static_cast<uint32_t>(rng_.nextLong()) >> (64 - bits));
        }

        int64_t nextLong() override { return rng_.nextLong(); }

        // Vanilla overrides the modulo implementation with a multiply-shift
        // (Lemire) variant that keeps the low 32 bits for a rejection test.
        int32_t nextInt(int32_t bound) override {
            if (bound <= 0) return 0;
            uint32_t randomBits = static_cast<uint32_t>(static_cast<uint32_t>(rng_.nextLong()));
            uint64_t multiplied = static_cast<uint64_t>(randomBits) * static_cast<uint64_t>(bound);
            uint32_t fractionalPart = static_cast<uint32_t>(multiplied & 0xFFFFFFFFULL);
            if (fractionalPart < static_cast<uint32_t>(bound)) {
                // Integer.remainderUnsigned(~bound + 1, bound)
                const uint32_t unbiasedStart =
                    static_cast<uint32_t>((static_cast<uint64_t>(~bound) + 1ULL) %
                                          static_cast<uint64_t>(bound));
                while (fractionalPart < unbiasedStart) {
                    randomBits = static_cast<uint32_t>(static_cast<uint32_t>(rng_.nextLong()));
                    multiplied = static_cast<uint64_t>(randomBits) * static_cast<uint64_t>(bound);
                    fractionalPart = static_cast<uint32_t>(multiplied & 0xFFFFFFFFULL);
                }
            }
            return static_cast<int32_t>(multiplied >> 32);
        }

        // nextBits(53) is 53 real bits here, unlike the legacy path's float-rounded
        // nextDouble, so this one is full double precision.
        double nextDouble() override {
            return static_cast<double>(static_cast<uint64_t>(rng_.nextLong()) >> 11) *
                   (1.0 / 9007199254740992.0);
        }

        std::unique_ptr<RandomSource> fork() override {
            const int64_t lo = rng_.nextLong();
            const int64_t hi = rng_.nextLong();
            return std::make_unique<XoroshiroRandom>(lo, hi);
        }

        std::unique_ptr<PositionalRandomFactory> forkPositionalFactory() override {
            const int64_t lo = rng_.nextLong();
            const int64_t hi = rng_.nextLong();
            return std::make_unique<XoroshiroPositionalRandomFactory>(lo, hi);
        }

    private:
        class XoroshiroPositionalRandomFactory final : public PositionalRandomFactory {
        public:
            XoroshiroPositionalRandomFactory(int64_t lo, int64_t hi) : lo_(lo), hi_(hi) {}

            std::unique_ptr<RandomSource> at(int x, int y, int z) const override {
                return std::make_unique<XoroshiroRandom>(LegacyRandom::getSeed(x, y, z) ^ lo_, hi_);
            }
            std::unique_ptr<RandomSource> fromHashOf(const std::string& name) const override {
                return std::make_unique<XoroshiroRandom>(seedFromHashOf(name).xorWith(lo_, hi_));
            }
            std::unique_ptr<RandomSource> fromSeed(int64_t seed) const override {
                return std::make_unique<XoroshiroRandom>(seed ^ lo_, seed ^ hi_);
            }

        private:
            int64_t lo_;
            int64_t hi_;
        };

        Xoroshiro128PlusPlus rng_;
    };

}
