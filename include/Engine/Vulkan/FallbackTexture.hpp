#pragma once

#include "Device.hpp"
#include "Image.hpp"
#include "ImageView.hpp"
#include "Sampler.hpp"

#include <cstdint>
#include <memory>
#include <vector>

namespace kc {

    // The engine's single definition of a missing texture: a magenta/black
    // checkerboard, the Minecraft "missingno" look. Every texture failure path
    // uses these pixels, and this component owns the minimal one-layer stand-in
    // that is bound before the block atlas exists.
    class FallbackTexture {
    public:
        // Edge length used whenever no real texture size is known (atlas not
        // built yet, or every layer failed).
        static constexpr int SIZE = 16;

        explicit FallbackTexture(Device& device);

        FallbackTexture(const FallbackTexture&) = delete;
        FallbackTexture& operator=(const FallbackTexture&) = delete;

        VkImageView getView() const { return view_->getHandle(); }
        VkSampler getSampler() const { return sampler_->getHandle(); }

        // Tightly packed RGBA8 pixels, `w * h * 4` bytes, magenta on black with
        // a 2x2 cell split. Returns empty for a non-positive size.
        static std::vector<unsigned char> checkerboard(int w, int h);

        // Nearest + clamp sampler matching an image with `mipLevels` levels.
        static VkSamplerCreateInfo samplerInfo(uint32_t mipLevels);

    private:
        std::unique_ptr<Image> image_;
        std::unique_ptr<ImageView> view_;
        std::unique_ptr<Sampler> sampler_;
    };

} // namespace kc
