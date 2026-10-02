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

// Constant brightness per face direction, Minecraft style: the top is fully lit
// and every other face gets a fixed darker multiplier. This is not lighting --
// nothing here depends on where the camera or the sun is -- it just gives cubes
// a readable edge so flat terrain does not read as a painted plane.
//
// Face ids match ChunkVertex::face: 0=PosY, 1=NegY, 2=PosZ, 3=NegZ, 4=PosX, 5=NegX.
float faceBrightness(uint face) {
    switch (face) {
        case 0u: return 1.00;  // top
        case 1u: return 0.48;  // bottom
        case 2u: return 0.86;  // +Z
        case 3u: return 0.86;  // -Z
        case 4u: return 0.64;  // +X
        default: return 0.64;  // -X
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

    // Distance fog. gl_FragCoord.w is the view-space depth (1/w of the clip
    // position), which is already correct under any projection, so this needs no
    // camera position. Quadratic so the fade is gentle up close and only really
    // bites near the far edge.
    float depth = gl_FragCoord.w;
    float span = max(pc.fogParams.y - pc.fogParams.x, 1e-4);
    float f = clamp((depth - pc.fogParams.x) / span, 0.0, 1.0);
    color = mix(color, pc.fogColor.rgb, f * f);

    outColor = vec4(color, 1.0);
}
