#include <KCLink/EventLoop.hpp>

#include "internal/ConnectionCore.hpp"
#include "internal/EventLoopCore.hpp"
#include "internal/Unix/UniqueFd.hpp"

#include <KCLink/Connection.hpp>
#include <KCLink/ProcessWatcher.hpp>
#include <KCLink/Server.hpp>

#include <memory>
#include <utility>

namespace kc::link {

    EventLoop::EventLoop() : impl(std::make_unique<detail::EventLoopCore>()) {}

    EventLoop::~EventLoop() = default;

    void EventLoop::watch(Connection& connection, MessageHandler onMessage,
                          DisconnectHandler onDisconnect) {
        impl->watchConnection(connection, connection.core(),
                              std::move(onMessage), std::move(onDisconnect));
    }

    void EventLoop::watch(Server& server, AcceptHandler onAccept) {
        impl->watchServer(server, server.core(),
            [](detail::UniqueFd fd) {
                return Connection(std::make_unique<detail::ConnectionCore>(std::move(fd)));
            },
            std::move(onAccept));
    }

    void EventLoop::watch(ProcessWatcher& process, ProcessExitHandler onExit) {
        impl->watchProcess(process, process.core(), std::move(onExit));
    }

    void EventLoop::run() { impl->run(); }

    void EventLoop::quit() { impl->quit(); }

}
