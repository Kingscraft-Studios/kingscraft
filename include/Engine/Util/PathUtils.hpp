#pragma once
#include <string>

namespace kc {
    class PathUtils {
    public:
        static std::string basename(const std::string& path) {
            const auto pos = path.find_last_of("/\\");
            return pos == std::string::npos ? path : path.substr(pos + 1);
        }

        static std::string stem(const std::string& path) {
            const std::string base = basename(path);
            const auto pos = base.find_last_of('.');
            return (pos == std::string::npos || pos == 0) ? base : base.substr(0, pos);
        }
    };
}
