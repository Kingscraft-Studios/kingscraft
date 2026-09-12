#pragma once

#include <atomic>
#include <cstdint>

namespace kc::detail {

    inline uint64_t typeIdCounter() {
        static std::atomic<uint64_t> counter{1};
        return counter.fetch_add(1);
    }

    template<typename T>
    inline uint64_t typeId() {
        static const uint64_t id = typeIdCounter();
        return id;
    }
}