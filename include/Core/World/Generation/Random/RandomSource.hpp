#pragma once

#include <cstdint>
#include <memory>
#include <string>

namespace kc {

    class RandomSource;

    // Vanilla's PositionalRandomFactory: an immutable seed holder that derives
    // child generators without consuming any state of its own. Every octave of a
    // NormalNoise is seeded from one of these, so getting the "no consumption"
    // part wrong shifts all subsequent octave permutations.
    class PositionalRandomFactory {
    public:
        virtual ~PositionalRandomFactory() = default;

        virtual std::unique_ptr<RandomSource> at(int x, int y, int z) const = 0;
        virtual std::unique_ptr<RandomSource> fromHashOf(const std::string& name) const = 0;
        virtual std::unique_ptr<RandomSource> fromSeed(int64_t seed) const = 0;
    };

    // The subset of net.minecraft.util.RandomSource that worldgen uses.
    class RandomSource {
    public:
        virtual ~RandomSource() = default;

        virtual int32_t next(int bits) = 0;
        virtual int64_t nextLong() = 0;

        virtual int32_t nextInt() { return next(32); }
        virtual bool nextBoolean() { return next(1) != 0; }
        virtual float nextFloat() { return static_cast<float>(next(24)) * 5.9604645e-8f; }
        virtual double nextDouble();

        virtual int32_t nextInt(int32_t bound) {
            if (bound <= 0) return 0;
            // Power-of-two bounds take the fast multiply-shift path; everything
            // else rejects the biased low tail, as BitRandomSource does.
            if ((bound & (bound - 1)) == 0) {
                return static_cast<int32_t>((static_cast<int64_t>(bound) * next(31)) >> 31);
            }
            int32_t sample, modulo;
            do {
                sample = next(31);
                modulo = sample % bound;
            } while (sample - modulo + (bound - 1) < 0);
            return modulo;
        }

        virtual std::unique_ptr<RandomSource> fork() = 0;
        virtual std::unique_ptr<PositionalRandomFactory> forkPositionalFactory() = 0;
    };

}

namespace kc {

    inline double RandomSource::nextDouble() {
        const int32_t upper = next(26);
        const int32_t lower = next(27);
        const int64_t combined = (static_cast<int64_t>(upper) << 27) + lower;
        // Vanilla computes `(long * float)`, and Java's binary numeric promotion
        // evaluates that product in FLOAT precision before widening to double, so
        // the result carries only ~24 mantissa bits. Reproducing the intermediate
        // float rounding is required for the Perlin offset tables to line up.
        const float product = static_cast<float>(combined) * 1.110223e-16f;
        return static_cast<double>(product);
    }

    // Port of Minecraft's java.util.Random as used by worldgen, where exact
    // bit-for-bit agreement with vanilla matters: the noise permutation tables
    // are built by drawing from this generator, so a different LCG produces a
    // different (equally plausible but non-identical) world.
    //
    // LegacyRandomSource is a 48-bit linear congruential generator (multiplier
    // 0x5DEECE66D, increment 0xB), and every "next(bits)" call returns the TOP
    // `bits` bits of the new state.
    //
    // Java wraps on long overflow; C++ signed overflow is undefined, so all the
    // arithmetic runs through uint64_t (defined modulo 2^64) and is masked back
    // down afterwards. Masking the low 48 bits of a wrapped product gives the
    // same answer as Java's wrap-then-mask.
    class LegacyRandom final : public RandomSource {
    public:
        static constexpr uint64_t kModulusMask = 281474976710655ULL;  // 2^48 - 1

        explicit LegacyRandom(int64_t seed = 0) { setSeed(seed); }
        ~LegacyRandom() override = default;

        void setSeed(int64_t seed) {
            state_ = (static_cast<uint64_t>(seed) ^ 0x5DEECE66DULL) & kModulusMask;
        }

        int32_t next(int bits) override {
            state_ = (state_ * 0x5DEECE66DULL + 0xBULL) & kModulusMask;
            return static_cast<int32_t>(state_ >> (48 - bits));
        }

        int64_t nextLong() override {
            const int32_t upper = next(32);
            const int32_t lower = next(32);
            return (static_cast<int64_t>(upper) << 32) + lower;
        }

        std::unique_ptr<RandomSource> fork() override {
            return std::make_unique<LegacyRandom>(nextLong());
        }

        std::unique_ptr<PositionalRandomFactory> forkPositionalFactory() override {
            return std::make_unique<LegacyPositionalRandomFactory>(nextLong());
        }

        static int32_t javaHashCode(const std::string& s) {
            int32_t h = 0;
            for (unsigned char c : s) h = h * 31 + static_cast<int32_t>(c);
            return h;
        }

        // Mth.getSeed: the vanilla per-coordinate seed hash.
        static int64_t getSeed(int x, int y, int z) {
            // `x * 3129871` is int arithmetic in Java, so it wraps at 32 bits
            // before being widened into the xor. The multiply below is 64-bit
            // and wraps too; uint64_t makes that well defined.
            const int32_t a = static_cast<int32_t>(static_cast<uint32_t>(x) * 3129871u);
            int64_t seed = static_cast<int64_t>(a) ^ (static_cast<int64_t>(z) * 116129781LL) ^
                           static_cast<int64_t>(y);
            uint64_t u = static_cast<uint64_t>(seed);
            u = u * u * 42317861ULL + u * 11ULL;
            // C++20 guarantees an arithmetic (sign-preserving) shift.
            return static_cast<int64_t>(u) >> 16;
        }

    private:
        // Immutable seed holder, matching LegacyPositionalRandomFactory: deriving
        // a child generator does NOT consume state.
        class LegacyPositionalRandomFactory final : public PositionalRandomFactory {
        public:
            explicit LegacyPositionalRandomFactory(int64_t seed) : seed_(seed) {}

            std::unique_ptr<RandomSource> at(int x, int y, int z) const override {
                return std::make_unique<LegacyRandom>(LegacyRandom::getSeed(x, y, z) ^ seed_);
            }
            std::unique_ptr<RandomSource> fromHashOf(const std::string& name) const override {
                return std::make_unique<LegacyRandom>(
                    static_cast<int64_t>(LegacyRandom::javaHashCode(name)) ^ seed_);
            }
            std::unique_ptr<RandomSource> fromSeed(int64_t seed) const override {
                return std::make_unique<LegacyRandom>(seed ^ seed_);
            }

        private:
            int64_t seed_;
        };

        uint64_t state_ = 0;
    };

}
