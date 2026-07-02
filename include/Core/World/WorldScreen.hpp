#pragma once

#include "Core/Screen.hpp"
#include "Core/Camera.hpp"
#include "Core/Keys.hpp"
#include "Core/World/Chunk.hpp"
#include "Core/World/World.hpp"
#include "UI/Debug/UiFpsCounter.hpp"
#include "Vulkan/Pipeline.hpp"
#include <memory>
#include <glm/glm.hpp>

#include "Vulkan/App.hpp"

namespace lve {

    class WorldScreen : public Screen {
    public:
        explicit WorldScreen(VkExtent2D extent);
        ~WorldScreen() override;

        void init() override;
        void tick(double dt) override;
        void render(const FrameContext& ctx) override;
        void renderGlow(const FrameContext& ctx) override;
        void cleanup() override;
        void onRenderPassChanged(VkRenderPass renderPass) override;
        void onSwapChainRecreated(VkExtent2D extent) override;

        FrameRenderInfo getFrameRenderInfo(const Renderer& renderer, uint32_t imageIndex) const override;

    private:
        void createPipelineLayout();
        void createPipeline(bool disableTextures);

        VkExtent2D extent_{};

        VkPipelineLayout pipelineLayout_ = VK_NULL_HANDLE;
        std::unique_ptr<Pipeline> pipeline_;

        World* world_ = App::get().getWorld();

        Camera camera_;
        UiFpsCounter fpsCounter_;
        double lastMouseX_ = 0.0;
        double lastMouseY_ = 0.0;
        bool cursorCaptured_ = false;
        bool lastDisableTextures_ = false;
    };

} // namespace lve
