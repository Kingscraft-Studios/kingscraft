#pragma once

#include <KCLink/Export.hpp>
#include <KCLink/Message.hpp>

#include <functional>
#include <memory>

namespace kc::link {

    class Connection;
    class Server;
    class ProcessWatcher;

    namespace detail {
        struct EventLoopCore;
    }

    enum class DisconnectReason {
        Graceful,
        PeerLost
    };

    // Platform event dispatcher. run() blocks until quit() is called or no
    // watched objects remain active. Watched objects must outlive the loop.
    class KCLINK_API EventLoop {
    public:
        using MessageHandler = std::function<void(Connection&, const Message&)>;
        using DisconnectHandler = std::function<void(Connection&, DisconnectReason)>;
        using AcceptHandler = std::function<void(Connection)>;
        using ProcessExitHandler = std::function<void()>;

        EventLoop();
        ~EventLoop();

        EventLoop(const EventLoop&) = delete;
        EventLoop& operator=(const EventLoop&) = delete;

        void watch(Connection& connection, MessageHandler onMessage,
                   DisconnectHandler onDisconnect);
        void watch(Server& server, AcceptHandler onAccept);
        void watch(ProcessWatcher& process, ProcessExitHandler onExit);

        void run();
        void quit();

    private:
        std::unique_ptr<detail::EventLoopCore> impl;
    };

}
