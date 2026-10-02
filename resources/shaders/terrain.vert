#version 450

layout(push_constant) uniform PushConstants {
    mat4 viewProj;
    vec4 chunkOrigin;
} pc;

layout(location = 0) in uvec2 inPosPZ;   // px, pz
layout(location = 1) in int   inPosY;   // chunk-local Y
layout(location = 2) in uint  inFace;
layout(location = 3) in ivec2 inUv;
layout(location = 4) in uint  inTexIndex;

layout(location = 0) out vec2  fragUv;
layout(location = 1) out float fragTexIndex;
// Face direction handed straight to the fragment stage. flat because every
// vertex of a quad shares one face id, so there is nothing to interpolate.
layout(location = 2) flat out uint fragFace;

void main() {
    // chunkOrigin.y carries the world's minY, so local Y 0 maps to world minY.
    vec3 worldPos = vec3(float(inPosPZ.x), float(inPosY), float(inPosPZ.y)) + pc.chunkOrigin.xyz;
    gl_Position = pc.viewProj * vec4(worldPos, 1.0);
    fragUv = vec2(inUv);
    fragTexIndex = float(inTexIndex);
    fragFace = inFace;
}
