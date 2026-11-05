#include "Input/ActionRouter.hpp"

#include <algorithm>

namespace kc {

    namespace detail {

        void RouterState::remove(uint64_t id) {
            std::lock_guard<std::mutex> lock(mutex);
            for (auto it = handlers.begin(); it != handlers.end();) {
                auto& entries = it->second;
                entries.erase(
                    std::remove_if(entries.begin(), entries.end(),
                                   [id](const auto& entry) { return entry.first == id; }),
                    entries.end());
                if (entries.empty()) {
                    it = handlers.erase(it);
                } else {
                    ++it;
                }
            }
        }

    } // namespace detail

    // --- Subscription ------------------------------------------------------

    ActionRouter::Subscription::Subscription(std::weak_ptr<detail::RouterState> state,
                                             uint64_t id)
        : state_(std::move(state)), id_(id) {}

    ActionRouter::Subscription::~Subscription() {
        reset();
    }

    ActionRouter::Subscription::Subscription(Subscription&& other) noexcept
        : state_(std::move(other.state_)), id_(other.id_) {
        other.id_ = 0;
    }

    ActionRouter::Subscription& ActionRouter::Subscription::operator=(
        Subscription&& other) noexcept {
        if (this != &other) {
            reset();
            state_ = std::move(other.state_);
            id_ = other.id_;
            other.id_ = 0;
        }
        return *this;
    }

    void ActionRouter::Subscription::reset() {
        if (id_ == 0) return;
        if (auto state = state_.lock()) {
            state->remove(id_);
        }
        state_.reset();
        id_ = 0;
    }

    // --- ActionRouter ------------------------------------------------------

    ActionRouter::ActionRouter(std::shared_ptr<BindingRegistry> registry)
        : registry_(std::move(registry)) {}

    void ActionRouter::setRegistry(std::shared_ptr<BindingRegistry> registry) {
        registry_ = std::move(registry);
    }

    ContextHandle ActionRouter::addContext(ContextSpec spec) {
        if (!registry_) return {};
        return registry_->addContext(std::move(spec));
    }

    ActionRouter::Subscription ActionRouter::subscribe(Action action, Handler handler) {
        if (action == Action::None || !handler) return {};

        auto state = state_;
        uint64_t id = 0;
        {
            std::lock_guard<std::mutex> lock(state->mutex);
            id = state->nextId++;
            state->handlers[action].emplace_back(id, std::move(handler));
        }
        return Subscription(std::weak_ptr<detail::RouterState>(state), id);
    }

    void ActionRouter::dispatch(const ActionEvent& event) {
        // Keep the state alive locally: a handler may destroy the owner of this
        // ActionRouter (e.g. Esc tearing down the current screen).
        auto state = state_;

        std::vector<Handler> handlers;
        {
            std::lock_guard<std::mutex> lock(state->mutex);
            auto it = state->handlers.find(event.action);
            if (it == state->handlers.end()) return;

            handlers.reserve(it->second.size());
            for (const auto& entry : it->second) {
                handlers.push_back(entry.second);
            }
        }

        for (const Handler& handler : handlers) {
            if (handler) handler(event);
        }
    }

} // namespace kc
