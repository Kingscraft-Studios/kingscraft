#include <iostream>
#include <string>
#include <string_view>

#include "GameEngineMain.hpp"
#include "WatchdogMain.hpp"

int main(int argc, char* argv[]) {
    if (argc == 1) {
        return kc::WatchdogMain::initialize();
    }

    constexpr std::string_view engineFlag = "--gamengine";
    constexpr std::string_view ipcPrefix = "--ipcfile=";

    bool runGameEngine = false;
    std::string ipcFilePath;

    for (int i = 1; i < argc; ++i) {
        const std::string_view arg = argv[i];

        if (arg == engineFlag) {
            runGameEngine = true;
        } else if (arg.starts_with(ipcPrefix)) {
            ipcFilePath = arg.substr(ipcPrefix.size());
        } else {
            std::cerr << "Unknown argument: " << arg << '\n';
            return 2;
        }
    }

    if (!runGameEngine) {
        std::cerr << "Use --gamengine to start the game engine.\n";
        return 2;
    }

    if (ipcFilePath.empty()) {
        std::cerr << "--gamengine requires --ipcfile=<path/to/file.sock>\n";
        return 2;
    }

    return kc::GameEngineMain::initialize(ipcFilePath);
}
