#pragma once

#include "Vulkan/Device.hpp"
#include <vulkan/vulkan.h>
#include <cstdint>
#include <memory>
#include <vector>

namespace kc {

class TextureCache {
public:
    explicit TextureCache(Device& device);
    ~TextureCache();

    TextureCache(const TextureCache&) = delete;
    TextureCache& operator=(const TextureCache&) = delete;

    // Rebuilds the block texture array. Never leaves the cache without a bound
    // texture: a layer that fails to read or decode becomes the global
    // missing-texture checkerboard, and a failure while creating GPU resources
    // keeps the previous atlas (or falls back to an all-checkerboard one).
    // Only device loss / out-of-memory escapes, and those are fatal.
    //
    // The descriptor set and layout handles are replaced, so the caller must
    // idle the device (nothing may still reference the old atlas) and rebuild
    // the world pipeline that was created from getLayout().
    void updateFromRegistry();

    VkDescriptorSetLayout getLayout() const;
    VkDescriptorSet getDescriptorSet() const;
    VkImageView getImageView() const;
    VkSampler getSampler() const;
    uint32_t getLayerCount() const;

private:
    struct Atlas;

    std::unique_ptr<Atlas> buildAtlas(const std::vector<std::vector<unsigned char>>& layers,
                                      int width, int height);

    Device& device_;
    std::unique_ptr<Atlas> atlas_;
};

} // namespace kc
