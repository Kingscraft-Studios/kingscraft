#pragma once

#include "Device.hpp"

namespace kc {

    class ImageView {
    public:
        ImageView(Device& device, const VkImageViewCreateInfo& info);

        ImageView(Device& device, VkImage image, VkFormat format, VkImageAspectFlags aspectFlags,
                  uint32_t mipLevels = 1, uint32_t baseMipLevel = 0,
                  VkImageViewType viewType = VK_IMAGE_VIEW_TYPE_2D, uint32_t layerCount = 1);

        ~ImageView();

        ImageView(const ImageView&) = delete;
        ImageView& operator=(const ImageView&) = delete;

        ImageView(ImageView&& other) noexcept;
        ImageView& operator=(ImageView&& other) noexcept;

        VkImageView getHandle() const { return view_; }
        void reset();

    private:
        Device& device_;
        VkImageView view_ = VK_NULL_HANDLE;
    };

} // namespace kc