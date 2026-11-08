#include "Core/World/WorldScreen.hpp"

#include <utility>
#include <vector>

#include "Bus/MessageBus.hpp"
#include "Core/World/World.hpp"
#include "Event/EventManager.hpp"
#include "Event/Events/BlockBreakEvent.hpp"
#include "Event/Events/BlockPlaceEvent.hpp"
#include "UI/Debug/ProfilingCapture.hpp"
#include "Vulkan/TextureCache.hpp"
#include "UI/UiWrapper.hpp"
#include "UI/Engine/UiStyle.hpp"
#include "Renderer/RendererSettings.hpp"
#include "Core/Keys.hpp"
#include "Core/Raycast.hpp"
#include "Core/Runtime.hpp"
#include "Core/Blocks/Blocks.hpp"
#include "Core/World/Physics/CollisionSystem.hpp"
#include "Core/MainMenu.hpp"
#include "Threads/Kingscraft.hpp"
#include "Threads/InputThread.hpp"
#include "Threads/RenderThread.hpp"
#include "Threads/Engine.hpp"
#include "Util/LogUtils.hpp"

namespace kc {

    namespace {
        const Chunk* worldChunkLookup(int gx, int gz, void* ctx) {
            return static_cast<const World*>(ctx)->getChunk(gx, gz);
        }
    }

    WorldScreen::WorldScreen() {
        extent_ = Runtime::get().inputThread->getExtent().toVKExtent();
    }

    WorldScreen::~WorldScreen() {
        cleanup();
    }

    void WorldScreen::init() {
        World* worldPtr = Runtime::get().kingscraft->getWorldManager().getWorld();
        if (!worldPtr) {
            // Should be unreachable: WorldManager::createWorld's callback only
            // runs after the world is published. Fail safe rather than deref.
            LogUtils::error(ThreadName::GameLogic,
                "WorldScreen::init: no active world; aborting screen setup");
            return;
        }
        World& world = *worldPtr;
        auto& ui = Runtime::get().renderThread->getUI();
        ui.setBlockTexture(Runtime::get().renderThread->getTexCache().getImageView(), Runtime::get().renderThread->getTexCache().getSampler());

        world.getPlayerController().setCaptured(true);
        updateUiCapture();

        debugOverlay_ = std::make_shared<UiDebugOverlay>();
        debugOverlay_->init(ui, static_cast<float>(extent_.width), static_cast<float>(extent_.height));
        debugOverlaySub_ = EventManager::get().registerScopedListener(debugOverlay_);
        hotbar_.init(ui, static_cast<float>(extent_.width), static_cast<float>(extent_.height));
        deathScreen_.init(ui, static_cast<float>(extent_.width), static_cast<float>(extent_.height));

        crosshairStyle_ = ui.createStyle(UiStyle{
            .mode = RenderMode::Solid,
            .color1 = {1.0f, 1.0f, 1.0f, 0.9f},
        });
        crosshairH_.setAnchor({0.5f, 0.5f}, {-10.0f, -1.0f});
        crosshairH_.setSize({20.0f, 2.0f});
        crosshairH_.setStyleIndex(crosshairStyle_.index());
        crosshairH_.setName("CrosshairH");
        ui.addElement(&crosshairH_);
        crosshairV_.setAnchor({0.5f, 0.5f}, {-1.0f, -10.0f});
        crosshairV_.setSize({2.0f, 20.0f});
        crosshairV_.setStyleIndex(crosshairStyle_.index());
        crosshairV_.setName("CrosshairV");
        ui.addElement(&crosshairV_);
        ui.resize(static_cast<int>(extent_.width), static_cast<int>(extent_.height));

        // World-scoped bindings. Unlike the old Global Esc binding this context
        // only exists while this screen is alive, so Esc cannot tear down a
        // world that is no longer on screen.
        ActionRouter& actions = Runtime::get().kingscraft->getActionRouter();

        std::vector<Binding> worldBindings;
        worldBindings.reserve(1 + HOTBAR_SLOT_COUNT);
        worldBindings.push_back({{Keys::ESCAPE}, Action::OpenPauseMenu});
        for (int i = 0; i < HOTBAR_SLOT_COUNT; ++i) {
            worldBindings.push_back({{Keys::_1 + i}, hotbarAction(i)});
        }

        worldContext_ = actions.addContext(ContextSpec{
            .name = "world",
            .priority = 10,
            .captureText = false,
            .bindings = std::move(worldBindings),
        });

        pauseSub_ = actions.subscribe(Action::OpenPauseMenu, [this](const ActionEvent& event) {
            if (event.phase != Phase::Pressed) return;
            // Handled at the top of tick(), after the current mailbox batch,
            // so tearing down this screen never happens inside a handler.
            pauseRequested_ = true;
        });

        for (int i = 0; i < HOTBAR_SLOT_COUNT; ++i) {
            hotbarSubs_[i] = actions.subscribe(hotbarAction(i), [this, i](const ActionEvent& event) {
                if (event.phase != Phase::Pressed) return;
                // GameLogic updates the UI data here; the Renderer only reads
                // it. UiGuard makes the mutation safe.
                hotbar_.selectSlot(Runtime::get().renderThread->getUI(), i);
            });
        }
    }

