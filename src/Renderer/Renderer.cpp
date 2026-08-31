#include "Renderer/Renderer.hpp"
#include "Core/Constants.hpp"
#include "Util/TimeUtil.hpp"
#include "Util/ScopedTimer.hpp"
#include <stdexcept>
#include <limits>
#include <array>

namespace kc {

Renderer::Renderer(Device& device, VkExtent2D initialExtent)
    : device_(device), extent_(initialExtent) {
    commandPool_ = std::make_unique<CommandPool>(
        device_, device_.findPhysicalQueueFamilies().graphicsFamily,
        VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT);
    swapchain_ = std::make_unique<SwapChain>(device_, extent_);
    syncObjects_ = std::make_unique<SyncObjects>(device_, static_cast<uint32_t>(swapchain_->imageCount()));
    framebufferManager_ = std::make_unique<FramebufferManager>(device_);
    presentRenderPass_ = RenderPass::createDefault(device_, swapchain_->getSwapChainImageFormat());
    framebufferManager_->createFramebuffers(
        swapchain_->getImageViews(),
        presentRenderPass_->getHandle(),
        swapchain_->getSwapChainExtent());
    imagesInFlight_.resize(swapchain_->imageCount(), VK_NULL_HANDLE);
    createCommandBuffers();
    createWorldResources();
    createQueryPool();
    createPipelineStatsPool();
}

Renderer::~Renderer() {
    destroyWorldResources();
}

void Renderer::createCommandBuffers() {
    commandBuffers_ = commandPool_->allocate(static_cast<uint32_t>(swapchain_->imageCount()));
}

void Renderer::recreateSwapChain(VkExtent2D newExtent) {
    extent_ = newExtent;

    vkDeviceWaitIdle(device_.device());

    destroyWorldResources();

    auto oldSwapchain = std::move(swapchain_);
    swapchain_ = std::make_unique<SwapChain>(device_, extent_, std::move(oldSwapchain));

    presentRenderPass_ = RenderPass::createDefault(device_, swapchain_->getSwapChainImageFormat());

    framebufferManager_->destroyFramebuffers();
    framebufferManager_->createFramebuffers(
        swapchain_->getImageViews(),
        presentRenderPass_->getHandle(),
        swapchain_->getSwapChainExtent());

    imagesInFlight_.resize(swapchain_->imageCount(), VK_NULL_HANDLE);

    if (swapchain_->imageCount() != commandBuffers_.size()) {
        vkFreeCommandBuffers(
            device_.device(), commandPool_->getHandle(),
            static_cast<uint32_t>(commandBuffers_.size()), commandBuffers_.data());
        createCommandBuffers();
    }

    createWorldResources();
}

bool Renderer::beginFrame() {
    cIdleMs_ = 0.0;
    VkFence inFlightFence = syncObjects_->getInFlight(currentFrame_);
    {
        ScopedTimer t(cIdleMs_);
        vkWaitForFences(
            device_.device(),
            1,
            &inFlightFence,
            VK_TRUE,
            std::numeric_limits<uint64_t>::max());
    }

    device_.getStagingArena().advanceFrame();

    {
        uint32_t qBase = currentFrame_ * QUERIES_PER_FRAME;
        uint64_t ts[QUERIES_PER_FRAME];
        VkResult r = vkGetQueryPoolResults(
            device_.device(), gpuQueryPool_->getHandle(),
            qBase, QUERIES_PER_FRAME, sizeof(ts), ts, sizeof(uint64_t),
            VK_QUERY_RESULT_64_BIT);
        if (r == VK_SUCCESS) {
            auto toMs = [&](uint32_t end, uint32_t start) -> double {
                return (static_cast<double>(ts[end]) - static_cast<double>(ts[start]))
                       * timestampPeriod_ / 1000000.0;
            };
            gpuFrameTimeMs_  = toMs(TS_FRAME_END, TS_FRAME_START);
            worldGpuMs_      = toMs(TS_WORLD_END, TS_WORLD_START);
            uiGpuMs_         = toMs(TS_UI_END, TS_UI_START);
        }
    }

    {
        uint32_t qStatsBase = currentFrame_ * PIPELINE_STATS_PER_FRAME;
        VkResult r = vkGetQueryPoolResults(
            device_.device(), pipelineStatsPool_->getHandle(),
            qStatsBase, 1, sizeof(pipelineStats_), pipelineStats_.data(), sizeof(uint64_t),
            VK_QUERY_RESULT_64_BIT);
        if (r == VK_SUCCESS) {
            uint64_t fs = pipelineStats_[STAT_FS_INVOCATIONS];
            uint64_t w = extent_.width;
            uint64_t h = extent_.height;
            if (w > 0 && h > 0 && fs > 0) {
                overdraw_ = static_cast<double>(fs) / static_cast<double>(w * h);
                double bytesPerFragment = 8.0;
                double gpuSec = worldGpuMs_ / 1000.0;
                if (gpuSec > 0.0) {
                    memBandwidthGBs_ = (static_cast<double>(fs) * bytesPerFragment) / gpuSec / 1e9;
                }
            }
        } else {
            pipelineStats_.fill(0);
            overdraw_ = 0.0;
            memBandwidthGBs_ = 0.0;
        }
    }

    VkResult result;
    {
        ScopedTimer t(cIdleMs_);
        result = swapchain_->acquireNextImage(&currentImageIndex_, syncObjects_->getImageAvailable(currentFrame_));
    }
    if (result == VK_ERROR_OUT_OF_DATE_KHR) {
        return false;
    }
    if (result != VK_SUCCESS && result != VK_SUBOPTIMAL_KHR) {
        throw std::runtime_error("failed to acquire swap chain image!");
    }

    VkCommandBuffer cmd = commandBuffers_[currentImageIndex_];
    VkCommandBufferBeginInfo beginInfo{};
    beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;

    if (vkBeginCommandBuffer(cmd, &beginInfo) != VK_SUCCESS) {
        throw std::runtime_error("failed to begin recording command buffer!");
    }

    uint32_t qi = currentFrame_ * QUERIES_PER_FRAME;
    vkCmdResetQueryPool(cmd, gpuQueryPool_->getHandle(), qi, QUERIES_PER_FRAME);
    vkCmdWriteTimestamp(cmd, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, gpuQueryPool_->getHandle(), qi + TS_FRAME_START);

    uint32_t qs = currentFrame_ * PIPELINE_STATS_PER_FRAME;
    vkCmdResetQueryPool(cmd, pipelineStatsPool_->getHandle(), qs, PIPELINE_STATS_PER_FRAME);

    return true;
}

void Renderer::executeRenderPass(
    const RenderPassBegin& passBegin,
    const std::function<void(VkCommandBuffer)>& drawCommands) {

    VkCommandBuffer cmd = commandBuffers_[currentImageIndex_];
    uint32_t qi = currentFrame_ * QUERIES_PER_FRAME;

    vkCmdWriteTimestamp(cmd, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, gpuQueryPool_->getHandle(), qi + TS_WORLD_START);

    uint32_t qs = currentFrame_ * PIPELINE_STATS_PER_FRAME;
    vkCmdBeginQuery(cmd, pipelineStatsPool_->getHandle(), qs, 0);

    VkRenderPassBeginInfo rpInfo{};
    rpInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
    rpInfo.renderPass = passBegin.renderPass;
    rpInfo.framebuffer = passBegin.framebuffer;
    rpInfo.renderArea = passBegin.renderArea;

    rpInfo.clearValueCount = passBegin.clearCount;
    rpInfo.pClearValues = passBegin.clearValues.data();

    vkCmdBeginRenderPass(cmd, &rpInfo, VK_SUBPASS_CONTENTS_INLINE);
    vkCmdSetViewport(cmd, 0, 1, &passBegin.viewport);
    vkCmdSetScissor(cmd, 0, 1, &passBegin.scissor);

    double recStart = TimeUtil::uptimeSeconds();
    drawCommands(cmd);
    cmdRecordMs_ = (TimeUtil::uptimeSeconds() - recStart) * 1000.0;

    vkCmdEndRenderPass(cmd);

    vkCmdEndQuery(cmd, pipelineStatsPool_->getHandle(), qs);

    vkCmdWriteTimestamp(cmd, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, gpuQueryPool_->getHandle(), qi + TS_WORLD_END);
}

bool Renderer::endFrame() {
    VkCommandBuffer cmd = commandBuffers_[currentImageIndex_];
    uint32_t qi = currentFrame_ * QUERIES_PER_FRAME;

    vkCmdWriteTimestamp(cmd, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, gpuQueryPool_->getHandle(), qi + TS_FRAME_END);

    if (vkEndCommandBuffer(cmd) != VK_SUCCESS) {
        throw std::runtime_error("failed to record command buffer!");
    }

    if (imagesInFlight_[currentImageIndex_] != VK_NULL_HANDLE) {
        ScopedTimer t(cIdleMs_);
        vkWaitForFences(device_.device(), 1, &imagesInFlight_[currentImageIndex_], VK_TRUE, UINT64_MAX);
    }
    imagesInFlight_[currentImageIndex_] = syncObjects_->getInFlight(currentFrame_);

    VkSubmitInfo submitInfo{};
    submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;

    VkSemaphore waitSemaphores[] = {syncObjects_->getImageAvailable(currentFrame_)};
    VkPipelineStageFlags waitStages[] = {VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT};
    submitInfo.waitSemaphoreCount = 1;
    submitInfo.pWaitSemaphores = waitSemaphores;
    submitInfo.pWaitDstStageMask = waitStages;

    submitInfo.commandBufferCount = 1;
    submitInfo.pCommandBuffers = &cmd;

    VkSemaphore signalSemaphores[] = {syncObjects_->getRenderFinished(currentImageIndex_)};
    submitInfo.signalSemaphoreCount = 1;
    submitInfo.pSignalSemaphores = signalSemaphores;

    VkFence inFlightFence = syncObjects_->getInFlight(currentFrame_);
    vkResetFences(device_.device(), 1, &inFlightFence);

    if (vkQueueSubmit(device_.graphicsQueue(), 1, &submitInfo, inFlightFence) != VK_SUCCESS) {
        throw std::runtime_error("failed to submit draw command buffer!");
    }

    VkPresentInfoKHR presentInfo{};
    presentInfo.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
    presentInfo.waitSemaphoreCount = 1;
    presentInfo.pWaitSemaphores = signalSemaphores;

    VkSwapchainKHR swapChains[] = {swapchain_->getSwapChain()};
    presentInfo.swapchainCount = 1;
    presentInfo.pSwapchains = swapChains;
    presentInfo.pImageIndices = &currentImageIndex_;

    auto result = vkQueuePresentKHR(device_.presentQueue(), &presentInfo);

    currentFrame_ = (currentFrame_ + 1) % SyncObjects::MAX_FRAMES_IN_FLIGHT;

    if (result == VK_ERROR_OUT_OF_DATE_KHR || result == VK_SUBOPTIMAL_KHR) {
        return false;
    }
    if (result != VK_SUCCESS) {
        throw std::runtime_error("failed to submit command buffers!");
    }

    return true;
}

void Renderer::createWorldResources() {
    VkFormat depthFormat = device_.findSupportedFormat(
        {VK_FORMAT_D32_SFLOAT, VK_FORMAT_D32_SFLOAT_S8_UINT, VK_FORMAT_D24_UNORM_S8_UINT},
        VK_IMAGE_TILING_OPTIMAL,
        VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT
    );
    depthFormat_ = depthFormat;

    worldRenderPass_ = RenderPass::createWithDepth(device_, swapchain_->getSwapChainImageFormat(), depthFormat);

    size_t imageCount = swapchain_->imageCount();
    const auto& swapChainImageViews = swapchain_->getImageViews();
    VkExtent2D extent = swapchain_->getSwapChainExtent();

    depthImages_.resize(imageCount);
    depthImageViews_.resize(imageCount);
    worldFramebuffers_.resize(imageCount);

    for (size_t i = 0; i < imageCount; i++) {
        VkImageCreateInfo imageInfo{};
        imageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
        imageInfo.imageType = VK_IMAGE_TYPE_2D;
        imageInfo.extent.width = extent.width;
        imageInfo.extent.height = extent.height;
        imageInfo.extent.depth = 1;
        imageInfo.mipLevels = 1;
        imageInfo.arrayLayers = 1;
        imageInfo.format = depthFormat;
        imageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
        imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        imageInfo.usage = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT;
        imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;
        imageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
        imageInfo.flags = 0;

        depthImages_[i] = std::make_unique<Image>(
            device_, imageInfo, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);

        depthImageViews_[i] = std::make_unique<ImageView>(
            device_, depthImages_[i]->getHandle(), depthFormat,
            VK_IMAGE_ASPECT_DEPTH_BIT);

        std::array<VkImageView, 2> attachments = {
            swapChainImageViews[i],
            depthImageViews_[i]->getHandle()
        };

        VkFramebufferCreateInfo fbInfo{};
        fbInfo.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
        fbInfo.renderPass = worldRenderPass_->getHandle();
        fbInfo.attachmentCount = static_cast<uint32_t>(attachments.size());
        fbInfo.pAttachments = attachments.data();
        fbInfo.width = extent.width;
        fbInfo.height = extent.height;
        fbInfo.layers = 1;

        worldFramebuffers_[i] = std::make_unique<Framebuffer>(device_, fbInfo);
    }
}

void Renderer::destroyWorldResources() {
    worldRenderPass_.reset();

    worldFramebuffers_.clear();
    depthImageViews_.clear();
    depthImages_.clear();
}

void Renderer::createQueryPool() {
    VkPhysicalDeviceProperties props;
    vkGetPhysicalDeviceProperties(device_.getPhysicalDevice(), &props);
    timestampPeriod_ = props.limits.timestampPeriod;

    VkQueryPoolCreateInfo poolInfo{};
    poolInfo.sType = VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO;
    poolInfo.queryType = VK_QUERY_TYPE_TIMESTAMP;
    poolInfo.queryCount = MAX_FRAMES_IN_FLIGHT * QUERIES_PER_FRAME;
    gpuQueryPool_ = std::make_unique<QueryPool>(device_, poolInfo);

    VkCommandBuffer cmd = device_.beginSingleTimeCommands();
    vkCmdResetQueryPool(cmd, gpuQueryPool_->getHandle(), 0, MAX_FRAMES_IN_FLIGHT * QUERIES_PER_FRAME);
    device_.endSingleTimeCommands(cmd);
}

void Renderer::createPipelineStatsPool() {
    VkQueryPoolCreateInfo poolInfo{};
    poolInfo.sType = VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO;
    poolInfo.queryType = VK_QUERY_TYPE_PIPELINE_STATISTICS;
    poolInfo.queryCount = MAX_FRAMES_IN_FLIGHT * PIPELINE_STATS_PER_FRAME;
    poolInfo.pipelineStatistics =
        VK_QUERY_PIPELINE_STATISTIC_INPUT_ASSEMBLY_VERTICES_BIT |
        VK_QUERY_PIPELINE_STATISTIC_INPUT_ASSEMBLY_PRIMITIVES_BIT |
        VK_QUERY_PIPELINE_STATISTIC_VERTEX_SHADER_INVOCATIONS_BIT |
        VK_QUERY_PIPELINE_STATISTIC_CLIPPING_INVOCATIONS_BIT |
        VK_QUERY_PIPELINE_STATISTIC_CLIPPING_PRIMITIVES_BIT |
        VK_QUERY_PIPELINE_STATISTIC_FRAGMENT_SHADER_INVOCATIONS_BIT;

    pipelineStatsPool_ = std::make_unique<QueryPool>(device_, poolInfo);

    VkCommandBuffer cmd = device_.beginSingleTimeCommands();
    vkCmdResetQueryPool(cmd, pipelineStatsPool_->getHandle(), 0, MAX_FRAMES_IN_FLIGHT * PIPELINE_STATS_PER_FRAME);
    device_.endSingleTimeCommands(cmd);
}

} // namespace kc
