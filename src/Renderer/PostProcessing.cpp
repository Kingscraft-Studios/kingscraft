#include "Renderer/PostProcessing.hpp"
#include <cstring>

namespace kc {

void PostProcessing::addEffect(std::unique_ptr<PostProcessEffect> effect) {
    effects_.push_back(std::move(effect));
}

PostProcessEffect* PostProcessing::getEffect(const char* name) {
    for (auto& e : effects_) {
        if (std::strcmp(e->name(), name) == 0)
            return e.get();
    }
    return nullptr;
}

void PostProcessing::preScene(const FrameContext& ctx,
                              const std::function<void(const FrameContext&)>& renderGlow) {
    for (auto& e : effects_)
        e->preScene(ctx.cmd, ctx.frameIndex, renderGlow, ctx);
}

void PostProcessing::postScene(const FrameContext& ctx) {
    for (auto& e : effects_)
        e->postScene(ctx.cmd, ctx.frameIndex, ctx);
}

} // namespace kc
