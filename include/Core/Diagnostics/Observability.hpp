#pragma once

#include "Core/Diagnostics/Metrics/Metrics.hpp"

namespace kc {

    // Owns the canonical metrics snapshot. Producers hand it the latest CPU
    // (game-logic) and GPU (render thread) metrics once per tick; it copies them
    // together and publishes a fire-and-forget MetricsUpdateEvent so listeners
    // can observe a coherent frame without racing the producers.
    class Observability {
    public:
        void start();
        void update(const GameLogicMetrics& cpu, const RenderThreadMetrics& gpu);
        void cleanup();

        FrameMetrics snapshot() const { return metrics_.snapshot(); }

    private:
        Metrics metrics_;
    };

}
