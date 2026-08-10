#include "Core/World/WorldScreen.hpp"

#include "Bus/MessageBus.hpp"
#include "Core/World/World.hpp"
#include "UI/Debug/ProfilingCapture.hpp"
#include "Vulkan/TextureCache.hpp"
#include "UI/UiWrapper.hpp"
#include "UI/Engine/UiStyle.hpp"
#include "Renderer/RendererSettings.hpp"
#include "Core/KeyBindHandler.hpp"
#include "Core/Keys.hpp"
#include "Core/Raycast.hpp"
#include "Core/Blocks/Blocks.hpp"
#include "Core/World/Physics/CollisionSystem.hpp"
#include "Threads/GameLogicThread.hpp"
#include "Threads/InputThread.hpp"
#include "Threads/Renderer.hpp"
#include "Threads/Engine.hpp"

namespace lve {

    namespace {
        const Chunk* worldChunkLookup(int gx, int gz, void* ctx) {
            return static_cast<const World*>(ctx)->getChunk(gx, gz);
        }
    }

    WorldScreen::WorldScreen() {
        extent_ = InputThread::getInstance().getExtent().toVKExtent();
    }

    WorldScreen::~WorldScreen() {
        cleanup();
    }

    void WorldScreen::init() {
        auto& world = GameLogicThread::getInstance().getWorld();
        RenderThread::getInstance().getUI().setBlockTexture(RenderThread::getInstance().getTexCache().getImageView(), RenderThread::getInstance().getTexCache().getSampler());

        world.getPlayerController().setCaptured(true);
        MessageBus::Get().send(ThreadName::Input, []() {
            InputThread::getInstance().setCursorType(GLFW_CURSOR, GLFW_CURSOR_DISABLED);
        });
        MessageBus::Get().send(ThreadName::Input, []() {
            InputThread::getInstance().getKeyBindHandler().setLayerEnabled(BindLayer::UI, false);
        });
        fpsCounter_.init(RenderThread::getInstance().getUI());
        Engine::Get().getDiagnostics().setDispatcher(ThreadName::Renderer,
            [this](const FrameMetrics& frame) {
                fpsCounter_.setFrame(frame);
                fpsCounter_.update(RenderThread::getInstance().getUI());
            });
        hotbar_.init(RenderThread::getInstance().getUI(), static_cast<float>(extent_.width), static_cast<float>(extent_.height));
        deathScreen_.init(RenderThread::getInstance().getUI(), static_cast<float>(extent_.width), static_cast<float>(extent_.height));

        crosshairStyle_ = RenderThread::getInstance().getUI().registerStyle(UiStyle{
            .mode = RenderMode::Solid,
            .color1 = {1.0f, 1.0f, 1.0f, 0.9f},
        });
        crosshairH_.setAnchor({0.5f, 0.5f}, {-10.0f, -1.0f});
        crosshairH_.setSize({20.0f, 2.0f});
        crosshairH_.setStyleIndex(crosshairStyle_);
        crosshairH_.setName("CrosshairH");
        RenderThread::getInstance().getUI().addElement(&crosshairH_);
        crosshairV_.setAnchor({0.5f, 0.5f}, {-1.0f, -10.0f});
        crosshairV_.setSize({2.0f, 20.0f});
        crosshairV_.setStyleIndex(crosshairStyle_);
        crosshairV_.setName("CrosshairV");
        RenderThread::getInstance().getUI().addElement(&crosshairV_);
        RenderThread::getInstance().getUI().resize(static_cast<int>(extent_.width),
                             static_cast<int>(extent_.height));

        RenderThread::getInstance().getUI().setScrollCallback([this](double, double dy) {
            if (dy > 0)
                hotbar_.selectSlot(RenderThread::getInstance().getUI(), hotbar_.getSelectedSlot() - 1);
            else if (dy < 0)
                hotbar_.selectSlot(RenderThread::getInstance().getUI(), hotbar_.getSelectedSlot() + 1);
        });

        for (int i = 0; i < UiHotbar::SLOT_COUNT; ++i) {
            int key = Keys::_1 + i;
            MessageBus::Get().send(ThreadName::Input, [key, i, this]() {
                InputThread::getInstance().getKeyBindHandler().onPress(BindLayer::Screen, {key}, [this, i]() {
                    hotbar_.selectSlot(RenderThread::getInstance().getUI(), i);
                });
            });
        }
    }

