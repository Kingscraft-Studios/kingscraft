#include "Vulkan/SyncObjects.hpp"
#include <stdexcept>

namespace kc {

    SyncObjects::SyncObjects(Device& device, uint32_t swapchainImageCount)
        : device_(device) {

        imageAvailable_.resize(MAX_FRAMES_IN_FLIGHT);
        renderFinished_.resize(swapchainImageCount);
        inFlightFences_.resize(MAX_FRAMES_IN_FLIGHT);

        for (size_t i = 0; i < MAX_FRAMES_IN_FLIGHT; i++) {
            imageAvailable_[i] = std::make_unique<Semaphore>(device_);
            inFlightFences_[i] = std::make_unique<Fence>(device_, VK_FENCE_CREATE_SIGNALED_BIT);
        }

        for (size_t i = 0; i < swapchainImageCount; i++) {
            renderFinished_[i] = std::make_unique<Semaphore>(device_);
        }
    }

    SyncObjects::~SyncObjects() {
        imageAvailable_.clear();
        renderFinished_.clear();
        inFlightFences_.clear();
    }

} // namespace kc