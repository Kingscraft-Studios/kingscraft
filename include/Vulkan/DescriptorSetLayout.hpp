#pragma once

#include "Device.hpp"
#include <memory>

namespace lve {

    class DescriptorSetLayout {
    public:
        DescriptorSetLayout(Device& device, const VkDescriptorSetLayoutCreateInfo& info);
        ~DescriptorSetLayout();

        static std::unique_ptr<DescriptorSetLayout> adopt(Device& device, VkDescriptorSetLayout layout);

        DescriptorSetLayout(const DescriptorSetLayout&) = delete;
        DescriptorSetLayout& operator=(const DescriptorSetLayout&) = delete;

        DescriptorSetLayout(DescriptorSetLayout&& other) noexcept;
        DescriptorSetLayout& operator=(DescriptorSetLayout&& other) noexcept;

        VkDescriptorSetLayout getHandle() const { return layout_; }
        void reset();

    private:
        explicit DescriptorSetLayout(Device& device);

        Device& device_;
        VkDescriptorSetLayout layout_ = VK_NULL_HANDLE;
    };

} // namespace lve