#include "Input/InputSystem.hpp"

#include <string>
#include <utility>

#include "Bus/MessageBus.hpp"
#include "Core/Runtime.hpp"
#include "Threads/Kingscraft.hpp"
#include "Threads/RenderThread.hpp"
#include "Util/TimeUtil.hpp"

namespace kc {

    InputSystem::InputSystem(std::shared_ptr<BindingRegistry> registry)
        : registry_(std::move(registry)) {
        for (auto& down : actionDown_) {
            down.store(false, std::memory_order_relaxed);
        }
        if (registry_) {
            snapshot_ = registry_->snapshot();
            textCapture_.store(snapshot_ && snapshot_->captureText,
                               std::memory_order_relaxed);
        }
    }

    void InputSystem::onKey(int key, int scancode, int action, int mods) {
        (void)scancode;
        (void)mods;

        if (key >= 0 && key <= GLFW_KEY_LAST) {
            keyDown_[static_cast<std::size_t>(key)] =
                (action == GLFW_PRESS || action == GLFW_REPEAT);
        }

        // Raw text/key forwarding is only enabled while a captureText context
        // (main menu / debug UI / death screen) is alive.
        if (!textCapture_.load(std::memory_order_relaxed)) return;

        MessageBus::Get().send(ThreadName::Renderer, [key, action]() {
            if (auto* renderer = Runtime::get().renderThread) {
                renderer->getUI().onKey(key, action);
            }
        });
    }

    void InputSystem::onChar(unsigned int codepoint) {
        if (!textCapture_.load(std::memory_order_relaxed)) return;

        MessageBus::Get().send(ThreadName::Renderer, [codepoint]() {
            if (auto* renderer = Runtime::get().renderThread) {
                renderer->getUI().onChar(codepoint);
            }
        });
    }

    void InputSystem::update() {
        if (!registry_) return;

        // The registry rebuilds only when a context changed; the pointer is
        // stable between mutations, so this is just a shared_ptr compare.
        auto table = registry_->snapshot();
        snapshot_ = table;
        textCapture_.store(table && table->captureText, std::memory_order_relaxed);

        std::array<bool, ACTION_COUNT> satisfied{};
        std::array<bool, ACTION_COUNT> boundNow{};
        if (table) {
            for (std::size_t i = 0; i < table->entries.size(); ++i) {
                const BindingTable::Entry& entry = table->entries[i];
                const auto index = static_cast<std::size_t>(entry.action);
                if (index < ACTION_COUNT) {
                    boundNow[index] = true;
                }
                if (!chordSatisfied(entry.chord, keyDown_)) continue;
                if (isSuppressed(*table, i, keyDown_)) continue;

                if (index < ACTION_COUNT) {
                    satisfied[index] = true;
                }
            }
        }

        // Emit one event per action transition, regardless of how many
        // bindings map to that action (e.g. left and right shift both descend
        // while flying).
        for (std::size_t i = 1; i < ACTION_COUNT; ++i) {
            const bool now = satisfied[i];
            const bool was = prevSatisfied_[i];
            const bool bound = boundNow[i];
            const bool hadBinding = hadBinding_[i];
            hadBinding_[i] = bound;

            if (now == was) continue;

            prevSatisfied_[i] = now;
            actionDown_[i].store(now, std::memory_order_release);

            // The action just gained its first binding while the chord was
            // already held: update state, but do not report a Pressed edge.
            if (now && !hadBinding && bound) continue;

            ActionEvent event;
            event.action = static_cast<Action>(i);
            event.phase = now ? Phase::Pressed : Phase::Released;
            event.time = TimeUtil::uptimeSeconds();
            emit(event);
        }
    }

    bool InputSystem::isActionDown(Action action) const {
        const auto index = static_cast<std::size_t>(action);
        if (index >= ACTION_COUNT) return false;
        return actionDown_[index].load(std::memory_order_acquire);
    }

    bool InputSystem::chordSatisfied(const Chord& chord, const KeyState& keys) {
        if (chord.empty()) return false;
        for (uint8_t i = 0; i < chord.count; ++i) {
            const int key = chord.keys[i];
            if (key < 0 || key > GLFW_KEY_LAST) return false;
            if (!keys[static_cast<std::size_t>(key)]) return false;
        }
        return true;
    }

    bool InputSystem::isSuppressed(const BindingTable& table, std::size_t index,
                                   const KeyState& keys) {
        const BindingTable::Entry& entry = table.entries[index];

        for (std::size_t j = 0; j < table.entries.size(); ++j) {
            if (j == index) continue;

            const BindingTable::Entry& other = table.entries[j];
            const bool identical = other.chord == entry.chord;
            // A strictly longer chord suppresses its subset (F3+F6 beats F3),
            // and an identical chord from a strictly higher-priority context
            // shadows this one. Equal-priority duplicates keep the entry that
            // was registered first (stable sort order in the table).
            if (!identical && other.chord.count <= entry.chord.count) continue;
            if (other.priority < entry.priority) continue;
            if (identical && other.priority == entry.priority && j > index) continue;
            if (!entry.chord.isSubsetOf(other.chord)) continue;
            if (!chordSatisfied(other.chord, keys)) continue;

            return true;
        }
        return false;
    }

    void InputSystem::emit(const ActionEvent& event) {
        // The payload captures only this POD value. It runs on GameLogic and
        // reaches the router through Runtime there, never through a pointer
        // stored on the input thread.
        MessageBus::Get().send(ThreadName::GameLogic, [event]() {
            if (auto* kingscraft = Runtime::get().kingscraft) {
                kingscraft->getActionRouter().dispatch(event);
            }
        });
    }

} // namespace kc
