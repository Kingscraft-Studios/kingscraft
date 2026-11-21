#include <KCLink/Server.hpp>

#include "internal/ServerCore.hpp"

#include <memory>
#include <stdexcept>

namespace kc::link {
    namespace {

        detail::ServerCore& requireCore(const std::unique_ptr<detail::ServerCore>& core) {
            if (!core) throw std::logic_error("KCLink: operation on an empty server");
            return *core;
        }

    }

    Server::Server() noexcept : core_(std::make_unique<detail::ServerCore>()) {}

    Server::~Server() = default;

    void Server::start(const std::string& socketPath) { requireCore(core_).start(socketPath); }

    bool Server::isRunning() const noexcept { return core_ && core_->isRunning(); }

    void Server::stop() noexcept {
        if (core_) core_->stop();
    }

    detail::ServerCore& Server::core() noexcept { return requireCore(core_); }

    const detail::ServerCore& Server::core() const noexcept { return requireCore(core_); }

}
