#pragma once
#include <functional>

namespace kc {

enum class ThreadName {
    Engine,
    Renderer,
    Registry,
    Input,
    GameLogic,
    WorkerPool,
    MessageBus
};

struct Message {
    ThreadName target;
    std::function<void()> payload;
};

}