    void WorldScreen::updateUiCapture() {
        const bool enabled = debugCapture_ || deathCapture_;
        if (enabled) {
            if (!uiContext_) {
                uiContext_ = Runtime::get().kingscraft->getActionRouter().addContext(ContextSpec{
                    .name = "world-ui",
                    .priority = 100,
                    .captureText = true,
                    .bindings = {},
                });
            }
            MessageBus::Get().send(ThreadName::Input, []() {
                if (auto* input = Runtime::get().inputThread)
                    input->setCursorType(GLFW_CURSOR, GLFW_CURSOR_NORMAL);
            });
        } else {
            uiContext_.reset();
            MessageBus::Get().send(ThreadName::Input, []() {
                if (auto* input = Runtime::get().inputThread)
                    input->setCursorType(GLFW_CURSOR, GLFW_CURSOR_DISABLED);
            });
        }
    }

    void WorldScreen::tick(double dt) {
        // Esc is delivered as an action. Do the teardown here rather than in a
        // handler so this screen is never destroyed while its own handler is
        // on the stack.
        if (pauseRequested_) {
            pauseRequested_ = false;
            auto* kingscraft = Runtime::get().kingscraft;
            kingscraft->getWorldManager().destroyWorld();
            // Deferred: never destroy this screen while its tick() is on the
            // stack. ScreenManager swaps it in after we return.
            kingscraft->getScreenManager().queueScreen<MainMenu>(kingscraft->getActionRouter());
            return;
        }

        World* worldPtr = Runtime::get().kingscraft->getWorldManager().getWorld();
        if (!worldPtr) return;
        World& world = *worldPtr;
        bool debug = Runtime::get().renderThread->getUI().isDebugModeOn();

        if (debug != wasDebugOn_ && !world.getPlayerController().isDead()) {
            wasDebugOn_ = debug;
            debugCapture_ = debug;
            updateUiCapture();
            if (debug) {
                world.getPlayerController().setCaptured(false);
            } else {
                world.getPlayerController().setCaptured(true);
            }
            world.getPlayerController().resetMouse();
        }

        world.tick(dt);

        VkExtent2D currentExtent = Runtime::get().inputThread->getExtent().toVKExtent();
        if (currentExtent.width != extent_.width || currentExtent.height != extent_.height) {
            extent_ = currentExtent;
            hotbar_.resize(static_cast<float>(extent_.width), static_cast<float>(extent_.height));
            deathScreen_.resize(static_cast<float>(extent_.width), static_cast<float>(extent_.height));
        }
    }

    void WorldScreen::refreshHotbar() {
    hotbar_.refresh(Runtime::get().renderThread->getUI());
}

void WorldScreen::onPlayerDeath() {
        World* worldPtr = Runtime::get().kingscraft->getWorldManager().getWorld();
        if (!worldPtr) return;
        World& world = *worldPtr;
        deathCapture_ = true;
        updateUiCapture();
        world.getPlayerController().setCaptured(false);
        world.getPlayerController().resetMouse();
        deathScreen_.show(Runtime::get().renderThread->getUI());
    }

