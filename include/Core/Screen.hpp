#pragma once

#include "Core/FrameContext.hpp"
#include <vector>
#include <vulkan/vulkan.h>

namespace lve {

    class Renderer;

    struct FrameRenderInfo {
        VkRenderPass renderPass = VK_NULL_HANDLE;
        VkFramebuffer framebuffer = VK_NULL_HANDLE;
        std::vector<VkClearValue> clearValues;
        bool uiEnabled = true;
    };


    class Screen {
    public:
        virtual ~Screen() = default;

        virtual void init() = 0;
        virtual void tick(double dt) = 0;
        virtual void render(const FrameContext& ctx) = 0;
        virtual void renderGlow(const FrameContext& ctx) {}
        virtual void cleanup() = 0;

        virtual FrameRenderInfo getFrameRenderInfo(const Renderer& renderer, uint32_t imageIndex) const = 0;

        virtual void onRenderPassChanged(VkRenderPass) {}
        virtual void onSwapChainRecreated(VkExtent2D extent) {}
    };

} // namespace lve
