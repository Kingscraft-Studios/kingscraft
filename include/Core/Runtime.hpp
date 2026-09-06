#pragma once

namespace kc {
    class RenderThread;
    class Kingscraft;
    class InputThread;

    class Runtime {
    public:
        static Runtime& get();

        // Non-owning pointers pointing to the instances managed by WorkerPool
        RenderThread* renderThread = nullptr;
        Kingscraft*   kingscraft = nullptr;
        InputThread*  inputThread = nullptr;

    private:
        Runtime() = default;
    };
}
