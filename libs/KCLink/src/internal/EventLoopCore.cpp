#include "internal/EventLoopCore.hpp"

#include <sys/epoll.h>
#include <sys/eventfd.h>
#include <unistd.h>

#include <cerrno>
#include <system_error>
#include <utility>
#include <vector>

#ifndef EPOLLRDHUP
#define EPOLLRDHUP 0x2000
#endif

namespace kc::link::detail {

    EventLoopCore::EventLoopCore() {
        epollHandle = ::epoll_create1(EPOLL_CLOEXEC);
        if (epollHandle < 0)
            throw std::system_error(errno, std::generic_category(), "epoll_create1");

        wakeHandle = ::eventfd(0, EFD_CLOEXEC | EFD_NONBLOCK);
        if (wakeHandle < 0) {
            const int error = errno;
            ::close(epollHandle);
            epollHandle = -1;
            throw std::system_error(error, std::generic_category(), "eventfd");
        }

        epoll_event event{};
        event.events = EPOLLIN;
        event.data.fd = wakeHandle;
        if (::epoll_ctl(epollHandle, EPOLL_CTL_ADD, wakeHandle, &event) < 0) {
            const int error = errno;
            ::close(wakeHandle);
            ::close(epollHandle);
            wakeHandle = -1;
            epollHandle = -1;
            throw std::system_error(error, std::generic_category(), "epoll_ctl");
        }
    }

    EventLoopCore::~EventLoopCore() {
        if (wakeHandle >= 0) ::close(wakeHandle);
        if (epollHandle >= 0) ::close(epollHandle);
    }

    void EventLoopCore::add(int descriptor, std::uint32_t events, Watch watch) {
        const auto existing = watches.find(descriptor);
        const int operation = existing == watches.end() ? EPOLL_CTL_ADD : EPOLL_CTL_MOD;

        epoll_event event{};
        event.events = events;
        event.data.fd = descriptor;
        if (::epoll_ctl(epollHandle, operation, descriptor, &event) < 0)
            throw std::system_error(errno, std::generic_category(), "epoll_ctl");

        watches[descriptor] = std::move(watch);
    }

    void EventLoopCore::remove(int descriptor) noexcept {
        ::epoll_ctl(epollHandle, EPOLL_CTL_DEL, descriptor, nullptr);
        watches.erase(descriptor);
    }

    void EventLoopCore::watchConnection(Connection& owner, ConnectionCore& core,
                                        MessageHandler onMessage,
                                        DisconnectHandler onDisconnect) {
        Watch watch;
        watch.kind = Kind::Connection;
        watch.connection = &owner;
        watch.connectionCore = &core;
        watch.onMessage = std::move(onMessage);
        watch.onDisconnect = std::move(onDisconnect);
        add(core.handle(), EPOLLIN | EPOLLHUP | EPOLLERR | EPOLLRDHUP, std::move(watch));
    }

    void EventLoopCore::watchServer(Server& owner, ServerCore& core,
                                    ConnectionFactory factory, AcceptHandler onAccept) {
        Watch watch;
        watch.kind = Kind::Server;
        watch.server = &owner;
        watch.serverCore = &core;
        watch.connectionFactory = std::move(factory);
        watch.onAccept = std::move(onAccept);
        add(core.handle(), EPOLLIN | EPOLLHUP | EPOLLERR, std::move(watch));
    }

    void EventLoopCore::watchProcess(ProcessWatcher& owner, ProcessCore& core,
                                     ProcessExitHandler onExit) {
        Watch watch;
        watch.kind = Kind::Process;
        watch.process = &owner;
        watch.processCore = &core;
        watch.onExit = std::move(onExit);
        add(core.handle(), EPOLLIN | EPOLLHUP | EPOLLERR, std::move(watch));
    }

    void EventLoopCore::dispatchConnection(int descriptor, std::uint32_t events,
                                           const Watch& watch) {
        ConnectionCore& core = *watch.connectionCore;

        const auto disconnect = [&](DisconnectReason reason) {
            remove(descriptor);
            if (reason == DisconnectReason::PeerLost) core.abort();
            if (watch.onDisconnect) watch.onDisconnect(*watch.connection, reason);
        };

        if (events & EPOLLIN) {
            try {
                bool keepReading = true;
                while (keepReading && core.isOpen()) {
                    Message message;
                    switch (core.tryReceive(message)) {
                        case ReceiveStatus::Message:
                            if (watch.onMessage) watch.onMessage(*watch.connection, message);
                            break;
                        case ReceiveStatus::GracefulClose:
                            disconnect(DisconnectReason::Graceful);
                            return;
                        case ReceiveStatus::PeerLost:
                            disconnect(DisconnectReason::PeerLost);
                            return;
                        case ReceiveStatus::WouldBlock:
                            keepReading = false;
                            break;
                    }
                }
            } catch (...) {
                core.abort();
                disconnect(DisconnectReason::PeerLost);
                return;
            }
        }

        if (core.isOpen() && (events & (EPOLLHUP | EPOLLERR | EPOLLRDHUP))) {
            core.abort();
            disconnect(DisconnectReason::PeerLost);
        }
    }

    void EventLoopCore::dispatchServer(const Watch& watch) {
        ServerCore& core = *watch.serverCore;
        while (auto descriptor = core.tryAcceptFd()) {
            if (watch.onAccept)
                watch.onAccept(watch.connectionFactory(std::move(*descriptor)));
        }
    }

    void EventLoopCore::dispatchProcess(int descriptor, const Watch& watch) {
        std::uint64_t value = 0;
        while (::read(descriptor, &value, sizeof(value)) > 0) {}
        remove(descriptor);
        if (watch.onExit) watch.onExit();
        (void)watch.process;
        (void)watch.processCore;
    }

    void EventLoopCore::run() {
        std::uint64_t value = 0;
        while (::read(wakeHandle, &value, sizeof(value)) > 0) {}

        std::vector<epoll_event> events(64);
        while (true) {
            const int count = ::epoll_wait(epollHandle, events.data(),
                                           static_cast<int>(events.size()), -1);
            if (count < 0) {
                if (errno == EINTR) continue;
                throw std::system_error(errno, std::generic_category(), "epoll_wait");
            }

            for (int index = 0; index < count; ++index) {
                const int descriptor = events[index].data.fd;
                const std::uint32_t flags = events[index].events;

                if (descriptor == wakeHandle) {
                    while (::read(wakeHandle, &value, sizeof(value)) > 0) {}
                    return;
                }

                const auto entry = watches.find(descriptor);
                if (entry == watches.end()) continue;
                const Watch watch = entry->second;

                switch (watch.kind) {
                    case Kind::Connection:
                        dispatchConnection(descriptor, flags, watch);
                        break;
                    case Kind::Server:
                        dispatchServer(watch);
                        break;
                    case Kind::Process:
                        dispatchProcess(descriptor, watch);
                        break;
                }
            }
        }
    }

    void EventLoopCore::quit() noexcept {
        if (wakeHandle < 0) return;
        const std::uint64_t one = 1;
        const ssize_t written = ::write(wakeHandle, &one, sizeof(one));
        (void)written;
    }

}
