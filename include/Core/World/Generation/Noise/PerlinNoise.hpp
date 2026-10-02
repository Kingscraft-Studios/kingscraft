#pragma once

#include <array>
#include <cmath>
#include <cstdint>
#include <memory>
#include <string>

#include "Core/World/Generation/Random/RandomSource.hpp"

namespace kc {

    // Port of vanilla's PerlinNoise (net.minecraft.world.level.levelgen.synth.PerlinNoise).
    //
    // Perlin noise here is gradient noise over a 256-entry permutation table with
    // 16 hand-picked 3D gradients. Two details are easy to get wrong and both
    // change every sampled value:
    //   * the fade curve is the QUINTIC smoothstep x^3(6x^2-15x+10), not the
    //     classic cubic x^2(3-2x);
    //   * gradients are taken as `permute(...) & 15`, and the table holds 16
    //     gradients of which four are duplicates of others.
    class PerlinNoise {
    public:
        // The 16 gradients, in vanilla's exact order.
        static constexpr std::array<std::array<int, 3>, 16> kGradients = {{
            {{1, 1, 0}}, {{-1, 1, 0}}, {{1, -1, 0}}, {{-1, -1, 0}},
            {{1, 0, 1}}, {{-1, 0, 1}}, {{1, 0, -1}}, {{-1, 0, -1}},
            {{0, 1, 1}}, {{0, -1, 1}}, {{0, 1, -1}}, {{0, -1, -1}},
            {{1, 1, 0}}, {{0, -1, 1}}, {{-1, 1, 0}}, {{0, -1, -1}},
        }};

        static constexpr double kStandardDeviation = 0.2702247831245211;

        // Vanilla's bound: coordinates are wrapped into
        // [-Math.nextDown(1.6777216E7), 1.6777216E7).
        static constexpr double kWrapLimit = 1.6777216e7;
        static constexpr double kWrapPeriod = 3.3554432e7;

        explicit PerlinNoise(RandomSource& random) { init(random); }

        // The form the density graph uses: seed a Perlin from a positional factory
        // and an octave name such as "octave_3".
        PerlinNoise(const PositionalRandomFactory& factory, const std::string& octaveName) {
            const std::unique_ptr<RandomSource> seeded = factory.fromHashOf(octaveName);
            init(*seeded);
        }

        float get(double x, double y) const { return get(x, 0.0, y); }

        float get(double rawX, double rawY, double rawZ) const {
            const double x = wrap(rawX) + offsetX_;
            const double y = wrap(rawY) + offsetY_;
            const double z = wrap(rawZ) + offsetZ_;

            const int floorX = mthFloor(x);
            const int floorY = mthFloor(y);
            const int floorZ = mthFloor(z);
            const float rx = static_cast<float>(x - floorX);
            const float ry = static_cast<float>(y - floorY);
            const float rz = static_cast<float>(z - floorZ);

            const int x0 = permute(floorX);
            const int x1 = permute(floorX + 1);
            const int xy00 = permute(x0 + floorY);
            const int xy01 = permute(x0 + floorY + 1);
            const int xy10 = permute(x1 + floorY);
            const int xy11 = permute(x1 + floorY + 1);

            const float d000 = gradDot(permute(xy00 + floorZ), rx, ry, rz);
            const float d100 = gradDot(permute(xy10 + floorZ), rx - 1.0f, ry, rz);
            const float d010 = gradDot(permute(xy01 + floorZ), rx, ry - 1.0f, rz);
            const float d110 = gradDot(permute(xy11 + floorZ), rx - 1.0f, ry - 1.0f, rz);
            const float d001 = gradDot(permute(xy00 + floorZ + 1), rx, ry, rz - 1.0f);
            const float d101 = gradDot(permute(xy10 + floorZ + 1), rx - 1.0f, ry, rz - 1.0f);
            const float d011 = gradDot(permute(xy01 + floorZ + 1), rx, ry - 1.0f, rz - 1.0f);
            const float d111 = gradDot(permute(xy11 + floorZ + 1), rx - 1.0f, ry - 1.0f, rz - 1.0f);

            return lerp3(smoothstep(rx), smoothstep(ry), smoothstep(rz),
                         d000, d100, d010, d110, d001, d101, d011, d111);
        }

