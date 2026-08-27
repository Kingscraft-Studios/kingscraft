#include "UI/Elements/UiGroup.hpp"
#include "UI/Elements/UiElement.hpp"
#include "UI/UiWrapper.hpp"

namespace lve {

    void UiGroup::add(UiElement* child) {
        if (!child) return;
        for (auto* c : children_) {
            if (c == child) return;
        }
        children_.push_back(child);
    }

    void UiGroup::remove(UiElement* child) {
        children_.erase(
            std::remove(children_.begin(), children_.end(), child),
            children_.end());
    }

    void UiGroup::addToWrapper(UiWrapper& ui) {
        for (auto* child : children_) {
            ui.addElement(child);
        }
    }

    void UiGroup::removeFromWrapper(UiWrapper& ui) {
        for (auto* child : children_) {
            ui.removeElement(child);
        }
    }

    void UiGroup::setVisible(bool visible) {
        for (auto* child : children_) {
            child->setVisible(visible);
        }
    }

    void UiGroup::clear() {
        children_.clear();
    }

} // namespace lve
