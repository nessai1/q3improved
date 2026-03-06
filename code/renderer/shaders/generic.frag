#version 450

layout(constant_id = 0) const int ALPHA_TEST = 0;  // 0=none, 1=GT0, 2=LT80, 3=GE80
layout(constant_id = 1) const int TEX_ENV = 0;     // 0=single, 1=MODULATE, 2=ADD, 3=REPLACE, 4=DECAL

layout(push_constant) uniform PushConstants {
    mat4 mvp;
    float time;
    float alphaRef;
    float identityLight;
    float pad;
} pc;

layout(set = 0, binding = 0) uniform sampler2D tex0;
layout(set = 0, binding = 1) uniform sampler2D tex1;

layout(location = 0) in vec2 fragTexCoord0;
layout(location = 1) in vec2 fragTexCoord1;
layout(location = 2) in vec4 fragColor;

layout(location = 0) out vec4 outColor;

void main() {
    vec4 base = texture(tex0, fragTexCoord0);

    if (TEX_ENV == 0) {
        // Single texture, modulated by vertex color
        outColor = base * fragColor;
    } else {
        vec4 lightmap = texture(tex1, fragTexCoord1);

        if (TEX_ENV == 1) {
            // GL_MODULATE: base * lightmap
            outColor = base * lightmap;
        } else if (TEX_ENV == 2) {
            // GL_ADD: base + lightmap, alpha multiplied
            outColor = vec4(base.rgb + lightmap.rgb, base.a * lightmap.a);
        } else if (TEX_ENV == 3) {
            // GL_REPLACE: use lightmap only
            outColor = lightmap;
        } else {
            // GL_DECAL: mix base and lightmap by lightmap alpha
            outColor = vec4(mix(base.rgb, lightmap.rgb, lightmap.a), base.a);
        }

        outColor *= fragColor;
    }

    // Alpha test (via specialization constant -- branches compile away)
    if (ALPHA_TEST == 1 && outColor.a <= 0.0) discard;     // GT0
    if (ALPHA_TEST == 2 && outColor.a >= 0.5) discard;     // LT128 (0x80 = 128/255 ~ 0.5)
    if (ALPHA_TEST == 3 && outColor.a <  0.5) discard;     // GE128
}
