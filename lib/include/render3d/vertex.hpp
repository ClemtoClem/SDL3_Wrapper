#pragma once
#include "../math/math.hpp"
#include "../sdl3/structs.hpp"

namespace render3d {

// Layout attendu par assets/shaders/src/mesh_basic.vert et mesh_phong.vert :
// location 0 vec3 position, 1 vec3 normal, 2 vec2 uv, 3 vec4 color.
// `color` est stocké en UBYTE4_NORM (4 octets) plutôt qu'en FLOAT4 — le
// vertex input assembler du GPU le déballe en vec4 normalisé côté shader.
struct Vertex3D {
	math::FVector3 position;
	math::FVector3 normal{0.f, 1.f, 0.f};
	math::FVector2 uv;
	sdl3::Color color = sdl3::Color::WHITE();
};

// Sommet skinné (SkinnedMesh, M17, voir shader_chunks::VERTEX_ATTRIBUTES_SKINNED)
// — 4 indices/poids d'os. FLOAT4 pour les deux (pas UBYTE4 pour boneIndices :
// évite tout risque de conversion entier->flottant jamais éprouvée dans ce
// dépôt pour ce format) ; poids par défaut = entièrement lié à l'os 0, pour
// qu'un sommet non explicitement assigné reste rigide plutôt que dégénéré
// (somme des poids nulle).
struct SkinnedVertex3D {
	math::FVector3 position;
	math::FVector3 normal{0.f, 1.f, 0.f};
	math::FVector2 uv;
	sdl3::Color color = sdl3::Color::WHITE();
	float boneIndices[4] = {0.f, 0.f, 0.f, 0.f};
	float boneWeights[4] = {1.f, 0.f, 0.f, 0.f};
};

} // namespace render3d
