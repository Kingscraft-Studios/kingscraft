#include "UI/Engine/UiEngine.hpp"
#include "Threads/Logger.hpp"
#include "Util/StringBuilder.hpp"

namespace lve {

    UiEngine::UiEngine(Device& device, DescriptorManager& descriptorManager, VkExtent2D extent)
        : device_(device),
          descriptorManager_(descriptorManager),
          extent_(extent) {
        stylePool_.reserve(UiRenderer::MAX_STYLES);
        elementStyles_.resize(UiRenderer::MAX_ELEMENTS, 0);
        styleDirty_.resize(UiRenderer::MAX_ELEMENTS, false);
    }

    UiEngine::~UiEngine() {
        shutdown();
    }

    void UiEngine::init() {
        if (initialized_) return;

        batchQueue_ = std::make_unique<UiBatchQueue>(device_);
        fontAtlas_ = std::make_unique<UiFontAtlas>(device_);
        renderer_ = std::make_unique<UiRenderer>(device_, descriptorManager_, extent_);

        renderer_->init();
        initialized_ = true;
    }

    void UiEngine::shutdown() {
        if (!initialized_) return;

        renderer_->shutdown();
        renderer_.reset();
        fontAtlas_.reset();
        batchQueue_.reset();

        initialized_ = false;
    }

    void UiEngine::resize(VkExtent2D extent) {
        extent_ = extent;
        if (initialized_) {
            renderer_->resize(extent);
        }
    }

    void UiEngine::beginFrame() {
        batchQueue_->begin();
    }

    void UiEngine::renderOffscreen(VkCommandBuffer cmd, uint32_t frameIndex) {
        if (!initialized_) return;

        if (fontAtlas_->isReady() && !renderer_->hasFontAtlas()) {
            renderer_->setFontAtlas(fontAtlas_->getImageView(), fontAtlas_->getSampler());
        }

        uploadPendingStyles();

        renderer_->renderOffscreen(cmd, *batchQueue_, frameIndex);
    }

    void UiEngine::composite(VkCommandBuffer cmd, uint32_t frameIndex) {
        if (!initialized_) return;
        renderer_->composite(cmd, frameIndex);
    }

    void UiEngine::addQuad(const UiVertex verts[4], const uint32_t indices[6]) {
        batchQueue_->addQuad(verts, indices);
    }

    bool UiEngine::loadFont(const std::string& ttfPath, const std::string& name) {
        return fontAtlas_->loadFont(ttfPath, name);
    }

    const UiFontAtlas::Glyph* UiEngine::getGlyph(const std::string& font, char32_t codepoint) const {
        return fontAtlas_->getGlyph(font, codepoint);
    }

    void UiEngine::uploadStylePool(const void* data, uint32_t count) {
        if (initialized_) renderer_->uploadStylePool(data, count);
    }

    void UiEngine::uploadElementStyles(const void* data, uint32_t firstElement, uint32_t count) {
        if (initialized_) renderer_->uploadElementStyles(data, firstElement, count);
    }

    // ---- Style System ----

    uint32_t UiEngine::registerStyle(const UiStyle& style) {
        uint32_t index = static_cast<uint32_t>(stylePool_.size());
        stylePool_.push_back(style.toGpu());
        stylePoolDirty_ = true;
        return index;
    }

    void UiEngine::updateStylePool() {
        stylePoolDirty_ = true;
    }

    void UiEngine::markDirty(uint32_t elementId, int styleIndex) {
        if (elementId >= UiRenderer::MAX_ELEMENTS) return;
        if (styleIndex >= 0) elementStyles_[elementId] = static_cast<uint32_t>(styleIndex);
        if (!styleDirty_[elementId]) {
            styleDirty_[elementId] = true;
            firstDirty_ = std::min(firstDirty_, elementId);
            lastDirty_ = std::max(lastDirty_, elementId + 1);
        }
    }

    void UiEngine::setElementStyle(uint32_t id, uint32_t styleIndex) {
        if (id < UiRenderer::MAX_ELEMENTS) {
            elementStyles_[id] = styleIndex;
            styleDirty_[id] = true;
            firstDirty_ = std::min(firstDirty_, id);
            lastDirty_ = std::max(lastDirty_, id + 1);
        }
    }

    uint32_t UiEngine::allocateElementId() {
        return nextElementId_++;
    }

    void UiEngine::uploadPendingStyles() {
        if (stylePoolDirty_ && !stylePool_.empty()) {
            uploadStylePool(stylePool_.data(),
                static_cast<uint32_t>(stylePool_.size()));
            stylePoolDirty_ = false;
        }

        if (firstDirty_ < lastDirty_) {
            uint32_t uploadCount = lastDirty_ - firstDirty_;
            uploadElementStyles(
                &elementStyles_[firstDirty_],
                firstDirty_,
                uploadCount);
            for (uint32_t i = firstDirty_; i < lastDirty_; i++) {
                styleDirty_[i] = false;
            }
            firstDirty_ = UiRenderer::MAX_ELEMENTS;
            lastDirty_ = 0;
        }
    }

    // ---- Debug Editing Mode ----

    void UiEngine::setDebugMode(bool on) {
        if (!debugEditor_) debugEditor_ = std::make_unique<UiDebugEditor>(*this);
        debugEditor_->setEnabled(on);
        if (!on) {
            onElementRemoved(nullptr);
        }
    }

    bool UiEngine::isDebugModeOn() const {
        return debugEditor_ && debugEditor_->isEnabled();
    }

    void UiEngine::logSelectedElementPosition() {
        if (debugEditor_) debugEditor_->logElementState();
    }

    void UiEngine::handleDebugMouseMove(float x, float y, const std::vector<UiElement*>& elements, float parentW, float parentH) {
        if (debugEditor_) debugEditor_->onMouseMove(x, y, elements, parentW, parentH);
    }

    void UiEngine::handleDebugMouseButton(int button, int action, float x, float y, const std::vector<UiElement*>& elements) {
        if (debugEditor_) debugEditor_->onMouseButton(button, action, x, y, elements);
    }

    void UiEngine::renderDebugOverlays(const std::vector<UiElement*>& elements) {
        (void)elements;
        if (debugEditor_) debugEditor_->renderOverlays();
    }

    void UiEngine::onElementRemoved(UiElement* element) {
        if (debugEditor_) debugEditor_->onElementRemoved(element);
    }

} // namespace lve
