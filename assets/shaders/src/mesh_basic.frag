#version 450

layout(location = 0) in vec4 inColor;
layout(location = 1) in vec2 inUV; // Inutilisé, garde le layout aligné avec mesh_basic.vert.

layout(location = 0) out vec4 outFragColor;

layout(set = 3, binding = 0) uniform MaterialUBO {
    vec4 uBaseColor;
};

void main() {
    outFragColor = uBaseColor * inColor;
}
