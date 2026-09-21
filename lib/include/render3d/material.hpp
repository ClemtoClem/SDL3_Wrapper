#pragma once
#include <functional>

#include "../core/core.hpp"
#include "../sdl3/structs.hpp"
#include "../sdl3/gpu.hpp"

namespace render3d {

enum class ShaderKind { BASIC, PHONG, PBR };

// Topologie de primitive (M15, voir Points/LineSegments dans object3d.hpp) —
// portée par PipelineKey/DrawCall plutôt que par Mesh lui-même : Mesh reste
// "juste" vertices+indices+groups, la topologie est une propriété du pipeline
// (SDL_GPU en fait un état figé à la création, voir Canvas::GetOrCreatePipeline).
enum class PrimitiveTopology { TRIANGLES, LINES, POINTS };

// `metallic`/`roughness` sont consommés par la fonction d'éclairage PBR
// (shader_chunks::LIGHTING_FUNC_PBR, voir shader_builder.hpp) quand `pbr` est
// activé ; sous Phong (`pbr = false`, par défaut), `metallic` reste inerte —
// même honnêteté que le commentaire du shader mesh_phong.frag précompilé.
struct Material {
	sdl3::Color baseColor = sdl3::Color::WHITE();
	float roughness = 0.5f;
	float metallic = 0.f;
	Option<Ref<sdl3::GpuTexture>> albedo = NONE;
	bool lit = true;
	// PBR (Cook-Torrance metallic-roughness) au lieu de Phong précompilé —
	// toujours compilé via ShaderBuilder (voir Canvas::GetOrCreatePipeline),
	// Canvas n'ayant pas de shader PBR précompilé. Sans effet si `lit=false`.
	bool pbr = false;
	bool doubleSided = false;
	bool wireframe = false;
	/// Faux : dessiné PAR-DESSUS la scène, sans test ni écriture de
	/// profondeur. C'est ce qu'il faut pour un manipulateur d'éditeur, une
	/// silhouette de sélection ou des axes de débogage : ils doivent rester
	/// visibles même à l'intérieur d'un objet, sinon on manipule à l'aveugle.
	/// L'ordre de dessin décide alors seul du recouvrement.
	bool depthTest = true;

	[[nodiscard]] static Material Default() noexcept { return {}; }

	[[nodiscard]] static Material Plastic(sdl3::Color color) noexcept {
		Material m;
		m.baseColor = color;
		m.roughness = 0.4f;
		return m;
	}

	[[nodiscard]] static Material Metal(sdl3::Color color) noexcept {
		Material m;
		m.baseColor = color;
		m.roughness = 0.2f;
		m.metallic = 1.f;
		return m;
	}

	[[nodiscard]] static Material Wood(sdl3::Color color = sdl3::Color(133, 94, 66)) noexcept {
		Material m;
		m.baseColor = color;
		m.roughness = 0.8f;
		return m;
	}

	[[nodiscard]] static Material Unlit(sdl3::Color color) noexcept {
		Material m;
		m.baseColor = color;
		m.lit = false;
		return m;
	}

	/// Matériau PBR metallic-roughness (Cook-Torrance) — voir la note `pbr`
	/// ci-dessus : compilé via ShaderBuilder, pas de fichier précompilé.
	[[nodiscard]] static Material Pbr(sdl3::Color color, float metallic = 0.f, float roughness = 0.5f) noexcept {
		Material m;
		m.baseColor = color;
		m.metallic = metallic;
		m.roughness = roughness;
		m.pbr = true;
		return m;
	}
};

// Combinaison distincte de shader + états de pipeline nécessitant une
// `GpuGraphicsPipeline` propre — utilisée par Canvas pour mettre en cache un
// pipeline par combinaison réellement rencontrée plutôt que par Material.
struct PipelineKey {
	ShaderKind shader = ShaderKind::PHONG;
	bool doubleSided = false;
	bool wireframe = false;
	bool hasTexture = false;
	// Nombre de lumières ponctuelles/spot actives (Canvas::SetLights) au
	// moment du dessin : PHONG/PBR avec au moins une lumière ponctuelle ou
	// spot utilisent un shader généré par ShaderBuilder (voir
	// shader_builder.hpp) plutôt que le Phong précompilé de Canvas —
	// participe donc à l'identité du pipeline au même titre que `shader`.
	int pointLightCount = 0;
	int spotLightCount = 0;
	// Vrai si au moins une lumière de la scène projette une ombre (voir
	// DirectionalLight/SpotLight::castShadow, light.hpp) au moment du dessin
	// — comme pointLightCount/spotLightCount, force un shader ShaderBuilder
	// (voir Canvas::GetOrCreatePipeline, WantsBuiltShader).
	bool hasShadows = false;
	// Vrai si le matériau est PBR ET qu'un environnement est actif
	// (Canvas::SetEnvironment) au moment du dessin — voir Canvas::End().
	bool hasEnvironment = false;
	PrimitiveTopology topology = PrimitiveTopology::TRIANGLES;
	// Vrai pour les draws émis par Canvas::DrawInstancedMesh — layout de
	// sommet différent (2e vertex buffer, locations 4-7, voir
	// shader_chunks::INSTANCE_ATTRIBUTES), donc un pipeline distinct.
	bool instanced = false;
	// Vrai pour les draws émis par Canvas::DrawSkinnedMesh — layout de
	// sommet complètement différent (SkinnedVertex3D, voir vertex.hpp), donc
	// un pipeline distinct (jamais combiné à `instanced`, voir le plan).
	bool skinned = false;
	bool depthTest = true;
	sdl3::GpuTextureFormat colorFormat{};
	sdl3::GpuTextureFormat depthFormat{};

	[[nodiscard]] bool operator==(const PipelineKey &o) const noexcept {
		return shader == o.shader && doubleSided == o.doubleSided && wireframe == o.wireframe &&
			   hasTexture == o.hasTexture && pointLightCount == o.pointLightCount &&
			   spotLightCount == o.spotLightCount && hasShadows == o.hasShadows &&
			   hasEnvironment == o.hasEnvironment && topology == o.topology && instanced == o.instanced &&
			   skinned == o.skinned && depthTest == o.depthTest && colorFormat == o.colorFormat &&
			   depthFormat == o.depthFormat;
	}
};

} // namespace render3d

template <> struct std::hash<render3d::PipelineKey> {
	size_t operator()(const render3d::PipelineKey &k) const noexcept {
		size_t h = std::hash<int>()(int(k.shader));
		auto mix = [&h](size_t v) { h ^= v + 0x9e3779b97f4a7c15ULL + (h << 6) + (h >> 2); };
		mix(std::hash<bool>()(k.doubleSided));
		mix(std::hash<bool>()(k.wireframe));
		mix(std::hash<bool>()(k.hasTexture));
		mix(std::hash<int>()(k.pointLightCount));
		mix(std::hash<int>()(k.spotLightCount));
		mix(std::hash<bool>()(k.hasShadows));
		mix(std::hash<bool>()(k.hasEnvironment));
		mix(std::hash<int>()(int(k.topology)));
		mix(std::hash<bool>()(k.instanced));
		mix(std::hash<bool>()(k.skinned));
		mix(std::hash<bool>()(k.depthTest));
		mix(std::hash<int>()(int(k.colorFormat)));
		mix(std::hash<int>()(int(k.depthFormat)));
		return h;
	}
};
