#pragma once

#include <cmath>
#include <memory>
#include <string>
#include <vector>

#include "PerlinNoise.hpp"
#include "Core/World/Generation/Random/RandomSource.hpp"

namespace kc {

    // Port of vanilla's NormalNoise (net.minecraft.world.level.levelgen.synth.NormalNoise):
    // a sum of independently seeded Perlin octaves, each at a power-of-two frequency.
    //
    // Every octave is seeded from one of two PositionalRandomFactorys forked from
    // the parent generator, and the two stacks (the "first" and "second" halves) are
    // the pair that the density-function graph interpolates between. Note the
    // second stack's frequencies are scaled by 1.0181268882175227 so that the two
    // halves decorrelate; reproducing that constant is required.
    class NormalNoise {
    public:
        enum class Normalization { Disabled, Enabled, Legacy };

        struct Parameters {
            double baseAmplitude = 1.0;
            int baseOctave = 0;
            int octaveCount = 1;
            Normalization normalize = Normalization::Enabled;
            // Empty means "every octave unmodified"; otherwise exactly octaveCount
            // entries, matching vanilla's codec validation.
            std::vector<double> amplitudeModifiers;

            static constexpr int kMaxOctaveCount = 32;
        };

        struct OctaveInfo {
            int octaveIndex = 0;
            double frequency = 1.0;
            double amplitude = 1.0;

            double absAmplitude() const { return std::fabs(amplitude); }
            std::string seed() const { return "octave_" + std::to_string(octaveIndex); }
        };

        explicit NormalNoise(Parameters params) : params_(std::move(params)) { build(); }

        // Builds the samplable noise from a parent generator, mirroring
        // NormalNoise.create(): two factories are forked in order, then each octave
        // draws a Perlin from each factory by name.
        void create(RandomSource& random) {
            const std::unique_ptr<PositionalRandomFactory> firstFactory = random.forkPositionalFactory();
            const std::unique_ptr<PositionalRandomFactory> secondFactory = random.forkPositionalFactory();

            layers_.clear();
            layers_.reserve(octaves_.size() * 2);

            for (const OctaveInfo& octave : octaves_) {
                const std::string name = octave.seed();
                PerlinNoise first(*firstFactory, name);
                PerlinNoise second(*secondFactory, name);
                const double valueFactor = normalizationFactor_ * octave.amplitude;
                layers_.push_back(Layer{first, octave.frequency, static_cast<float>(valueFactor)});
                layers_.push_back(Layer{second, octave.frequency * kSecondStackScale,
                                        static_cast<float>(valueFactor)});
            }
        }

        // The sampled value, which is what the density functions use.
        //
        // 26.3 sums ALL layers (both halves) with no interpolation and no 0.5
        // averaging; the normalization factor already accounts for the sqrt(2)
        // variance of the two halves, so scaling here would halve the output.
        float getValue(double x, double y) const { return getValue(x, y, x); }

        float getValue(double x, double y, double z) const { return sample(x, y, z); }

        // The first half on its own, for callers that need the un-summed stack.
        // 2D means y == 0, matching NormalNoise.get(double, double).
        float getFirst(double x, double z) const { return getFirst(x, 0.0, z); }

        float getFirst(double x, double y, double z) const {
            float value = 0.0f;
            for (size_t i = 0; i < layers_.size(); i += 2) {
                const Layer& l = layers_[i];
                value += l.amplitude * static_cast<float>(l.noise.get(x * l.frequency, y * l.frequency,
                                                                   z * l.frequency));
            }
            return value;
        }

        float getSecond(double x, double z) const { return getSecond(x, 0.0, z); }

        float getSecond(double x, double y, double z) const {
            float value = 0.0f;
            for (size_t i = 1; i < layers_.size(); i += 2) {
                const Layer& l = layers_[i];
                value += l.amplitude * static_cast<float>(l.noise.get(x * l.frequency, y * l.frequency,
                                                                   z * l.frequency));
            }
            return value;
        }

        // PerlinNoise.range() is symmetric +-2, so the summed range is
        // 2 * sum(|amplitude| * normalizationFactor). The density graph uses this
        // for its error bounds; the constant factor matches vanilla's Interval math.
        double rangeMin() const { return -rangeAbs(); }
        double rangeAbs() const {
            double total = 0.0;
            for (const OctaveInfo& o : octaves_) {
                total += 2.0 * std::fabs(normalizationFactor_ * o.amplitude);
            }
            return total;
        }

        const Parameters& parameters() const { return params_; }
        const std::vector<OctaveInfo>& octaves() const { return octaves_; }
        double normalizationFactor() const { return normalizationFactor_; }

        // The frequency multiplier applied to the second stack so the two halves
        // do not line up.
        static constexpr double kSecondStackScale = 1.0181268882175227;

