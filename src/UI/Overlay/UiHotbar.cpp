#include "UI/Overlay/UiHotbar.hpp"
#include "UI/UiWrapper.hpp"
#include "UI/Engine/UiStyle.hpp"
#include <string>

namespace lve {

    void UiHotbar::init(UiWrapper& ui, float screenW, float screenH) {
        screenW_ = screenW;
        screenH_ = screenH;
        bgStyle_ = ui.registerStyle(UiStyle{
            .mode = RenderMode::Solid,
            .color1 = {0.1f, 0.1f, 0.1f, 0.8f},
        });
        slotStyle_ = ui.registerStyle(UiStyle{
            .mode = RenderMode::Solid,
            .color1 = {0.5f, 0.5f, 0.5f, 0.5f},
        });
        selectionStyle_ = ui.registerStyle(UiStyle{
            .mode = RenderMode::Solid,
            .color1 = {1.0f, 1.0f, 1.0f, 0.8f},
        });

        float totalW = totalWidth();
        float anchorX = -totalW / 2.0f;
        float pad = 4.0f;

        bg_.setAnchor({0.5f, 1.0f}, {anchorX - pad, -SLOT_SIZE - BOTTOM_MARGIN - pad});
        bg_.setSize({totalW + pad * 2, SLOT_SIZE + pad * 2});
        bg_.setStyleIndex(bgStyle_);
        bg_.setName("HotbarBg");
        ui.addElement(&bg_);

        selection_.setAnchor({0.5f, 1.0f}, {anchorX, -SLOT_SIZE - BOTTOM_MARGIN});
        selection_.setSize({SLOT_SIZE, SLOT_SIZE});
        selection_.setStyleIndex(selectionStyle_);
        selection_.setName("HotbarSel");
        ui.addElement(&selection_);

        for (int i = 0; i < SLOT_COUNT; ++i) {
            float offsetX = anchorX + i * (SLOT_SIZE + SLOT_GAP);

            slots_[i].setAnchor({0.5f, 1.0f}, {offsetX, -SLOT_SIZE - BOTTOM_MARGIN});
            slots_[i].setSize({SLOT_SIZE, SLOT_SIZE});
            slots_[i].setStyleIndex(slotStyle_);
            slots_[i].setName("HotbarSlot_" + std::to_string(i));
            ui.addElement(&slots_[i]);

            slotNumbers_[i].setAnchor({0.5f, 1.0f}, {offsetX + 4, -SLOT_SIZE - BOTTOM_MARGIN + 4});
            slotNumbers_[i].setSize({16, 16});
            slotNumbers_[i].setText(std::to_string((i + 1) % 10));
            slotNumbers_[i].setFont("default");
            slotNumbers_[i].setFontSize(12.0f);
            slotNumbers_[i].setColor({1.0f, 1.0f, 1.0f, 0.7f});
            slotNumbers_[i].setStyleIndex(slotStyle_);
            slotNumbers_[i].setName("HotbarNum_" + std::to_string(i));
            ui.addElement(&slotNumbers_[i]);
        }
    }

    void UiHotbar::cleanup(UiWrapper& ui) {
        ui.removeElement(&bg_);
        ui.removeElement(&selection_);
        for (int i = 0; i < SLOT_COUNT; ++i) {
            ui.removeElement(&slots_[i]);
            ui.removeElement(&slotNumbers_[i]);
        }
    }

    void UiHotbar::selectSlot(int index) {
        selectedSlot_ = ((index % SLOT_COUNT) + SLOT_COUNT) % SLOT_COUNT;

        float totalW = totalWidth();
        float anchorX = -totalW / 2.0f;
        float offsetX = anchorX + selectedSlot_ * (SLOT_SIZE + SLOT_GAP);
        selection_.setPixelOffset({offsetX, -SLOT_SIZE - BOTTOM_MARGIN});
        selection_.setPosition({
            0.5f * screenW_ + offsetX,
            screenH_ - SLOT_SIZE - BOTTOM_MARGIN
        });
    }

} // namespace lve
