#include "Vulkan/FramebufferManager.hpp"

namespace kc {

    FramebufferManager::FramebufferManager(Device& device)
        : device_(device) {}

    FramebufferManager::~FramebufferManager() {
        destroyFramebuffers();
    }

    void FramebufferManager::createFramebuffers(
        const std::vector<VkImageView>& imageViews,
        VkRenderPass renderPass,
        VkExtent2D extent) {

        destroyFramebuffers();
        framebuffers_.reserve(imageViews.size());

        for (size_t i = 0; i < imageViews.size(); i++) {
            VkFramebufferCreateInfo framebufferInfo{};
            framebufferInfo.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
            framebufferInfo.renderPass = renderPass;
            framebufferInfo.attachmentCount = 1;
            framebufferInfo.pAttachments = &imageViews[i];
            framebufferInfo.width = extent.width;
            framebufferInfo.height = extent.height;
            framebufferInfo.layers = 1;

            framebuffers_.push_back(std::make_unique<Framebuffer>(device_, framebufferInfo));
        }
    }

    void FramebufferManager::destroyFramebuffers() {
        framebuffers_.clear();
    }

} // namespace kc