        // Test-only introspection so parity harnesses can diff octave by octave.
        std::size_t layerCount() const { return layers_.size(); }
        double layerFrequency(std::size_t i) const { return layers_[i].frequency; }
        float layerAmplitude(std::size_t i) const { return layers_[i].amplitude; }
        float layerValue(std::size_t i, double x, double y, double z) const {
            const Layer& l = layers_[i];
            return l.amplitude * l.noise.get(x * l.frequency, y * l.frequency, z * l.frequency);
        }

    private:
        struct Layer {
            PerlinNoise noise;
            double frequency = 1.0;
            float amplitude = 0.0f;
        };

        float sample(double x, double y, double z) const {
            float value = 0.0f;
            for (const Layer& l : layers_) {
                value += l.amplitude * static_cast<float>(l.noise.get(x * l.frequency, y * l.frequency,
                                                                       z * l.frequency));
            }
            return value;
        }

        void build() {
            octaves_ = buildOctaves(params_.baseOctave, params_.baseAmplitude, params_.octaveCount,
                                    params_.normalize != Normalization::Disabled,
                                    params_.amplitudeModifiers);
            double targetAmplitude = 0.0;
            for (const OctaveInfo& o : octaves_) targetAmplitude += o.absAmplitude();
            double normalizationFactor = computeNormalizationFactor(targetAmplitude, octaves_);
            if (params_.normalize == Normalization::Legacy && normalizationFactor != 0.0) {
                const double parity = computeParityNormalizationFactor(
                    params_.baseAmplitude, params_.octaveCount, params_.amplitudeModifiers);
                targetAmplitude *= parity / normalizationFactor;
                normalizationFactor = parity;
            }
            normalizationFactor_ = normalizationFactor;
            (void)targetAmplitude;
        }

        static std::vector<OctaveInfo> buildOctaves(int baseOctave, double baseAmplitude, int octaveCount,
                                                    bool normalize,
                                                    const std::vector<double>& amplitudeModifiers) {
            double frequency = std::pow(2.0, static_cast<double>(baseOctave));
            double amplitude = baseAmplitude;
            if (normalize) {
                amplitude *= std::pow(0.5, -(octaveCount - 1)) /
                            (std::pow(0.5, -octaveCount) - 1.0);
            }
            std::vector<OctaveInfo> octaves;
            octaves.reserve(static_cast<size_t>(octaveCount));
            for (int i = 0; i < octaveCount; ++i) {
                const double modifier =
                    amplitudeModifiers.empty() ? 1.0 : amplitudeModifiers[static_cast<size_t>(i)];
                if (modifier != 0.0) {
                    octaves.push_back(OctaveInfo{baseOctave + i, frequency, amplitude * modifier});
                }
                frequency *= 2.0;
                amplitude *= 0.5;
            }
            return octaves;
        }

        static double getAmplitudeModifier(const std::vector<double>& mods, int index) {
            return mods.empty() ? 1.0 : mods[static_cast<size_t>(index)];
        }

        static double computeNormalizationFactor(double targetAmplitude,
                                                 const std::vector<OctaveInfo>& octaves) {
            const double inputDeviation = estimateDeviation(octaves);
            if (inputDeviation == 0.0) return 0.0;
            const double inputSumDeviation = inputDeviation * std::sqrt(2.0);
            const double targetDeviation = targetAmplitude * 0.3333333333333333;
            return targetDeviation / inputSumDeviation;
        }

        static double estimateDeviation(const std::vector<OctaveInfo>& octaves) {
            double variance = 0.0;
            for (const OctaveInfo& o : octaves) {
                const double layerDeviation = PerlinNoise::kStandardDeviation * o.absAmplitude();
                variance += layerDeviation * layerDeviation;
            }
            return std::sqrt(variance);
        }

        static double computeParityNormalizationFactor(double baseAmplitude, int octaveCount,
                                                        const std::vector<double>& mods) {
            int minOctave = INT32_MAX;
            int maxOctave = INT32_MIN;
            for (int i = 0; i < octaveCount; ++i) {
                if (getAmplitudeModifier(mods, i) != 0.0) {
                    if (i < minOctave) minOctave = i;
                    if (i > maxOctave) maxOctave = i;
                }
            }
            return baseAmplitude * 0.5 * 0.3333333333333333 / parityExpectedDeviation(maxOctave - minOctave);
        }

        static double parityExpectedDeviation(int octaveSpan) {
            return 0.1 * (1.0 + 1.0 / (octaveSpan + 1));
        }

        Parameters params_;
        std::vector<OctaveInfo> octaves_;
        std::vector<Layer> layers_;
        double normalizationFactor_ = 0.0;
    };

}
