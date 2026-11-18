#pragma once

#include "Core/FrameContext.hpp"
#include <memory>
#include <vector>
#include <functional>

namespace kc {

class PostProcessEffect {
public:
    virtual ~PostProcessEffect() = default;
    virtual const char* name() const = 0;

    virtual void preScene(VkCommandBuffer cmd, uint32_t frameIndex,
                          const std::function<void(const FrameContext&)>& renderGlow,
                          const FrameContext& ctx) = 0;

    virtual void postScene(VkCommandBuffer cmd, uint32_t frameIndex,
                           const FrameContext& ctx) = 0;
};

class PostProcessing {
public:
    void addEffect(std::unique_ptr<PostProcessEffect> effect);

    PostProcessEffect* getEffect(const char* name);

    void preScene(const FrameContext& ctx,
                  const std::function<void(const FrameContext&)>& renderGlow);

    void postScene(const FrameContext& ctx);

private:
    std::vector<std::unique_ptr<PostProcessEffect>> effects_;
};

} // namespace kc
