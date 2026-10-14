#pragma once
#include "IOTemplateBase.hpp"

#include <string>

namespace kc {
    // Append-only log sink. Kept deliberately dumb: no parsing, no reads, and
    // critically no logging of its own failures (DiskLayer::append bypasses
    // LogUtils) so a broken disk cannot recurse back into the logger.
    class LogTemplate final : public IOTemplateBase {
    public:
        explicit LogTemplate(DiskLayer& disk) : IOTemplateBase(disk) {}

        void append(const std::string& path, const std::string& text) {
            appendText(path, text);
        }
    };
}
