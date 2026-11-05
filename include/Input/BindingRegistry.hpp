#pragma once

#include <cstdint>
#include <memory>
#include <mutex>
#include <vector>

#include "Input/BindingTable.hpp"

namespace kc {

    class BindingRegistry;

    // RAII ownership of one registered context.
    //
    // Holds a weak reference so a handle can never keep the registry alive (or
    // dangle if the owner was destroyed first). Destroying it removes the
    // context, which is how screens/controllers unregister every binding they
    // added without touching the input thread.
    class ContextHandle {
    public:
        ContextHandle() = default;
        ~ContextHandle();

        ContextHandle(ContextHandle&& other) noexcept;
        ContextHandle& operator=(ContextHandle&& other) noexcept;

        ContextHandle(const ContextHandle&) = delete;
        ContextHandle& operator=(const ContextHandle&) = delete;

        void reset();

        uint64_t id() const { return id_; }
        explicit operator bool() const { return id_ != 0; }

    private:
        friend class BindingRegistry;

        ContextHandle(std::weak_ptr<BindingRegistry> registry, uint64_t id);

        std::weak_ptr<BindingRegistry> registry_;
        uint64_t id_ = 0;
    };

    // Thread-safe registry of contexts and their bindings.
    //
    // GameLogic mutates it (add/remove/enable); the input thread calls
    // snapshot() to obtain an immutable BindingTable. No shared mutable state
    // is ever exposed to both threads at once.
    class BindingRegistry : public std::enable_shared_from_this<BindingRegistry> {
    public:
        ContextHandle addContext(ContextSpec spec);
        void removeContext(uint64_t id);
        void setContextEnabled(uint64_t id, bool enabled);

        // Any thread. Returns an immutable snapshot, rebuilt only after a
        // mutation. The returned pointer is stable until the next mutation.
        std::shared_ptr<const BindingTable> snapshot() const;

    private:
        struct Context {
            uint64_t id = 0;
            ContextSpec spec;
            bool enabled = true;
        };

        void rebuildLocked() const;

        mutable std::mutex mutex_;
        std::vector<Context> contexts_;
        uint64_t nextId_ = 1;
        mutable std::shared_ptr<const BindingTable> snapshot_;
        mutable bool dirty_ = true;
    };

} // namespace kc
