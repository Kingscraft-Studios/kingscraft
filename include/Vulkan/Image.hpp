#pragma once

#include "Device.hpp"

namespace kc {

    class Image {
    public:
        Image(Device& device, const VkImageCreateInfo& info, VkMemoryPropertyFlags properties);
        ~Image();

        Image(const Image&) = delete;
        Image& operator=(const Image&) = delete;

        Image(Image&& other) noexcept;
        Image& operator=(Image&& other) noexcept;

        VkImage getHandle() const { return image_; }
        VkDeviceMemory getMemory() const { return memory_; }
        void reset();

    private:
        Device& device_;
        VkImage image_ = VK_NULL_HANDLE;
        VkDeviceMemory memory_ = VK_NULL_HANDLE;
    };

} // namespace kc