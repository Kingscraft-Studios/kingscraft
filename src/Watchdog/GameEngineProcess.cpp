#include "GameEngineProcess.hpp"

#include <spawn.h>
#include <signal.h>
#include <sys/wait.h>
#include <unistd.h>

#include <cerrno>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <system_error>
#include <thread>

extern char** environ;

namespace kc {
    namespace {
        int exitCodeFromStatus(int status) {
            if (WIFEXITED(status)) return WEXITSTATUS(status);
            if (WIFSIGNALED(status)) return 128 + WTERMSIG(status);
            return 1;
        }
    }

    pid_t GameEngineProcess::start() {
        char executable[] = "/proc/self/exe";
        char gameEngineFlag[] = "--gamengine";
        char ipcArgument[] = "--ipcfile=/tmp/kingscraft.sock";
        char* arguments[] = {executable, gameEngineFlag, ipcArgument, nullptr};

        pid_t child = -1;
        const int spawnError = ::posix_spawn(
            &child, executable, nullptr, nullptr, arguments, environ);
        if (spawnError != 0) {
            throw std::system_error(spawnError, std::generic_category(),
                                     "Watchdog: could not start game engine");
        }
        return child;
    }

    int GameEngineProcess::waitForExit(pid_t process) {
        int status = 0;
        while (::waitpid(process, &status, 0) < 0) {
            if (errno == EINTR) continue;
            std::fprintf(stderr, "Watchdog: could not wait for game engine: %s\n",
                         std::strerror(errno));
            return 1;
        }

        return exitCodeFromStatus(status);
    }

    bool GameEngineProcess::tryWaitForExit(pid_t process, int& exitCode) {
        int status = 0;
        pid_t result;
        do {
            result = ::waitpid(process, &status, WNOHANG);
        } while (result < 0 && errno == EINTR);

        if (result < 0)
            throw std::system_error(errno, std::generic_category(), "waitpid");
        if (result == 0) return false;

        exitCode = exitCodeFromStatus(status);
        return true;
    }

    int GameEngineProcess::terminateAndWait(pid_t process) {
        if (::kill(process, SIGTERM) < 0 && errno != ESRCH) {
            std::fprintf(stderr, "Watchdog: could not stop game engine: %s\n",
                         std::strerror(errno));
        }

        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(1);
        while (std::chrono::steady_clock::now() < deadline) {
            int status = 0;
            const pid_t result = ::waitpid(process, &status, WNOHANG);
            if (result == process) return exitCodeFromStatus(status);
            if (result < 0 && errno != EINTR) {
                std::fprintf(stderr, "Watchdog: could not wait for game engine: %s\n",
                             std::strerror(errno));
                return 1;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(20));
        }

        if (::kill(process, SIGKILL) < 0 && errno != ESRCH) {
            std::fprintf(stderr, "Watchdog: could not force-stop game engine: %s\n",
                         std::strerror(errno));
        }
        return waitForExit(process);
    }

}
