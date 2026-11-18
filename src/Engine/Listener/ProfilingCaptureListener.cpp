#include "Listener/ProfilingCaptureListener.hpp"

#include "Core/Runtime.hpp"
#include "Threads/RenderThread.hpp"
#include "UI/Debug/ProfilingCapture.hpp"

namespace kc {

    void ProfilingCaptureListener::onMetricsUpdate(MetricsUpdateEvent& event) {
        if (!Runtime::get().renderThread) return;
        ProfilingCapture& capture = Runtime::get().renderThread->getProfilerCapture();
        if (capture.isActive())
            capture.feedFrame(event.getMetrics());
    }

}
