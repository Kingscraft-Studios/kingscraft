#pragma once

#include <vulkan/vulkan.h>

#include "UI/Engine/UiEngine.hpp"
#include "UI/Engine/UiStyleHandle.hpp"
#include "UI/Elements/UiElement.hpp"
#include "Engine/UiStyle.hpp"
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>
#include <cstdint>

namespace kc {

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
        void setScrollCallback(ScrollCallback cb);
        void onKey(int key, int action);
        void onChar(unsigned int codepoint);

        // Builds this frame's UI vertex batch on the host. Must be called before
        // the frame's render pass is recorded.
        void build();
        // Issues the UI draws inside the already-recording world/menu pass.
        void render(VkCommandBuffer cmdBuffer, VkRenderPass renderPass, uint32_t frameIndex);

        void addElement(UiElement* element);
        void removeElement(UiElement* element);
        void registerButtonHandler(std::string name, std::function<void()> handler);

        // Style system — delegates to engine
        //
        // createStyle() returns a handle that releases its pool slot when the
        // owning object dies. Use it for anything attached to a screen or
        // overlay.
        UiStyleHandle createStyle(const UiStyle& style);
        // Registers a style that lives until the engine shuts down. Only for
        // process-wide registrations (e.g. MainMenu statics) whose handles
        // must not be destroyed during screen teardown.
        uint32_t registerPinnedStyle(const UiStyle& style);
        void releaseStyle(uint32_t index);
        void updateStyle(uint32_t index, const UiStyle& style);
        void updateStylePool();
        void markDirty(uint32_t elementId);

        // Block texture — delegates to renderer
        void setBlockTexture(VkImageView imageView, VkSampler sampler);

        // Debug editing mode — delegates to engine
        void setDebugMode(bool on);
        bool isDebugModeOn() const;
        void logSelectedElementPosition();

        // Cross-thread UI mutation. The UI layer is owned by the render thread
        // but is mutated by the game thread (screen init/cleanup, FPS text) and
        // the input thread (key/scroll shortcuts). All public methods lock this
        // mutex so element/engine state is never accessed concurrently.
        void lockUI() const { uiMutex_.lock(); }
        void unlockUI() const { uiMutex_.unlock(); }

    private:
        bool initialized_ = false;
        int width_ = 0;
        int height_ = 0;
        uint64_t frameIndex_ = 0;

        std::unique_ptr<UiEngine> engine_;
        // Shared lifetime token for UiStyleHandle. Reset in shutdown(), which
        // makes any handle that outlives the UI a harmless no-op.
        std::shared_ptr<UiStyleRegistry> styleRegistry_;
        std::vector<UiElement*> elements_;

        VkRenderPass currentRenderPass_ = VK_NULL_HANDLE;
        std::unordered_map<std::string, std::function<void()>> buttonHandlers_;
        ScrollCallback scrollCallback_;
        mutable std::recursive_mutex uiMutex_;
    };

    // RAII guard for mutating UI elements from a non-render thread.
    class UiGuard {
    public:
        explicit UiGuard(const UiWrapper& ui) : ui_(ui) { ui_.lockUI(); }
        ~UiGuard() { ui_.unlockUI(); }
        UiGuard(const UiGuard&) = delete;
        UiGuard& operator=(const UiGuard&) = delete;

    private:
        const UiWrapper& ui_;
    };

} // namespace kc
