#include "Watchdog.hpp"

#include "GameEngineProcess.hpp"

#include <KCLink/KCLink.hpp>

#include <iostream>
#include <memory>
#include <string>
#include <utility>

namespace kc {

    struct Watchdog::Context {
        link::EventLoop loop;
        std::unique_ptr<link::Connection> connection;
        pid_t process = -1;
        bool connectionAccepted = false;
        bool connectionClosed = false;
        bool childExited = false;
        link::DisconnectReason reason = link::DisconnectReason::PeerLost;
        int exitCode = 1;
    };

    int Watchdog::run(link::Server& server, pid_t engineProcess) {
        Context context;
        context.process = engineProcess;

        link::ProcessWatcher processWatcher(engineProcess);
        context.loop.watch(processWatcher, [&context]() { onProcessExit(context); });
        context.loop.watch(server, [&context](link::Connection connection) {
            onConnectionAccepted(context, std::move(connection), context.process);
        });

        context.loop.run();
        return context.exitCode;
    }

    void Watchdog::onProcessExit(Context& context) {
        context.exitCode = GameEngineProcess::waitForExit(context.process);

        if (!context.connectionAccepted) {
            std::cerr << "[Watchdog] Game Engine exited before connecting.\n";
            context.connectionClosed = true;
        } else if (!context.connectionClosed) {
            std::cerr << "[Watchdog] Game Engine exited without disconnecting from KCLink.\n";
            context.connectionClosed = true;
        }

        context.childExited = true;
        stopWhenDone(context);
    }

    void Watchdog::onConnectionAccepted(Context& context, link::Connection connection,
                                        pid_t expectedProcess) {
        if (context.connectionAccepted) {
            std::cerr << "[Watchdog] Rejected a second IPC connection.\n";
            connection.abort();
            return;
        }
        if (connection.peerProcessId() != expectedProcess) {
            std::cerr << "[Watchdog] Rejected an IPC connection from another process.\n";
            connection.abort();
            return;
        }

        context.connection = std::make_unique<link::Connection>(std::move(connection));
        context.connectionAccepted = true;
        context.loop.watch(*context.connection,
            [&context](link::Connection&, const link::Message& message) {
                onMessage(context, message);
            },
            [&context](link::Connection&, link::DisconnectReason reason) {
                onDisconnect(context, reason);
            });
    }

    void Watchdog::onMessage(Context& context, const link::Message& message) {
        const std::string request = message.asText();
        if (request != "Ping") {
            std::cerr << "[Watchdog] Unexpected message from Game Engine: " << request << "\n";
            return;
        }

        if (context.connection)
            context.connection->sendMessage(link::Message::fromText("Pong"));
        std::cout << "[Watchdog] Received Ping and sent Pong." << std::endl;
    }

    void Watchdog::onDisconnect(Context& context, link::DisconnectReason reason) {
        context.reason = reason;
        if (reason == link::DisconnectReason::Graceful) {
            std::cout << "[Watchdog] Game Engine disconnected gracefully." << std::endl;
        } else {
            std::cerr << "[Watchdog] Game Engine connection lost abruptly (crash or kill)."
                      << std::endl;
        }

        context.connectionClosed = true;
        stopWhenDone(context);
    }

    void Watchdog::stopWhenDone(Context& context) {
        if (context.connectionClosed && context.childExited)
            context.loop.quit();
    }

}
