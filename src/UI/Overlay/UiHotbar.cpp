#include "UI/Overlay/UiHotbar.hpp"
#include "UI/UiWrapper.hpp"
#include "UI/Engine/UiStyle.hpp"
#include "Core/Blocks/Blocks.hpp"
#include "Core/Blocks/Block.hpp"
#include <string>

namespace kc {

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

        // Register icon styles: first 3 slots use block textures, rest are solid
        Block* grass = Blocks::GRASS_BLOCK;
        Block* dirt = Blocks::DIRT;
        Block* stone = Blocks::STONE;
        slotBlocks_[0] = Blocks::GRASS_BLOCK;
        slotBlocks_[1] = Blocks::DIRT;
        slotBlocks_[2] = Blocks::STONE;

        auto iconLayer = [](Block* block) -> int {
            if (!block) return 0;
            const Quad* side = block->getModel().findQuad(FaceDir::PosX);
            return block->getTextureBaseOffset() + (side ? side->tileIndex : 0);
        };

        int layers[3] = {
            iconLayer(grass),
            iconLayer(dirt),
            iconLayer(stone),
        };

        for (int i = 0; i < SLOT_COUNT; ++i) {
            if (i < 3) {
                iconStyles_[i] = ui.registerStyle(UiStyle{
                    .mode = RenderMode::Texture,
                    .color1 = {1.0f, 1.0f, 1.0f, 1.0f},
                    .textureLayer = static_cast<float>(layers[i]),
                });
            } else {
                iconStyles_[i] = ui.registerStyle(UiStyle{
                    .mode = RenderMode::Solid,
                    .color1 = {0.3f, 0.3f, 0.3f, 0.5f},
                });
            }
        }

        float totalW = totalWidth();
        float anchorX = -totalW / 2.0f;
        float pad = 4.0f;

        bg_.setAnchor({0.5f, 1.0f}, {anchorX - pad, -SLOT_SIZE - BOTTOM_MARGIN - pad});
        bg_.setSize({totalW + pad * 2, SLOT_SIZE + pad * 2});
        bg_.setStyleIndex(bgStyle_);
        bg_.setName("HotbarBg");
        group_.add(&bg_);

        selection_.setAnchor({0.5f, 1.0f}, {anchorX, -SLOT_SIZE - BOTTOM_MARGIN});
        selection_.setSize({SLOT_SIZE, SLOT_SIZE});
        selection_.setStyleIndex(selectionStyle_);
        selection_.setName("HotbarSel");
        group_.add(&selection_);

        for (int i = 0; i < SLOT_COUNT; ++i) {
            float offsetX = anchorX + i * (SLOT_SIZE + SLOT_GAP);

            slots_[i].setAnchor({0.5f, 1.0f}, {offsetX, -SLOT_SIZE - BOTTOM_MARGIN});
            slots_[i].setSize({SLOT_SIZE, SLOT_SIZE});
            slots_[i].setStyleIndex(slotStyle_);
            slots_[i].setName("HotbarSlot_" + std::to_string(i));
            group_.add(&slots_[i]);

            slotIcons_[i].setAnchor({0.5f, 1.0f}, {offsetX + 2, -SLOT_SIZE - BOTTOM_MARGIN + 2});
            slotIcons_[i].setSize({SLOT_SIZE - 4, SLOT_SIZE - 4});
            slotIcons_[i].setStyleIndex(iconStyles_[i]);
            slotIcons_[i].setName("HotbarIcon_" + std::to_string(i));
            group_.add(&slotIcons_[i]);

            slotNumbers_[i].setAnchor({0.5f, 1.0f}, {offsetX + 4, -SLOT_SIZE - BOTTOM_MARGIN + 4});
            slotNumbers_[i].setSize({16, 16});
            slotNumbers_[i].setText(std::to_string((i + 1) % 10));
            slotNumbers_[i].setFont("default");
            slotNumbers_[i].setFontSize(12.0f);
            slotNumbers_[i].setColor({1.0f, 1.0f, 1.0f, 0.7f});
            slotNumbers_[i].setStyleIndex(slotStyle_);
            slotNumbers_[i].setName("HotbarNum_" + std::to_string(i));
            group_.add(&slotNumbers_[i]);
        }

        group_.addToWrapper(ui);
    }

    void UiHotbar::refresh(UiWrapper& ui) {
        UiGuard guard(ui);

        auto iconLayer = [](Block* block) -> int {
            if (!block) return 0;
            const Quad* side = block->getModel().findQuad(FaceDir::PosX);
            return block->getTextureBaseOffset() + (side ? side->tileIndex : 0);
        };

        int layers[3] = {
            iconLayer(Blocks::GRASS_BLOCK),
            iconLayer(Blocks::DIRT),
            iconLayer(Blocks::STONE),
        };

        for (int i = 0; i < 3; ++i) {
            ui.updateStyle(iconStyles_[i], UiStyle{
                .mode = RenderMode::Texture,
                .color1 = {1.0f, 1.0f, 1.0f, 1.0f},
                .textureLayer = static_cast<float>(layers[i]),
            });
        }
    }

    void UiHotbar::cleanup(UiWrapper& ui) {
        group_.removeFromWrapper(ui);
    }

    void UiHotbar::resize(float screenW, float screenH) {
        screenW_ = screenW;
        screenH_ = screenH;
    }

    void UiHotbar::selectSlot(UiWrapper& ui, int index) {
        UiGuard guard(ui);
        selectedSlot_ = ((index % SLOT_COUNT) + SLOT_COUNT) % SLOT_COUNT;

        float totalW = totalWidth();
        float anchorX = -totalW / 2.0f;
        float offsetX = anchorX + selectedSlot_.load(std::memory_order_relaxed) * (SLOT_SIZE + SLOT_GAP);
        selection_.setPixelOffset({offsetX, -SLOT_SIZE - BOTTOM_MARGIN});
        selection_.updateLayout(screenW_, screenH_);
    }

} // namespace kc
