#pragma once

#include "Vulkan/Device.hpp"
#include "Vulkan/Image.hpp"
#include "Vulkan/ImageView.hpp"
#include "Vulkan/Sampler.hpp"
#include "Vulkan/DescriptorPool.hpp"
#include "Vulkan/DescriptorSetLayout.hpp"
#include <vulkan/vulkan.h>
#include <memory>
#include <cstdint>

namespace kc {

class TextureCache {
public:
    explicit TextureCache(Device& device);
    ~TextureCache();

    TextureCache(const TextureCache&) = delete;
    TextureCache& operator=(const TextureCache&) = delete;

    void updateFromRegistry();

    VkDescriptorSetLayout getLayout() const { return descriptorSetLayout_ ? descriptorSetLayout_->getHandle() : VK_NULL_HANDLE; }
    VkDescriptorSet getDescriptorSet() const { return descriptorSet_; }
    VkImageView getImageView() const { return imageView_ ? imageView_->getHandle() : VK_NULL_HANDLE; }
    VkSampler getSampler() const { return sampler_ ? sampler_->getHandle() : VK_NULL_HANDLE; }
    uint32_t getLayerCount() const { return layerCount_; }

private:
    void cleanup();

    Device& device_;

    std::unique_ptr<Image> image_;
    std::unique_ptr<ImageView> imageView_;
    std::unique_ptr<Sampler> sampler_;
    std::unique_ptr<DescriptorPool> descriptorPool_;
    std::unique_ptr<DescriptorSetLayout> descriptorSetLayout_;
    VkDescriptorSet descriptorSet_ = VK_NULL_HANDLE;
    uint32_t layerCount_ = 0;
};

} // namespace kc
