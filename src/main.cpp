#include "Bus/MessageBus.hpp"
#include "Threads/Engine.hpp"

int main() {
    kc::MessageBus::Init();
    kc::Engine::Init();
    kc::Engine::get().run();
    kc::Engine::Shutdown();
    kc::MessageBus::Shutdown();
    return 0;
}
