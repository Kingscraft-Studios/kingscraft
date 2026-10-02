#pragma once
#include <vector>
#include <glm/glm.hpp>

namespace kc {

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
        // Whether the world pass runs this frame. Kept separate from
        // WorldBackground so that a screen which wants the world drawn can never
        // be mistaken for one that does not.
        bool renderTerrain;
    };

    // Which clear colour the frame starts from. This used to be inferred from
    // TerrainPass::renderTerrain, which meant the world screen fell back to the
    // menu's dark grey whenever the culling pass happened to produce zero visible
    // chunks -- a full-screen grey flash in the middle of gameplay. Picking the
    // background explicitly removes that coupling.
    enum class WorldBackground {
        // Sky blue, with depth: the world pass.
        Sky,
        // Flat dark grey, no depth: the main menu backdrop.
        MenuDim,
    };

    struct UiPass {
        bool enabled;
    };

    struct HighlightPass {
        bool enabled = false;
        glm::vec3 position{0.0f};
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

    // Defaults to Sky: a screen has to opt in to the dim menu backdrop, so
    // forgetting to set it can never blank out the world.
    WorldBackground background = WorldBackground::Sky;

    UiPass ui;

    HighlightPass highlight;

    Settings settings;

    FrameStats stats;
};

} // namespace kc
