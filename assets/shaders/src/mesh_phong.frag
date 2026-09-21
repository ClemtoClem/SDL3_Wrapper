#version 450

// Éclairage Lambert + spéculaire Blinn-Phong — équivalent du
// MeshPhongMaterial de three.js. Convention de binding : voir shape2d.vert.

layout(location = 0) in vec3 inWorldPos;
layout(location = 1) in vec3 inNormal;
layout(location = 2) in vec2 inUV;
layout(location = 3) in vec4 inColor;

layout(location = 0) out vec4 outFragColor;

layout(set = 2, binding = 0) uniform sampler2D uAlbedo;

layout(set = 3, binding = 0) uniform MaterialUBO {
    vec4 uBaseColor;
    float uRoughness;
    float uMetallic;
    float uUseTexture; // 0.0 ou 1.0 — les bool ne sont pas portables en std140.
    float uPadding0;
};

layout(set = 3, binding = 1) uniform LightUBO {
    vec3 uLightDir;
    float uPadding1;
    vec4 uLightColor;
    vec4 uAmbientColor;
    vec3 uCameraPos;
    float uPadding2;
};

void main() {
    vec3 N = normalize(inNormal);
    vec3 L = normalize(-uLightDir);
    vec3 V = normalize(uCameraPos - inWorldPos);
    vec3 H = normalize(L + V);

    float diffuse = max(dot(N, L), 0.0);
    float shininess = mix(64.0, 4.0, uRoughness);
    float specular = pow(max(dot(N, H), 0.0), shininess) * (1.0 - uRoughness);

    vec4 albedo = uBaseColor * inColor;
    if (uUseTexture > 0.5)
        albedo *= texture(uAlbedo, inUV);

    vec3 color =
        albedo.rgb * uAmbientColor.rgb +
        albedo.rgb * uLightColor.rgb * diffuse +
        uLightColor.rgb * specular;

    outFragColor = vec4(color, albedo.a);
}
