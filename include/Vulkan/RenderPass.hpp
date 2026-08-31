#pragma once

#include "Device.hpp"
#include <memory>
#include <vector>

namespace kc {

    class RenderPass {
    public:
        struct AttachmentDescription {
            VkFormat format;
            VkSampleCountFlagBits samples = VK_SAMPLE_COUNT_1_BIT;
            VkAttachmentLoadOp loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
            VkAttachmentStoreOp storeOp = VK_ATTACHMENT_STORE_OP_STORE;
            VkAttachmentLoadOp stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
            VkAttachmentStoreOp stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
            VkImageLayout initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
            VkImageLayout finalLayout;
        };

        RenderPass(Device& device,
                   const std::vector<AttachmentDescription>& attachments,
                   const std::vector<VkSubpassDescription>& subpasses,
                   const std::vector<VkSubpassDependency>& dependencies);

        ~RenderPass();

        RenderPass(const RenderPass&) = delete;
        RenderPass& operator=(const RenderPass&) = delete;

        VkRenderPass getHandle() const { return renderPass_; }

        static std::unique_ptr<RenderPass> createDefault(Device& device, VkFormat colorFormat);
        static std::unique_ptr<RenderPass> createWithDepth(Device& device, VkFormat colorFormat, VkFormat depthFormat);
        static std::unique_ptr<RenderPass> createOffscreen(Device& device, VkFormat colorFormat);

    private:
        Device& device_;
        VkRenderPass renderPass_;
    };

} // namespace kc
