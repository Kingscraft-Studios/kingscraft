#include "Vulkan/Image.hpp"

namespace kc {

    Image::Image(Device& device, const VkImageCreateInfo& info, VkMemoryPropertyFlags properties)
        : device_(device) {
        device_.createImageWithInfo(info, properties, image_, memory_);
    }

    Image::~Image() {
        if (image_ != VK_NULL_HANDLE || memory_ != VK_NULL_HANDLE) {
            vkDestroyImage(device_.device(), image_, nullptr);
            vkFreeMemory(device_.device(), memory_, nullptr);
        }
    }

    Image::Image(Image&& other) noexcept
        : device_(other.device_), image_(other.image_), memory_(other.memory_) {
        other.image_ = VK_NULL_HANDLE;
        other.memory_ = VK_NULL_HANDLE;
    }

    Image& Image::operator=(Image&& other) noexcept {
        if (this != &other) {
            if (image_ != VK_NULL_HANDLE || memory_ != VK_NULL_HANDLE) {
                vkDestroyImage(device_.device(), image_, nullptr);
                vkFreeMemory(device_.device(), memory_, nullptr);
            }
            image_ = other.image_;
            memory_ = other.memory_;
            other.image_ = VK_NULL_HANDLE;
            other.memory_ = VK_NULL_HANDLE;
        }
        return *this;
    }

    void Image::reset() {
        if (image_ != VK_NULL_HANDLE || memory_ != VK_NULL_HANDLE) {
            vkDestroyImage(device_.device(), image_, nullptr);
            vkFreeMemory(device_.device(), memory_, nullptr);
        }
        image_ = VK_NULL_HANDLE;
        memory_ = VK_NULL_HANDLE;
    }

} // namespace kc