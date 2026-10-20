#version 450
// Shader d'eau du mod (chargé et compilé au démarrage par le jeu).
layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec3 inNormal;
layout(location = 2) in vec2 inUV;
layout(location = 3) in vec4 inColor;
layout(location = 0) out vec2 outUV;
layout(location = 1) out vec4 outColor;
layout(set = 1, binding = 0) uniform Camera { mat4 uViewProjection; };
layout(set = 1, binding = 1) uniform Object { mat4 uModel; mat4 uNormal; };
void main() {
    outUV = inUV;
    outColor = inColor;
    gl_Position = uViewProjection * uModel * vec4(inPosition, 1.0);
}
