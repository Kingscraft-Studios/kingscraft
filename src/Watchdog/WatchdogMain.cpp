#include "WatchdogMain.hpp"
#include "GameEngineProcess.hpp"

#include <iostream>

namespace kc {
    int WatchdogMain::initialize() {
        std::cout << "[Watchdog] Starting Game Engine..." << std::endl;

        const int exitCode = GameEngineProcess::run();

        std::cout << "[Watchdog] Game Engine stopped with exit code " << exitCode << std::endl;
        return exitCode;
    }
}
