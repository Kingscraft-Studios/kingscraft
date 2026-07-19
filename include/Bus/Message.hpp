#pragma once
#include <functional>

namespace lve {

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
