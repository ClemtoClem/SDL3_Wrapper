#version 450

// Maillage non éclairé, couleur plate — équivalent du MeshBasicMaterial de
// three.js. Utilisé notamment pour Canvas::DrawSphere/DrawAABB (dessin de
// debug). Convention de binding : voir shape2d.vert.

layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec3 inNormal;
layout(location = 2) in vec2 inUV;
layout(location = 3) in vec4 inColor;

layout(location = 0) out vec4 outColor;
layout(location = 1) out vec2 outUV;

layout(set = 1, binding = 0) uniform PerFrameUBO {
    mat4 uViewProjection;
};

layout(set = 1, binding = 1) uniform PerObjectUBO {
    mat4 uModel;
    mat4 uNormalMatrix;
};

void main() {
    gl_Position = uViewProjection * uModel * vec4(inPosition, 1.0);
    outColor = inColor;
    outUV = inUV;
}