    void WorldScreen::onPlayerRespawn() {
        World* worldPtr = Runtime::get().kingscraft->getWorldManager().getWorld();
        if (!worldPtr) return;
        World& world = *worldPtr;
        deathCapture_ = false;
        updateUiCapture();
        // Debug capture may still be active after respawn; keep the cursor
        // usable in that case instead of blindly re-capturing it.
        world.getPlayerController().setCaptured(!(debugCapture_ || deathCapture_));
        world.getPlayerController().resetMouse();
        deathScreen_.hide(Runtime::get().renderThread->getUI());
    }

void WorldScreen::render(FrameScene& scene) {
    World* worldPtr = Runtime::get().kingscraft->getWorldManager().getWorld();
    if (!worldPtr) {
        // World destroyed between ticks (or screen outliving its session):
        // submit an empty sky frame rather than dereferencing a null world.
        scene.background = WorldBackground::Sky;
        scene.terrain.draws.clear();
        scene.terrain.renderTerrain = false;
        return;
    }
    World& world = *worldPtr;
    auto& settings = RendererSettings::get();

    // Explicit, and independent of how many chunks survived culling. The world
    // screen always draws the world pass and clears to sky, so a frame where the
    // culling pass yields nothing shows an empty sky rather than the menu's flat
    // grey.
    scene.background = WorldBackground::Sky;

    world.getPlayerController().updateProjection(extent_, settings.fov,
                                           settings.nearPlane, settings.farPlane);

        glm::mat4 viewProj = world.getPlayerController().getViewProj();
        scene.camera.viewProj = viewProj;

        scene.highlight.enabled = false;
        if (world.getPlayerController().isCursorCaptured()) {
            const Camera& cam = world.getPlayerController().getCamera();
            RaycastHit hit = raycastBlock(cam.getPosition(), cam.getForward(), 8.0f,
                                          world);
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
        terrainRenderer_.render(scene, world.getLoadedChunks(),
                                viewProj, world.getPlayerController().getCamera().getPosition(),
                                settings.enableFrustumCulling,
                                worldChunkLookup, &world,
                                &frustumMs, &drawMs, &visibleChunks,
                                &visibleSubChunks, &occlusionTested,
                                &occlusionRemoved);

        auto& cpuMetrics = Runtime::get().kingscraft->getCPUMetrics();
        cpuMetrics.frustumMs = frustumMs;
        cpuMetrics.drawMs = drawMs;
        cpuMetrics.visibleChunks = visibleChunks;
        cpuMetrics.visibleSubChunks = visibleSubChunks;
        cpuMetrics.occlusionTested = occlusionTested;
        cpuMetrics.occlusionRemoved = occlusionRemoved;
    }

    void WorldScreen::cleanup() {
        if (cleanedUp_) return;
        cleanedUp_ = true;

        // Remove every input registration before the UI elements go away.
        pauseSub_.reset();
        for (auto& sub : hotbarSubs_) sub.reset();
        uiContext_.reset();
        worldContext_.reset();

        auto& ui = Runtime::get().renderThread->getUI();
        hotbar_.cleanup(ui);
        if (debugOverlay_) {
            debugOverlay_->cleanup(ui);
            debugOverlaySub_.unsubscribe();
        }
        deathScreen_.cleanup(ui);
        ui.removeElement(&crosshairH_);
        ui.removeElement(&crosshairV_);
        if (Runtime::get().inputThread && Runtime::get().inputThread->isInitialized()) {
            MessageBus::Get().send(ThreadName::Input, []() {
                if (auto* input = Runtime::get().inputThread)
                    input->setCursorType(GLFW_CURSOR, GLFW_CURSOR_NORMAL);
            });
        }
    }

    void WorldScreen::onScroll(double dx, double dy) {
        (void)dx;
        // Runs on GameLogic (InputThread routes scroll here). selectSlot only
        // changes UI data under UiGuard; the Renderer reads it.
        if (dy > 0) {
            hotbar_.selectSlot(Runtime::get().renderThread->getUI(),
                               hotbar_.getSelectedSlot() - 1);
        } else if (dy < 0) {
            hotbar_.selectSlot(Runtime::get().renderThread->getUI(),
                               hotbar_.getSelectedSlot() + 1);
        }
    }

    void WorldScreen::onMouseButton(int button, int action, int mods) {
        (void)mods;
        World* worldPtr = Runtime::get().kingscraft->getWorldManager().getWorld();
        if (!worldPtr) return;
        World& world = *worldPtr;
        if (action != GLFW_PRESS) return;
        if (!world.getPlayerController().isCursorCaptured()) return;

        const Camera& cam = world.getPlayerController().getCamera();
        glm::vec3 origin = cam.getPosition();
        glm::vec3 dir = cam.getForward();

        auto hit = raycastBlock(origin, dir, 8.0f, world);
        if (!hit.hit) return;

        if (button == GLFW_MOUSE_BUTTON_LEFT) {
            EventManager::get().callEvent<BlockBreakEvent>(hit.x, hit.y, hit.z);
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

        const RegistryKey<Block>& key = hotbar_.getSlotBlock(hotbar_.getSelectedSlot());
        if (!key) return;

        const uint64_t replacedEnc = world.getBlock(placeX, placeY, placeZ);
        RegistryKey<Block> replaced{
            replacedEnc, Registry<Block>::getRegistry().getIdentifier(replacedEnc)};

        EventManager::get().callEvent<BlockPlaceEvent>(
            placeX, placeY, placeZ, key, std::move(replaced), hit.face);
    }

} // namespace kc
