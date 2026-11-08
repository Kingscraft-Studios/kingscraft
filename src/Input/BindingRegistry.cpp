#include "Input/BindingRegistry.hpp"

#include <algorithm>
#include <utility>

namespace kc {

    // --- ContextHandle -----------------------------------------------------

    ContextHandle::ContextHandle(std::weak_ptr<BindingRegistry> registry, uint64_t id)
        : registry_(std::move(registry)), id_(id) {}

    ContextHandle::~ContextHandle() {
        reset();
    }

    ContextHandle::ContextHandle(ContextHandle&& other) noexcept
        : registry_(std::move(other.registry_)), id_(other.id_) {
        other.id_ = 0;
    }

    ContextHandle& ContextHandle::operator=(ContextHandle&& other) noexcept {
        if (this != &other) {
            reset();
            registry_ = std::move(other.registry_);
            id_ = other.id_;
            other.id_ = 0;
        }
        return *this;
    }

    void ContextHandle::reset() {
        if (id_ == 0) return;
        if (auto registry = registry_.lock()) {
            registry->removeContext(id_);
        }
        registry_.reset();
        id_ = 0;
    }

    // --- BindingRegistry ---------------------------------------------------

    ContextHandle BindingRegistry::addContext(ContextSpec spec) {
        uint64_t id = 0;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            id = nextId_++;
            contexts_.push_back(Context{id, std::move(spec), true});
            dirty_ = true;
        }
        return ContextHandle(weak_from_this(), id);
    }

    void BindingRegistry::removeContext(uint64_t id) {
        std::lock_guard<std::mutex> lock(mutex_);
        const auto before = contexts_.size();
        contexts_.erase(
            std::remove_if(contexts_.begin(), contexts_.end(),
                           [id](const Context& context) { return context.id == id; }),
            contexts_.end());
        if (contexts_.size() != before) {
            dirty_ = true;
        }
    }

    void BindingRegistry::setContextEnabled(uint64_t id, bool enabled) {
        std::lock_guard<std::mutex> lock(mutex_);
        for (Context& context : contexts_) {
            if (context.id == id) {
                if (context.enabled != enabled) {
                    context.enabled = enabled;
                    dirty_ = true;
                }
                return;
            }
        }
    }

    std::shared_ptr<const BindingTable> BindingRegistry::snapshot() const {
        std::lock_guard<std::mutex> lock(mutex_);
        if (!snapshot_ || dirty_) {
            rebuildLocked();
        }
        return snapshot_;
    }

    void BindingRegistry::rebuildLocked() const {
        auto table = std::make_shared<BindingTable>();

        for (const Context& context : contexts_) {
            if (!context.enabled) continue;

            if (context.spec.captureText) {
                table->captureText = true;
            }

            for (const Binding& binding : context.spec.bindings) {
                if (binding.action == Action::None || binding.chord.empty()) continue;
                table->entries.push_back(BindingTable::Entry{
                    binding.chord, binding.action, context.id, context.spec.priority});
            }
        }

        // Higher priority contexts are evaluated first. Suppression rules:
        //   * a satisfied strictly-longer chord shadows its subsets, and
        //   * a satisfied identical chord shadows one from a lower priority
        //     context; equal-priority duplicates keep registration order.
        std::stable_sort(table->entries.begin(), table->entries.end(),
                         [](const BindingTable::Entry& a, const BindingTable::Entry& b) {
                             return a.priority > b.priority;
                         });

        snapshot_ = std::move(table);
        dirty_ = false;
    }

} // namespace kc
