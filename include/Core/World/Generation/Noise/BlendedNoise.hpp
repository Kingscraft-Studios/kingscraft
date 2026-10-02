#pragma once

#include <array>
#include <cmath>
#include <cstdint>
#include <memory>
#include <stdexcept>
#include <vector>

#include "Core/World/Generation/Noise/PerlinNoise.hpp"
#include "Core/World/Generation/Random/RandomSource.hpp"

namespace kc {

    // Port of SmearedPerlinNoise. It is a PerlinNoise whose Y gradient is evaluated
    // at a "fudged" fractional height: the relative Y is snapped to a multiple of
    // fudgeYScale, which stretches the noise vertically into smooth bands while
    // keeping X/Z crisp. This is what gives vanilla's 3D terrain its
    // horizontally-soft, vertically-stretched look.
    class SmearedPerlinNoise {
    public:
        SmearedPerlinNoise(RandomSource& random, double fudgeYScale)
            : perlin_(random), fudgeYScale_(fudgeYScale) {}

        float get(double rawX, double rawY, double rawZ) const {
            const double x = PerlinNoise::wrap(rawX) + perlin_.offsetX();
            const double y = PerlinNoise::wrap(rawY) + perlin_.offsetY();
            const double z = PerlinNoise::wrap(rawZ) + perlin_.offsetZ();

            const int floorX = PerlinNoise::mthFloor(x);
            const int floorY = PerlinNoise::mthFloor(y);
            const int floorZ = PerlinNoise::mthFloor(z);

            const float relativeX = static_cast<float>(x - floorX);
            const double relativeY = y - floorY;  // kept in double: the fudge uses it
            const float relativeZ = static_cast<float>(z - floorZ);
            const float fudgedRelativeY = static_cast<float>(relativeY - computeFudgeY(rawY, relativeY));

            return sampleAndLerp(floorX, floorY, floorZ, relativeX, fudgedRelativeY, relativeZ,
                                 static_cast<float>(relativeY));
        }

        double fudgeYScale() const { return fudgeYScale_; }
        const std::array<std::uint8_t, 256>& permutation() const { return perlin_.permutation(); }
        double perlinOffset(int axis) const { return perlin_.perlinOffset(axis); }
        double offsetX() const { return perlin_.offsetX(); }
        double offsetY() const { return perlin_.offsetY(); }
        double offsetZ() const { return perlin_.offsetZ(); }

    private:
        // Note the fudge limit is derived from the ORIGINAL (unwrapped) y, which is
        // what makes the smear periodic in world space.
        double computeFudgeY(double originalY, double relativeY) const {
            const double fudgeLimit = (originalY >= 0.0 && originalY < relativeY) ? originalY : relativeY;
            return static_cast<double>(PerlinNoise::mthFloor(fudgeLimit / fudgeYScale_ + 1.0E-7F)) *
                   fudgeYScale_;
        }

