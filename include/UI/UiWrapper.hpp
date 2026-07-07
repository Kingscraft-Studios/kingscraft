#pragma once

#include <vulkan/vulkan.h>

#include "UI/Engine/UiEngine.hpp"
#include "UI/Elements/UiElement.hpp"
#include "Engine/UiStyle.hpp"
#include <functional>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>
#include <cstdint>

namespace lve {

    class Device;
    class DescriptorManager;


    class UiWrapper {
    public:
        UiWrapper();
        ~UiWrapper();

        void init(Device& device, DescriptorManager& descriptorManager, VkExtent2D extent);
        void shutdown();
        void update(double deltaTime);
        void resize(int width, int height);
        void onMouseMove(double x, double y);
        void onMouseButton(int button, int action, int mods, double x, double y);
        using ScrollCallback = std::function<void(double, double)>;
        void onScroll(double dx, double dy);
        void setScrollCallback(ScrollCallback cb) { scrollCallback_ = std::move(cb); }
        void onKey(int key, int action);
        void onChar(unsigned int codepoint);

        void renderOffscreen(VkCommandBuffer cmdBuffer);
        void render(VkCommandBuffer cmdBuffer, VkRenderPass renderPass);

        void addElement(UiElement* element);
        void removeElement(UiElement* element);
        void registerButtonHandler(std::string name, std::function<void()> handler);

        // Style system — delegates to engine
        uint32_t registerStyle(const UiStyle& style) { return engine_ ? engine_->registerStyle(style) : 0; }
        void updateStylePool() { if (engine_) engine_->updateStylePool(); }
        void markDirty(uint32_t elementId);

        // Block texture — delegates to renderer
        void setBlockTexture(VkImageView imageView, VkSampler sampler) {
            if (engine_) engine_->getRenderer().setBlockTexture(imageView, sampler);
        }

        // Debug editing mode — delegates to engine
        void setDebugMode(bool on) { if (engine_) engine_->setDebugMode(on); }
        bool isDebugModeOn() const { return engine_ && engine_->isDebugModeOn(); }
        void logSelectedElementPosition() { if (engine_) engine_->logSelectedElementPosition(); }

    private:
        bool initialized_ = false;
        int width_ = 0;
        int height_ = 0;
        uint64_t frameIndex_ = 0;

        std::unique_ptr<UiEngine> engine_;
        std::vector<UiElement*> elements_;

        VkRenderPass currentRenderPass_ = VK_NULL_HANDLE;
        std::unordered_map<std::string, std::function<void()>> buttonHandlers_;
        ScrollCallback scrollCallback_;
    };

} // namespace lve
