#pragma once
#include <vector>
#include <glm/glm.hpp>

namespace lve {

    struct CameraData {
        glm::mat4 viewProj{1.0f};
        glm::vec3 position{0.0f};
    };

    struct TerrainDraw {
        uint64_t chunkKey;
        glm::vec3 worldOrigin;
    };

    struct TerrainPass {
        std::vector<TerrainDraw> draws;
        bool renderTerrain;
    };

    struct UiPass {
        bool enabled = true;
    };

    struct FrameStats {
        float delta;

        double cpuTickMs;
        double cpuFrameTimeMs;
    };

    struct Settings {
        // RendererSettings renderSettings;
    };

struct FrameScene {
    CameraData camera;

    TerrainPass terrain;

    UiPass ui;

    Settings settings;

    FrameStats stats;
};

} // namespace lve
