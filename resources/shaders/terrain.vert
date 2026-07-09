#version 450

layout(push_constant) uniform PushConstants {
    mat4 viewProj;
    vec4 chunkOrigin;
} pc;

layout(location = 0) in uvec4 inPosFace;
layout(location = 1) in ivec2 inUv;
layout(location = 2) in uint  inTexIndex;

layout(location = 0) out vec2  fragUv;
layout(location = 1) out float fragTexIndex;

void main() {
    vec3 worldPos = vec3(inPosFace.xyz) + pc.chunkOrigin.xyz;
    gl_Position = pc.viewProj * vec4(worldPos, 1.0);
    fragUv = vec2(inUv);
    fragTexIndex = float(inTexIndex);
}
