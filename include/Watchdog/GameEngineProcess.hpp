#pragma once

#include <sys/types.h>

namespace kc {

    class GameEngineProcess {
    public:
        static pid_t start();
        static int waitForExit(pid_t process);
        static bool tryWaitForExit(pid_t process, int& exitCode);
        static int terminateAndWait(pid_t process);
    };

}
