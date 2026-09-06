#include "Core/Runtime.hpp"
#include "Threads/RenderThread.hpp"
#include "Threads/Kingscraft.hpp"
#include "Threads/InputThread.hpp"

namespace kc {
    Runtime& Runtime::get() {
        static Runtime instance;
        return instance;
    }
}
