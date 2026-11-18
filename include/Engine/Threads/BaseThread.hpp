#pragma once

namespace kc {
    class BaseThread {
    public:
        BaseThread() = default;
        virtual ~BaseThread() = default;

        /* Use method to Initialize Thread Specific Resources Before the run() */
        virtual void start() = 0;

        /* Signal the Thread to Stop.*/
        /* WARNING: DO NOT USE THIS function for cleanup use stop() instead. */
        virtual void signalQuit() = 0;

        /* Cleanup Thread Resources */
        virtual void stop() = 0;

        /* Main Run Loop */
        virtual void run() = 0;
    };
}