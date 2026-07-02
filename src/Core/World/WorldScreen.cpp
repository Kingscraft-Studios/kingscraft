#include "Core/World/WorldScreen.hpp"
#include "Core/Frustum.hpp"
#include "Vulkan/App.hpp"
#include "Vulkan/TextureCache.hpp"
#include "Renderer/Renderer.hpp"
#include "Renderer/RendererSettings.hpp"
#include "Util/TimeUtil.hpp"
#include "Core/KeyBindHandler.hpp"
#include "Core/Registries.hpp"
#include "Util/Preloader.hpp"
#include <array>
#include <iostream>
#include <stdexcept>
#include <cassert>
#include <thread>
#include <algorithm>

namespace lve {

    WorldScreen::WorldScreen(VkExtent2D extent)
        : extent_(extent) {}

    WorldScreen::~WorldScreen() {
        cleanup();
    }

    void WorldScreen::init() {
        auto& keybinds = App::get().getKeyBindHandler();
        auto& window = App::get().getWindow();
        auto& textureCache = App::get().getTextureCache();

        // Wait for block registrations to complete
        while (!Registries::isBuilt()) {
            std::this_thread::yield();
        }

        textureCache.updateFromRegistry();

        createPipelineLayout();
        lastDisableTextures_ = RendererSettings::get().disableTextures;
        createPipeline(lastDisableTextures_);

        float aspect = static_cast<float>(extent_.width) / static_cast<float>(extent_.height);
        camera_.setAspectRatio(aspect);
        camera_.setPosition({67.5f, 15.0f, 67.5f});
        camera_.setRotation(0.0f, -35.0f);

        window.setCursorType(GLFW_CURSOR, GLFW_CURSOR_DISABLED);
        keybinds.setLayerEnabled(BindLayer::UI, false);
        fpsCounter_.init(App::get().getUiSystem());
        App::get().getUiSystem().resize(static_cast<int>(extent_.width),
                                        static_cast<int>(extent_.height));
        cursorCaptured_ = true;
        lastMouseX_ = window.getLastX();
        lastMouseY_ = window.getLastY();
    }

    void WorldScreen::tick(double dt) {
        float speed = 3.0f * static_cast<float>(dt);
        auto& keybinds = App::get().getKeyBindHandler();
        auto& window = App::get().getWindow();

        if (keybinds.isDown(Keys::W)) camera_.moveForward(speed);
        if (keybinds.isDown(Keys::S)) camera_.moveForward(-speed);
        if (keybinds.isDown(Keys::A)) camera_.moveRight(-speed);
        if (keybinds.isDown(Keys::D)) camera_.moveRight(speed);
        if (keybinds.isDown(Keys::SPACE)) camera_.moveUp(speed);
        if (keybinds.isDown(Keys::LEFT_SHIFT)) camera_.moveUp(-speed);

        double mx = window.getLastX();
        double my = window.getLastY();
        double dx = mx - lastMouseX_;
        double dy = lastMouseY_ - my;
        lastMouseX_ = mx;
        lastMouseY_ = my;

        if (dx != 0.0 || dy != 0.0) {
            camera_.rotate(static_cast<float>(dx) * 0.1f, static_cast<float>(dy) * 0.1f);
        }

        world_->update(camera_.getPosition().x, camera_.getPosition().z, RendererSettings::get().renderDistance);
        world_->flushPendingCleanup();
        world_->processCompletedChunks();
    }

