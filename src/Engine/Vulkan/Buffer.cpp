#include "Vulkan/Buffer.hpp"
#include <cstring>
#include <stdexcept>

namespace kc {

    Buffer::Buffer(
        Device& device,
        VkDeviceSize size,
        VkBufferUsageFlags usage,
        VkMemoryPropertyFlags properties)
        : device_(device), size_(size), usage_(usage), properties_(properties) {

        VkBufferCreateInfo bufferInfo{};
        bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
        bufferInfo.size = size_;
        bufferInfo.usage = usage_;
        bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

        if (vkCreateBuffer(device_.device(), &bufferInfo, nullptr, &buffer_) != VK_SUCCESS) {
            throw std::runtime_error("failed to create buffer!");
        }

        VkMemoryRequirements memRequirements;
        vkGetBufferMemoryRequirements(device_.device(), buffer_, &memRequirements);

        VkMemoryAllocateInfo allocInfo{};
        allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
        allocInfo.allocationSize = memRequirements.size;
        allocInfo.memoryTypeIndex = device_.findMemoryType(memRequirements.memoryTypeBits, properties_);

        if (vkAllocateMemory(device_.device(), &allocInfo, nullptr, &memory_) != VK_SUCCESS) {
            throw std::runtime_error("failed to allocate buffer memory!");
        }

        vkBindBufferMemory(device_.device(), buffer_, memory_, 0);
    }

    Buffer::~Buffer() {
        if (mapped_) unmap();
        if (buffer_ != VK_NULL_HANDLE) {
            vkDestroyBuffer(device_.device(), buffer_, nullptr);
        }
        if (memory_ != VK_NULL_HANDLE) {
            vkFreeMemory(device_.device(), memory_, nullptr);
        }
    }

    void* Buffer::map() {
        if (mapped_) return mapped_;
        vkMapMemory(device_.device(), memory_, 0, size_, 0, &mapped_);
        return mapped_;
    }

    void Buffer::unmap() {
        if (mapped_) {
            vkUnmapMemory(device_.device(), memory_);
            mapped_ = nullptr;
        }
    }

    void Buffer::flush(VkDeviceSize offset, VkDeviceSize size) {
        if (properties_ & VK_MEMORY_PROPERTY_HOST_COHERENT_BIT) return;
        VkMappedMemoryRange range{};
        range.sType = VK_STRUCTURE_TYPE_MAPPED_MEMORY_RANGE;
        range.memory = memory_;
        range.offset = offset;
        range.size = size;
        vkFlushMappedMemoryRanges(device_.device(), 1, &range);
    }

    void Buffer::write(const void* data, VkDeviceSize offset, VkDeviceSize size) {
        if (size == VK_WHOLE_SIZE) size = size_;
        void* dst = static_cast<char*>(map()) + offset;
        std::memcpy(dst, data, static_cast<size_t>(size));
        flush(offset, size);
    }

} // namespace kc
