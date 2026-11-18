#pragma once

#include "UI/Elements/UiElement.hpp"
#include <string>
#include <functional>

namespace kc {

    class UiButton : public UiElement {
    public:
        UiButton() = default;
        ~UiButton() override = default;

        UiButton(const UiButton&) = delete;
        UiButton& operator=(const UiButton&) = delete;

        void render(UiEngine& engine) override;
        bool containsPoint(glm::vec2 point) const override;

        void setOnClick(std::function<void()> callback) { onClick_ = std::move(callback); }
        void setHandlerName(const std::string& name) { handlerName_ = name; }

        const std::string& getHandlerName() const { return handlerName_; }

        void setHovered(bool hovered) { isHovered_ = hovered; }
        bool isHovered() const { return isHovered_; }

        void setNormalColor(glm::vec4 color) { normalColor_ = color; }
        void setHoverColor(glm::vec4 color) { hoverColor_ = color; }
        const glm::vec4& getNormalColor() const { return normalColor_; }
        const glm::vec4& getHoverColor() const { return hoverColor_; }

        void setHoverStyleIndex(uint32_t index) { hoverStyleIndex_ = index; }
        uint32_t getHoverStyleIndex() const { return hoverStyleIndex_; }
        uint32_t getActiveStyleIndex() const override {
            return isHovered_ ? hoverStyleIndex_ : styleIndex_;
        }

        void setNormalTextColor(const glm::vec4& c) { normalTextColor_ = c; }
        void setHoverTextColor(const glm::vec4& c) { hoverTextColor_ = c; }
        const glm::vec4& getActiveTextColor() const {
            return isHovered_ ? hoverTextColor_ : normalTextColor_;
        }

        void click();

    private:
        std::string handlerName_;
        bool isHovered_ = false;

        glm::vec4 normalColor_{0.3f, 0.3f, 0.3f, 1.0f};
        glm::vec4 hoverColor_{0.5f, 0.5f, 0.5f, 1.0f};
        glm::vec4 normalTextColor_{1.0f, 1.0f, 1.0f, 1.0f};
        glm::vec4 hoverTextColor_{1.0f, 1.0f, 1.0f, 1.0f};

        uint32_t hoverStyleIndex_ = 0;

        std::function<void()> onClick_;
    };

} // namespace kc