    void WorldScreen::render(const FrameContext& ctx) {
        fpsCounter_.update();
        if (!pipeline_) return;

        bool currentDisableTextures = RendererSettings::get().disableTextures;
        if (currentDisableTextures != lastDisableTextures_) {
            lastDisableTextures_ = currentDisableTextures;
            pipeline_.reset();
            createPipeline(currentDisableTextures);
        }

        pipeline_->bind(ctx.cmd);

        auto& settings = RendererSettings::get();
        float aspect = static_cast<float>(ctx.extent.width) / static_cast<float>(ctx.extent.height);
        camera_.setAspectRatio(aspect);
        camera_.setFov(settings.fov);
        camera_.setNearPlane(settings.nearPlane);
        camera_.setFarPlane(settings.farPlane);
        glm::mat4 viewProj = camera_.getProjectionMatrix() * camera_.getViewMatrix();

        vkCmdPushConstants(ctx.cmd, pipelineLayout_, VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(glm::mat4), &viewProj);

        // Bind texture descriptor set
        auto& textureCache = App::get().getTextureCache();
        VkDescriptorSet texSet = textureCache.getDescriptorSet();
        if (texSet != VK_NULL_HANDLE) {
            vkCmdBindDescriptorSets(ctx.cmd, VK_PIPELINE_BIND_POINT_GRAPHICS,
                                    pipelineLayout_, 0, 1, &texSet, 0, nullptr);
        }

        float chunkSize = static_cast<float>(world_->getChunkSize() - 1);
        float worldHeight = static_cast<float>(world_->getHeight());
        glm::vec3 halfExtents(chunkSize * 0.5f, worldHeight * 0.5f, chunkSize * 0.5f);

        // Terrain GPU timestamp start
        uint32_t qi = ctx.frameIndex * 4;
        vkCmdWriteTimestamp(ctx.cmd, VK_PIPELINE_STAGE_ALL_GRAPHICS_BIT, ctx.gpuQueryPool, qi + 2);

        double frustumTime = 0.0;
        double drawTime = 0.0;

        // Collect visible chunks
        std::vector<Chunk*> visible;
        visible.reserve(world_->getLoadedChunks().size());

        if (settings.enableFrustumCulling) {
            auto frustumStart = TimeUtil::uptimeSeconds();
            Frustum frustum(viewProj);
            for (Chunk* chunk : world_->getLoadedChunks()) {
                glm::vec3 origin = chunk->getWorldOrigin();
                glm::vec3 min = origin;
                glm::vec3 max = origin + glm::vec3(chunkSize, worldHeight, chunkSize);
                if (!frustum.isVisible(min, max)) continue;
                if (chunk->getIndexCount() == 0) continue;
                visible.push_back(chunk);
            }
            frustumTime = TimeUtil::uptimeSeconds() - frustumStart;
        } else {
            for (Chunk* chunk : world_->getLoadedChunks()) {
                if (chunk->getIndexCount() == 0) continue;
                visible.push_back(chunk);
            }
        }

        // Sort front-to-back by center distance (squared, no sqrt needed)
        if (!visible.empty()) {
            glm::vec3 camPos = camera_.getPosition();
            std::sort(visible.begin(), visible.end(),
                [camPos, halfExtents](Chunk* a, Chunk* b) {
                    glm::vec3 da = (a->getWorldOrigin() + halfExtents) - camPos;
                    glm::vec3 db = (b->getWorldOrigin() + halfExtents) - camPos;
                    return da.x * da.x + da.y * da.y + da.z * da.z <
                           db.x * db.x + db.y * db.y + db.z * db.z;
                });
        }

        // Draw sorted
        {
            auto drawStart = TimeUtil::uptimeSeconds();
            for (Chunk* chunk : visible) {
                chunk->bindAndDraw(ctx.cmd);
            }
            drawTime = TimeUtil::uptimeSeconds() - drawStart;
        }

        // Terrain GPU timestamp end
        vkCmdWriteTimestamp(ctx.cmd, VK_PIPELINE_STAGE_ALL_GRAPHICS_BIT, ctx.gpuQueryPool, qi + 3);

        fpsCounter_.setCpuGpuTimes(App::get().getCpuFrameTimeMs(),
                                   App::get().getRenderer().getGpuFrameTimeMs(),
                                   App::get().getRenderer().getTerrainGpuTimeMs(),
                                   App::get().getCpuTickMs(),
                                   App::get().getCpuSubmitMs(),
                                   frustumTime * 1000.0,
                                   drawTime * 1000.0);

        App::get().getUiSystem().render(ctx.cmd, ctx.renderPass);
    }

