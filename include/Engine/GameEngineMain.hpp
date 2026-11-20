#pragma once
#include <string>

namespace kc {
    class GameEngineMain {
    public:
        static int initialize(const std::string& ipcPath);
    };
}
