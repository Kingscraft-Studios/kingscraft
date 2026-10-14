#pragma once
#include <array>
#include <cstdint>

#include "Cache/Constants/ShaderCacheConstants.hpp"

namespace kc {
    class ShaderCacheValidator {
    public:
        // True if the cached header was produced from `sourceHash` by the
        // current cache version. Logs the rejection reason otherwise.
        bool validate(const ShaderCacheHeader& header,
                      const std::array<uint8_t, 16>& sourceHash) const;
    };
}
