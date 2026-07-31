#include "UI/UiWrapper.hpp"
#include "UI/Elements/UiButton.hpp"
#include "Vulkan/Device.hpp"
#include "Vulkan/DescriptorManager.hpp"

namespace lve {

    UiWrapper::UiWrapper() {
    }

    UiWrapper::~UiWrapper() {
        shutdown();
    }

    void UiWrapper::init(Device& device, DescriptorManager& descriptorManager, VkExtent2D extent) {
        std::lock_guard<std::recursive_mutex> lock(uiMutex_);
        if (initialized_) return;

        width_ = static_cast<int>(extent.width);
        height_ = static_cast<int>(extent.height);

        engine_ = std::make_unique<UiEngine>(device, descriptorManager, extent);
        engine_->init();

        engine_->loadFont("resources/fonts/Arial.ttf", "default");

        initialized_ = true;
    }

    void UiWrapper::shutdown() {
        std::lock_guard<std::recursive_mutex> lock(uiMutex_);
        if (!initialized_) return;
        elements_.clear();
        engine_->shutdown();
        engine_.reset();
        initialized_ = false;
    }

    void UiWrapper::update(double deltaTime) {
        std::lock_guard<std::recursive_mutex> lock(uiMutex_);
        (void)deltaTime;
    }

    void UiWrapper::resize(int width, int height) {
        std::lock_guard<std::recursive_mutex> lock(uiMutex_);
        width_ = width;
        height_ = height;
        if (engine_) {
            engine_->resize({static_cast<uint32_t>(width), static_cast<uint32_t>(height)});
        }
        for (auto* element : elements_) {
            element->updateLayout(static_cast<float>(width), static_cast<float>(height));
        }
    }

    void UiWrapper::onMouseMove(double x, double y) {
        std::lock_guard<std::recursive_mutex> lock(uiMutex_);
        if (!engine_) return;

        if (engine_->isDebugModeOn()) {
            engine_->handleDebugMouseMove(
                static_cast<float>(x), static_cast<float>(y),
                elements_, static_cast<float>(width_), static_cast<float>(height_));
            return;
        }

        // Normal mode: button hover state updates
        glm::vec2 point(static_cast<float>(x), static_cast<float>(y));
        for (auto* element : elements_) {
            auto* button = dynamic_cast<UiButton*>(element);
            if (button) {
                bool prevHover = button->isHovered();
                button->setHovered(button->containsPoint(point));
                if (prevHover != button->isHovered()) {
                    markDirty(button->getElementId());
                }
            }
        }
    }

    void UiWrapper::onMouseButton(int button, int action, int mods, double x, double y) {
        std::lock_guard<std::recursive_mutex> lock(uiMutex_);
        (void)mods;
        if (!engine_) return;

        if (engine_->isDebugModeOn()) {
            engine_->handleDebugMouseButton(
                button, action,
                static_cast<float>(x), static_cast<float>(y),
                elements_);
            return;
        }

        // Normal mode: button click handling
        if (button != 0 || action != 1) return;

        glm::vec2 point(static_cast<float>(x), static_cast<float>(y));

        for (auto it = elements_.rbegin(); it != elements_.rend(); ++it) {
            auto* btn = dynamic_cast<UiButton*>(*it);
            if (btn && btn->containsPoint(point)) {
                btn->click();
                std::string name = btn->getHandlerName();
                if (!name.empty()) {
                    auto handlerIt = buttonHandlers_.find(name);
                    if (handlerIt != buttonHandlers_.end()) {
                        handlerIt->second();
                    }
                }
                return;
            }
        }
    }

    void UiWrapper::onScroll(double dx, double dy) {
        std::lock_guard<std::recursive_mutex> lock(uiMutex_);
        if (scrollCallback_) scrollCallback_(dx, dy);
    }

    void UiWrapper::onKey(int key, int action) {
        std::lock_guard<std::recursive_mutex> lock(uiMutex_);
        (void)key; (void)action;
    }

