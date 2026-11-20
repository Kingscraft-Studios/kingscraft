#pragma once

namespace kc {

    class GameEngineProcess {
    public:
        // Starts the game engine as a child process, waits for it to exit,
        // and returns its exit status.
        static int run();
    };

}
