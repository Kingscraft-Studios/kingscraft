#include "UI/Debug/UiDebugEditor.hpp"
#include "UI/Engine/UiEngine.hpp"
#include "UI/Elements/UiElement.hpp"
#include "UI/Elements/UiRect.hpp"
#include "UI/Engine/UiRenderer.hpp"
#include "UI/Engine/UiBatchQueue.hpp"
#include "UI/Engine/UiStyle.hpp"
#include "Threads/Logger.hpp"
#include "Util/StringBuilder.hpp"
#include <algorithm>
#include <cmath>

#include "Util/LogUtils.hpp"

namespace kc {

    UiDebugEditor::UiDebugEditor(UiEngine& engine)
        : engine_(engine) {}

    void UiDebugEditor::setEnabled(bool on) {
        if (enabled_ == on) return;
        enabled_ = on;
        if (enabled_) {
            ensureOverlayResources();
        }
        selectedElement_ = nullptr;
        hoveredElement_ = nullptr;
        isDragging_ = false;
        activeHandle_ = DragHandle::None;
        LogUtils::info(ThreadName::Renderer, StringBuilder::build("[Debug] Editing mode: ", enabled_ ? "ON" : "OFF"));
    }

    bool UiDebugEditor::isDebugOverlay(const UiElement* el) const {
        return el == debugHoverRect_.get() ||
               el == debugBorderTop_.get() ||
               el == debugBorderBottom_.get() ||
               el == debugBorderLeft_.get() ||
               el == debugBorderRight_.get();
    }

    DragHandle UiDebugEditor::hitTestHandle(const UiElement* el, glm::vec2 point) const {
        glm::vec2 pos = el->getPosition();
        glm::vec2 sz = el->getSize();

        if (sz.x < HANDLE_THRESHOLD * 2 || sz.y < HANDLE_THRESHOLD * 2) {
            return DragHandle::Move;
        }

        float dl = std::abs(point.x - pos.x);
        float dr = std::abs(point.x - (pos.x + sz.x));
        float dt = std::abs(point.y - pos.y);
        float db = std::abs(point.y - (pos.y + sz.y));

        bool nearLeft = dl < HANDLE_THRESHOLD;
        bool nearRight = dr < HANDLE_THRESHOLD;
        bool nearTop = dt < HANDLE_THRESHOLD;
        bool nearBottom = db < HANDLE_THRESHOLD;

        if (nearLeft || nearRight || nearTop || nearBottom) {
            if (nearTop && nearLeft) return DragHandle::TopLeft;
            if (nearTop && nearRight) return DragHandle::TopRight;
            if (nearBottom && nearLeft) return DragHandle::BottomLeft;
            if (nearBottom && nearRight) return DragHandle::BottomRight;
            if (nearLeft) return DragHandle::Left;
            if (nearRight) return DragHandle::Right;
            if (nearTop) return DragHandle::Top;
            if (nearBottom) return DragHandle::Bottom;
        }

        if (point.x >= pos.x && point.x <= pos.x + sz.x &&
            point.y >= pos.y && point.y <= pos.y + sz.y) {
            return DragHandle::Move;
        }

        return DragHandle::None;
    }

