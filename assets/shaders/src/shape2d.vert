#version 450

// Convention de binding SPIR-V imposée par SDL_GPU (voir README, section
// Shaders) : vertex -> set=0 échantillonneurs/stockage, set=1 uniform
// buffers ; fragment -> set=2 échantillonneurs/stockage, set=3 uniform
// buffers. Toute violation de cette règle échoue silencieusement à
// l'exécution (pas d'erreur de compilation).

layout(location = 0) in vec2 inPosition;
layout(location = 1) in vec4 inColor;
layout(location = 2) in vec2 inTexCoord;

layout(location = 0) out vec4 outColor;
layout(location = 1) out vec2 outTexCoord;

layout(set = 1, binding = 0) uniform UBO {
    mat4 uProjection;
};

void main() {
    gl_Position = uProjection * vec4(inPosition, 0.0, 1.0);
    outColor = inColor;
    outTexCoord = inTexCoord;
}
