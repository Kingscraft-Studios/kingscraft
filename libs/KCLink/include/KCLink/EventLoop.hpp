#pragma once

#include <KCLink/Message.hpp>

#include <functional>
#include <memory>

namespace kc::link {

    class Connection;
    class Server;
    class ProcessWatcher;

    enum class DisconnectReason {
        Graceful,
        PeerLost
    };

    // epoll-based dispatcher. run() blocks until quit() is called or the
    // watched objects are gone; no polling or timeouts are involved.
    // Watched objects must outlive the EventLoop.
    class EventLoop {
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
        struct Impl;
        std::unique_ptr<Impl> impl;
    };

}
