#include "WatchdogMain.hpp"
#include "GameEngineProcess.hpp"

#include <KCLink/KCLink.hpp>

#include <chrono>
#include <iostream>
#include <exception>
#include <string>
#include <utility>
#include <vector>

namespace kc {
    int WatchdogMain::initialize() {
        constexpr const char* ipcPath = "/tmp/kingscraft.sock";

        kc::link::Server server;
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
            return 1;
        }

        kc::link::Connection connection;
        const auto startupDeadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
        try {
            bool connected = false;
            while (!connected) {
                auto candidate = server.acceptFor(std::chrono::milliseconds(100));
                if (candidate) {
                    if (candidate->peerProcessId() != process) {
                        std::cerr << "[Watchdog] Rejected an IPC connection from another process.\n";
                    } else {
                        connection = std::move(*candidate);
                        connected = true;
                    }
                }
                if (connected) break;

                int childExitCode = 0;
                if (GameEngineProcess::tryWaitForExit(process, childExitCode)) {
                    std::cerr << "[Watchdog] Game Engine exited before connecting.\n";
                    return childExitCode;
                }
                if (std::chrono::steady_clock::now() >= startupDeadline) {
                    std::cerr << "[Watchdog] Timed out waiting for Game Engine to connect.\n";
                    GameEngineProcess::terminateAndWait(process);
                    return 1;
                }
            }

            connection.setReceiveTimeout(std::chrono::seconds(5));
            kc::link::Message incoming;
            if (!connection.receive(incoming)) {
                std::cerr << "[Watchdog] Game Engine disconnected before sending Ping.\n";
                GameEngineProcess::terminateAndWait(process);
                return 1;
            }

            const std::string request(incoming.payload().begin(), incoming.payload().end());
            if (request != "Ping") {
                std::cerr << "[Watchdog] Unexpected message from Game Engine: "
                          << request << std::endl;
                connection.close();
                GameEngineProcess::terminateAndWait(process);
                return 1;
            }

            const std::string response = "Pong";
            connection.send(kc::link::Message(
                std::vector<std::uint8_t>(response.begin(), response.end())));
        } catch (const std::exception& error) {
            std::cerr << "[Watchdog] KCLink handshake failed: " << error.what() << std::endl;
            connection.close();
            GameEngineProcess::terminateAndWait(process);
            return 1;
        }

        std::cout << "[Watchdog] Received Ping and sent Pong." << std::endl;

        const int exitCode = GameEngineProcess::waitForExit(process);

        std::cout << "[Watchdog] Game Engine stopped with exit code " << exitCode << std::endl;
        return exitCode;
    }
}
