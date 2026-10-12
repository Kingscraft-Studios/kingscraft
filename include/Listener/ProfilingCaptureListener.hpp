#pragma once

#include "Event/Events/MetricsUpdateEvent.hpp"
#include "Event/KCEventHandler.hpp"
#include "Event/Listener.hpp"
#include "Event/detail/EventRegistrar.hpp"

namespace kc {

    // Feeds the on-demand profiler capture from the GameLogic thread.
    // Observability fires MetricsUpdateEvent from the engine thread; the
    // dispatcher routes it here, so the capture is fed on the same thread it
    // always was (previously WorldScreen::render).
    class ProfilingCaptureListener : public Listener {
    public:
        // Must be public: EventManager's generated dispatch lambda calls the
        // kcOnEvent helper from outside the class.
        KC_EVENT_HANDLER(MetricsUpdateEvent, onMetricsUpdate, ThreadName::GameLogic)

    private:
        void onMetricsUpdate(MetricsUpdateEvent& event);
    };

}
