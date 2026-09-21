#version 450

// Convention de binding : voir shape2d.vert.

layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec3 inNormal;
layout(location = 2) in vec2 inUV;
layout(location = 3) in vec4 inColor;

layout(location = 0) out vec3 outWorldPos;
layout(location = 1) out vec3 outNormal;
layout(location = 2) out vec2 outUV;
layout(location = 3) out vec4 outColor;

layout(set = 1, binding = 0) uniform PerFrameUBO {
    mat4 uViewProjection;
};

layout(set = 1, binding = 1) uniform PerObjectUBO {
    mat4 uModel;
    mat4 uNormalMatrix;
};

void main() {
    vec4 worldPos = uModel * vec4(inPosition, 1.0);

    outWorldPos = worldPos.xyz;
    outNormal = mat3(uNormalMatrix) * inNormal;
    outUV = inUV;
    outColor = inColor;

    gl_Position = uViewProjection * worldPos;
}
