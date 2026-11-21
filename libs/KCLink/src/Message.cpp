#include <KCLink/Message.hpp>

namespace kc::link {

    Message::Message(std::vector<std::uint8_t> payload) : data(std::move(payload)) {}

    Message Message::fromText(std::string_view text) {
        return Message(std::vector<std::uint8_t>(text.begin(), text.end()));
    }

    std::string Message::asText() const {
        return std::string(data.begin(), data.end());
    }

    const std::vector<std::uint8_t>& Message::payload() const noexcept { return data; }
    std::vector<std::uint8_t>& Message::payload() noexcept { return data; }

}
