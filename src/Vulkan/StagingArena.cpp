#include "Vulkan/StagingArena.hpp"
#include "Vulkan/Buffer.hpp"
#include "Vulkan/Device.hpp"
#include <cassert>

namespace kc {

    static constexpr int RECYCLE_DELAY = 2;

    StagingArena::StagingArena(Device& device, VkDeviceSize slotSize)
        : device_(device), slotSize_(slotSize) {

        buffer_ = std::make_unique<Buffer>(
            device_,
            SLOT_COUNT * slotSize_,
            VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);

        // Mapped for the arena's whole lifetime; every allocation writes through
        // this pointer.
        mapped_ = buffer_->map();
    }

    StagingArena::~StagingArena() = default;

    StagingAllocation StagingArena::alloc(VkDeviceSize size) {
        int slot = frameIndex_ % SLOT_COUNT;
        VkDeviceSize offset = slotOffset_[slot];
        assert(offset + size <= slotSize_ && "Staging arena slot overflow!");
        slotOffset_[slot] = offset + size;
        return { buffer_->getHandle(), static_cast<VkDeviceSize>(slot) * slotSize_ + offset,
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
