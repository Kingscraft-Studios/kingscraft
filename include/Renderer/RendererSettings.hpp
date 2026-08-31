#pragma once

namespace kc {

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
        bool enableOcclusionCulling = false;
        bool disableTextures = false;
        int maxFps = 0; // 0 = None

        // IO / world storage
        int regionCacheLimit = 16;  // max region texts kept in memory (LRU)

        // Occlusion ray grid (screen-space)
        // Low=32x18, Medium=48x27, High=64x36, Ultra=96x54
        int occlusionGridW = 64;
        int occlusionGridH = 36;

    private:
        RendererSettings() = default;
    };

} // namespace kc
