#pragma once

#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <unordered_map>
#include <utility>
#include <vector>

#include "Input/BindingRegistry.hpp"

namespace kc {

    namespace detail {

        struct RouterState {
            std::mutex mutex;
            std::unordered_map<Action, std::vector<std::pair<uint64_t, std::function<void(const ActionEvent&)>>>> handlers;
            uint64_t nextId = 1;

            void remove(uint64_t id);
        };

    }

    // GameLogic-side action dispatcher.
    //
    // Contract: every handler runs on the GameLogic thread. Handlers must never
    // touch InputThread/RenderThread directly; cross-thread work goes through
    // MessageBus (see Kingscraft::registerAllKeys for examples). The router
    // deliberately does not auto-marshal to a target thread: one action can
    // legitimately need work on several threads, which a single target would
    // make impossible.
    class ActionRouter {
    public:
        using Handler = std::function<void(const ActionEvent&)>;

        // RAII unsubscribe token. Holds a weak reference to the router state so
        // it is harmless if the router is destroyed first.
        class Subscription {
        public:
            Subscription() = default;
            ~Subscription();

            Subscription(Subscription&& other) noexcept;
            Subscription& operator=(Subscription&& other) noexcept;

            Subscription(const Subscription&) = delete;
            Subscription& operator=(const Subscription&) = delete;

            void reset();
            explicit operator bool() const { return id_ != 0; }

        private:
            friend class ActionRouter;

            Subscription(std::weak_ptr<detail::RouterState> state, uint64_t id);

            std::weak_ptr<detail::RouterState> state_;
            uint64_t id_ = 0;
        };

        ActionRouter() = default;
        explicit ActionRouter(std::shared_ptr<BindingRegistry> registry);

        // GameLogic. Shared with the InputSystem, which owns the same registry.
        void setRegistry(std::shared_ptr<BindingRegistry> registry);
        std::shared_ptr<BindingRegistry> getRegistry() const { return registry_; }

        ContextHandle addContext(ContextSpec spec);
        Subscription subscribe(Action action, Handler handler);

        // GameLogic. Copies the handler list before invoking so a handler may
        // safely add/remove subscriptions (or tear down its owner).
        void dispatch(const ActionEvent& event);

    private:
        std::shared_ptr<BindingRegistry> registry_;
        std::shared_ptr<detail::RouterState> state_ =
            std::make_shared<detail::RouterState>();
    };

}
