#version 450

// TODO: SSBO Style System
// Fragment shader receives flat-interpolated style from vertex shader.
// Zero SSBO reads — all lookups happen in VS.
// Dispatches on mode for: Solid, Font, GradientLinear, GradientRadial,
// RoundedRect, Texture.

layout(location = 0) in vec2 inUv;
layout(location = 1) flat in vec4 inColor1;
layout(location = 2) flat in vec4 inColor2;
layout(location = 3) flat in vec4 inParams;

layout(set = 0, binding = 1) uniform sampler2D fontAtlas;
layout(set = 1, binding = 0) uniform sampler2DArray blockTextures;

layout(location = 0) out vec4 outColor;

void main() {
    float mode = inParams.x;

    // Texture (-5): sample from block texture array using inParams.y as layer
    if (mode < -4.5) {
        int layer = int(inParams.y);
        outColor = texture(blockTextures, vec3(inUv, layer));

    // RoundedRect (-4): SDF-based rounded rect with optional border
    } else if (mode < -3.5) {
        vec2 h = vec2(0.5);
        vec2 d = abs(inUv - h) - h + inParams.y;
        float dist = min(max(d.x, d.y), 0.0) + length(max(d, 0.0));
        float alpha = 1.0 - smoothstep(0.0, 1.5 / 64.0, dist);
        vec4 fill = inColor1;
        if (inParams.z > 0.0 && dist > -inParams.z) {
            fill = inColor2;
        }
        outColor = vec4(fill.rgb, fill.a * alpha);

    // GradientRadial (-3)
    } else if (mode < -2.5) {
        float d = distance(inUv, vec2(0.5)) / max(inParams.w, 0.001);
        outColor = mix(inColor1, inColor2, clamp(d, 0.0, 1.0));

    // GradientLinear (-2)
    } else if (mode < -1.5) {
        vec2 dir = vec2(1.0, 0.0);
        float t = dot(inUv, dir);
        outColor = mix(inColor1, inColor2, clamp(t, 0.0, 1.0));

    // Solid (-1)
    } else if (mode < 0.0) {
        outColor = inColor1;

    // Font (>= 0)
    } else {
        float a = texture(fontAtlas, inUv).a;
        outColor = vec4(inColor1.rgb, inColor1.a * a);
    }
}
