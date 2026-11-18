#pragma once
#include <cctype>
#include <sstream>
#include <string>

namespace kc {
    class StringUtils {
    public:
        static std::string toLower(const std::string& value) {
            std::string out = value;
            for (char& c : out)
                c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
            return out;
        }

        template<typename... Args>
        static std::string build(Args... args) {
            std::stringstream ss;

            (ss << ... << args);

            return ss.str();
        }
    };
}