    void UiDebugEditor::onMouseMove(float x, float y, const std::vector<UiElement*>& elements,
                                     float parentW, float parentH) {
        parentW_ = parentW;
        parentH_ = parentH;
        glm::vec2 point(x, y);

        if (isDragging_ && selectedElement_) {
            glm::vec2 delta = point - dragStartMouse_;
            glm::vec2 newPos = dragStartPos_;
            glm::vec2 newSize = dragStartSize_;

            switch (activeHandle_) {
                case DragHandle::Move:
                    selectedElement_->setPixelOffset(dragStartPixelOffset_ + (point - dragStartMouse_));
                    selectedElement_->updateLayout(parentW, parentH);
                    engine_.markDirty(selectedElement_->getElementId());
                    return;

                case DragHandle::Right:
                    newSize.x = std::max(dragStartSize_.x + delta.x, 10.0f);
                    break;
                case DragHandle::Left: {
                    float maxX = dragStartPos_.x + dragStartSize_.x - 10.0f;
                    newPos.x = std::min(dragStartPos_.x + delta.x, maxX);
                    newSize.x = dragStartSize_.x - (newPos.x - dragStartPos_.x);
                    break;
                }
                case DragHandle::Bottom:
                    newSize.y = std::max(dragStartSize_.y + delta.y, 10.0f);
                    break;
                case DragHandle::Top: {
                    float maxY = dragStartPos_.y + dragStartSize_.y - 10.0f;
                    newPos.y = std::min(dragStartPos_.y + delta.y, maxY);
                    newSize.y = dragStartSize_.y - (newPos.y - dragStartPos_.y);
                    break;
                }
                case DragHandle::BottomRight:
                    newSize = glm::max(dragStartSize_ + delta, {10.0f, 10.0f});
                    break;
                case DragHandle::BottomLeft: {
                    float maxX = dragStartPos_.x + dragStartSize_.x - 10.0f;
                    newPos.x = std::min(dragStartPos_.x + delta.x, maxX);
                    newSize.x = dragStartSize_.x - (newPos.x - dragStartPos_.x);
                    newSize.y = std::max(dragStartSize_.y + delta.y, 10.0f);
                    break;
                }
                case DragHandle::TopRight: {
                    float maxY = dragStartPos_.y + dragStartSize_.y - 10.0f;
                    newPos.y = std::min(dragStartPos_.y + delta.y, maxY);
                    newSize.y = dragStartSize_.y - (newPos.y - dragStartPos_.y);
                    newSize.x = std::max(dragStartSize_.x + delta.x, 10.0f);
                    break;
                }
                case DragHandle::TopLeft: {
                    float maxX = dragStartPos_.x + dragStartSize_.x - 10.0f;
                    float maxY = dragStartPos_.y + dragStartSize_.y - 10.0f;
                    newPos.x = std::min(dragStartPos_.x + delta.x, maxX);
                    newPos.y = std::min(dragStartPos_.y + delta.y, maxY);
                    newSize.x = dragStartSize_.x - (newPos.x - dragStartPos_.x);
                    newSize.y = dragStartSize_.y - (newPos.y - dragStartPos_.y);
                    break;
                }
                default:
                    return;
            }

            selectedElement_->setPosition(newPos);
            selectedElement_->setSize(newSize);
            engine_.markDirty(selectedElement_->getElementId());
            return;
        }

        lastMouse_ = point;

        // Hover highlight
        hoveredElement_ = nullptr;
        for (auto it = elements.rbegin(); it != elements.rend(); ++it) {
            UiElement* el = *it;
            if (isDebugOverlay(el)) continue;
            if (el->getNormalizedSize().x >= 0.999f && el->getNormalizedSize().y >= 0.999f) continue;
            if (el->containsPoint(point)) {
                hoveredElement_ = el;
                break;
            }
        }
    }

    void UiDebugEditor::onMouseButton(int button, int action, float x, float y,
                                       const std::vector<UiElement*>& elements) {
        if (button != 0) return;
        glm::vec2 point(x, y);

        if (action == 1) {  // Press
            isDragging_ = false;
            activeHandle_ = DragHandle::None;
            selectedElement_ = nullptr;

            for (auto it = elements.rbegin(); it != elements.rend(); ++it) {
                UiElement* el = *it;
                if (isDebugOverlay(el)) continue;
                if (el->getNormalizedSize().x >= 0.999f && el->getNormalizedSize().y >= 0.999f) continue;
                if (el->containsPoint(point)) {
                    DragHandle handle = hitTestHandle(el, point);
                    if (handle != DragHandle::None) {
                        selectedElement_ = el;
                        isDragging_ = true;
                        activeHandle_ = handle;
                        dragStartMouse_ = point;
                        dragStartPos_ = el->getPosition();
                        dragStartSize_ = el->getSize();
                        dragStartPixelOffset_ = el->getPixelOffset();
                    }
                    break;
                }
            }
        } else {  // Release
            if (isDragging_ && selectedElement_ && activeHandle_ != DragHandle::Move) {
                glm::vec2 newPos = selectedElement_->getPosition();
                glm::vec2 newSz = selectedElement_->getSize();

                if (parentW_ > 0.0f && parentH_ > 0.0f) {
                    selectedElement_->setNormalizedSize({
                        newSz.x / parentW_,
                        newSz.y / parentH_
                    });
                    selectedElement_->setPixelOffset({
                        newPos.x - selectedElement_->getNormalizedPos().x * parentW_,
                        newPos.y - selectedElement_->getNormalizedPos().y * parentH_
                    });
                }
            }
            isDragging_ = false;
            activeHandle_ = DragHandle::None;
        }
    }

    void UiDebugEditor::onElementRemoved(UiElement* element) {
        if (selectedElement_ == element) selectedElement_ = nullptr;
        if (hoveredElement_ == element) hoveredElement_ = nullptr;
    }

