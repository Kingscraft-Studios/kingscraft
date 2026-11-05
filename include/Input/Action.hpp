#pragma once

#include <cstddef>
#include <cstdint>

namespace kc {

    // Stable, POD identifiers for every semantic game action.
    //
    // The input thread only ever stores and forwards these IDs (inside
    // ActionEvent). It never holds a std::function or a pointer to a game
    // object, which is what made the old KeyBindHandler unsafe: bindings were
    // mutated by GameLogic while the input thread iterated them.
    enum class Action : uint16_t {
        None = 0,

        // Player movement (state is read with InputSystem::isActionDown).
        MoveForward,
        MoveBackward,
        MoveLeft,
        MoveRight,
        Jump,
        FlyDown,

        // World-screen actions.
        OpenPauseMenu,
        Hotbar1,
        Hotbar2,
        Hotbar3,
        Hotbar4,
        Hotbar5,
        Hotbar6,
        Hotbar7,
        Hotbar8,
        Hotbar9,

        // Global actions.
        ToggleFullscreen,
        ToggleDebug,
        LogSelectedElement,
        StartProfiler,
        ReloadRegistries,
        DumpRegions,

        Count
    };

    constexpr std::size_t ACTION_COUNT = static_cast<std::size_t>(Action::Count);
    constexpr int HOTBAR_SLOT_COUNT = 9;

    // Hotbar actions are contiguous so slot N maps directly onto an action ID.
    constexpr Action hotbarAction(int slot) {
        return static_cast<Action>(
            static_cast<uint16_t>(Action::Hotbar1) + static_cast<uint16_t>(slot));
    }

    constexpr bool isHotbarAction(Action action) {
        const auto value = static_cast<uint16_t>(action);
        return value >= static_cast<uint16_t>(Action::Hotbar1) &&
               value <= static_cast<uint16_t>(Action::Hotbar9);
    }

    constexpr int hotbarActionSlot(Action action) {
        return static_cast<int>(static_cast<uint16_t>(action) -
                                static_cast<uint16_t>(Action::Hotbar1));
    }

    enum class Phase : uint8_t {
        Pressed,
        Released
    };

    // Fully self-contained value: safe to capture in a MessageBus payload and
    // execute on any thread.
    struct ActionEvent {
        Action action = Action::None;
        Phase phase = Phase::Pressed;
        int key = -1;
        int mods = 0;
        double time = 0.0;
    };

} // namespace kc
