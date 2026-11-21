#include "WatchdogMain.hpp"
#include "GameEngineProcess.hpp"
#include "Watchdog.hpp"

#include <KCLink/KCLink.hpp>

#include <exception>
#include <iostream>

namespace kc {
    int WatchdogMain::initialize() {
        constexpr const char* ipcPath = "/tmp/kingscraft.sock";

        link::Server server;
        try {
            server.start(ipcPath);
        } catch (const std::exception& error) {
            std::cerr << "[Watchdog] Could not start IPC server: " << error.what() << std::endl;
            return 1;
        }

        std::cout << "[Watchdog] Starting Game Engine..." << std::endl;
        pid_t process;
        try {
            process = GameEngineProcess::start();
        } catch (const std::exception& error) {
            std::cerr << "[Watchdog] Could not start Game Engine: " << error.what() << std::endl;
            server.stop();
            return 1;
        }

        int exitCode = 1;
        try {
            Watchdog watchdog;
            exitCode = watchdog.run(server, process);
        } catch (const std::exception& error) {
            std::cerr << "[Watchdog] Watchdog failure: " << error.what() << std::endl;
            GameEngineProcess::terminateAndWait(process);
            exitCode = 1;
        }

        server.stop();
        std::cout << "[Watchdog] Game Engine stopped with exit code " << exitCode << std::endl;
        return exitCode;
    }
}
