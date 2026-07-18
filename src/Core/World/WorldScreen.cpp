#include "Core/World/WorldScreen.hpp"

#include "Bus/MessageBus.hpp"
#include "Core/World/World.hpp"
#include "Core/AppContext.hpp"
#include "UI/Debug/ProfilingCapture.hpp"
#include "Vulkan/TextureCache.hpp"
#include "Renderer/Renderer.hpp"
#include "UI/UiWrapper.hpp"
#include "Renderer/RendererSettings.hpp"
#include "Core/KeyBindHandler.hpp"
#include "Core/Registries.hpp"
#include "Core/Keys.hpp"
#include "Core/Raycast.hpp"
#include "Core/Blocks/Blocks.hpp"
#include "Threads/InputThread.hpp"
#include "Vulkan/Window.hpp"
#include "Vulkan/Device.hpp"

namespace lve {

    namespace {
        const Chunk* worldChunkLookup(int gx, int gz, void* ctx) {
            return static_cast<const World*>(ctx)->getChunk(gx, gz);
        }
    }

    WorldScreen::WorldScreen(VkExtent2D extent)
        : extent_(extent) {}

    WorldScreen::~WorldScreen() {
        cleanup();
    }

    void WorldScreen::init() {
        auto& ctx = AppContext::get();
        world_ = ctx.world;
        auto* textureCache = ctx.textureCache;
        auto* renderer = ctx.renderer;

        Registries::waitForBuild();

        textureCache->updateFromRegistry();
        ctx.uiSystem->setBlockTexture(textureCache->getImageView(), textureCache->getSampler());

        camera_.setPosition({67.5f, 15.0f, 67.5f});
        camera_.setRotation(0.0f, -35.0f);

        playerController_.init(camera_, InputThread::getInstance().getKeyBindHandler());
        playerController_.setCaptured(true);
        terrainRenderer_.init(*ctx.device, *textureCache, renderer->getWorldRenderPass());
        MessageBus::Get().send(ThreadName::Input, []() {
            InputThread::getInstance().setCursorType(GLFW_CURSOR, GLFW_CURSOR_DISABLED);
        });
        MessageBus::Get().send(ThreadName::Input, []() {
            InputThread::getInstance().getKeyBindHandler().setLayerEnabled(BindLayer::UI, false);
        });
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
            MessageBus::Get().send(ThreadName::Input, [key, i, this]() {
                InputThread::getInstance().getKeyBindHandler().onPress(BindLayer::Screen, {key}, [this, i]() {
                    hotbar_.selectSlot(i);
                });
            });
        }
    }

    void WorldScreen::tick(double dt) {
        auto& ctx = AppContext::get();
        bool debug = ctx.uiSystem->isDebugModeOn();

        if (debug != wasDebugOn_) {
            wasDebugOn_ = debug;
            if (debug) {
                MessageBus::Get().send(ThreadName::Input, []() {
                    InputThread::getInstance().setCursorType(GLFW_CURSOR, GLFW_CURSOR_NORMAL);
                });
                MessageBus::Get().send(ThreadName::Input, []() {
                    InputThread::getInstance().getKeyBindHandler().setLayerEnabled(BindLayer::UI, true);
                });
                playerController_.setCaptured(false);
            } else {
                MessageBus::Get().send(ThreadName::Input, []() {
                    InputThread::getInstance().setCursorType(GLFW_CURSOR, GLFW_CURSOR_DISABLED);
                });
                MessageBus::Get().send(ThreadName::Input, []() {
                    InputThread::getInstance().getKeyBindHandler().setLayerEnabled(BindLayer::UI, false);
                });
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
        uint32_t visibleChunks = 0, visibleSubChunks = 0;
        uint32_t occlusionTested = 0, occlusionRemoved = 0;
        terrainRenderer_.render(ctx.cmd, world_->getLoadedChunks(),
                                viewProj, camera_.getPosition(),
                                settings.enableFrustumCulling, worldHeight,
                                worldChunkLookup, static_cast<void*>(world_),
                                &frustumMs, &drawMs, &visibleChunks,
                                &visibleSubChunks, &occlusionTested,
                                &occlusionRemoved);

        auto& app = AppContext::get();
        auto* r = app.renderer;
        fpsCounter_.setCpuGpuTimes(
            ctx.cpuFrameTimeMs,
            r->getGpuFrameTimeMs(),
            r->getWorldGpuMs(),
            r->getUiGpuMs(),
            ctx.cpuTickMs,
            ctx.cpuSubmitMs,
            r->getCmdRecordMs(),
            frustumMs, drawMs,
            r->getMemBandwidthGBs(),
            r->getOverdraw(),
            r->getPipelineStat(Renderer::STAT_IA_VERTICES),
            r->getPipelineStat(Renderer::STAT_IA_PRIMITIVES),
            r->getPipelineStat(Renderer::STAT_VS_INVOCATIONS),
            r->getPipelineStat(Renderer::STAT_FS_INVOCATIONS),
            r->getPipelineStat(Renderer::STAT_CLIP_PRIMS),
            visibleChunks,
            visibleSubChunks,
            occlusionTested,
            occlusionRemoved);

        auto* pc = app.profilingCapture;
        if (pc && pc->isActive()) {
            pc->feedFrame(
                ctx.cpuFrameTimeMs,
                r->getGpuFrameTimeMs(),
                r->getWorldGpuMs(),
                r->getUiGpuMs(),
                ctx.cpuTickMs,
                ctx.cpuSubmitMs,
                r->getCmdRecordMs(),
                frustumMs, drawMs,
                r->getMemBandwidthGBs(),
                r->getOverdraw(),
                r->getPipelineStat(Renderer::STAT_IA_VERTICES),
                r->getPipelineStat(Renderer::STAT_IA_PRIMITIVES),
                r->getPipelineStat(Renderer::STAT_VS_INVOCATIONS),
                r->getPipelineStat(Renderer::STAT_FS_INVOCATIONS),
                r->getPipelineStat(Renderer::STAT_CLIP_PRIMS),
                visibleChunks,
                visibleSubChunks,
                occlusionTested,
                occlusionRemoved);
        }

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
        if (InputThread::getInstance().isInitialized()) {
            MessageBus::Get().send(ThreadName::Input, []() {
                InputThread::getInstance().setCursorType(GLFW_CURSOR, GLFW_CURSOR_NORMAL);
            });
        }
        terrainRenderer_.cleanup();
    }

    void WorldScreen::onRenderPassChanged(VkRenderPass renderPass) {
        terrainRenderer_.onRenderPassChanged(renderPass);
    }

    void WorldScreen::onMouseButton(int button, int action, int mods) {
        (void)mods;
        if (action != GLFW_PRESS) return;
        if (!playerController_.isCursorCaptured()) return;

        const Camera& cam = playerController_.getCamera();
        glm::vec3 origin = cam.getPosition();
        glm::vec3 dir = cam.getForward();

        auto hit = raycastBlock(origin, dir, 8.0f, *world_);
        if (!hit.hit) return;

        if (button == GLFW_MOUSE_BUTTON_LEFT) {
            world_->setBlock(hit.x, hit.y, hit.z, 0);
            world_->remeshDirtyChunks();
            return;
        }

        if (button != GLFW_MOUSE_BUTTON_RIGHT) return;

        static constexpr int faceNormals[6][3] = {
            {1, 0, 0},  {-1, 0, 0},
            {0, 1, 0},  {0, -1, 0},
            {0, 0, 1},  {0, 0, -1},
        };

        int placeX = hit.x + faceNormals[hit.face][0];
        int placeY = hit.y + faceNormals[hit.face][1];
        int placeZ = hit.z + faceNormals[hit.face][2];

        if (placeY < 0 || placeY >= world_->getHeight()) return;

        auto* key = hotbar_.getSlotBlock(hotbar_.getSelectedSlot());
        if (!key || key->getId() == 0) return;

        uint8_t blockId = static_cast<uint8_t>(key->getId());
        world_->setBlock(placeX, placeY, placeZ, blockId);
        world_->remeshDirtyChunks();
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
