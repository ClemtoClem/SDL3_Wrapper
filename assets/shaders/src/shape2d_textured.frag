#version 450

// Pipeline 2D "texturé" — utilisé par Canvas::DrawImage ET Canvas::DrawText
// (le texte est rasterisé en une texture RGBA déjà colorée par
// TTF_RenderText_Blended, puis dessiné comme une simple image texturée —
// voir README, section Canvas). Convention de binding : voir shape2d.vert.

layout(location = 0) in vec4 inColor;
layout(location = 1) in vec2 inTexCoord;

layout(location = 0) out vec4 outFragColor;

layout(set = 2, binding = 0) uniform sampler2D uTexture;

void main() {
    outFragColor = inColor * texture(uTexture, inTexCoord);
}
