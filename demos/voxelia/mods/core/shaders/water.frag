#version 450
layout(location = 0) in vec2 inUV;
layout(location = 1) in vec4 inColor;
layout(location = 0) out vec4 outFragColor;
layout(set = 2, binding = 0) uniform sampler2D uAlbedo;
void main() {
    vec4 c = texture(uAlbedo, inUV) * inColor;
    outFragColor = vec4(c.rgb * vec3(0.9, 0.95, 1.1), c.a);
}
