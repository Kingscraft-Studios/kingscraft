#include "Vulkan/StagingArena.hpp"
#include "Vulkan/Device.hpp"
#include <cassert>
#include <cstring>
#include <stdexcept>

namespace kc {

    static constexpr int RECYCLE_DELAY = 2;

    StagingArena::StagingArena(Device& device, VkDeviceSize slotSize)
        : device_(device), slotSize_(slotSize) {

        VkDeviceSize totalSize = SLOT_COUNT * slotSize_;

        VkBufferCreateInfo bufferInfo{};
        bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
        bufferInfo.size = totalSize;
        bufferInfo.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
        bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

        if (vkCreateBuffer(device_.device(), &bufferInfo, nullptr, &buffer_) != VK_SUCCESS) {
            throw std::runtime_error("failed to create staging arena buffer!");
        }

        VkMemoryRequirements memRequirements;
        vkGetBufferMemoryRequirements(device_.device(), buffer_, &memRequirements);

        VkMemoryAllocateInfo allocInfo{};
        allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
        allocInfo.allocationSize = memRequirements.size;
        allocInfo.memoryTypeIndex = device_.findMemoryType(
            memRequirements.memoryTypeBits,
            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);

        if (vkAllocateMemory(device_.device(), &allocInfo, nullptr, &memory_) != VK_SUCCESS) {
            vkDestroyBuffer(device_.device(), buffer_, nullptr);
            throw std::runtime_error("failed to allocate staging arena memory!");
        }

        vkBindBufferMemory(device_.device(), buffer_, memory_, 0);

        vkMapMemory(device_.device(), memory_, 0, VK_WHOLE_SIZE, 0, &mapped_);
    }

    StagingArena::~StagingArena() {
        if (mapped_) vkUnmapMemory(device_.device(), memory_);
        if (buffer_ != VK_NULL_HANDLE) vkDestroyBuffer(device_.device(), buffer_, nullptr);
        if (memory_ != VK_NULL_HANDLE) vkFreeMemory(device_.device(), memory_, nullptr);
    }

    StagingAllocation StagingArena::alloc(VkDeviceSize size) {
        int slot = frameIndex_ % SLOT_COUNT;
        VkDeviceSize offset = slotOffset_[slot];
        assert(offset + size <= slotSize_ && "Staging arena slot overflow!");
        slotOffset_[slot] = offset + size;
        return { buffer_, static_cast<VkDeviceSize>(slot) * slotSize_ + offset,
                 static_cast<char*>(mapped_) + slot * slotSize_ + offset };
    }

    void StagingArena::advanceFrame() {
        if (frameIndex_ >= RECYCLE_DELAY) {
            uint64_t recyclableSlot = (frameIndex_ - RECYCLE_DELAY) % SLOT_COUNT;
            slotOffset_[recyclableSlot] = 0;
        }
        frameIndex_++;
    }

} // namespace kc
