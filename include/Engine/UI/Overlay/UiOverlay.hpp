#pragma once

namespace kc {

    class UiWrapper;

    // Screen-attached overlay. The owning screen drives the lifecycle:
    // init()/cleanup(), per-frame tick()/render(), and resize() on window size
    // changes. Overlays only add their element groups to the shared UiWrapper,
    // which the renderer composites every frame, so they overlap the screen's
    // own UI instead of replacing it.
    class UiOverlay {
    public:
        virtual ~UiOverlay() = default;

        virtual void init(UiWrapper& ui, float screenW, float screenH) = 0;
        virtual void cleanup(UiWrapper& ui) = 0;

        virtual void tick(UiWrapper& ui, double dt) {}
        virtual void render(UiWrapper& ui, double dt) {}
        virtual void resize(float screenW, float screenH) {}
    };

} // namespace kc