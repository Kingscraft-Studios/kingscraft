#pragma once

#include "Device.hpp"
#include <vector>

namespace kc {

    class DescriptorManager {
    public:
        explicit DescriptorManager(Device& device);
        ~DescriptorManager() = default;

        DescriptorManager(const DescriptorManager&) = delete;
        DescriptorManager& operator=(const DescriptorManager&) = delete;

        VkDescriptorSetLayout createLayout(const std::vector<VkDescriptorSetLayoutBinding>& bindings);
        void destroyLayout(VkDescriptorSetLayout layout);

        VkDescriptorPool createPool(const std::vector<VkDescriptorPoolSize>& poolSizes, uint32_t maxSets);
        void destroyPool(VkDescriptorPool pool);

        VkDescriptorSet allocateSet(VkDescriptorPool pool, VkDescriptorSetLayout layout);

        void writeImageDescriptor(VkDescriptorSet set, VkImageView imageView, VkSampler sampler,
                                  VkImageLayout layout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                                  VkDescriptorType type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
                                  uint32_t binding = 0);

        void updateDescriptorSets(const std::vector<VkWriteDescriptorSet>& descriptorWrites);

        void freeDescriptorSets(VkDescriptorPool pool, const std::vector<VkDescriptorSet>& sets);

    private:
        Device& device_;
    };

} // namespace kc
