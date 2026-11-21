#include <KCLink/EventLoop.hpp>

#include <KCLink/Connection.hpp>
#include <KCLink/ProcessWatcher.hpp>
#include <KCLink/Server.hpp>

#include <sys/epoll.h>
#include <sys/eventfd.h>
#include <unistd.h>

#include <cerrno>
#include <map>
#include <system_error>
#include <utility>
#include <vector>

#ifndef EPOLLRDHUP
#define EPOLLRDHUP 0x2000
#endif

namespace kc::link {

    struct EventLoop::Impl {
        enum class Kind { Connection, Server, Process };

        struct Watch {
            Kind kind;
            void* object = nullptr;
            MessageHandler onMessage;
            DisconnectHandler onDisconnect;
            AcceptHandler onAccept;
            ProcessExitHandler onExit;
        };

        int epollHandle = -1;
        int wakeHandle = -1;
        std::map<int, Watch> watches;

        void add(int descriptor, std::uint32_t events, Watch watch) {
            const auto existing = watches.find(descriptor);
            const int operation = existing == watches.end() ? EPOLL_CTL_ADD : EPOLL_CTL_MOD;
            epoll_event event{};
            event.events = events;
            event.data.fd = descriptor;
            if (::epoll_ctl(epollHandle, operation, descriptor, &event) < 0)
                throw std::system_error(errno, std::generic_category(), "epoll_ctl");
            watches[descriptor] = std::move(watch);
        }

        void remove(int descriptor) noexcept {
            ::epoll_ctl(epollHandle, EPOLL_CTL_DEL, descriptor, nullptr);
            watches.erase(descriptor);
        }

        void dispatchConnection(int descriptor, std::uint32_t events, const Watch& watch) {
            auto& connection = *static_cast<Connection*>(watch.object);

            const auto disconnect = [&](DisconnectReason reason) {
                remove(descriptor);
                if (watch.onDisconnect) watch.onDisconnect(connection, reason);
            };

            if (events & EPOLLIN) {
                try {
                    bool keepReading = true;
                    while (keepReading && connection.isOpen()) {
                        Message message;
                        switch (connection.tryReceive(message)) {
                            case ReceiveStatus::Message:
                                if (watch.onMessage) watch.onMessage(connection, message);
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
                    connection.close();
                    disconnect(DisconnectReason::PeerLost);
                    return;
                }
            }

            if (connection.isOpen() && (events & (EPOLLHUP | EPOLLERR | EPOLLRDHUP)))
                disconnect(DisconnectReason::PeerLost);
        }

        void dispatchServer(const Watch& watch) {
            auto& server = *static_cast<Server*>(watch.object);
            while (auto candidate = server.tryAccept()) {
                if (watch.onAccept) watch.onAccept(std::move(*candidate));
            }
        }

        void dispatchProcess(int descriptor, const Watch& watch) {
            auto& process = *static_cast<ProcessWatcher*>(watch.object);
            std::uint64_t value = 0;
            while (::read(descriptor, &value, sizeof(value)) > 0) {}
            remove(descriptor);
            if (watch.onExit) watch.onExit();
            (void)process;
        }
    };

    EventLoop::EventLoop() : impl(std::make_unique<Impl>()) {
        impl->epollHandle = ::epoll_create1(EPOLL_CLOEXEC);
        if (impl->epollHandle < 0)
            throw std::system_error(errno, std::generic_category(), "epoll_create1");

        impl->wakeHandle = ::eventfd(0, EFD_CLOEXEC | EFD_NONBLOCK);
        if (impl->wakeHandle < 0) {
            const int error = errno;
            ::close(impl->epollHandle);
            impl->epollHandle = -1;
            throw std::system_error(error, std::generic_category(), "eventfd");
        }

        epoll_event event{};
        event.events = EPOLLIN;
        event.data.fd = impl->wakeHandle;
        if (::epoll_ctl(impl->epollHandle, EPOLL_CTL_ADD, impl->wakeHandle, &event) < 0) {
            const int error = errno;
            ::close(impl->wakeHandle);
            ::close(impl->epollHandle);
            impl->wakeHandle = -1;
            impl->epollHandle = -1;
            throw std::system_error(error, std::generic_category(), "epoll_ctl");
        }
    }

    EventLoop::~EventLoop() {
        if (impl->wakeHandle >= 0) ::close(impl->wakeHandle);
        if (impl->epollHandle >= 0) ::close(impl->epollHandle);
    }

    void EventLoop::watch(Connection& connection, MessageHandler onMessage,
                          DisconnectHandler onDisconnect) {
        Impl::Watch watch;
        watch.kind = Impl::Kind::Connection;
        watch.object = &connection;
        watch.onMessage = std::move(onMessage);
        watch.onDisconnect = std::move(onDisconnect);
        impl->add(connection.nativeHandle(),
                  EPOLLIN | EPOLLHUP | EPOLLERR | EPOLLRDHUP, std::move(watch));
    }

    void EventLoop::watch(Server& server, AcceptHandler onAccept) {
        Impl::Watch watch;
        watch.kind = Impl::Kind::Server;
        watch.object = &server;
        watch.onAccept = std::move(onAccept);
        impl->add(server.nativeHandle(), EPOLLIN | EPOLLHUP | EPOLLERR, std::move(watch));
    }

    void EventLoop::watch(ProcessWatcher& process, ProcessExitHandler onExit) {
        Impl::Watch watch;
        watch.kind = Impl::Kind::Process;
        watch.object = &process;
        watch.onExit = std::move(onExit);
        impl->add(process.nativeHandle(), EPOLLIN | EPOLLHUP | EPOLLERR, std::move(watch));
    }

    void EventLoop::run() {
        std::uint64_t value = 0;
        while (::read(impl->wakeHandle, &value, sizeof(value)) > 0) {}

        std::vector<epoll_event> events(64);
        while (true) {
            const int count = ::epoll_wait(impl->epollHandle, events.data(),
                                           static_cast<int>(events.size()), -1);
            if (count < 0) {
                if (errno == EINTR) continue;
                throw std::system_error(errno, std::generic_category(), "epoll_wait");
            }

            for (int index = 0; index < count; ++index) {
                const int descriptor = events[index].data.fd;
                const std::uint32_t flags = events[index].events;

                if (descriptor == impl->wakeHandle) {
                    while (::read(impl->wakeHandle, &value, sizeof(value)) > 0) {}
                    return;
                }

                const auto entry = impl->watches.find(descriptor);
                if (entry == impl->watches.end()) continue;
                const Impl::Watch watch = entry->second;

                switch (watch.kind) {
                    case Impl::Kind::Connection:
                        impl->dispatchConnection(descriptor, flags, watch);
                        break;
                    case Impl::Kind::Server:
                        impl->dispatchServer(watch);
                        break;
                    case Impl::Kind::Process:
                        impl->dispatchProcess(descriptor, watch);
                        break;
                }
            }
        }
    }

    void EventLoop::quit() {
        if (!impl || impl->wakeHandle < 0) return;
        const std::uint64_t one = 1;
        (void)::write(impl->wakeHandle, &one, sizeof(one));
    }

}
