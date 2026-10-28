#version 450

layout(push_constant) uniform PushConstants {
    mat4 viewProj;
    vec4 chunkOrigin;
} pc;

// ChunkVertex packs everything into two words (8 bytes per vertex):
//   word 0: px:5 | pz:5<<5 | py:9<<10 | face:3<<19 | uvX:5<<22 | uvY:5<<27
//   word 1: texture array layer
// Keep these shifts in sync with ChunkVertex in include/Core/World/Chunk.hpp.
layout(location = 0) in uint inPacked;
layout(location = 1) in uint inTexIndex;

layout(location = 0) out vec2  fragUv;
layout(location = 1) out float fragTexIndex;

void main() {
    uint px = inPacked & 0x1Fu;
    uint pz = (inPacked >> 5) & 0x1Fu;
    uint py = (inPacked >> 10) & 0x1FFu;
    uint uvX = (inPacked >> 22) & 0x1Fu;
    uint uvY = (inPacked >> 27) & 0x1Fu;

    // chunkOrigin.y carries the world's minY, so local Y 0 maps to world minY.
    vec3 worldPos = vec3(float(px), float(py), float(pz)) + pc.chunkOrigin.xyz;
    gl_Position = pc.viewProj * vec4(worldPos, 1.0);
    fragUv = vec2(float(uvX), float(uvY));
    fragTexIndex = float(inTexIndex);
}

