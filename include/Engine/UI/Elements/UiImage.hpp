#pragma once

#include "UI/Elements/UiElement.hpp"

namespace kc {

    class UiImage : public UiElement {
    public:
        UiImage() = default;
        ~UiImage() override = default;

        UiImage(const UiImage&) = delete;
        UiImage& operator=(const UiImage&) = delete;

        void render(UiEngine& engine) override;
    };

} // namespace kc