    void UiDebugEditor::renderOverlays() {
        if (!enabled_) return;

        if (hoveredElement_ && hoveredElement_ != selectedElement_) {
            debugHoverRect_->setPosition(hoveredElement_->getPosition());
            debugHoverRect_->setSize(hoveredElement_->getSize());
            debugHoverRect_->render(engine_);
        }

        if (selectedElement_) {
            glm::vec2 pos = selectedElement_->getPosition();
            glm::vec2 sz = selectedElement_->getSize();
            float bw = 2.0f;

            debugBorderTop_->setPosition(pos);
            debugBorderTop_->setSize({sz.x, bw});
            debugBorderTop_->render(engine_);

            debugBorderBottom_->setPosition({pos.x, pos.y + sz.y - bw});
            debugBorderBottom_->setSize({sz.x, bw});
            debugBorderBottom_->render(engine_);

            debugBorderLeft_->setPosition(pos);
            debugBorderLeft_->setSize({bw, sz.y});
            debugBorderLeft_->render(engine_);

            debugBorderRight_->setPosition({pos.x + sz.x - bw, pos.y});
            debugBorderRight_->setSize({bw, sz.y});
            debugBorderRight_->render(engine_);
        }
    }

    void UiDebugEditor::logElementState() const {
        if (!selectedElement_) {
            LogUtils::info(ThreadName::Renderer, "No element selected. Select an element first");
            return;
        }

        auto* el = selectedElement_;
        glm::vec2 n = el->getNormalizedPos();
        glm::vec2 off = el->getPixelOffset();
        glm::vec2 ns = el->getNormalizedSize();
        glm::vec2 sz = el->getSize();

        LogUtils::debug(ThreadName::Renderer,
    StringBuilder::build("--- Element State ---"));

        LogUtils::debug(ThreadName::Renderer,
            StringBuilder::build("Name: \"", el->getName(), "\""));

        LogUtils::debug(ThreadName::Renderer,
            StringBuilder::build(".setAnchor({", n.x, "f, ", n.y, "f}, {", off.x, "f, ", off.y, "f});"));

        LogUtils::debug(ThreadName::Renderer,
            StringBuilder::build(".setNormalizedSize({", ns.x, "f, ", ns.y, "f});"));

        LogUtils::debug(ThreadName::Renderer,
            StringBuilder::build(".setSize({", sz.x, "f, ", sz.y, "f})  (current pixel)"));

        LogUtils::debug(ThreadName::Renderer,
            StringBuilder::build("Visible: ", el->isVisible() ? "true" : "false"));

        LogUtils::debug(ThreadName::Renderer,
            StringBuilder::build("Style index: ", el->getStyleIndex(),
                                 "  Active: ", el->getActiveStyleIndex()));

        if (!el->getText().empty()) {
            LogUtils::debug(ThreadName::Renderer,
                StringBuilder::build("Text: \"", el->getText(), "\""));

            LogUtils::debug(ThreadName::Renderer,
                StringBuilder::build("Centered: ", el->isTextCentered() ? "true" : "false"));
        }

        LogUtils::debug(ThreadName::Renderer,
            "--- End ---");
    }

    void UiDebugEditor::ensureOverlayResources() {
        if (overlayReady_) return;

        debugHoverStyle_ = engine_.registerStyle(UiStyle{
            .mode = RenderMode::Solid,
            .color1 = {0.0f, 0.0f, 0.0f, 0.20f}
        });

        debugBorderStyle_ = engine_.registerStyle(UiStyle{
            .mode = RenderMode::Solid,
            .color1 = {0.0f, 0.0f, 0.0f, 0.50f}
        });

        debugElementHoverId_ = engine_.allocateElementId();
        debugElementSelectId_ = engine_.allocateElementId();

        if (debugElementHoverId_ < UiRenderer::MAX_ELEMENTS) {
            engine_.setElementStyle(debugElementHoverId_, debugHoverStyle_);
        }
        if (debugElementSelectId_ < UiRenderer::MAX_ELEMENTS) {
            engine_.setElementStyle(debugElementSelectId_, debugBorderStyle_);
        }

        debugHoverRect_ = std::make_unique<UiRect>();
        debugHoverRect_->setElementId(debugElementHoverId_);
        debugHoverRect_->setStyleIndex(debugHoverStyle_);

        auto makeBorder = [&]() {
            auto rect = std::make_unique<UiRect>();
            rect->setElementId(debugElementSelectId_);
            rect->setStyleIndex(debugBorderStyle_);
            return rect;
        };

        debugBorderTop_ = makeBorder();
        debugBorderBottom_ = makeBorder();
        debugBorderLeft_ = makeBorder();
        debugBorderRight_ = makeBorder();

        overlayReady_ = true;
    }

}
