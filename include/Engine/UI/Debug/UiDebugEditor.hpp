#pragma once

#include <glm/glm.hpp>
#include <vector>
#include <cstdint>
#include <memory>

#include "UI/Elements/UiRect.hpp"

namespace kc {

    class UiEngine;
    class UiElement;

    enum class DragHandle : uint8_t {
        None,
        Move,
        Left,
        Right,
        Top,
        Bottom,
        TopLeft,
        TopRight,
        BottomLeft,
        BottomRight
    };

    class UiDebugEditor {
    public:
        explicit UiDebugEditor(UiEngine& engine);

        void setEnabled(bool on);
        bool isEnabled() const { return enabled_; }

        void onMouseMove(float x, float y, const std::vector<UiElement*>& elements, float parentW, float parentH);
        void onMouseButton(int button, int action, float x, float y, const std::vector<UiElement*>& elements);
        void onElementRemoved(UiElement* element);
        void renderOverlays();
        void logElementState() const;

    private:
        void ensureOverlayResources();
        DragHandle hitTestHandle(const UiElement* el, glm::vec2 point) const;
        bool isDebugOverlay(const UiElement* el) const;

        static constexpr float HANDLE_THRESHOLD = 6.0f;

        UiEngine& engine_;
        bool enabled_ = false;
        UiElement* selectedElement_ = nullptr;
        UiElement* hoveredElement_ = nullptr;
        bool isDragging_ = false;
        DragHandle activeHandle_ = DragHandle::None;

        glm::vec2 lastMouse_{0.0f};
        glm::vec2 dragStartMouse_{0.0f};
        glm::vec2 dragStartPos_{0.0f};
        glm::vec2 dragStartSize_{0.0f};
        glm::vec2 dragStartPixelOffset_{0.0f};
        float parentW_ = 0.0f;
        float parentH_ = 0.0f;

        std::unique_ptr<UiRect> debugHoverRect_;
        std::unique_ptr<UiRect> debugBorderTop_;
        std::unique_ptr<UiRect> debugBorderBottom_;
        std::unique_ptr<UiRect> debugBorderLeft_;
        std::unique_ptr<UiRect> debugBorderRight_;
        uint32_t debugElementHoverId_ = 0;
        uint32_t debugElementSelectId_ = 0;
        uint32_t debugHoverStyle_ = 0;
        uint32_t debugBorderStyle_ = 0;
        bool overlayReady_ = false;
    };

}
