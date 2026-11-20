#pragma once

#include <cstdint>
#include <vector>

namespace kc::link {

    // KCLink transports opaque bytes; applications define what they mean.
    class Message {
    public:
        Message() = default;
        explicit Message(std::vector<std::uint8_t> payload);

        const std::vector<std::uint8_t>& payload() const noexcept;
        std::vector<std::uint8_t>& payload() noexcept;

    private:
        std::vector<std::uint8_t> data;
    };

}
