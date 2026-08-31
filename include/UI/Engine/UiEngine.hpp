#pragma once

#include "Vulkan/Device.hpp"
#include "Vulkan/DescriptorManager.hpp"
#include "UI/Engine/UiBatchQueue.hpp"
#include "UI/Engine/UiFontAtlas.hpp"
#include "UI/Engine/UiRenderer.hpp"
#include "UI/Debug/UiDebugEditor.hpp"
#include "UiStyle.hpp"
#include <glm/glm.hpp>
#include <memory>
#include <string>
#include <vector>
#include <algorithm>
#include <cmath>

namespace kc {

    class UiElement;

    class UiEngine {
    public:
        UiEngine(Device& device, DescriptorManager& descriptorManager, VkExtent2D extent);
        ~UiEngine();

        UiEngine(const UiEngine&) = delete;
        UiEngine& operator=(const UiEngine&) = delete;

        void init();
        void shutdown();
        void resize(VkExtent2D extent);

        // Frame lifecycle
        void beginFrame();
        void renderOffscreen(VkCommandBuffer cmd, uint32_t frameIndex);
        void composite(VkCommandBuffer cmd, uint32_t frameIndex);

        // Draw commands
        void addQuad(const UiVertex verts[4], const uint32_t indices[6]);

        // Font access
        bool loadFont(const std::string& ttfPath, const std::string& name);
        const UiFontAtlas::Glyph* getGlyph(const std::string& font, char32_t codepoint) const;

        // Style SSBO upload
        void uploadStylePool(const void* data, uint32_t count);
        void uploadElementStyles(const void* data, uint32_t firstElement, uint32_t count);

        // Style system
        uint32_t registerStyle(const UiStyle& style);
        void markDirty(uint32_t elementId, int styleIndex = -1);
        void updateStylePool();
        uint32_t allocateElementId();
        void setElementStyle(uint32_t id, uint32_t styleIndex);

        // Debug editing mode — forwards to UiDebugEditor
        void setDebugMode(bool on);
        bool isDebugModeOn() const;
        void logSelectedElementPosition();
        void handleDebugMouseMove(float x, float y, const std::vector<UiElement*>& elements, float parentW, float parentH);
        void handleDebugMouseButton(int button, int action, float x, float y, const std::vector<UiElement*>& elements);
        void renderDebugOverlays(const std::vector<UiElement*>& elements);
        void onElementRemoved(UiElement* element);

        // Subsystem access (for advanced use)
        UiFontAtlas& getFontAtlas() { return *fontAtlas_; }
        UiBatchQueue& getBatchQueue() { return *batchQueue_; }
        UiRenderer& getRenderer() { return *renderer_; }

    private:
        void uploadPendingStyles();

        Device& device_;
        DescriptorManager& descriptorManager_;
        VkExtent2D extent_;

        std::unique_ptr<UiBatchQueue> batchQueue_;
        std::unique_ptr<UiFontAtlas> fontAtlas_;
        std::unique_ptr<UiRenderer> renderer_;

        bool initialized_ = false;

        // Style system state
        uint32_t nextElementId_ = 0;
        std::vector<GpuStyle> stylePool_;
        std::vector<uint32_t> elementStyles_;
        std::vector<bool> styleDirty_;
        uint32_t firstDirty_ = UiRenderer::MAX_ELEMENTS;
        uint32_t lastDirty_ = 0;
        bool stylePoolDirty_ = false;

        std::unique_ptr<UiDebugEditor> debugEditor_;
    };

} // namespace kc
