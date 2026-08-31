#pragma once

#include "Device.hpp"
#include <memory>

namespace kc {

    class DescriptorPool {
    public:
        DescriptorPool(Device& device, const VkDescriptorPoolCreateInfo& info);
        ~DescriptorPool();

        static std::unique_ptr<DescriptorPool> adopt(Device& device, VkDescriptorPool pool);

        DescriptorPool(const DescriptorPool&) = delete;
        DescriptorPool& operator=(const DescriptorPool&) = delete;

        DescriptorPool(DescriptorPool&& other) noexcept;
        DescriptorPool& operator=(DescriptorPool&& other) noexcept;

        VkDescriptorPool getHandle() const { return pool_; }
        void reset();

    private:
        explicit DescriptorPool(Device& device);

        Device& device_;
        VkDescriptorPool pool_ = VK_NULL_HANDLE;
    };

} // namespace kc