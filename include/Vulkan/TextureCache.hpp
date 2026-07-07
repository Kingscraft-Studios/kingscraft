#pragma once

#include "Vulkan/Device.hpp"
#include <vulkan/vulkan.h>
#include <cstdint>

namespace lve {

class TextureCache {
public:
    explicit TextureCache(Device& device);
    ~TextureCache();

    TextureCache(const TextureCache&) = delete;
    TextureCache& operator=(const TextureCache&) = delete;

    void updateFromRegistry();

    VkDescriptorSetLayout getLayout() const { return descriptorSetLayout_; }
    VkDescriptorSet getDescriptorSet() const { return descriptorSet_; }
    VkImageView getImageView() const { return imageView_; }
    VkSampler getSampler() const { return sampler_; }
    uint32_t getLayerCount() const { return layerCount_; }

private:
    void cleanup();

    Device& device_;

    VkImage image_ = VK_NULL_HANDLE;
    VkDeviceMemory imageMemory_ = VK_NULL_HANDLE;
    VkImageView imageView_ = VK_NULL_HANDLE;
    VkSampler sampler_ = VK_NULL_HANDLE;
    VkDescriptorPool descriptorPool_ = VK_NULL_HANDLE;
    VkDescriptorSetLayout descriptorSetLayout_ = VK_NULL_HANDLE;
    VkDescriptorSet descriptorSet_ = VK_NULL_HANDLE;
    uint32_t layerCount_ = 0;
};

} // namespace lve