        float sampleAndLerp(int x, int y, int z, float relativeX, float relativeY, float relativeZ,
                            float originalRelativeY) const {
            const int x0 = perlin_.permute(x);
            const int x1 = perlin_.permute(x + 1);
            const int xy00 = perlin_.permute(x0 + y);
            const int xy01 = perlin_.permute(x0 + y + 1);
            const int xy10 = perlin_.permute(x1 + y);
            const int xy11 = perlin_.permute(x1 + y + 1);

            const auto& g000 = perlin_.permuteToGrad(xy00 + z);
            const auto& g100 = perlin_.permuteToGrad(xy10 + z);
            const auto& g010 = perlin_.permuteToGrad(xy01 + z);
            const auto& g110 = perlin_.permuteToGrad(xy11 + z);
            const auto& g001 = perlin_.permuteToGrad(xy00 + z + 1);
            const auto& g101 = perlin_.permuteToGrad(xy10 + z + 1);
            const auto& g011 = perlin_.permuteToGrad(xy01 + z + 1);
            const auto& g111 = perlin_.permuteToGrad(xy11 + z + 1);

            return lerp3(PerlinNoise::smoothstep(relativeX), PerlinNoise::smoothstep(originalRelativeY),
                         PerlinNoise::smoothstep(relativeZ),
                         dotXz(g000, relativeX, relativeZ) + gy(g000) * relativeY,
                         dotXz(g100, relativeX - 1.0f, relativeZ) + gy(g100) * relativeY,
                         dotXz(g010, relativeX, relativeZ) + gy(g010) * (relativeY - 1.0f),
                         dotXz(g110, relativeX - 1.0f, relativeZ) + gy(g110) * (relativeY - 1.0f),
                         dotXz(g001, relativeX, relativeZ - 1.0f) + gy(g001) * relativeY,
                         dotXz(g101, relativeX - 1.0f, relativeZ - 1.0f) + gy(g101) * relativeY,
                         dotXz(g011, relativeX, relativeZ - 1.0f) + gy(g011) * (relativeY - 1.0f),
                         dotXz(g111, relativeX - 1.0f, relativeZ - 1.0f) + gy(g111) * (relativeY - 1.0f));
        }

        static float dotXz(const std::array<int, 3>& g, float x, float z) {
            return static_cast<float>(g[0]) * x + static_cast<float>(g[2]) * z;
        }
        static float gy(const std::array<int, 3>& g) { return static_cast<float>(g[1]); }

        static float lerp(float alpha, float p0, float p1) { return p0 + alpha * (p1 - p0); }
        static float lerp2(float a1, float a2, float x00, float x10, float x01, float x11) {
            return lerp(a2, lerp(a1, x00, x10), lerp(a1, x01, x11));
        }
        static float lerp3(float a1, float a2, float a3, float x000, float x100, float x010, float x110,
                           float x001, float x101, float x011, float x111) {
            return lerp(a3, lerp2(a1, a2, x000, x100, x010, x110),
                        lerp2(a1, a2, x001, x101, x011, x111));
        }

        PerlinNoise perlin_;
        double fudgeYScale_;
    };

    // A NoiseStack built out of SmearedPerlinNoise layers. Vanilla uses the
    // all-SmearedPerlin fast path, which just sums the layers; there is no
    // interpolation or 0.5 averaging as in NormalNoise.
    class SmearedNoiseStack {
    public:
        static SmearedNoiseStack createFbm(RandomSource& random, int firstOctave, double smearScaleY,
                                           double valueFactor) {
            if (firstOctave > 0) throw std::invalid_argument("firstOctave>0");

            const int octaves = -firstOctave + 1;
            double factor = 1.0;
            valueFactor /= std::pow(2.0, static_cast<double>(octaves)) - 1.0;

            SmearedNoiseStack stack;
            stack.layers_.reserve(static_cast<std::size_t>(octaves));
            for (int i = octaves - 1; i >= 0; --i) {
                stack.layers_.push_back(
                    Layer{SmearedPerlinNoise(random, smearScaleY * factor), factor,
                          static_cast<float>(valueFactor)});
                factor /= 2.0;
                valueFactor *= 2.0;
            }
            return stack;
        }

        float get(double x, double y, double z) const {
            float value = 0.0f;
            for (const Layer& layer : layers_) {
                value += layer.amplitude *
                         layer.noise.get(x * layer.frequency, y * layer.frequency, z * layer.frequency);
            }
            return value;
        }

        std::size_t layerCount() const { return layers_.size(); }

