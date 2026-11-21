#pragma once

#include <KCLink/EventLoop.hpp>
#include <KCLink/Server.hpp>

#include <sys/types.h>

namespace kc {

    class Watchdog {
    public:
        int run(link::Server& server, pid_t engineProcess);

    private:
        struct Context;

        static void onProcessExit(Context& context);
        static void onConnectionAccepted(Context& context, link::Connection connection,
                                         pid_t expectedProcess);
        static void onMessage(Context& context, const link::Message& message);
        static void onDisconnect(Context& context, link::DisconnectReason reason);
        static void stopWhenDone(Context& context);
    };

}
