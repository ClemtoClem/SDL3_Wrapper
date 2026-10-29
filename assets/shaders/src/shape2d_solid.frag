#version 450

// Pipeline 2D "couleur unie" (formes géométriques sans texture) — voir
// shape2d.vert pour la convention de binding.

layout(location = 0) in vec4 inColor;
layout(location = 1) in vec2 inTexCoord; // Inutilisé, garde le layout aligné avec shape2d.vert.

layout(location = 0) out vec4 outFragColor;

void main() {
    outFragColor = inColor;
}
