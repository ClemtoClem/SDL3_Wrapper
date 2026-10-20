#pragma once
#include "shader_chunks.hpp"
#include "shader_compiler.hpp"
#include "shader_program.hpp"
#include "shadow.hpp"

namespace render3d {

enum class LightingModel { UNLIT, PHONG, PBR };

/// Factory fluide de shaders : compose du GLSL à partir de shader_chunks.hpp
/// selon les options demandées, puis le compile via shader_compiler.hpp
/// (GLSL -> SPIR-V -> MSL au besoin) en un ShaderProgram prêt à l'emploi.
/// L'équivalent C++ de la composition ShaderChunk/ShaderLib de three.js, mais
/// pilotée par un petit nombre d'options plutôt qu'un système de matériaux
/// complet (voir render3d::Material pour la couche "matériau" au-dessus).
///
/// @code
/// auto program = ShaderBuilder()
///     .Color(FColor{0.1f, 0.4f, 0.2f, 1.0f})
///     .Texture(albedoTexture)
///     .Lit(LightingModel::PBR)
///     .Metallic(0.8f).Roughness(0.3f)
///     .PointLights(2)
///     .Shadow(4)
///     .Build(device);
/// @endcode
class ShaderBuilder {
	sdl3::FColor m_color{1.f, 1.f, 1.f, 1.f};
	Option<Ref<sdl3::GpuTexture>> m_texture = NONE;
	LightingModel m_lightingModel = LightingModel::PHONG;
	float m_metallic = 0.f;
	float m_roughness = 0.5f;
	sdl3::FColor m_emissive{0.f, 0.f, 0.f, 0.f};
	int m_pointLights = 0;
	int m_spotLights = 0;
	// >0 active le shadow mapping réel (directionnelle + jusqu'à 2 spots,
	// voir shader_chunks::SHADOW_MATRIX_UBO/SHADOW_REAL_FUNC) — la valeur
	// elle-même ne module pas la qualité du PCF (fixe, 3x3) cette phase,
	// seul son signe (0 vs >0) est consommé par Build().
	int m_shadowSamples = 0;
	bool m_environment = false; // IBL (M14) — voir Canvas::SetEnvironment
	bool m_instanced = false;   // InstancedMesh (M16) — voir Canvas::DrawInstancedMesh
	bool m_skinned = false;     // SkinnedMesh (M17) — voir Canvas::DrawSkinnedMesh
	bool m_doubleSided = false;
	bool m_wireframe = false;

public:
	ShaderBuilder() = default;

	ShaderBuilder &Color(sdl3::FColor c) noexcept;
	ShaderBuilder &Texture(Ref<sdl3::GpuTexture> texture) noexcept;
	ShaderBuilder &Lit(LightingModel model = LightingModel::PHONG) noexcept;
	ShaderBuilder &Metallic(float value) noexcept;
	ShaderBuilder &Roughness(float value) noexcept;
	ShaderBuilder &Emissive(sdl3::FColor c) noexcept;
	ShaderBuilder &PointLights(int count) noexcept;
	ShaderBuilder &SpotLights(int count) noexcept;
	/// Active le shadow mapping réel (directionnelle + jusqu'à 2 lumières
	/// spot projetant une ombre, voir Canvas::SetLighting/SetLights et
	/// DirectionalLight/SpotLight::castShadow dans light.hpp) quand
	/// `samples > 0`. La valeur elle-même ne sélectionne pas encore la
	/// qualité du PCF (fixe, 3x3) — seul son signe est consommé.
	ShaderBuilder &Shadow(int samples) noexcept;
	/// Active l'IBL (irradiance diffuse + spéculaire préfiltré + BRDF LUT,
	/// voir environment.hpp) — n'a d'effet que combiné à `.Lit(PBR)`, ignoré
	/// par Unlit/Phong (LIGHTING_FUNC_PHONG n'a pas de branche IBL_ENABLED).
	ShaderBuilder &Environment(bool value = true) noexcept;
	/// Active le rendu instancié (mat4 par instance, locations 4-7, voir
	/// shader_chunks::INSTANCE_ATTRIBUTES) à la place de PerObjectUBO.uModel —
	/// utilisé par Canvas::DrawInstancedMesh, pas par DrawMesh/DrawMeshGroup.
	ShaderBuilder &Instanced(bool value = true) noexcept;
	/// Active le skinning GPU (matrices de squelette en storage buffer, voir
	/// shader_chunks::BONE_MATRIX_STORAGE_BUFFER/VERTEX_ATTRIBUTES_SKINNED) —
	/// remplace complètement le layout de sommet standard (pas de combinaison
	/// avec .Instanced(), voir le plan : un maillage skinné n'est jamais
	/// aussi instancié dans ce port).
	ShaderBuilder &Skinned(bool value = true) noexcept;
	ShaderBuilder &DoubleSided(bool value = true) noexcept;
	ShaderBuilder &Wireframe(bool value = true) noexcept;

	[[nodiscard]] LightingModel Model() const noexcept { return m_lightingModel; }
	[[nodiscard]] sdl3::FColor GetColor() const noexcept { return m_color; }
	[[nodiscard]] float GetMetallic() const noexcept { return m_metallic; }
	[[nodiscard]] float GetRoughness() const noexcept { return m_roughness; }
	[[nodiscard]] sdl3::FColor GetEmissive() const noexcept { return m_emissive; }
	[[nodiscard]] bool HasTexture() const noexcept { return m_texture.IsSome(); }
	[[nodiscard]] Option<Ref<sdl3::GpuTexture>> GetTexture() const noexcept { return m_texture; }
	[[nodiscard]] int GetPointLightCount() const noexcept { return m_pointLights; }
	[[nodiscard]] int GetSpotLightCount() const noexcept { return m_spotLights; }
	[[nodiscard]] bool HasShadows() const noexcept { return m_shadowSamples > 0; }
	[[nodiscard]] bool HasEnvironment() const noexcept { return m_environment; }
	[[nodiscard]] bool IsInstanced() const noexcept { return m_instanced; }
	[[nodiscard]] bool IsSkinned() const noexcept { return m_skinned; }
	[[nodiscard]] bool IsDoubleSided() const noexcept { return m_doubleSided; }
	[[nodiscard]] bool IsWireframe() const noexcept { return m_wireframe; }

	/// Nombre de vec4/UBO fragment attendus par le shader généré — pour que
	/// l'appelant (Canvas/Material) sache combien de PushFragmentUniformData
	/// il doit émettre pour cette configuration.
	[[nodiscard]] uint32_t FragmentUniformBufferCount() const noexcept;

	/// Nombre d'échantillonneurs fragment attendus — uAlbedo toujours
	/// présent (binding=0), +4 (directionnel + 2 spots + point/cubemap) si
	/// HasShadows() (voir shader_chunks::SAMPLER_SHADOW_MAPS), +3 (irradiance
	/// + préfiltré + BRDF LUT) si HasEnvironment().
	[[nodiscard]] uint32_t FragmentSamplerCount() const noexcept;

	[[nodiscard]] Result<ShaderProgram, String> Build(sdl3::GpuDevice &device) const;
};

} // namespace render3d
