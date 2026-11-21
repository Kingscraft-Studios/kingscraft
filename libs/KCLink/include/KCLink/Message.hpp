#pragma once

#include <KCLink/Export.hpp>

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace kc::link {

    // KCLink transports opaque bytes; applications define what they mean.
    // fromText/asText are conveniences for the common string payload case.
    class KCLINK_API Message {
    public:
        Message() = default;
        explicit Message(std::vector<std::uint8_t> payload);

        static Message fromText(std::string_view text);
        std::string asText() const;

        const std::vector<std::uint8_t>& payload() const noexcept;
        std::vector<std::uint8_t>& payload() noexcept;

    private:
        std::vector<std::uint8_t> data;
    };

}
