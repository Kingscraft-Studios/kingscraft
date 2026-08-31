#include "Core/Diagnostics/Metrics/Metrics.hpp"

namespace kc {

    void Metrics::clear() {
        current_ = FrameMetrics{};
    }

    GameLogicMetrics& Metrics::cpu() {
        return current_.cpu;
    }

    RenderThreadMetrics& Metrics::gpu() {
        return current_.gpu;
    }

    FrameMetrics Metrics::snapshot() const {
        return current_;
    }

}
