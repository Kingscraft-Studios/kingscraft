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

        // Minecraft-exact vertical extent: y = -64 .. 319.
        // minY is the world Y of the bottom layer; local chunk Y is
        // worldY - minY, so it stays 0-based inside Chunk/ChunkMesher.
        int minY = -64;
        int worldHeight = 384;

        // Derived
        int maxY() const { return minY + worldHeight - 1; }
        bool containsY(int worldY) const { return worldY >= minY && worldY <= maxY(); }

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
        int regionCacheLimit = 16;  // max regions cached in memory (directory + staged edits, LRU)

        // Streaming prefetch ring: the ring just outside render distance is
        // loaded from disk (or generated from the seed) ahead of time and held
        // in memory but never meshed or rendered, so a chunk crossing into view
        // skips its disk read / generation. This caps how many such grids are
        // kept; each is chunkSize*height*chunkSize*8 bytes (~768 KiB at 16x384).
        int prefetchCacheLimit = 128;

        // Occlusion ray grid (screen-space)
        // Low=32x18, Medium=48x27, High=64x36, Ultra=96x54
        int occlusionGridW = 64;
        int occlusionGridH = 36;

        // Vulkan Settings
        bool enableVkDebugValidation = false;

    private:
        RendererSettings() = default;
    };

} // namespace kc
