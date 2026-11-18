#pragma once
#include <queue>
#include <mutex>
#include <condition_variable>
#include <chrono>
#include "Bus/Message.hpp"

namespace kc {

class Mailbox {
public:
    void push(Message msg) {
        {
            std::lock_guard<std::mutex> lock(mtx);
            queue.push(std::move(msg));
        }
        cv.notify_one();
    }

    bool try_pop(Message& out) {
        std::lock_guard<std::mutex> lock(mtx);
        if (queue.empty()) return false;
        out = std::move(queue.front());
        queue.pop();
        return true;
    }

    template<typename Rep, typename Period>
    bool pop_for(Message& out, const std::chrono::duration<Rep, Period>& timeout) {
        std::unique_lock<std::mutex> lock(mtx);
        if (!cv.wait_for(lock, timeout, [&]() { return !queue.empty() || stopped; }))
            return false;
        if (queue.empty()) return false;
        out = std::move(queue.front());
        queue.pop();
        return true;
    }

    void stop() {
        {
            std::lock_guard<std::mutex> lock(mtx);
            stopped = true;
        }
        cv.notify_all();
    }

private:
    std::queue<Message> queue;
    std::mutex mtx;
    std::condition_variable cv;
    bool stopped = false;
};

}
