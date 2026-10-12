#pragma once

#include <array>
#include <atomic>
#include <cstdint>
#include <limits>

#include "Core/Observability/Metrics/MetricsTypes.hpp"

namespace kc {

    // Triple-buffered handoff of the render thread's metrics to the game logic
    // thread. Same protocol as FrameExchange, carrying the "gpu" half of
    // FrameMetrics: the renderer writes + publishes a frame, the game logic
    // thread reads a stable frame and calls endRead() once it has copied what it
    // needs. Lock-free; neither side ever blocks the other.
    class MetricsExchange {
    public:
        // Returns the buffer to write into, or nullptr if the reader is
        // currently using it and no spare buffer is available. A producer that
        // gets nullptr simply skips publishing this frame.
        RenderThreadMetrics* writeMetrics() {
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

        // Marks the published buffer as in use. The consumer must call
        // endRead() once it is finished copying from the returned metrics.
        const RenderThreadMetrics& readMetrics() const {
            uint32_t idx = publishedIndex_.load(std::memory_order_acquire);
            inUseIndex_.store(idx, std::memory_order_release);
            return buffers_[idx];
        }

        void endRead() const {
            inUseIndex_.store(std::numeric_limits<uint32_t>::max(), std::memory_order_release);
        }

    private:
        std::array<RenderThreadMetrics, 3> buffers_;
        uint32_t writeIndex_ = 0;
        std::atomic<uint32_t> publishedIndex_{0};
        mutable std::atomic<uint32_t> inUseIndex_{std::numeric_limits<uint32_t>::max()};
    };

} // namespace kc
