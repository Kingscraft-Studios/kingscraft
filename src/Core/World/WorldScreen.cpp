#include "Core/World/WorldScreen.hpp"
#include "Core/World/World.hpp"
#include "Vulkan/TextureCache.hpp"
#include "Renderer/Renderer.hpp"
#include "UI/UiWrapper.hpp"
#include "Renderer/RendererSettings.hpp"
#include "Core/KeyBindHandler.hpp"
#include "Core/Registries.hpp"
#include "Vulkan/Window.hpp"
#include "Vulkan/Device.hpp"

namespace lve {

    WorldScreen::WorldScreen(VkExtent2D extent)
        : extent_(extent) {}

    WorldScreen::~WorldScreen() {
        cleanup();
    }

    void WorldScreen::init(const AppContext& ctx) {
        appCtx_ = ctx;
        world_ = ctx.world;
        auto* window = ctx.window;
        auto* keybinds = ctx.keybinds;
        auto* textureCache = ctx.textureCache;
        auto* renderer = ctx.renderer;

        // Wait for block registrations to complete
        Registries::waitForBuild();

        textureCache->updateFromRegistry();

        camera_.setPosition({67.5f, 15.0f, 67.5f});
        camera_.setRotation(0.0f, -35.0f);

        playerController_.init(camera_, *window, *keybinds);
        terrainRenderer_.init(*ctx.device, *textureCache, renderer->getWorldRenderPass());

        window->setCursorType(GLFW_CURSOR, GLFW_CURSOR_DISABLED);
        keybinds->setLayerEnabled(BindLayer::UI, false);
        fpsCounter_.init(*ctx.uiSystem);
        ctx.uiSystem->resize(static_cast<int>(extent_.width),
                             static_cast<int>(extent_.height));
    }

    void WorldScreen::tick(double dt) {
        playerController_.tick(dt);

        world_->update(camera_.getPosition().x, camera_.getPosition().z,
                       RendererSettings::get().renderDistance);
        world_->flushPendingCleanup();
        world_->processCompletedChunks();
    }

    void WorldScreen::render(const FrameContext& ctx) {
        fpsCounter_.update();

        auto& settings = RendererSettings::get();
        playerController_.updateProjection(ctx.extent, settings.fov,
                                           settings.nearPlane, settings.farPlane);

        glm::mat4 viewProj = playerController_.getViewProj();
        float worldHeight = static_cast<float>(world_->getHeight());

        terrainRenderer_.render(ctx.cmd, world_->getLoadedChunks(),
                                viewProj, camera_.getPosition(),
                                ctx.frameIndex, ctx.gpuQueryPool,
                                settings.enableFrustumCulling, worldHeight);

        fpsCounter_.setCpuGpuTimes(ctx.cpuFrameTimeMs,
                                   appCtx_.renderer->getGpuFrameTimeMs(),
                                   appCtx_.renderer->getTerrainGpuTimeMs(),
                                   ctx.cpuTickMs,
                                   ctx.cpuSubmitMs,
                                   0.0, 0.0);

        appCtx_.uiSystem->render(ctx.cmd, ctx.renderPass);
    }

    void WorldScreen::renderGlow(const FrameContext& ctx) {
        (void)ctx;
    }

    void WorldScreen::cleanup() {
        fpsCounter_.cleanup(*appCtx_.uiSystem);
        if (appCtx_.window) {
            glfwSetInputMode(appCtx_.window->getGLFWWindow(), GLFW_CURSOR, GLFW_CURSOR_NORMAL);
        }
        terrainRenderer_.cleanup();
    }

    void WorldScreen::onRenderPassChanged(VkRenderPass renderPass) {
        terrainRenderer_.onRenderPassChanged(renderPass);
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

} // namespace lve
