#pragma once

#include <vector>
#include <cstdint>

namespace kc {

    class UiElement;
    class UiWrapper;

    class UiGroup {
    public:
        UiGroup() = default;
        ~UiGroup() = default;

        UiGroup(const UiGroup&) = delete;
        UiGroup& operator=(const UiGroup&) = delete;

        void add(UiElement* child);
        void remove(UiElement* child);
        void addToWrapper(UiWrapper& ui);
        void removeFromWrapper(UiWrapper& ui);
        void setVisible(bool visible);
        const std::vector<UiElement*>& children() const { return children_; }
        void clear();

    private:
        std::vector<UiElement*> children_;
    };

} // namespace kc
