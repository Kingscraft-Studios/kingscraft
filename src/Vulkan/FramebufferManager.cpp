#include "Vulkan/FramebufferManager.hpp"
#include <stdexcept>

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
        framebuffers_.resize(imageViews.size());

        for (size_t i = 0; i < imageViews.size(); i++) {
            VkFramebufferCreateInfo framebufferInfo{};
            framebufferInfo.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
            framebufferInfo.renderPass = renderPass;
            framebufferInfo.attachmentCount = 1;
            framebufferInfo.pAttachments = &imageViews[i];
            framebufferInfo.width = extent.width;
            framebufferInfo.height = extent.height;
            framebufferInfo.layers = 1;

            if (vkCreateFramebuffer(
                    device_.device(),
                    &framebufferInfo,
                    nullptr,
                    &framebuffers_[i]) != VK_SUCCESS) {
                throw std::runtime_error("failed to create framebuffer!");
            }
        }
    }

    void FramebufferManager::destroyFramebuffers() {
        for (auto framebuffer : framebuffers_) {
            vkDestroyFramebuffer(device_.device(), framebuffer, nullptr);
        }
        framebuffers_.clear();
    }

} // namespace kc
