#include "Core/Diagnostics/Diagnostics.hpp"

#include "Event/EventManager.hpp"
#include "Event/Events/MetricsUpdateEvent.hpp"

namespace kc {

    void Diagnostics::start() {
        metrics_.clear();
    }

    void Diagnostics::update(const GameLogicMetrics& cpu, const RenderThreadMetrics& gpu) {
        // Copy both halves into one snapshot before publishing, so the event
        // carries a consistent frame and never aliases the live metrics.
        metrics_.cpu() = cpu;
        metrics_.gpu() = gpu;

        EventManager::get().callEventAsync<MetricsUpdateEvent>(metrics_.snapshot());
    }

    void Diagnostics::cleanup() {
        metrics_.clear();
    }

}