    void WorldScreen::tick(double dt) {
        auto& world = GameLogicThread::getInstance().getWorld();
        bool debug = RenderThread::getInstance().getUI().isDebugModeOn();

        if (debug != wasDebugOn_ && !world.getPlayerController().isDead()) {
            wasDebugOn_ = debug;
            if (debug) {
                MessageBus::Get().send(ThreadName::Input, []() {
                    InputThread::getInstance().setCursorType(GLFW_CURSOR, GLFW_CURSOR_NORMAL);
                    InputThread::getInstance().getKeyBindHandler().setLayerEnabled(BindLayer::UI, true);
                });
                world.getPlayerController().setCaptured(false);
            } else {
                MessageBus::Get().send(ThreadName::Input, []() {
                    InputThread::getInstance().setCursorType(GLFW_CURSOR, GLFW_CURSOR_DISABLED);
                    InputThread::getInstance().getKeyBindHandler().setLayerEnabled(BindLayer::UI, false);
                });
        world.getPlayerController().setCaptured(true);
        world.getPlayerController().resetMouse();
            }
            world.getPlayerController().resetMouse();
        }

        world.tick(dt);

        bool dead = world.getPlayerController().isDead();
        if (dead && !wasDead_) {
            MessageBus::Get().send(ThreadName::Input, []() {
                InputThread::getInstance().setCursorType(GLFW_CURSOR, GLFW_CURSOR_NORMAL);
                InputThread::getInstance().getKeyBindHandler().setLayerEnabled(BindLayer::UI, true);
            });
            world.getPlayerController().setCaptured(false);
            world.getPlayerController().resetMouse();
            deathScreen_.show(RenderThread::getInstance().getUI());
        } else if (!dead && wasDead_) {
            MessageBus::Get().send(ThreadName::Input, []() {
                InputThread::getInstance().setCursorType(GLFW_CURSOR, GLFW_CURSOR_DISABLED);
                InputThread::getInstance().getKeyBindHandler().setLayerEnabled(BindLayer::UI, false);
            });
            world.getPlayerController().setCaptured(true);
            world.getPlayerController().resetMouse();
            deathScreen_.hide(RenderThread::getInstance().getUI());
        }
        wasDead_ = dead;

        VkExtent2D currentExtent = InputThread::getInstance().getExtent().toVKExtent();
        if (currentExtent.width != extent_.width || currentExtent.height != extent_.height) {
            extent_ = currentExtent;
            hotbar_.resize(static_cast<float>(extent_.width), static_cast<float>(extent_.height));
            deathScreen_.resize(static_cast<float>(extent_.width), static_cast<float>(extent_.height));
        }
    }

