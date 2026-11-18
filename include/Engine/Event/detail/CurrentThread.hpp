#pragma once
#include "Bus/Message.hpp"

namespace kc::detail {

    inline thread_local ThreadName t_currentThread = ThreadName::Unknown;

    inline ThreadName currentThread() {
        return t_currentThread;
    }

    inline void setCurrentThread(ThreadName name) {
        t_currentThread = name;
    }

}
