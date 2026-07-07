#include "Core/World/WorldScreen.hpp"
#include "Core/World/World.hpp"
#include "Core/AppContext.hpp"
#include "Vulkan/TextureCache.hpp"
#include "Renderer/Renderer.hpp"
#include "UI/UiWrapper.hpp"
#include "Renderer/RendererSettings.hpp"
#include "Core/KeyBindHandler.hpp"
#include "Core/Registries.hpp"
#include "Core/Keys.hpp"
#include "Vulkan/Window.hpp"
#include "Vulkan/Device.hpp"

namespace lve {

    WorldScreen::WorldScreen(VkExtent2D extent)
        : extent_(extent) {}

    WorldScreen::~WorldScreen() {
        cleanup();
    }

    void WorldScreen::init() {
        auto& ctx = AppContext::get();
        world_ = ctx.world;
        auto* window = ctx.window;
        auto* keybinds = ctx.keybinds;
        auto* textureCache = ctx.textureCache;
        auto* renderer = ctx.renderer;

        Registries::waitForBuild();

        textureCache->updateFromRegistry();
        ctx.uiSystem->setBlockTexture(textureCache->getImageView(), textureCache->getSampler());

        camera_.setPosition({67.5f, 15.0f, 67.5f});
        camera_.setRotation(0.0f, -35.0f);

        playerController_.init(camera_, *window, *keybinds);
        playerController_.setCaptured(true);
        terrainRenderer_.init(*ctx.device, *textureCache, renderer->getWorldRenderPass());

        window->setCursorType(GLFW_CURSOR, GLFW_CURSOR_DISABLED);
        keybinds->setLayerEnabled(BindLayer::UI, false);
        fpsCounter_.init(*ctx.uiSystem);
        hotbar_.init(*ctx.uiSystem, static_cast<float>(extent_.width), static_cast<float>(extent_.height));
        ctx.uiSystem->resize(static_cast<int>(extent_.width),
                             static_cast<int>(extent_.height));

        ctx.uiSystem->setScrollCallback([this](double, double dy) {
            if (dy > 0)
                hotbar_.selectSlot(hotbar_.getSelectedSlot() - 1);
            else if (dy < 0)
                hotbar_.selectSlot(hotbar_.getSelectedSlot() + 1);
        });

        for (int i = 0; i < UiHotbar::SLOT_COUNT; ++i) {
            int key = Keys::_1 + i;
            keybinds->onPress(BindLayer::Screen, {key}, [this, i]() {
                hotbar_.selectSlot(i);
            });
        }
    }

    void WorldScreen::tick(double dt) {
        auto& ctx = AppContext::get();
        bool debug = ctx.uiSystem->isDebugModeOn();

        if (debug != wasDebugOn_) {
            wasDebugOn_ = debug;
            if (debug) {
                ctx.window->setCursorType(GLFW_CURSOR, GLFW_CURSOR_NORMAL);
                ctx.keybinds->setLayerEnabled(BindLayer::UI, true);
                playerController_.setCaptured(false);
            } else {
                ctx.window->setCursorType(GLFW_CURSOR, GLFW_CURSOR_DISABLED);
                ctx.keybinds->setLayerEnabled(BindLayer::UI, false);
                playerController_.setCaptured(true);
            }
            playerController_.resetMouse();
        }

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

        double frustumMs = 0.0, drawMs = 0.0;
        terrainRenderer_.render(ctx.cmd, world_->getLoadedChunks(),
                                viewProj, camera_.getPosition(),
                                ctx.frameIndex, ctx.gpuQueryPool,
                                settings.enableFrustumCulling, worldHeight,
                                &frustumMs, &drawMs);

        auto& app = AppContext::get();
        fpsCounter_.setCpuGpuTimes(ctx.cpuFrameTimeMs,
                                   app.renderer->getGpuFrameTimeMs(),
                                   app.renderer->getTerrainGpuTimeMs(),
                                   ctx.cpuTickMs,
                                   ctx.cpuSubmitMs,
                                   frustumMs, drawMs);

        app.uiSystem->render(ctx.cmd, ctx.renderPass);
    }

    void WorldScreen::renderGlow(const FrameContext& ctx) {
        (void)ctx;
    }

    void WorldScreen::cleanup() {
        auto& app = AppContext::get();
        app.uiSystem->setScrollCallback(nullptr);
        hotbar_.cleanup(*app.uiSystem);
        fpsCounter_.cleanup(*app.uiSystem);
        if (app.window) {
            glfwSetInputMode(app.window->getGLFWWindow(), GLFW_CURSOR, GLFW_CURSOR_NORMAL);
        }
        terrainRenderer_.cleanup();
    }

    void WorldScreen::onRenderPassChanged(VkRenderPass renderPass) {
        terrainRenderer_.onRenderPassChanged(renderPass);
    }

    void WorldScreen::onSwapChainRecreated(VkExtent2D extent) {
        extent_ = extent;
        hotbar_.resize(static_cast<float>(extent.width), static_cast<float>(extent.height));
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
