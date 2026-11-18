#include "Core/Observability/Observability.hpp"

#include "Event/EventManager.hpp"
#include "Event/Events/MetricsUpdateEvent.hpp"

namespace kc {

    void Observability::start() {
        metrics_.clear();
    }

    void Observability::update(const GameLogicMetrics& cpu, const RenderThreadMetrics& gpu) {
        // Copy both halves into one snapshot before publishing, so the event
        // carries a consistent frame and never aliases the live metrics.
        metrics_.cpu() = cpu;
        metrics_.gpu() = gpu;

        EventManager::get().callEventAsync<MetricsUpdateEvent>(metrics_.snapshot());
    }

    void Observability::cleanup() {
        metrics_.clear();
    }

}
