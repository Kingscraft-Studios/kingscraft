#pragma once

#ifndef KCLINK_INTERNAL_COMPILATION
#error "KCLink internal header: not part of the public API"
#endif

#include "internal/ConnectionCore.hpp"
#include "internal/ProcessCore.hpp"
#include "internal/ServerCore.hpp"
#include "internal/Unix/UniqueFd.hpp"

#include <KCLink/Connection.hpp>
#include <KCLink/EventLoop.hpp>
#include <KCLink/ProcessWatcher.hpp>
#include <KCLink/Server.hpp>

#include <cstdint>
#include <functional>
#include <map>

namespace kc::link::detail {

    class EventLoopCore {
    public:
        using MessageHandler = EventLoop::MessageHandler;
        using DisconnectHandler = EventLoop::DisconnectHandler;
        using AcceptHandler = EventLoop::AcceptHandler;
        using ProcessExitHandler = EventLoop::ProcessExitHandler;
        using ConnectionFactory = std::function<Connection(UniqueFd)>;

        EventLoopCore();
        ~EventLoopCore();

        EventLoopCore(const EventLoopCore&) = delete;
        EventLoopCore& operator=(const EventLoopCore&) = delete;

        void watchConnection(Connection& owner, ConnectionCore& core,
                             MessageHandler onMessage, DisconnectHandler onDisconnect);
        void watchServer(Server& owner, ServerCore& core,
                         ConnectionFactory factory, AcceptHandler onAccept);
        void watchProcess(ProcessWatcher& owner, ProcessCore& core,
                          ProcessExitHandler onExit);

        void run();
        void quit() noexcept;

    private:
        enum class Kind { Connection, Server, Process };

        struct Watch {
            Kind kind = Kind::Connection;
            Connection* connection = nullptr;
            ConnectionCore* connectionCore = nullptr;
            Server* server = nullptr;
            ServerCore* serverCore = nullptr;
            ProcessWatcher* process = nullptr;
            ProcessCore* processCore = nullptr;
            ConnectionFactory connectionFactory;
            MessageHandler onMessage;
            DisconnectHandler onDisconnect;
            AcceptHandler onAccept;
            ProcessExitHandler onExit;
        };

        int epollHandle = -1;
        int wakeHandle = -1;
        std::map<int, Watch> watches;

        void add(int descriptor, std::uint32_t events, Watch watch);
        void remove(int descriptor) noexcept;
        void dispatchConnection(int descriptor, std::uint32_t events, const Watch& watch);
        void dispatchServer(const Watch& watch);
        void dispatchProcess(int descriptor, const Watch& watch);
    };

}
