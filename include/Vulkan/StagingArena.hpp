#pragma once

#include <vulkan/vulkan.h>
#include <cstdint>

namespace kc {

    class Device;

    struct StagingAllocation {
        VkBuffer buffer;
        VkDeviceSize offset;
        void* data;
    };

    class StagingArena {
    public:
        static constexpr int SLOT_COUNT = 4;
        static constexpr VkDeviceSize SLOT_SIZE = 8 * 1024 * 1024;

        StagingArena(Device& device, VkDeviceSize slotSize = SLOT_SIZE);
        ~StagingArena();

        StagingAllocation alloc(VkDeviceSize size);

        void advanceFrame();

    private:
        Device& device_;
        VkDeviceSize slotSize_;
        VkBuffer buffer_ = VK_NULL_HANDLE;
        VkDeviceMemory memory_ = VK_NULL_HANDLE;
        void* mapped_ = nullptr;
        VkDeviceSize slotOffset_[SLOT_COUNT] = {};
        uint64_t frameIndex_ = 0;
    };

} // namespace kc
