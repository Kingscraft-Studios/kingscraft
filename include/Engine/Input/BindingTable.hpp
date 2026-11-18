#pragma once

#include <array>
#include <cstdint>
#include <initializer_list>
#include <string>
#include <vector>

#include "Input/Action.hpp"

namespace kc {

    // A set of keys that must be held simultaneously.
    //
    // Fixed-size and trivially copyable so it can live inside an immutable
    // binding snapshot shared between threads without allocating.
    struct Chord {
        static constexpr std::size_t MAX_KEYS = 4;

        std::array<int, MAX_KEYS> keys{};
        uint8_t count = 0;

        Chord() = default;
        Chord(std::initializer_list<int> values);

        bool empty() const { return count == 0; }
        bool contains(int key) const;
        bool isSubsetOf(const Chord& other) const;
        bool operator==(const Chord& other) const;
    };

    struct Binding {
        Chord chord;
        Action action = Action::None;
    };

    // A named, replaceable group of bindings. Contexts are the action-based
    // replacement for the old BindLayer enum: a screen adds one in init() and
    // its RAII ContextHandle removes it in cleanup().
    struct ContextSpec {
        std::string name;
        int priority = 0;
        // When true, raw key/char events are forwarded to the Renderer's UI
        // while this context is alive (the old BindLayer::UI layer).
        bool captureText = false;
        std::vector<Binding> bindings;
    };

    // Immutable flattened view of every enabled context. The input thread reads
    // this; GameLogic only ever replaces the whole snapshot under a mutex.
    struct BindingTable {
        struct Entry {
            Chord chord;
            Action action = Action::None;
            uint64_t contextId = 0;
            int priority = 0;
        };

        std::vector<Entry> entries;
        bool captureText = false;
    };

} // namespace kc
