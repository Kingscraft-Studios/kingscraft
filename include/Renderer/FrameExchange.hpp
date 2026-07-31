#pragma once
#include "Renderer/FrameScene.hpp"
#include <array>
#include <atomic>
#include <cstdint>
#include <limits>

namespace lve {

class FrameExchange {
public:
    FrameExchange() = default;
    FrameExchange(const FrameExchange&) = delete;
    FrameExchange& operator=(const FrameExchange&) = delete;

    // Picks a buffer that is neither the one currently being drawn nor the
    // last published frame. Returns nullptr when no buffer is free; the
    // producer should then skip publishing this frame instead of overwriting
    // a buffer the render thread is still using.
    FrameScene* writeFrame() {
        uint32_t published = publishedIndex_.load(std::memory_order_acquire);
        uint32_t inUse = inUseIndex_.load(std::memory_order_acquire);
        for (uint32_t i = 0; i < 3; ++i) {
            if (i != writeIndex_ && i != published && i != inUse) {
                writeIndex_ = i;
                break;
            }
        }
        if (writeIndex_ == published || writeIndex_ == inUse) {
            return nullptr;
        }
        return &buffers_[writeIndex_];
    }

    void publish() {
        publishedIndex_.store(writeIndex_, std::memory_order_release);
    }

    // Marks the published buffer as in use. The consumer must call endRead()
    // once it is finished with the returned scene (after command recording).
    const FrameScene& readFrame() const {
        uint32_t idx = publishedIndex_.load(std::memory_order_acquire);
        inUseIndex_.store(idx, std::memory_order_release);
        return buffers_[idx];
    }

    void endRead() const {
        inUseIndex_.store(std::numeric_limits<uint32_t>::max(), std::memory_order_release);
    }

private:
    std::array<FrameScene, 3> buffers_; // Triple Buffer
    uint32_t writeIndex_ = 0;
    std::atomic<uint32_t> publishedIndex_{0};
    mutable std::atomic<uint32_t> inUseIndex_{std::numeric_limits<uint32_t>::max()};
};

} // namespace lve
