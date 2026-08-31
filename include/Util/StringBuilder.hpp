#pragma once
#include <sstream>
#include <string>

namespace kc {

class StringBuilder {
public:
    template<typename... Args>
    static std::string build(Args... args) {
        std::stringstream ss;

        (ss << ... << args);

        return ss.str();
    }
};

}
