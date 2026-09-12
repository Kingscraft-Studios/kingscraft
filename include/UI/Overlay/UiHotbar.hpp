#pragma once

#include "UI/Elements/UiRect.hpp"
#include "UI/Elements/UiTextBlock.hpp"
#include "UI/Elements/UiImage.hpp"
#include "UI/Elements/UiGroup.hpp"
#include "Core/RegistryKey.hpp"
#include <atomic>

namespace kc { class Block; }

namespace kc {

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
        // Recomputes the block-icon texture layers after a registry reload and
        // patches the existing styles in place (no pool growth).
        void refresh(UiWrapper& ui);

        void resize(float screenW, float screenH);
        void selectSlot(UiWrapper& ui, int index);
        int getSelectedSlot() const { return selectedSlot_.load(std::memory_order_relaxed); }

        const RegistryKey<Block>& getSlotBlock(int slot) const {
            static const RegistryKey<Block> kInvalid;
            return (slot >= 0 && slot < SLOT_COUNT) ? slotBlocks_[slot] : kInvalid;
        }

    private:
        float totalWidth() const {
            return SLOT_COUNT * SLOT_SIZE + (SLOT_COUNT - 1) * SLOT_GAP;
        }

        UiGroup group_;
        std::atomic<int> selectedSlot_{0};
        float screenW_ = 0.0f;
        float screenH_ = 0.0f;

        UiRect bg_;
        UiRect selection_;
        UiRect slots_[SLOT_COUNT];
        UiImage slotIcons_[SLOT_COUNT];
        UiTextBlock slotNumbers_[SLOT_COUNT];

        RegistryKey<Block> slotBlocks_[SLOT_COUNT] = {};

        uint32_t bgStyle_ = 0;
        uint32_t slotStyle_ = 0;
        uint32_t selectionStyle_ = 0;
        uint32_t iconStyles_[SLOT_COUNT] = {};
    };

} // namespace kc
