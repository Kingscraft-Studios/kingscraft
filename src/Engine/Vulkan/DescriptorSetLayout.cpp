#include "Vulkan/DescriptorSetLayout.hpp"
#include <stdexcept>

namespace kc {

    DescriptorSetLayout::DescriptorSetLayout(Device& device, const VkDescriptorSetLayoutCreateInfo& info)
        : device_(device) {
        if (vkCreateDescriptorSetLayout(device_.device(), &info, nullptr, &layout_) != VK_SUCCESS) {
            throw std::runtime_error("failed to create descriptor set layout!");
        }
    }

    DescriptorSetLayout::DescriptorSetLayout(Device& device)
        : device_(device) {}

    DescriptorSetLayout::~DescriptorSetLayout() {
        if (layout_ != VK_NULL_HANDLE)
            vkDestroyDescriptorSetLayout(device_.device(), layout_, nullptr);
    }

    std::unique_ptr<DescriptorSetLayout> DescriptorSetLayout::adopt(Device& device, VkDescriptorSetLayout layout) {
        std::unique_ptr<DescriptorSetLayout> result(new DescriptorSetLayout(device));
        result->layout_ = layout;
        return result;
    }

    DescriptorSetLayout::DescriptorSetLayout(DescriptorSetLayout&& other) noexcept
        : device_(other.device_), layout_(other.layout_) {
        other.layout_ = VK_NULL_HANDLE;
    }

    DescriptorSetLayout& DescriptorSetLayout::operator=(DescriptorSetLayout&& other) noexcept {
        if (this != &other) {
            if (layout_ != VK_NULL_HANDLE)
                vkDestroyDescriptorSetLayout(device_.device(), layout_, nullptr);
            layout_ = other.layout_;
            other.layout_ = VK_NULL_HANDLE;
        }
        return *this;
    }

    void DescriptorSetLayout::reset() {
        if (layout_ != VK_NULL_HANDLE)
            vkDestroyDescriptorSetLayout(device_.device(), layout_, nullptr);
        layout_ = VK_NULL_HANDLE;
    }

} // namespace kc