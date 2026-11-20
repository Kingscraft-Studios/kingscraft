#include "GameEngineProcess.hpp"

#include <spawn.h>
#include <sys/wait.h>
#include <unistd.h>

#include <cerrno>
#include <cstdio>
#include <cstring>

extern char** environ;

namespace kc {

    int GameEngineProcess::run() {
        char executable[] = "/proc/self/exe";
        char gameEngineFlag[] = "--gamengine";
        char ipcArgument[] = "--ipcfile=/tmp/kingscraft.sock";
        char* arguments[] = {executable, gameEngineFlag, ipcArgument, nullptr};

        pid_t child = -1;
        const int spawnError = ::posix_spawn(
            &child, executable, nullptr, nullptr, arguments, environ);
        if (spawnError != 0) {
            std::fprintf(stderr, "Watchdog: could not start game engine: %s\n",
                         std::strerror(spawnError));
            return 1;
        }

        int status = 0;
        while (::waitpid(child, &status, 0) < 0) {
            if (errno == EINTR) continue;
            std::fprintf(stderr, "Watchdog: could not wait for game engine: %s\n",
                         std::strerror(errno));
            return 1;
        }

        if (WIFEXITED(status)) return WEXITSTATUS(status);
        if (WIFSIGNALED(status)) return 128 + WTERMSIG(status);
        return 1;
    }

}