    void UiWrapper::onChar(unsigned int codepoint) {
        std::lock_guard<std::recursive_mutex> lock(uiMutex_);
        (void)codepoint;
    }

    void UiWrapper::renderOffscreen(VkCommandBuffer cmdBuffer, uint32_t frameIndex) {
        std::lock_guard<std::recursive_mutex> lock(uiMutex_);
        if (!engine_) return;

        engine_->beginFrame();

        for (auto* element : elements_) {
            element->render(*engine_);
        }

        if (engine_->isDebugModeOn()) {
            engine_->renderDebugOverlays(elements_);
        }

        engine_->renderOffscreen(cmdBuffer, frameIndex);
    }

    void UiWrapper::render(VkCommandBuffer cmdBuffer, VkRenderPass renderPass, uint32_t frameIndex) {
        std::lock_guard<std::recursive_mutex> lock(uiMutex_);
        if (!engine_) return;

        if (renderPass != currentRenderPass_) {
            currentRenderPass_ = renderPass;
            engine_->getRenderer().setTargetRenderPass(renderPass);
        }

        engine_->composite(cmdBuffer, frameIndex);
    }

    void UiWrapper::addElement(UiElement* element) {
        std::lock_guard<std::recursive_mutex> lock(uiMutex_);
        uint32_t id = engine_->allocateElementId();
        element->setElementId(id);
        engine_->setElementStyle(id, element->getStyleIndex());
        elements_.push_back(element);
    }

    void UiWrapper::removeElement(UiElement* element) {
        std::lock_guard<std::recursive_mutex> lock(uiMutex_);
        auto it = std::find(elements_.begin(), elements_.end(), element);
        if (it != elements_.end()) {
            elements_.erase(it);
        }
        engine_->onElementRemoved(element);
    }

    void UiWrapper::markDirty(uint32_t elementId) {
        std::lock_guard<std::recursive_mutex> lock(uiMutex_);
        if (!engine_) return;
        int styleIndex = -1;
        for (auto* element : elements_) {
            if (element->getElementId() == elementId) {
                styleIndex = static_cast<int>(element->getActiveStyleIndex());
                break;
            }
        }
        engine_->markDirty(elementId, styleIndex);
    }

    void UiWrapper::registerButtonHandler(std::string name, std::function<void()> handler) {
        std::lock_guard<std::recursive_mutex> lock(uiMutex_);
        buttonHandlers_[name] = std::move(handler);
    }

    void UiWrapper::setScrollCallback(ScrollCallback cb) {
        std::lock_guard<std::recursive_mutex> lock(uiMutex_);
        scrollCallback_ = std::move(cb);
    }

    uint32_t UiWrapper::registerStyle(const UiStyle& style) {
        std::lock_guard<std::recursive_mutex> lock(uiMutex_);
        return engine_ ? engine_->registerStyle(style) : 0;
    }

    void UiWrapper::updateStylePool() {
        std::lock_guard<std::recursive_mutex> lock(uiMutex_);
        if (engine_) engine_->updateStylePool();
    }

    void UiWrapper::setBlockTexture(VkImageView imageView, VkSampler sampler) {
        std::lock_guard<std::recursive_mutex> lock(uiMutex_);
        if (engine_) engine_->getRenderer().setBlockTexture(imageView, sampler);
    }

    void UiWrapper::setDebugMode(bool on) {
        std::lock_guard<std::recursive_mutex> lock(uiMutex_);
        if (engine_) engine_->setDebugMode(on);
    }

    bool UiWrapper::isDebugModeOn() const {
        std::lock_guard<std::recursive_mutex> lock(uiMutex_);
        return engine_ && engine_->isDebugModeOn();
    }

    void UiWrapper::logSelectedElementPosition() {
        std::lock_guard<std::recursive_mutex> lock(uiMutex_);
        if (engine_) engine_->logSelectedElementPosition();
    }

} // namespace lve