        // Mth.smoothstep: the quintic fade curve.
        static float smoothstep(float x) { return x * x * x * (x * (x * 6.0f - 15.0f) + 10.0f); }

        // Mth.floor, which unlike a plain cast rounds down for negatives.
        static int mthFloor(double d) {
            const int i = static_cast<int>(d);
            return d < static_cast<double>(i) ? i - 1 : i;
        }

        // GradientNoise.wrap: folds far-out coordinates back into range so that
        // huge world coordinates keep sampling sensibly.
        // GradientNoise.wrap compares against Math.nextDown(1.6777216E7) for BOTH
        // bounds; the upper bound is not 1.6777216E7 itself.
        static double wrap(double x) {
            if (x >= -kWrapLimitNextDown && x < kWrapLimitNextDown) return x;
            return x - std::floor(x / kWrapPeriod + 0.5) * kWrapPeriod;
        }

        int permute(int x) const { return perms_[static_cast<uint8_t>(x)]; }

        // GradientNoise.permuteToGrad: the permutation is applied BEFORE the & 15
        // mask. Skipping the permute still yields a plausible in-range value, so the
        // bug is silent, but the noise is wrong.
        const std::array<int, 3>& permuteToGrad(int x) const {
            return kGradients[static_cast<std::size_t>(perms_[static_cast<std::uint8_t>(x)] & 15)];
        }
        const std::array<uint8_t, 256>& perms() const { return perms_; }
        const std::array<std::uint8_t, 256>& permutation() const { return perms_; }
        double perlinOffset(int axis) const {
            return axis == 0 ? offsetX_ : (axis == 1 ? offsetY_ : offsetZ_);
        }
        double offsetX() const { return offsetX_; }
        double offsetY() const { return offsetY_; }
        double offsetZ() const { return offsetZ_; }

    private:
        // 1.6777216E7 is exactly 2^24, so Math.nextDown(1.6777216E7) is one ulp
        // (2^-28) below it. Vanilla hardcodes Math.nextDown here.
        static constexpr double kWrapLimitNextDown = 0x1p24 - 0x1p-28;
        static_assert(kWrapLimitNextDown < kWrapLimit, "wrap lower bound must be nextDown");

        void init(RandomSource& random) {
            // The offsets are drawn FIRST, then the table is shuffled.
            offsetX_ = random.nextDouble() * 256.0;
            offsetY_ = random.nextDouble() * 256.0;
            offsetZ_ = random.nextDouble() * 256.0;

            for (int i = 0; i < 256; ++i) perms_[i] = static_cast<uint8_t>(i);
            for (int i = 0; i < 256; ++i) {
                const int j = random.nextInt(256 - i);
                const uint8_t tmp = perms_[i];
                perms_[i] = perms_[j + i];
                perms_[j + i] = tmp;
            }
        }

        static float gradDot(int hash, float x, float y, float z) {
            const auto& g = kGradients[hash & 15];
            return static_cast<float>(g[0]) * x + static_cast<float>(g[1]) * y +
                   static_cast<float>(g[2]) * z;
        }

        static float lerp(float a, float p0, float p1) { return p0 + a * (p1 - p0); }

        static float lerp2(float a1, float a2, float x00, float x10, float x01, float x11) {
            return lerp(a2, lerp(a1, x00, x10), lerp(a1, x01, x11));
        }

        static float lerp3(float a1, float a2, float a3, float x000, float x100, float x010, float x110,
                           float x001, float x101, float x011, float x111) {
            return lerp(a3, lerp2(a1, a2, x000, x100, x010, x110),
                        lerp2(a1, a2, x001, x101, x011, x111));
        }

        std::array<uint8_t, 256> perms_{};
        double offsetX_ = 0.0, offsetY_ = 0.0, offsetZ_ = 0.0;
    };

}
