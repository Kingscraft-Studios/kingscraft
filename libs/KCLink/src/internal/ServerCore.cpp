#include "internal/ServerCore.hpp"

#include <stdexcept>
#include <utility>

namespace kc::link::detail {

    ServerCore::ServerCore() noexcept = default;

    ServerCore::~ServerCore() { stop(); }

    void ServerCore::start(const std::string& socketPath) {
        if (isRunning()) throw std::logic_error("KCLink: server is already running");

        Listener listener = listenLocal(socketPath);
        socket = std::move(listener.socket);
        identity = std::move(listener.identity);
    }

    bool ServerCore::isRunning() const noexcept { return socket.valid(); }

    void ServerCore::stop() noexcept {
        if (socket.valid()) closeSocket(socket);
        removeSocketPathIfOwned(identity);
        identity = SocketIdentity{};
    }

    std::optional<UniqueFd> ServerCore::tryAcceptFd() {
        if (!isRunning()) return std::nullopt;
        return acceptLocal(socket);
    }

    int ServerCore::handle() const noexcept { return socket.get(); }

}
