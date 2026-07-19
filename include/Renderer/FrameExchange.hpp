#pragma once
#include "Renderer/FrameScene.hpp"
#include <array>
#include <atomic>

namespace lve {

class FrameExchange {
public:
    FrameExchange() = default;
    FrameExchange(const FrameExchange&) = delete;
    FrameExchange& operator=(const FrameExchange&) = delete;

    FrameScene& writeFrame()
    {
        return buffers_[writeIndex_];
    }

    void publish()
    {
        publishedIndex_.store(writeIndex_, std::memory_order_release);
        writeIndex_ = 1 - writeIndex_;
    }

    const FrameScene& readFrame() const
    {
        return buffers_[publishedIndex_.load(std::memory_order_acquire)];
    }

private:
    std::array<FrameScene, 2> buffers_;
    uint32_t writeIndex_ = 0;
    std::atomic<uint32_t> publishedIndex_{0};
};

} // namespace lve
