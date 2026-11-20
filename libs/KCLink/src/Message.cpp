#include <KCLink/Message.hpp>

#include <utility>

namespace kc::link {

    Message::Message(std::vector<std::uint8_t> payload)
        : data(std::move(payload)) {}

    const std::vector<std::uint8_t>& Message::payload() const noexcept {
        return data;
    }

    std::vector<std::uint8_t>& Message::payload() noexcept {
        return data;
    }

}
