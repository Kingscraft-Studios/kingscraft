#pragma once

#include <vector>

#include "Renderer/FrameScene.hpp"

namespace kc {

    class Screen {
    public:
        virtual ~Screen() = default;

        virtual void init() = 0;
        virtual void tick(double dt) = 0;
        virtual void render(FrameScene& scene) = 0;
        virtual void cleanup() = 0;

        virtual void onMouseButton(int button, int action, int mods) {}
    };

} // namespace kc