    void WorldScreen::renderGlow(const FrameContext& ctx) {
        (void)ctx;
    }

    void WorldScreen::cleanup() {
        fpsCounter_.cleanup(App::get().getUiSystem());
        if (cursorCaptured_) {
            glfwSetInputMode(App::get().getWindow().getGLFWWindow(), GLFW_CURSOR, GLFW_CURSOR_NORMAL);
            cursorCaptured_ = false;
        }
        pipeline_.reset();
        if (pipelineLayout_ != VK_NULL_HANDLE) {
            vkDestroyPipelineLayout(App::get().getDevice().device(), pipelineLayout_, nullptr);
            pipelineLayout_ = VK_NULL_HANDLE;
        }
    }

    void WorldScreen::onRenderPassChanged(VkRenderPass renderPass) {
        (void)renderPass;
    }

    void WorldScreen::onSwapChainRecreated(VkExtent2D extent) {
        extent_ = extent;
    }

    FrameRenderInfo WorldScreen::getFrameRenderInfo(const Renderer& renderer, uint32_t imageIndex) const {
        return {
            renderer.getWorldRenderPass(),
            renderer.getWorldFramebuffer(imageIndex),
            {{{0.4f, 0.6f, 0.9f, 1.0f}}, {1.0f, 0}},
            true
        };
    }

    void WorldScreen::createPipelineLayout() {
        auto& textureCache = App::get().getTextureCache();

        VkPushConstantRange pushConstantRange{};
        pushConstantRange.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;
        pushConstantRange.offset = 0;
        pushConstantRange.size = sizeof(glm::mat4);

        VkDescriptorSetLayout texLayout = textureCache.getLayout();

        VkPipelineLayoutCreateInfo layoutInfo{};
        layoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
        layoutInfo.pushConstantRangeCount = 1;
        layoutInfo.pPushConstantRanges = &pushConstantRange;
        layoutInfo.setLayoutCount = (texLayout != VK_NULL_HANDLE) ? 1 : 0;
        layoutInfo.pSetLayouts = (texLayout != VK_NULL_HANDLE) ? &texLayout : nullptr;

        if (vkCreatePipelineLayout(App::get().getDevice().device(), &layoutInfo, nullptr, &pipelineLayout_) != VK_SUCCESS) {
            throw std::runtime_error("failed to create pipeline layout!");
        }
    }

    void WorldScreen::createPipeline(bool disableTextures) {
        auto& vertShaderCode = Preloader::Get().getShader("resources/shaders/terrain.vert.spv");
        auto& fragShaderCode = Preloader::Get().getShader("resources/shaders/terrain.frag.spv");

        auto& device = App::get().getDevice();
        auto& renderer = App::get().getRenderer();

        PipelineConfigInfo configInfo{};
        Pipeline::defaultPipelineConfigInfo(configInfo);

        // Specialization constant: DISABLE_TEXTURES (constant_id = 0)
        VkSpecializationMapEntry entry{};
        entry.constantID = 0;
        entry.offset = 0;
        entry.size = sizeof(uint32_t);
        configInfo.specMapEntries = {entry};
        uint32_t specValue = disableTextures ? 1 : 0;
        configInfo.specData = {specValue};

        auto bindingDesc = ChunkVertex::getBindingDescription();
        auto attributeDescs = ChunkVertex::getAttributeDescriptions();
        configInfo.bindingDescriptions = {bindingDesc};
        configInfo.attributeDescriptions = {attributeDescs.begin(), attributeDescs.end()};

        configInfo.renderPass = renderer.getWorldRenderPass();
        configInfo.pipelineLayout = pipelineLayout_;

        pipeline_ = std::make_unique<Pipeline>(device, vertShaderCode, fragShaderCode, configInfo);
    }

} // namespace lve
