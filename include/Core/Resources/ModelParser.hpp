#pragma once

#include <functional>
#include <string>
#include "Core/Resources/BlockModel.hpp"

namespace kc {

class ModelParser {
public:
    static void loadAsync(const std::string& path,
                          std::function<void(BlockModel)> callback);
};

} // namespace kc
