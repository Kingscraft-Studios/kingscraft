#include "UI/Engine/UiLabel.hpp"
#include "UI/Engine/UiEngine.hpp"
#include "UI/Engine/UiFontAtlas.hpp"
#include "UI/Engine/UiBatchQueue.hpp"
#include "Threads/Logger.hpp"
#include "Util/LogUtils.hpp"
#include "Util/StringBuilder.hpp"

namespace kc {

    float Label::measureWidth(UiEngine& engine) const {
        float scale = fontSize / UiFontAtlas::ATLAS_FONT_SIZE;
        float width = 0.0f;
        for (char32_t c : text) {
            const auto* glyph = engine.getGlyph(fontName, c);
            if (glyph) {
                width += glyph->advance * scale;
            }
        }
        return width;
    }

    bool Label::render(UiEngine& engine, glm::vec2 pos, glm::vec2 size,
                        uint32_t elementId, glm::vec4 activeColor) const {
        if (text.empty()) return false;

        float scale = fontSize / UiFontAtlas::ATLAS_FONT_SIZE;
        float textWidth = measureWidth(engine);

        float penX = pos.x;
        if (centered) {
            penX = pos.x + (size.x - textWidth) * 0.5f;
            if (penX < pos.x) penX = pos.x;
        }
        float penY = pos.y + size.y * 0.5f + fontSize * 0.35f;

        uint32_t indices[6] = {0, 1, 2, 0, 2, 3};

        bool clippedThisFrame = false;

        for (char32_t c : text) {
            const auto* g = engine.getGlyph(fontName, c);
            if (!g) continue;

            float gx0 = penX + g->bearingX * scale;
            float gy0 = penY + g->bearingY * scale;
            float gx1 = gx0 + g->width * scale;
            float gy1 = penY + (g->bearingY + g->height) * scale;

            if (gx1 > pos.x + size.x) {
                clippedThisFrame = true;
                if (!clipReported_) {
                    clipReported_ = true;
                    LogUtils::warn(ThreadName::Renderer,
                        StringBuilder::build("Label clipped: text \"", text, "\" overflows element bounds"));
                }
                break;
            }

            if (gx1 < pos.x) {
                penX += g->advance * scale;
                continue;
            }

            UiVertex verts[4] = {
                {{gx0, gy0}, {g->uv0.x, g->uv0.y}, activeColor, elementId},
                {{gx1, gy0}, {g->uv1.x, g->uv0.y}, activeColor, elementId},
                {{gx1, gy1}, {g->uv1.x, g->uv1.y}, activeColor, elementId},
                {{gx0, gy1}, {g->uv0.x, g->uv1.y}, activeColor, elementId},
            };
            engine.addQuad(verts, indices);

            penX += g->advance * scale;
        }

        if (!clippedThisFrame) {
            clipReported_ = false;
        }

        return clippedThisFrame;
    }

}
