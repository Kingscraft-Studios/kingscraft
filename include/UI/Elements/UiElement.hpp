#pragma once

#include <glm/glm.hpp>
#include <string>
#include <cstdint>

#include "../Engine/UiLabel.hpp"

namespace kc {

    class UiEngine;

    class UiElement {
    public:
        UiElement() = default;
        virtual ~UiElement() = default;

        UiElement(const UiElement&) = delete;
        UiElement& operator=(const UiElement&) = delete;

        virtual void render(UiEngine& engine) = 0;
        virtual bool containsPoint(glm::vec2 point) const;

        void setPosition(glm::vec2 pos) { position_ = pos; }
        void setSize(glm::vec2 size) { size_ = size; }
        void setColor(glm::vec4 color) { color_ = color; }
        void setVisible(bool visible) { visible_ = visible; }
        void setName(const std::string& name) { name_ = name; }

        glm::vec2 getPosition() const { return position_; }
        glm::vec2 getSize() const { return size_; }
        glm::vec4 getColor() const { return color_; }
        bool isVisible() const { return visible_; }
        const std::string& getName() const { return name_; }

        void setElementId(uint32_t id) { elementId_ = id; }
        uint32_t getElementId() const { return elementId_; }

        void setStyleIndex(uint32_t index) { styleIndex_ = index; }
        uint32_t getStyleIndex() const { return styleIndex_; }
        virtual uint32_t getActiveStyleIndex() const { return styleIndex_; }

        // Text accessors — delegates to label_
        void setText(const std::string& text) { label_.text = text; }
        void setFont(const std::string& fontName) { label_.fontName = fontName; }
        void setFontSize(float size) { label_.fontSize = size; }
        void setTextCentered(bool centered) { label_.centered = centered; }
        const std::string& getText() const { return label_.text; }
        bool isTextCentered() const { return label_.centered; }

        void setAnchor(glm::vec2 normPos, glm::vec2 pixOffset) {
            normalizedPos_ = normPos;
            pixelOffset_ = pixOffset;
        }
        void setNormalizedSize(glm::vec2 normSize) { normalizedSize_ = normSize; }

        void setInteractionSize(glm::vec2 size) { interactionSize_ = size; }
        glm::vec2 getInteractionSize() const {
            return (interactionSize_.x > 0.0f) ? interactionSize_ : size_;
        }
        void setInteractionPadding(float px) {
            interactionSize_ = size_ + glm::vec2{px * 2.0f};
        }

        void setFillParent() {
            normalizedPos_ = {0.0f, 0.0f};
            pixelOffset_ = {0.0f, 0.0f};
            normalizedSize_ = {1.0f, 1.0f};
        }
        void updateLayout(float parentW, float parentH) {
            position_.x = normalizedPos_.x * parentW + pixelOffset_.x;
            position_.y = normalizedPos_.y * parentH + pixelOffset_.y;
            if (normalizedSize_.x > 0.0f) size_.x = normalizedSize_.x * parentW;
            if (normalizedSize_.y > 0.0f) size_.y = normalizedSize_.y * parentH;
        }

        glm::vec2 getNormalizedPos() const { return normalizedPos_; }
        glm::vec2 getNormalizedSize() const { return normalizedSize_; }
        glm::vec2 getPixelOffset() const { return pixelOffset_; }
        void setPixelOffset(glm::vec2 off) { pixelOffset_ = off; }
        void adjustPixelOffset(float dx, float dy) { pixelOffset_.x += dx; pixelOffset_.y += dy; }

    protected:
        void renderLabel(UiEngine& engine, glm::vec4 color);

        glm::vec2 position_{0.0f};
        glm::vec2 size_{0.0f};
        glm::vec4 color_{1.0f};
        bool visible_ = true;
        std::string name_;
        uint32_t elementId_ = 0;
        uint32_t styleIndex_ = 0;
        glm::vec2 normalizedPos_{0.0f};
        glm::vec2 normalizedSize_{0.0f};
        glm::vec2 pixelOffset_{0.0f};
        glm::vec2 interactionSize_{0.0f};
        Label label_;
    };

} // namespace kc
