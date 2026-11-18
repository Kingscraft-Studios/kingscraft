#include "Vulkan/ImageView.hpp"
#include <stdexcept>

namespace kc {

    ImageView::ImageView(Device& device, const VkImageViewCreateInfo& info)
        : device_(device) {
        if (vkCreateImageView(device_.device(), &info, nullptr, &view_) != VK_SUCCESS) {
            throw std::runtime_error("failed to create image view!");
        }
    }

    ImageView::ImageView(Device& device, VkImage image, VkFormat format, VkImageAspectFlags aspectFlags,
                         uint32_t mipLevels, uint32_t baseMipLevel,
                         VkImageViewType viewType, uint32_t layerCount)
        : device_(device) {
        VkImageViewCreateInfo info{};
        info.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        info.image = image;
        info.viewType = viewType;
        info.format = format;
        info.subresourceRange.aspectMask = aspectFlags;
        info.subresourceRange.baseMipLevel = baseMipLevel;
        info.subresourceRange.levelCount = mipLevels;
        info.subresourceRange.baseArrayLayer = 0;
        info.subresourceRange.layerCount = layerCount;
        if (vkCreateImageView(device_.device(), &info, nullptr, &view_) != VK_SUCCESS) {
            throw std::runtime_error("failed to create image view!");
        }
    }

    ImageView::~ImageView() {
        if (view_ != VK_NULL_HANDLE)
            vkDestroyImageView(device_.device(), view_, nullptr);
    }

    ImageView::ImageView(ImageView&& other) noexcept
        : device_(other.device_), view_(other.view_) {
        other.view_ = VK_NULL_HANDLE;
    }

    ImageView& ImageView::operator=(ImageView&& other) noexcept {
        if (this != &other) {
            if (view_ != VK_NULL_HANDLE)
                vkDestroyImageView(device_.device(), view_, nullptr);
            view_ = other.view_;
            other.view_ = VK_NULL_HANDLE;
        }
        return *this;
    }

    void ImageView::reset() {
        if (view_ != VK_NULL_HANDLE)
            vkDestroyImageView(device_.device(), view_, nullptr);
        view_ = VK_NULL_HANDLE;
    }

} // namespace kc