        // Test-only introspection so parity harnesses can diff octave by octave.
        double layerFrequency(std::size_t i) const { return layers_[i].frequency; }
        float layerAmplitude(std::size_t i) const { return layers_[i].amplitude; }
        float layerValue(std::size_t i, double x, double y, double z) const {
            const Layer& layer = layers_[i];
            return layer.noise.get(x * layer.frequency, y * layer.frequency, z * layer.frequency);
        }
        const std::array<std::uint8_t, 256>& layerPermutation(std::size_t i) const {
            return layers_[i].noise.permutation();
        }
        double layerOffset(std::size_t i, int axis) const { return layers_[i].noise.perlinOffset(axis); }
        float smearedOffsets(std::size_t i, double& ox, double& oy, double& oz) const {
            ox = layers_[i].noise.offsetX();
            oy = layers_[i].noise.offsetY();
            oz = layers_[i].noise.offsetZ();
            return layers_[i].noise.fudgeYScale();
        }

    private:
        struct Layer {
            SmearedPerlinNoise noise;
            double frequency;
            float amplitude;
        };
        std::vector<Layer> layers_;
    };

    // Port of BlendedNoise, registered as `minecraft:overworld/base_3d_noise`.
    //
    // It interpolates between two 16-octave smeared fbms ("limit" noises) based on
    // where an 8-octave "main" noise falls, which gives flat plateaus in lowlands
    // and full-strength detail on peaks. Overworld params:
    //   BlendedNoise(0.25, 0.125, 80.0, 160.0, 8.0)
    class BlendedNoise {
    public:
        static constexpr double kBaseScale = 684.412;
        static constexpr double kLimitFactor = 0.99998474;
        static constexpr double kMainFactor = 12.75;
        static constexpr int kLimitFirstOctave = -15;
        static constexpr int kMainFirstOctave = -7;
        static constexpr const char* kNoiseSeed = "minecraft:terrain";

        BlendedNoise(double xzScale, double yScale, double xzFactor, double yFactor,
                     double smearScaleMultiplier)
            : xzScale_(xzScale), yScale_(yScale), xzFactor_(xzFactor), yFactor_(yFactor),
              smearScaleMultiplier_(smearScaleMultiplier) {}

        void create(RandomSource& random) {
            const double limitSmearScaleY = yMultiplier() * smearScaleMultiplier_;
            const double mainSmearScaleY = limitSmearScaleY / yFactor_;
            // Order matters: the three stacks draw from `random` in sequence, so
            // minLimit and maxLimit are NOT the same noise.
            minLimit_ = SmearedNoiseStack::createFbm(random, kLimitFirstOctave, limitSmearScaleY, kLimitFactor);
            maxLimit_ = SmearedNoiseStack::createFbm(random, kLimitFirstOctave, limitSmearScaleY, kLimitFactor);
            main_ = SmearedNoiseStack::createFbm(random, kMainFirstOctave, mainSmearScaleY, kMainFactor);
        }

        float sample(double x, double y, double z) const {
            const double xzM = xzMultiplier();
            const double yM = yMultiplier();

            const float minValue = minLimit_.get(x * xzM, y * yM, z * xzM);
            const float maxValue = maxLimit_.get(x * xzM, y * yM, z * xzM);

            const float mainValue =
                main_.get(x * (xzM / xzFactor_), y * (yM / yFactor_), z * (xzM / xzFactor_));
            const float choice = clamp(mainValue + 0.5f, 0.0f, 1.0f);
            return minValue + choice * (maxValue - minValue);
        }

        static BlendedNoise overworld() { return BlendedNoise(0.25, 0.125, 80.0, 160.0, 8.0); }

        // Test-only introspection for parity harnesses.
        const SmearedNoiseStack& minLimit() const { return minLimit_; }
        const SmearedNoiseStack& maxLimit() const { return maxLimit_; }
        const SmearedNoiseStack& mainNoise() const { return main_; }

    private:
        double xzMultiplier() const { return kBaseScale * xzScale_; }
        double yMultiplier() const { return kBaseScale * yScale_; }
        static float clamp(float v, float lo, float hi) { return v < lo ? lo : (v > hi ? hi : v); }

        double xzScale_, yScale_, xzFactor_, yFactor_, smearScaleMultiplier_;
        SmearedNoiseStack minLimit_, maxLimit_, main_;
    };

}
