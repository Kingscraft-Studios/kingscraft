#pragma once

#include "StagingArena.hpp"

// std lib headers
#include <memory>
#include <string>
#include <vector>

namespace kc {

    struct SwapChainSupportDetails {
        VkSurfaceCapabilitiesKHR capabilities;
        std::vector<VkSurfaceFormatKHR> formats;
        std::vector<VkPresentModeKHR> presentModes;
    };

    struct QueueFamilyIndices {
        uint32_t graphicsFamily;
        uint32_t presentFamily;
        bool graphicsFamilyHasValue = false;
        bool presentFamilyHasValue = false;

        bool isComplete() { return graphicsFamilyHasValue && presentFamilyHasValue; }
    };

    class Device {
    public:
#ifdef NDEBUG
        const bool enableValidationLayers = false;
#else
        const bool enableValidationLayers = false;
#endif

        Device();

        ~Device();

        // Not copyable or movable
        Device(const Device &) = delete;

        Device &operator=(const Device &) = delete;

        Device(Device &&) = delete;

        Device &operator=(Device &&) = delete;

        VkCommandPool getCommandPool() { return commandPool; }
        VkPhysicalDevice getPhysicalDevice() { return physicalDevice; }

        void createImage(
                uint32_t width,
                uint32_t height,
                VkFormat format,
                VkImageTiling tiling,
                VkImageUsageFlags usage,
                VkMemoryPropertyFlags properties,
                VkImage &image,
                VkDeviceMemory &imageMemory,
                uint32_t mipLevels = 1,
                uint32_t arrayLayers = 1);

        VkImageView createImageView(
                VkImage image,
                VkFormat format,
                VkImageAspectFlags aspectFlags,
                uint32_t mipLevels = 1,
                uint32_t baseMipLevel = 0,
                VkImageViewType viewType = VK_IMAGE_VIEW_TYPE_2D,
                uint32_t layerCount = 1);

        void transitionImageLayout(
                VkImage image,
                VkFormat format,
                VkImageLayout oldLayout,
                VkImageLayout newLayout,
                uint32_t layerCount = 1,
                uint32_t mipLevels = 1,
                uint32_t baseMipLevel = 0);

        VkDevice device() { return device_; }

        VkSurfaceKHR surface() { return surface_; }

        VkQueue graphicsQueue() { return graphicsQueue_; }

        VkQueue presentQueue() { return presentQueue_; }

        SwapChainSupportDetails getSwapChainSupport() { return querySwapChainSupport(physicalDevice); }

        uint32_t findMemoryType(uint32_t typeFilter, VkMemoryPropertyFlags properties);

        QueueFamilyIndices findPhysicalQueueFamilies() { return findQueueFamilies(physicalDevice); }

        VkFormat findSupportedFormat(
                const std::vector<VkFormat> &candidates, VkImageTiling tiling, VkFormatFeatureFlags features);

        // Buffer Helper Functions
        void createBuffer(
                VkDeviceSize size,
                VkBufferUsageFlags usage,
                VkMemoryPropertyFlags properties,
                VkBuffer &buffer,
                VkDeviceMemory &bufferMemory);

        VkCommandBuffer beginSingleTimeCommands();

        void endSingleTimeCommands(VkCommandBuffer commandBuffer);

        void copyBuffer(VkBuffer srcBuffer, VkBuffer dstBuffer, VkDeviceSize size);

        void submitAsync(VkCommandBuffer cmd, VkFence fence);

        void copyBufferToImage(
                VkBuffer buffer, VkImage image, uint32_t width, uint32_t height, uint32_t layerCount);

        void createImageWithInfo(
                const VkImageCreateInfo &imageInfo,
                VkMemoryPropertyFlags properties,
                VkImage &image,
                VkDeviceMemory &imageMemory);

        VkPhysicalDeviceProperties properties{};
        VkPhysicalDeviceDriverProperties driverProperties{};
        VkInstance getInstance() { return instance; }

        StagingArena& getStagingArena() { return *stagingArena_; }
        VkPipelineCache getPipelineCache() const { return pipelineCache_; }

    private:
        void createInstance();

        void setupDebugMessenger();

        void createSurface();

        void pickPhysicalDevice();

        void createLogicalDevice();

        void createCommandPool();

        // helper functions
        bool isDeviceSuitable(VkPhysicalDevice device);

        std::vector<const char *> getRequiredExtensions();

        bool checkValidationLayerSupport();

        QueueFamilyIndices findQueueFamilies(VkPhysicalDevice device);

        void populateDebugMessengerCreateInfo(VkDebugUtilsMessengerCreateInfoEXT &createInfo);

        void hasGflwRequiredInstanceExtensions();

        bool checkDeviceExtensionSupport(VkPhysicalDevice device);

        SwapChainSupportDetails querySwapChainSupport(VkPhysicalDevice device);

        void writePipelineCache();

        static const char* driverIdToString(VkDriverId id) {
            switch (id) {
                case VK_DRIVER_ID_AMD_PROPRIETARY:          return "AMD Proprietary";
                case VK_DRIVER_ID_AMD_OPEN_SOURCE:          return "AMD Open Source";
                case VK_DRIVER_ID_MESA_RADV:                return "Mesa RADV";
                case VK_DRIVER_ID_NVIDIA_PROPRIETARY:       return "NVIDIA";
                case VK_DRIVER_ID_INTEL_PROPRIETARY_WINDOWS:return "Intel (Windows)";
                case VK_DRIVER_ID_INTEL_OPEN_SOURCE_MESA:   return "Intel Mesa";
                case VK_DRIVER_ID_MESA_LLVMPIPE:            return "Mesa LLVMpipe";
                case VK_DRIVER_ID_MESA_V3DV:                return "Mesa V3DV";
                case VK_DRIVER_ID_MESA_TURNIP:              return "Mesa Turnip";
                default:                                    return "Unknown";
            }
        }

        VkInstance instance;
        VkDebugUtilsMessengerEXT debugMessenger;
        VkPhysicalDevice physicalDevice = VK_NULL_HANDLE;
        VkCommandPool commandPool;

        VkDevice device_;
        VkSurfaceKHR surface_;
        VkQueue graphicsQueue_;
        VkQueue presentQueue_;

        const std::vector<const char *> validationLayers = {"VK_LAYER_KHRONOS_validation"};
        const std::vector<const char *> deviceExtensions = {
            VK_KHR_SWAPCHAIN_EXTENSION_NAME,
            VK_KHR_DRIVER_PROPERTIES_EXTENSION_NAME
        };

        std::unique_ptr<StagingArena> stagingArena_;
        VkPipelineCache pipelineCache_ = VK_NULL_HANDLE;
    };

}  // namespace kc
