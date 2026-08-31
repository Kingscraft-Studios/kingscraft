#pragma once

#include "Device.hpp"

// vulkan headers
#include "vulkan/vulkan.h"

// std lib headers
#include <memory>
#include <string>
#include <vector>

namespace kc {

    class SwapChain {
    public:
        SwapChain(Device &deviceRef, VkExtent2D windowExtent);

        SwapChain(Device &deviceRef, VkExtent2D windowExtent, std::shared_ptr<SwapChain> previous);

        ~SwapChain();

        SwapChain(const SwapChain &) = delete;

        SwapChain &operator=(const SwapChain &) = delete;

        VkSwapchainKHR getSwapChain() { return swapChain; }

        const std::vector<VkImageView>& getImageViews() const { return swapChainImageViews; }

        VkImageView getImageView(int index) { return swapChainImageViews[index]; }

        size_t imageCount() { return swapChainImages.size(); }

        VkFormat getSwapChainImageFormat() { return swapChainImageFormat; }

        VkExtent2D getSwapChainExtent() { return swapChainExtent; }

        uint32_t width() { return swapChainExtent.width; }

        uint32_t height() { return swapChainExtent.height; }

        float extentAspectRatio() {
            return static_cast<float>(swapChainExtent.width) / static_cast<float>(swapChainExtent.height);
        }

        VkResult acquireNextImage(uint32_t *imageIndex, VkSemaphore signalSemaphore);

    private:
        void init();

        void createSwapChain();

        void createImageViews();

        // Helper functions
        VkSurfaceFormatKHR chooseSwapSurfaceFormat(
                const std::vector<VkSurfaceFormatKHR> &availableFormats);

        VkPresentModeKHR chooseSwapPresentMode(
                const std::vector<VkPresentModeKHR> &availablePresentModes);

        VkExtent2D chooseSwapExtent(const VkSurfaceCapabilitiesKHR &capabilities);

        VkFormat swapChainImageFormat;
        VkExtent2D swapChainExtent;

        std::vector<VkImage> swapChainImages;
        std::vector<VkImageView> swapChainImageViews;

        Device &device;
        VkExtent2D windowExtent;

        VkSwapchainKHR swapChain;
        std::shared_ptr<SwapChain> oldSwapchain;
    };

}  // namespace kc