    void WorldScreen::render(FrameScene& scene) {
        auto& world = GameLogicThread::getInstance().getWorld();
        auto& settings = RendererSettings::get();
        world.getPlayerController().updateProjection(extent_, settings.fov,
                                           settings.nearPlane, settings.farPlane);

        glm::mat4 viewProj = world.getPlayerController().getViewProj();
        scene.camera.viewProj = viewProj;
        float worldHeight = static_cast<float>(GameLogicThread::getInstance().getWorld().getHeight());

        scene.highlight.enabled = false;
        if (world.getPlayerController().isCursorCaptured()) {
            const Camera& cam = world.getPlayerController().getCamera();
            RaycastHit hit = raycastBlock(cam.getPosition(), cam.getForward(), 8.0f,
                                          GameLogicThread::getInstance().getWorld());
            if (hit.hit) {
                scene.highlight.enabled = true;
                scene.highlight.position = glm::vec3(static_cast<float>(hit.x),
                                                     static_cast<float>(hit.y),
                                                     static_cast<float>(hit.z));
            }
        }

        double frustumMs = 0.0, drawMs = 0.0;
        uint32_t visibleChunks = 0, visibleSubChunks = 0;
        uint32_t occlusionTested = 0, occlusionRemoved = 0;
        terrainRenderer_.render(scene, GameLogicThread::getInstance().getWorld().getLoadedChunks(),
                                viewProj, world.getPlayerController().getCamera().getPosition(),
                                settings.enableFrustumCulling, worldHeight,
                                worldChunkLookup, &GameLogicThread::getInstance().getWorld(),
                                &frustumMs, &drawMs, &visibleChunks,
                                &visibleSubChunks, &occlusionTested,
                                &occlusionRemoved);

        auto& cpuMetrics = Engine::Get().getDiagnostics().getCPUMetrics();
        cpuMetrics.frustumMs = frustumMs;
        cpuMetrics.drawMs = drawMs;
        cpuMetrics.visibleChunks = visibleChunks;
        cpuMetrics.visibleSubChunks = visibleSubChunks;
        cpuMetrics.occlusionTested = occlusionTested;
        cpuMetrics.occlusionRemoved = occlusionRemoved;

        if (RenderThread::getInstance().getProfilerCapture().isActive()) {
            RenderThread::getInstance().getProfilerCapture().feedFrame(
                Engine::Get().getDiagnostics().snapshot());
            scene.ui.enabled = true;
        }
    }
    void WorldScreen::cleanup() {
        Engine::Get().getDiagnostics().clearDispatcher(ThreadName::Renderer);
        RenderThread::getInstance().getUI().setScrollCallback(nullptr);
        hotbar_.cleanup(RenderThread::getInstance().getUI());
        fpsCounter_.cleanup(RenderThread::getInstance().getUI());
        deathScreen_.cleanup(RenderThread::getInstance().getUI());
        RenderThread::getInstance().getUI().removeElement(&crosshairH_);
        RenderThread::getInstance().getUI().removeElement(&crosshairV_);
        if (InputThread::getInstance().isInitialized()) {
            MessageBus::Get().send(ThreadName::Input, []() {
                InputThread::getInstance().setCursorType(GLFW_CURSOR, GLFW_CURSOR_NORMAL);
            });
        }
    }

    void WorldScreen::onMouseButton(int button, int action, int mods) {
        (void)mods;
        auto& world = GameLogicThread::getInstance().getWorld();
        if (action != GLFW_PRESS) return;
        if (!world.getPlayerController().isCursorCaptured()) return;

        const Camera& cam = world.getPlayerController().getCamera();
        glm::vec3 origin = cam.getPosition();
        glm::vec3 dir = cam.getForward();

        auto hit = raycastBlock(origin, dir, 8.0f, GameLogicThread::getInstance().getWorld());
        if (!hit.hit) return;

        if (button == GLFW_MOUSE_BUTTON_LEFT) {
            GameLogicThread::getInstance().getWorld().setBlock(hit.x, hit.y, hit.z, Blocks::AIR);
            GameLogicThread::getInstance().getWorld().remeshDirtyChunks();
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

        if (placeY < 0 || placeY >= GameLogicThread::getInstance().getWorld().getHeight()) return;

        auto* key = hotbar_.getSlotBlock(hotbar_.getSelectedSlot());
        if (!key) return;

        const Block& block = *key;
        if (&block == Blocks::AIR) return;

        if (world.getPlayerController().getBodyAABB().overlaps(
                CollisionSystem::blockAABBAt(block, placeX, placeY, placeZ))) {
            return;
        }

        GameLogicThread::getInstance().getWorld().setBlock(placeX, placeY, placeZ, block);
        GameLogicThread::getInstance().getWorld().remeshDirtyChunks();
    }

} // namespace lve
