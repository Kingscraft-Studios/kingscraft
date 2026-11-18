#pragma once

#include "UiStyle.hpp"

#include <cstdint>
#include <functional>
#include <memory>
#include <utility>

namespace kc {

    class UiWrapper;

    // Seam between a handle and the UiWrapper/UiEngine that owns the pool.
    // Kept here so the handle stays free of UI-engine dependencies; UiWrapper
    // builds one with callbacks that lock its UI mutex.
    class UiStyleRegistry {
    public:
        using ReleaseFn = std::function<void(uint32_t)>;
        using UpdateFn = std::function<void(uint32_t, const UiStyle&)>;

        UiStyleRegistry(ReleaseFn release, UpdateFn update)
            : release_(std::move(release)), update_(std::move(update)) {}

        void release(uint32_t slot) const {
            if (release_) release_(slot);
        }

        void update(uint32_t slot, const UiStyle& style) const {
            if (update_) update_(slot, style);
        }

    private:
        ReleaseFn release_;
        UpdateFn update_;
    };

    // RAII handle to one slot in the GPU style pool.
    //
    // Created by UiWrapper::createStyle() and released automatically when the
    // owning screen/overlay dies, so per-screen styles no longer accumulate in
    // the fixed-size style SSBO across enter/exit cycles.
    //
    // Move-only: a handle is the sole owner of its slot, and the registry is
    // held by weak_ptr, so a handle that outlives the UiWrapper becomes a no-op
    // instead of touching freed engine state.
    class UiStyleHandle {
    public:
        static constexpr uint32_t INVALID_SLOT = 0xFFFFFFFFu;

        UiStyleHandle() = default;
        ~UiStyleHandle() { reset(); }

        UiStyleHandle(UiStyleHandle&& other) noexcept
            : registry_(std::move(other.registry_)), slot_(other.slot_) {
            other.slot_ = INVALID_SLOT;
        }

        UiStyleHandle& operator=(UiStyleHandle&& other) noexcept {
            if (this != &other) {
                reset();
                registry_ = std::move(other.registry_);
                slot_ = other.slot_;
                other.slot_ = INVALID_SLOT;
            }
            return *this;
        }

        UiStyleHandle(const UiStyleHandle&) = delete;
        UiStyleHandle& operator=(const UiStyleHandle&) = delete;

        void reset() {
            if (slot_ == INVALID_SLOT) return;
            if (auto registry = registry_.lock()) registry->release(slot_);
            slot_ = INVALID_SLOT;
            registry_.reset();
        }

        // Patches this slot in place. Used by overlays that rebuild their
        // content after a registry reload without re-registering.
        void update(const UiStyle& style) const {
            if (slot_ == INVALID_SLOT) return;
            if (auto registry = registry_.lock()) registry->update(slot_, style);
        }

        // Slot index consumed by UiElement::setStyleIndex / the GPU SSBO.
        // Invalid handles fall back to slot 0, which the engine reserves as a
        // fully transparent style, so a stale element draws nothing instead of
        // indexing past the pool.
        uint32_t index() const { return slot_ == INVALID_SLOT ? 0u : slot_; }

        bool valid() const { return slot_ != INVALID_SLOT; }
        explicit operator bool() const { return valid(); }

    private:
        friend class UiWrapper;

        UiStyleHandle(std::weak_ptr<UiStyleRegistry> registry, uint32_t slot)
            : registry_(std::move(registry)), slot_(slot) {}

        std::weak_ptr<UiStyleRegistry> registry_;
        uint32_t slot_ = INVALID_SLOT;
    };

} // namespace kc
