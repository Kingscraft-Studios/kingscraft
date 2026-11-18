#pragma once

// Compatibility header.
//
// The old mutable KeyBindHandler (per-layer std::function tables mutated by
// GameLogic while the input thread iterated them) has been replaced by the
// action-based subsystem below:
//
//   Action          - POD action IDs
//   BindingTable    - immutable flattened snapshot
//   BindingRegistry - thread-safe context ownership (RAII ContextHandle)
//   ActionRouter    - GameLogic-only action dispatch (RAII Subscription)
//   InputSystem     - input-thread binding evaluation + action state
//
// New code should include the specific headers directly.
#include "Input/Action.hpp"
#include "Input/BindingTable.hpp"
#include "Input/BindingRegistry.hpp"
#include "Input/ActionRouter.hpp"
#include "Input/InputSystem.hpp"
