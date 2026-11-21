#include <KCLink/Connection.hpp>

#include "internal/ConnectionCore.hpp"

#include <memory>
#include <stdexcept>
#include <utility>

namespace kc::link {
    namespace {

        detail::ConnectionCore& requireCore(
            const std::unique_ptr<detail::ConnectionCore>& core) {
            if (!core) throw std::logic_error("KCLink: operation on an empty connection");
            return *core;
        }

    }

    Connection::Connection() noexcept : core_(nullptr) {}

    Connection::~Connection() = default;

    Connection::Connection(Connection&& other) noexcept = default;

    Connection& Connection::operator=(Connection&& other) noexcept = default;

    Connection::Connection(std::unique_ptr<detail::ConnectionCore> core) noexcept
        : core_(std::move(core)) {}

    detail::ConnectionCore& Connection::core() noexcept { return requireCore(core_); }

    const detail::ConnectionCore& Connection::core() const noexcept { return requireCore(core_); }

    bool Connection::isOpen() const noexcept { return core_ && core_->isOpen(); }

    pid_t Connection::peerProcessId() const { return requireCore(core_).peerProcessId(); }

    void Connection::sendMessage(const Message& message) { requireCore(core_).sendMessage(message); }

    void Connection::beginMessage(std::uint64_t totalPayloadSize) {
        requireCore(core_).beginMessage(totalPayloadSize);
    }

    void Connection::appendChunk(std::span<const std::uint8_t> chunk) {
        requireCore(core_).appendChunk(chunk);
    }

    void Connection::endMessage() { requireCore(core_).endMessage(); }

    bool Connection::isSendingMessage() const noexcept {
        return core_ && core_->isSendingMessage();
    }

    void Connection::setMaxMessageSize(std::uint64_t size) noexcept {
        if (core_) core_->setMaxMessageSize(size);
    }

    std::uint64_t Connection::maxMessageSize() const noexcept {
        return core_ ? core_->maxMessageSize() : 0;
    }

    void Connection::disconnect() {
        if (core_) core_->disconnect();
    }

    void Connection::abort() noexcept {
        if (core_) core_->abort();
    }

}
