#version 450

layout(constant_id = 0) const bool DISABLE_TEXTURES = false;

layout(push_constant) uniform PushConstants {
    mat4 viewProj;
    vec4 chunkOrigin;
    vec4 fogColor;    // rgb = fog colour, w unused
    vec4 fogParams;   // x = distance fog starts, y = distance fog is total
} pc;

layout(binding = 0) uniform sampler2DArray uTexture;

layout(location = 0) in vec2 fragUv;
layout(location = 1) in float fragTexIndex;
layout(location = 2) flat in uint fragFace;

layout(location = 0) out vec4 outColor;

// Same fixed per-face brightness as terrain.frag, so a glass or water face that
// shows against a lit wall is not brighter than the wall behind it. Most water
// is a top face anyway, so this is close to a no-op there.
float faceBrightness(uint face) {
    switch (face) {
        case 0u: return 1.00;
        case 1u: return 0.48;
        case 2u: return 0.86;
        case 3u: return 0.86;
        case 4u: return 0.64;
        default: return 0.64;
    }
}

void main() {
    vec3 color;
    if (DISABLE_TEXTURES) {
        color = vec3(1.0, 0.0, 0.0);
    } else {
        int layer = int(fragTexIndex);
        color = texture(uTexture, vec3(fragUv, layer)).rgb;
    }

    color *= faceBrightness(fragFace);

    // Same fog curve as the opaque pass, so distant water dissolves into the
    // horizon at the same rate the terrain behind it does.
    float depth = gl_FragCoord.w;
    float span = max(pc.fogParams.y - pc.fogParams.x, 1e-4);
    float f = clamp((depth - pc.fogParams.x) / span, 0.0, 1.0);
    color = mix(color, pc.fogColor.rgb, f * f);

    outColor = vec4(color, 0.60);
}
