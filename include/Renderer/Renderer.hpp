#pragma once

#include "Vulkan/Device.hpp"
#include "Vulkan/Swapchain.hpp"
#include "Vulkan/SyncObjects.hpp"
#include "Vulkan/FramebufferManager.hpp"
#include "Vulkan/RenderPass.hpp"
#include <memory>
#include <vector>
#include <functional>
#include <array>

#include "FrameScene.hpp"

namespace lve {

struct RenderPassBegin {
    VkRenderPass renderPass = VK_NULL_HANDLE;
    VkFramebuffer framebuffer = VK_NULL_HANDLE;
    VkRect2D renderArea{};
    VkViewport viewport{};
    VkRect2D scissor{};

    std::array<VkClearValue, 2> clearValues{};
    uint32_t clearCount = 0;
};

    struct RenderTarget {
        VkRenderPass renderPass;
        VkFramebuffer framebuffer;
        std::array<VkClearValue, 2> clearValues;
        uint32_t clearCount;
    };

class Renderer {
public:
    static constexpr uint32_t QUERIES_PER_FRAME = 6;
    static constexpr uint32_t PIPELINE_STATS_PER_FRAME = 1;

    enum GpuTsSlot : uint32_t {
        TS_FRAME_START = 0,
        TS_FRAME_END   = 1,
        TS_WORLD_START = 2,
        TS_WORLD_END   = 3,
        TS_UI_START    = 4,
        TS_UI_END      = 5,
    };

    enum PipeStatsIdx : uint32_t {
        STAT_IA_VERTICES    = 0,
        STAT_IA_PRIMITIVES  = 1,
        STAT_VS_INVOCATIONS = 2,
        STAT_CLIP_INVOC     = 3,
        STAT_CLIP_PRIMS     = 4,
        STAT_FS_INVOCATIONS = 5,
        STAT_COUNT          = 6,
    };

    Renderer(Device& device, VkExtent2D initialExtent);
    ~Renderer();

    Renderer(const Renderer&) = delete;
    Renderer& operator=(const Renderer&) = delete;

    void recreateSwapChain(VkExtent2D newExtent);

    bool beginFrame();
    void executeRenderPass(
        const RenderPassBegin& passBegin,
        const std::function<void(VkCommandBuffer)>& drawCommands);
    bool endFrame();

    VkCommandBuffer getActiveCommandBuffer() const { return commandBuffers_[currentImageIndex_]; }
    VkExtent2D getExtent() const { return swapchain_->getSwapChainExtent(); }
    VkFramebuffer getCurrentFramebuffer() const { return framebufferManager_->getFramebuffer(currentImageIndex_); }
    uint32_t getCurrentImageIndex() const { return currentImageIndex_; }
    Device& getDevice() { return device_; }

    VkFormat getSwapChainImageFormat() const { return swapchain_->getSwapChainImageFormat(); }
    uint32_t getSwapChainImageCount() const { return static_cast<uint32_t>(swapchain_->imageCount()); }
    const std::vector<VkImageView>& getSwapChainImageViews() const { return swapchain_->getImageViews(); }
    SwapChain& getSwapChain() { return *swapchain_; }

    VkRenderPass getRenderPass() const { return presentRenderPass_->getHandle(); }
    uint32_t getFrameIndex() const { return currentFrame_; }

    VkRenderPass getWorldRenderPass() const { return worldRenderPass_->getHandle(); }
    VkFramebuffer getWorldFramebuffer(uint32_t imageIndex) const { return worldFramebuffers_[imageIndex]; }
    VkFormat getDepthFormat() const { return depthFormat_; }

    double getGpuFrameTimeMs() const { return gpuFrameTimeMs_; }
    double getWorldGpuMs() const { return worldGpuMs_; }
    double getUiGpuMs() const { return uiGpuMs_; }
    double getCmdRecordMs() const { return cmdRecordMs_; }

    uint64_t getPipelineStat(PipeStatsIdx idx) const { return pipelineStats_[idx]; }
    double getMemBandwidthGBs() const { return memBandwidthGBs_; }
    double getOverdraw() const { return overdraw_; }
    double getCIdleMs() const { return cIdleMs_; }

    VkQueryPool getGpuQueryPool() const { return gpuQueryPool_; }
    VkQueryPool getPipelineStatsPool() const { return pipelineStatsPool_; }

    RenderTarget buildRenderTarget(const FrameScene& scene) const {
        if (scene.terrain.renderTerrain)
        {
            return {
                .renderPass = worldRenderPass_->getHandle(),
                .framebuffer = worldFramebuffers_[currentImageIndex_],
                .clearValues = {
                    VkClearValue{
                        .color = {{0.4f, 0.6f, 0.9f, 1.0f}}
                    },
                    VkClearValue{
                        .depthStencil = {1.0f, 0}
                    }
                },
                .clearCount = 2
            };
        }

        return {
            .renderPass = presentRenderPass_->getHandle(),
            .framebuffer = framebufferManager_->getFramebuffer(currentImageIndex_),
            .clearValues = {
                VkClearValue{
                    .color = {{0.1f, 0.1f, 0.1f, 1.0f}}
                }
            },
            .clearCount = 1
        };
    }

private:
    void createCommandBuffers();
    void createWorldResources();
    void destroyWorldResources();
    void createQueryPool();
    void createPipelineStatsPool();

    Device& device_;
    VkExtent2D extent_;
    std::unique_ptr<SwapChain> swapchain_;
    std::unique_ptr<SyncObjects> syncObjects_;
    std::unique_ptr<FramebufferManager> framebufferManager_;
    std::unique_ptr<RenderPass> presentRenderPass_;

    VkFormat depthFormat_ = VK_FORMAT_UNDEFINED;
    std::unique_ptr<RenderPass> worldRenderPass_;
    std::vector<VkImage> depthImages_;
    std::vector<VkDeviceMemory> depthImageMemories_;
    std::vector<VkImageView> depthImageViews_;
    std::vector<VkFramebuffer> worldFramebuffers_;

    std::vector<VkCommandBuffer> commandBuffers_;
    std::vector<VkFence> imagesInFlight_;
    uint32_t currentFrame_ = 0;
    uint32_t currentImageIndex_ = 0;

    VkQueryPool gpuQueryPool_ = VK_NULL_HANDLE;
    double timestampPeriod_ = 1.0;

    VkQueryPool pipelineStatsPool_ = VK_NULL_HANDLE;

    double gpuFrameTimeMs_ = 0.0;
    double worldGpuMs_ = 0.0;
    double uiGpuMs_ = 0.0;
    double cmdRecordMs_ = 0.0;

    std::array<uint64_t, STAT_COUNT> pipelineStats_{};

    double memBandwidthGBs_ = 0.0;
    double overdraw_ = 0.0;
    double cIdleMs_ = 0.0;
};

} // namespace lve
