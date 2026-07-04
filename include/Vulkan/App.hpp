#pragma once

#include "Vulkan/Window.hpp"
#include "Vulkan/Device.hpp"
#include "Vulkan/RenderPass.hpp"
#include "Vulkan/DescriptorManager.hpp"
#include "Vulkan/TextureCache.hpp"
#include "UI/UiWrapper.hpp"
#include "Renderer/Renderer.hpp"
#include "Renderer/PostProcessing.hpp"
#include "Resource/ResourceManager.hpp"
#include "Core/ScreenManager.hpp"
#include "Core/AppContext.hpp"
#include "Core/Keys.hpp"
#include "Core/KeyBindHandler.hpp"
#include <memory>

#include "Core/World/World.hpp"
#include "Core/World/TerrainGenerator.hpp"
#include "Renderer/RendererSettings.hpp"
#include "Util/TimeUtil.hpp"

namespace lve {

    class App {
    public:
        static constexpr int WIDTH = DEFAULT_WINDOW_WIDTH;
        static constexpr int HEIGHT = DEFAULT_WINDOW_HEIGHT;

        App();
        ~App();
        App(const App &) = delete;
        App &operator=(const App &) = delete;



        bool windowShouldClose() const;
        void tick();
        void cleanup();

        enum class RenderState {
            Running,
            Paused,
            Rebuilding
        };

        void pauseRenderer();
        void resumeRenderer();

        Window& getWindow() { return window; }
        Device& getDevice() { return device; }
        Renderer& getRenderer() { return *renderer; }
        ResourceManager& getResourceManager() { return *resourceManager; }
        UiWrapper& getUiSystem() { return *uiSystem; }
        ScreenManager& getScreenManager() { return *screenManager; }
        KeyBindHandler& getKeyBindHandler() { return *keybinds_; }
        PostProcessing& getPostProcessor() { return *postProcessor_; }
        TextureCache& getTextureCache() { return *textureCache_; }
        World* getWorld() { return world_.get(); }
        VkExtent2D getExtent() { return window.getExtent(); }
        double getCpuFrameTimeMs() const { return cpuFrameTimeMs_; }
        double getCpuTickMs() const { return cpuTickMs_; }
        double getCpuSubmitMs() const { return cpuSubmitMs_; }

    private:
        void drawFrame();
        void recreateSwapChain();

        Window window{WIDTH, HEIGHT, "Kingscraft"};
        Device device{window};
        std::unique_ptr<Renderer> renderer = std::make_unique<Renderer>(device, window.getExtent());
        std::unique_ptr<DescriptorManager> descriptorManager_ = std::make_unique<DescriptorManager>(device);
        std::unique_ptr<TextureCache> textureCache_ = std::make_unique<TextureCache>(device);
        std::unique_ptr<PostProcessing> postProcessor_;
        std::unique_ptr<ResourceManager> resourceManager = std::make_unique<ResourceManager>(device);
        std::unique_ptr<UiWrapper> uiSystem = std::make_unique<UiWrapper>();
        std::unique_ptr<ScreenManager> screenManager = std::make_unique<ScreenManager>();
        std::unique_ptr<KeyBindHandler> keybinds_ = std::make_unique<KeyBindHandler>();
        DefaultTerrainGenerator terrainGen_;
        std::unique_ptr<World> world_ = std::make_unique<World>(device, terrainGen_, RendererSettings::get().chunkSize, RendererSettings::get().worldHeight);
        static constexpr double TICK_RATE = 100.0;
        static constexpr double TICK_INTERVAL = 1.0 / TICK_RATE;

        QueueFamilyIndices indices = device.findPhysicalQueueFamilies();
        bool requestSwapchainRecreate = false;
        double cpuFrameTimeMs_ = 0.0;
        double cpuTickMs_ = 0.0;
        double cpuSubmitMs_ = 0.0;
        VkExtent2D lastExtent{0, 0};
        RenderState renderState = RenderState::Running;
        double prevTime_ = 0.0;
        double dt_ = 0.0;
        double tickAccumulator_ = 0.0;
        double currentTime = 0.0;
    };

}  // namespace lve
