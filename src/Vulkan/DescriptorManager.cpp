#include "Vulkan/DescriptorManager.hpp"

#include <iostream>
#include <stdexcept>

namespace lve {

    DescriptorManager::DescriptorManager(Device& device)
        : device_(device) {}

    VkDescriptorSetLayout DescriptorManager::createLayout(
        const std::vector<VkDescriptorSetLayoutBinding>& bindings) {
        VkDescriptorSetLayoutCreateInfo layoutInfo{};
        layoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
        layoutInfo.bindingCount = static_cast<uint32_t>(bindings.size());
        layoutInfo.pBindings = bindings.data();

        VkDescriptorSetLayout layout;
        if (vkCreateDescriptorSetLayout(device_.device(), &layoutInfo, nullptr, &layout) != VK_SUCCESS) {
            throw std::runtime_error("failed to create descriptor set layout!");
        }
        return layout;
    }

    void DescriptorManager::destroyLayout(VkDescriptorSetLayout layout) {
        vkDestroyDescriptorSetLayout(device_.device(), layout, nullptr);
    }

    VkDescriptorPool DescriptorManager::createPool(
        const std::vector<VkDescriptorPoolSize>& poolSizes, uint32_t maxSets) {
        VkDescriptorPoolCreateInfo poolInfo{};
        poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
        poolInfo.flags = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT;
        poolInfo.poolSizeCount = static_cast<uint32_t>(poolSizes.size());
        poolInfo.pPoolSizes = poolSizes.data();
        poolInfo.maxSets = maxSets;

        VkDescriptorPool pool;
        if (vkCreateDescriptorPool(device_.device(), &poolInfo, nullptr, &pool) != VK_SUCCESS) {
            throw std::runtime_error("failed to create descriptor pool!");
        }
        return pool;
    }

    void DescriptorManager::destroyPool(VkDescriptorPool pool) {
        vkDestroyDescriptorPool(device_.device(), pool, nullptr);
    }

    VkDescriptorSet DescriptorManager::allocateSet(VkDescriptorPool pool, VkDescriptorSetLayout layout) {
        VkDescriptorSetAllocateInfo allocInfo{};
        allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
        allocInfo.descriptorPool = pool;
        allocInfo.descriptorSetCount = 1;
        allocInfo.pSetLayouts = &layout;

        VkDescriptorSet set;
        if (vkAllocateDescriptorSets(device_.device(), &allocInfo, &set) != VK_SUCCESS) {
            throw std::runtime_error("failed to allocate descriptor set!");
        }
        return set;
    }

    void DescriptorManager::writeImageDescriptor(
        VkDescriptorSet set, VkImageView imageView, VkSampler sampler,
        VkImageLayout layout, VkDescriptorType type, uint32_t binding) {

        VkDescriptorImageInfo imageInfo{};
        imageInfo.imageLayout = layout;
        imageInfo.imageView = imageView;
        imageInfo.sampler = sampler;

        VkWriteDescriptorSet descriptorWrite{};
        descriptorWrite.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        descriptorWrite.dstSet = set;
        descriptorWrite.dstBinding = binding;
        descriptorWrite.dstArrayElement = 0;
        descriptorWrite.descriptorType = type;
        descriptorWrite.descriptorCount = 1;
        descriptorWrite.pImageInfo = &imageInfo;

        vkUpdateDescriptorSets(device_.device(), 1, &descriptorWrite, 0, nullptr);
    }

    void DescriptorManager::updateDescriptorSets(
        const std::vector<VkWriteDescriptorSet>& descriptorWrites) {
        vkUpdateDescriptorSets(
            device_.device(),
            static_cast<uint32_t>(descriptorWrites.size()),
            descriptorWrites.data(),
            0, nullptr);
    }

    void DescriptorManager::freeDescriptorSets(
        VkDescriptorPool pool, const std::vector<VkDescriptorSet>& sets) {
        vkFreeDescriptorSets(
            device_.device(),
            pool,
            static_cast<uint32_t>(sets.size()),
            sets.data());
    }

} // namespace lve
