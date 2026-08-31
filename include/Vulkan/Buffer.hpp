#pragma once

#include "Device.hpp"
#include <vector>

namespace kc {

    class Buffer {
    public:
        Buffer(
            Device& device,
            VkDeviceSize size,
            VkBufferUsageFlags usage,
            VkMemoryPropertyFlags properties);
        ~Buffer();

        Buffer(const Buffer&) = delete;
        Buffer& operator=(const Buffer&) = delete;

        VkBuffer getHandle() const { return buffer_; }
        VkDeviceMemory getMemory() const { return memory_; }
        VkDeviceSize getSize() const { return size_; }

        void* map();
        void unmap();
        void flush(VkDeviceSize offset = 0, VkDeviceSize size = VK_WHOLE_SIZE);
        void write(const void* data, VkDeviceSize offset = 0, VkDeviceSize size = VK_WHOLE_SIZE);

    private:
        Device& device_;
        VkBuffer buffer_ = VK_NULL_HANDLE;
        VkDeviceMemory memory_ = VK_NULL_HANDLE;
        VkDeviceSize size_;
        VkBufferUsageFlags usage_;
        VkMemoryPropertyFlags properties_;
        void* mapped_ = nullptr;
    };

} // namespace kc
