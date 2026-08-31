#pragma once

#include "Device.hpp"
#include "Semaphore.hpp"
#include "Fence.hpp"
#include <vector>

#include "Core/Constants.hpp"

namespace lve {

    class SyncObjects {
    public:
        static constexpr uint32_t MAX_FRAMES_IN_FLIGHT = lve::MAX_FRAMES_IN_FLIGHT;

        SyncObjects(Device& device, uint32_t swapchainImageCount);
        ~SyncObjects();

        SyncObjects(const SyncObjects&) = delete;
        SyncObjects& operator=(const SyncObjects&) = delete;

        VkSemaphore getImageAvailable(uint32_t frameIndex) const { return imageAvailable_[frameIndex]->getHandle(); }
        VkSemaphore getRenderFinished(uint32_t imageIndex) const { return renderFinished_[imageIndex]->getHandle(); }
        VkFence getInFlight(uint32_t frameIndex) const { return inFlightFences_[frameIndex]->getHandle(); }

        uint32_t getFrameCount() const { return MAX_FRAMES_IN_FLIGHT; }

    private:
        Device& device_;
        std::vector<std::unique_ptr<Semaphore>> imageAvailable_;
        std::vector<std::unique_ptr<Semaphore>> renderFinished_;
        std::vector<std::unique_ptr<Fence>> inFlightFences_;
    };

} // namespace lve
