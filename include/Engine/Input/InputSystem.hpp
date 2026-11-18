#pragma once

#include <array>
#include <atomic>
#include <cstddef>
#include <memory>

#include <GLFW/glfw3.h>

#include "Input/Action.hpp"
#include "Input/BindingRegistry.hpp"

namespace kc {

    // Input-thread-owned binding evaluator.
    //
    // Owned by InputThread. GameLogic interacts with it only through:
    //   * getRegistry()/addContext() (BindingRegistry is mutex-protected), and
    //   * isActionDown() (atomic, for continuous movement state).
    // Action press/release events are posted to the GameLogic mailbox as POD
    // ActionEvent values and dispatched by ActionRouter there.
    class InputSystem {
    public:
        explicit InputSystem(std::shared_ptr<BindingRegistry> registry);

        // --- Input thread only ------------------------------------------
        void onKey(int key, int scancode, int action, int mods);
        void onChar(unsigned int codepoint);
        void update();

        // --- Any thread (atomic) ----------------------------------------
        bool isActionDown(Action action) const;

        std::shared_ptr<BindingRegistry> getRegistry() const { return registry_; }

    private:
        using KeyState = std::array<bool, GLFW_KEY_LAST + 1>;

        static bool chordSatisfied(const Chord& chord, const KeyState& keys);
        static bool isSuppressed(const BindingTable& table, std::size_t index,
                                 const KeyState& keys);
        void emit(const ActionEvent& event);

        std::shared_ptr<BindingRegistry> registry_;
        std::shared_ptr<const BindingTable> snapshot_;

        KeyState keyDown_{};
        std::array<bool, ACTION_COUNT> prevSatisfied_{};
        // Whether the action had any binding in the previous update. Used to
        // suppress the synthetic Pressed edge when a context is added while
        // the chord is already held (e.g. holding Space during world load).
        std::array<bool, ACTION_COUNT> hadBinding_{};
        std::array<std::atomic<bool>, ACTION_COUNT> actionDown_{};
        std::atomic<bool> textCapture_{false};
    };

} // namespace kc
