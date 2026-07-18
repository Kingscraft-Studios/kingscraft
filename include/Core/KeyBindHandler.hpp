#pragma once

#include <GLFW/glfw3.h>

#include <array>
#include <atomic>
#include <functional>
#include <vector>
#include <algorithm>

namespace lve {

    enum class BindLayer {
        Global,
        Screen,
        UI
    };

    class KeyBindHandler {
    public:
        using Callback = std::function<void()>;
        using Chord = std::vector<int>;
        using UiKeyCallback = std::function<void(int key, int action)>;
        using UiCharCallback = std::function<void(unsigned int codepoint)>;

        void onKeyEvent(int key, int scancode, int action, int mods) {
            if (key < 0 || key > GLFW_KEY_LAST) return;
            currKeys_[key].store(action == GLFW_PRESS || action == GLFW_REPEAT, std::memory_order_relaxed);
            if (uiKeyCallback_ && layerEnabled_[static_cast<int>(BindLayer::UI)]) {
                if (dispatcher)
                    dispatcher([this, key, action]() { uiKeyCallback_(key, action); });
                else
                    uiKeyCallback_(key, action);
            }
        }

        void onChar(unsigned int codepoint) {
            if (uiCharCallback_ && layerEnabled_[static_cast<int>(BindLayer::UI)]) {
                if (dispatcher) {
                    dispatcher([this, codepoint]() { uiCharCallback_(codepoint); });
                } else {
                    uiCharCallback_(codepoint);
                }
            }

        }

        void update() {
            for (size_t i = 0; i < bindings_.size(); i++) {
                if (!layerEnabled_[static_cast<int>(bindings_[i].layer)])
                    continue;

                bool now = isSatisfied(i) && !isSuppressed(i);
                bool was = prevSatisfied_[i];

                if (now && !was && bindings_[i].onPress) {
                    if (dispatcher)
                        dispatcher(bindings_[i].onPress);  // runs on game logic thread
                    else
                        bindings_[i].onPress();            // fallback: run directly
                }
                if (!now && was && bindings_[i].onRelease) {
                    if (dispatcher)
                        dispatcher(bindings_[i].onRelease);  // runs on game logic thread
                    else
                        bindings_[i].onRelease();            // fallback: run directly
                }

                prevSatisfied_[i] = now;
            }

            for (size_t i = 0; i <= GLFW_KEY_LAST; i++)
                prevKeys_[i].store(currKeys_[i].load(std::memory_order_relaxed), std::memory_order_relaxed);
        }

        bool isDown(int key) const { return currKeys_[key].load(std::memory_order_relaxed); }
        bool isPressed(int key) const { return currKeys_[key].load(std::memory_order_relaxed) && !prevKeys_[key].load(std::memory_order_relaxed); }
        bool isReleased(int key) const { return !currKeys_[key].load(std::memory_order_relaxed) && prevKeys_[key].load(std::memory_order_relaxed); }

        void onPress(BindLayer layer, Chord chord, Callback cb) {
            bindings_.push_back({layer, std::move(chord), std::move(cb), {}});
            prevSatisfied_.push_back(false);
        }

        void onRelease(BindLayer layer, Chord chord, Callback cb) {
            bindings_.push_back({layer, std::move(chord), {}, std::move(cb)});
            prevSatisfied_.push_back(false);
        }

        void setUiKeyCallback(UiKeyCallback cb) { uiKeyCallback_ = std::move(cb); }
        void setUiCharCallback(UiCharCallback cb) { uiCharCallback_ = std::move(cb); }

        void setLayerEnabled(BindLayer layer, bool enabled) {
            layerEnabled_[static_cast<int>(layer)] = enabled;
        }

        bool isLayerEnabled(BindLayer layer) const {
            return layerEnabled_[static_cast<int>(layer)];
        }

        void clear() { bindings_.clear(); prevSatisfied_.clear(); }

        void setDispatcher(std::function<void(std::function<void()>)> fn) {
            dispatcher = std::move(fn);
        }

    private:
        struct Binding {
            BindLayer layer;
            Chord chord;
            Callback onPress;
            Callback onRelease;
        };

        bool isSatisfied(size_t idx) const {
            for (int k : bindings_[idx].chord)
                if (!currKeys_[k].load(std::memory_order_relaxed)) return false;
            return true;
        }

        bool isSuppressed(size_t idx) const {
            const auto& chord = bindings_[idx].chord;
            for (size_t j = 0; j < bindings_.size(); j++) {
                if (j == idx) continue;
                const auto& other = bindings_[j].chord;
                if (other.size() <= chord.size()) continue;

                bool otherSatisfied = true;
                for (int k : other)
                    if (!currKeys_[k].load(std::memory_order_relaxed)) { otherSatisfied = false; break; }
                if (!otherSatisfied) continue;

                bool isSubset = true;
                for (int k : chord)
                    if (std::find(other.begin(), other.end(), k) == other.end())
                        { isSubset = false; break; }
                if (isSubset) return true;
            }
            return false;
        }

        std::array<std::atomic<bool>, GLFW_KEY_LAST + 1> prevKeys_{};
        std::array<std::atomic<bool>, GLFW_KEY_LAST + 1> currKeys_{};
        std::vector<Binding> bindings_;
        std::vector<bool> prevSatisfied_;
        UiKeyCallback uiKeyCallback_;
        UiCharCallback uiCharCallback_;
        std::array<bool, 3> layerEnabled_{true, true, true};
        std::function<void(std::function<void()>)> dispatcher;
    };

} // namespace lve
