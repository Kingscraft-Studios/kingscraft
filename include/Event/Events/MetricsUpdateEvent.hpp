#pragma once

#include "Core/Diagnostics/Metrics/MetricsTypes.hpp"
#include "Event.hpp"

namespace kc {

    // Fired by Diagnostics once it has copied the latest game-logic and render
    // thread metrics into a coherent snapshot. Dispatched fire-and-forget: the
    // carrier is a by-value copy, so listeners on other threads never race the
    // producers.
    class MetricsUpdateEvent : public Event {
    public:
        explicit MetricsUpdateEvent(FrameMetrics metrics) : metrics_(metrics) {}

        const FrameMetrics& getMetrics() const { return metrics_; }

    private:
        FrameMetrics metrics_;
    };

}
