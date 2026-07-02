#pragma once

namespace lve {

    class RendererSettings {
    public:
        static RendererSettings& get() {
            static RendererSettings instance;
            return instance;
        }

        // World / terrain
        int renderDistance = 10;

        int chunkSize = 16;
        int worldHeight = 100;
        
        // Derived
        float farPlaneCalc = renderDistance * chunkSize;

        // Camera
        float fov = 70.0f;
        float nearPlane = 0.1f;
        float farPlane = farPlaneCalc * 1.5f;

        // Performance toggles
        bool vsync = false;
        bool enableFrustumCulling = true;
        bool disableTextures = false;
        int maxFps = 0; // 0 = None

    private:
        RendererSettings() = default;
    };

} // namespace lve
