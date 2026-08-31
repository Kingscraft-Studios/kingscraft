#include "Vulkan/PipelineLayout.hpp"
#include <stdexcept>

namespace lve {

    PipelineLayout::PipelineLayout(Device& device, const VkPipelineLayoutCreateInfo& info)
        : device_(device) {
        if (vkCreatePipelineLayout(device_.device(), &info, nullptr, &layout_) != VK_SUCCESS) {
            throw std::runtime_error("failed to create pipeline layout!");
        }
    }

    PipelineLayout::~PipelineLayout() {
        if (layout_ != VK_NULL_HANDLE)
            vkDestroyPipelineLayout(device_.device(), layout_, nullptr);
    }

    PipelineLayout::PipelineLayout(PipelineLayout&& other) noexcept
        : device_(other.device_), layout_(other.layout_) {
        other.layout_ = VK_NULL_HANDLE;
    }

    PipelineLayout& PipelineLayout::operator=(PipelineLayout&& other) noexcept {
        if (this != &other) {
            if (layout_ != VK_NULL_HANDLE)
                vkDestroyPipelineLayout(device_.device(), layout_, nullptr);
            layout_ = other.layout_;
            other.layout_ = VK_NULL_HANDLE;
        }
        return *this;
    }

    void PipelineLayout::reset() {
        if (layout_ != VK_NULL_HANDLE)
            vkDestroyPipelineLayout(device_.device(), layout_, nullptr);
        layout_ = VK_NULL_HANDLE;
    }

} // namespace lve