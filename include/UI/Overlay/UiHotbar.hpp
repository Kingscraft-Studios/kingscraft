#pragma once

#include "UI/Elements/UiRect.hpp"
#include "UI/Elements/UiTextBlock.hpp"
#include "UI/Elements/UiImage.hpp"
#include "Core/RegistryKey.hpp"

namespace lve { class Block; }

namespace lve {

    class UiWrapper;

    class UiHotbar {
    public:
        static constexpr int SLOT_COUNT = 9;
        static constexpr float SLOT_SIZE = 40.0f;
        static constexpr float SLOT_GAP = 4.0f;
        static constexpr float BOTTOM_MARGIN = 20.0f;

        UiHotbar() = default;
        ~UiHotbar() = default;

        void init(UiWrapper& ui, float screenW, float screenH);
        void cleanup(UiWrapper& ui);

        void resize(float screenW, float screenH);
        void selectSlot(int index);
        int getSelectedSlot() const { return selectedSlot_; }

        const RegistryKey<Block>* getSlotBlock(int slot) const {
            return (slot >= 0 && slot < SLOT_COUNT) ? slotBlocks_[slot] : nullptr;
        }

    private:
        float totalWidth() const {
            return SLOT_COUNT * SLOT_SIZE + (SLOT_COUNT - 1) * SLOT_GAP;
        }

        int selectedSlot_ = 0;
        float screenW_ = 0.0f;
        float screenH_ = 0.0f;

        UiRect bg_;
        UiRect selection_;
        UiRect slots_[SLOT_COUNT];
        UiImage slotIcons_[SLOT_COUNT];
        UiTextBlock slotNumbers_[SLOT_COUNT];

        const RegistryKey<Block>* slotBlocks_[SLOT_COUNT] = {};

        uint32_t bgStyle_ = 0;
        uint32_t slotStyle_ = 0;
        uint32_t selectionStyle_ = 0;
        uint32_t iconStyles_[SLOT_COUNT] = {};
    };

} // namespace lve
