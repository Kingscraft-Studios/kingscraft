#pragma once
#include <functional>

namespace kc {

enum class ThreadName {
    Engine,
    Renderer,
    Resource,
    Registration,
    Input,
    GameLogic
};

struct Message {
    ThreadName target;
    std::function<void()> payload;
};

}
