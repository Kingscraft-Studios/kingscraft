#pragma once
#include <vector>
#include <glm/glm.hpp>

namespace lve {

struct ChunkRenderData {
    uint64_t chunkKey;
    glm::vec3 worldOrigin;
};

struct FrameScene {
    glm::mat4 viewProj{1.0f};
    glm::vec3 cameraPosition{0.0f};
    float dt = 0.0f;
    float worldHeight = 0.0f;
    std::vector<ChunkRenderData> visibleChunks;
    bool uiEnabled = true;
    bool debugMode = false;
    double cpuTickMs = 0.0;
    double cpuFrameTimeMs = 0.0;
};

} // namespace lve
