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

	ShaderBuilder &Color(sdl3::FColor c) noexcept {
		m_color = c;
		return *this;
	}
	ShaderBuilder &Texture(Ref<sdl3::GpuTexture> texture) noexcept {
		m_texture = Some(texture);
		return *this;
	}
	ShaderBuilder &Lit(LightingModel model = LightingModel::PHONG) noexcept {
		m_lightingModel = model;
		return *this;
	}
	ShaderBuilder &Metallic(float value) noexcept {
		m_metallic = value;
		return *this;
	}
	ShaderBuilder &Roughness(float value) noexcept {
		m_roughness = value;
		return *this;
	}
	ShaderBuilder &Emissive(sdl3::FColor c) noexcept {
		m_emissive = c;
		return *this;
	}
	ShaderBuilder &PointLights(int count) noexcept {
		m_pointLights = count;
		return *this;
	}
	ShaderBuilder &SpotLights(int count) noexcept {
		m_spotLights = count;
		return *this;
	}
	/// Active le shadow mapping réel (directionnelle + jusqu'à 2 lumières
	/// spot projetant une ombre, voir Canvas::SetLighting/SetLights et
	/// DirectionalLight/SpotLight::castShadow dans light.hpp) quand
	/// `samples > 0`. La valeur elle-même ne sélectionne pas encore la
	/// qualité du PCF (fixe, 3x3) — seul son signe est consommé.
	ShaderBuilder &Shadow(int samples) noexcept {
		m_shadowSamples = samples;
		return *this;
	}
	/// Active l'IBL (irradiance diffuse + spéculaire préfiltré + BRDF LUT,
	/// voir environment.hpp) — n'a d'effet que combiné à `.Lit(PBR)`, ignoré
	/// par Unlit/Phong (LIGHTING_FUNC_PHONG n'a pas de branche IBL_ENABLED).
	ShaderBuilder &Environment(bool value = true) noexcept {
		m_environment = value;
		return *this;
	}
	/// Active le rendu instancié (mat4 par instance, locations 4-7, voir
	/// shader_chunks::INSTANCE_ATTRIBUTES) à la place de PerObjectUBO.uModel —
	/// utilisé par Canvas::DrawInstancedMesh, pas par DrawMesh/DrawMeshGroup.
	ShaderBuilder &Instanced(bool value = true) noexcept {
		m_instanced = value;
		return *this;
	}
	/// Active le skinning GPU (matrices de squelette en storage buffer, voir
	/// shader_chunks::BONE_MATRIX_STORAGE_BUFFER/VERTEX_ATTRIBUTES_SKINNED) —
	/// remplace complètement le layout de sommet standard (pas de combinaison
	/// avec .Instanced(), voir le plan : un maillage skinné n'est jamais
	/// aussi instancié dans ce port).
	ShaderBuilder &Skinned(bool value = true) noexcept {
		m_skinned = value;
		return *this;
	}
	ShaderBuilder &DoubleSided(bool value = true) noexcept {
		m_doubleSided = value;
		return *this;
	}
	ShaderBuilder &Wireframe(bool value = true) noexcept {
		m_wireframe = value;
		return *this;
	}

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
	[[nodiscard]] uint32_t FragmentUniformBufferCount() const noexcept {
		uint32_t count = 2; // MaterialUBO + LightUBO, toujours présents
		if (m_pointLights > 0)
			++count;
		if (m_spotLights > 0)
			++count;
		// Pas de +1 pour l'ombre : les matrices vivent dans LightUBO lui-même
		// (LIGHT_UBO_SHADOW remplace LIGHT_UBO_BASE au même binding=1) — SDL_GPU
		// limite à 4 uniform buffers fragment, déjà tous pris à 4 lumières.
		return count;
	}

	/// Nombre d'échantillonneurs fragment attendus — uAlbedo toujours
	/// présent (binding=0), +4 (directionnel + 2 spots + point/cubemap) si
	/// HasShadows() (voir shader_chunks::SAMPLER_SHADOW_MAPS), +3 (irradiance
	/// + préfiltré + BRDF LUT) si HasEnvironment().
	[[nodiscard]] uint32_t FragmentSamplerCount() const noexcept {
		uint32_t count = 1;
		if (HasShadows())
			count += 4;
		if (HasEnvironment())
			count += 3;
		return count;
	}

	[[nodiscard]] Result<ShaderProgram, String> Build(sdl3::GpuDevice &device) const {
		using namespace shader_chunks;
		bool lit = m_lightingModel != LightingModel::UNLIT;

		String defines = String::Format("#version 450\n#define NUM_POINT_LIGHTS %d\n#define NUM_SPOT_LIGHTS %d\n",
										m_pointLights, m_spotLights);
		if (HasShadows())
			defines += String::Format("#define SHADOW_ENABLED\n#define SHADOW_MAP_SIZE %.1f\n",
									  double(SHADOW_MAP_RESOLUTION));
		if (HasEnvironment())
			defines += "#define IBL_ENABLED\n";
		if (IsInstanced())
			defines += "#define INSTANCED_ENABLED\n";

		String vertexSrc = defines;
		vertexSrc += (IsSkinned() ? VERTEX_ATTRIBUTES_SKINNED : VERTEX_ATTRIBUTES);
		if (IsInstanced())
			vertexSrc += INSTANCE_ATTRIBUTES;
		if (IsSkinned())
			vertexSrc += BONE_MATRIX_STORAGE_BUFFER;
		vertexSrc += (lit ? VERTEX_OUTPUTS_LIT : VERTEX_OUTPUTS_UNLIT);
		vertexSrc += PER_FRAME_UBO;
		vertexSrc += PER_OBJECT_UBO;
		if (IsSkinned())
			vertexSrc += (lit ? VERTEX_MAIN_LIT_SKINNED : VERTEX_MAIN_UNLIT_SKINNED);
		else
			vertexSrc += (lit ? VERTEX_MAIN_LIT : VERTEX_MAIN_UNLIT);

		String fragmentSrc = defines;
		fragmentSrc += (lit ? FRAGMENT_INPUTS_LIT : FRAGMENT_INPUTS_UNLIT);
		fragmentSrc += FRAGMENT_OUTPUT;
		fragmentSrc += SAMPLER_ALBEDO;
		if (HasShadows())
			fragmentSrc += SAMPLER_SHADOW_MAPS;
		if (HasEnvironment())
			fragmentSrc += (HasShadows() ? SAMPLER_IBL_WITH_SHADOWS : SAMPLER_IBL_NO_SHADOWS);
		fragmentSrc += MATERIAL_UBO;
		if (lit) {
			fragmentSrc += (HasShadows() ? LIGHT_UBO_SHADOW : LIGHT_UBO_BASE);
			if (m_pointLights > 0)
				fragmentSrc += POINT_LIGHT_ARRAY;
			if (m_spotLights > 0)
				fragmentSrc += SPOT_LIGHT_ARRAY;
			if (m_pointLights > 0 || m_spotLights > 0)
				fragmentSrc += LIGHT_ATTENUATION_FUNC;
			fragmentSrc += (HasShadows() ? SHADOW_REAL_FUNC : SHADOW_STUB_FUNC);
			fragmentSrc += (m_lightingModel == LightingModel::PBR ? LIGHTING_FUNC_PBR : LIGHTING_FUNC_PHONG);
			fragmentSrc += FRAGMENT_MAIN_LIT;
		} else {
			fragmentSrc += FRAGMENT_MAIN_UNLIT;
		}

		auto vertexShader =
			CompileShader(device, StringView(vertexSrc.CStr(), vertexSrc.GetSize()), sdl3::gpu_shader_stage::VERTEX,
						 StringView("ShaderBuilder.vert"), 0, 2, IsSkinned() ? 1u : 0u);
		if (!vertexShader)
			return Err(std::move(vertexShader.Error()));

		uint32_t fragmentUniformBuffers = lit ? FragmentUniformBufferCount() : 1;
		uint32_t fragmentSamplers = lit ? FragmentSamplerCount() : 1;
		auto fragmentShader = CompileShader(device, StringView(fragmentSrc.CStr(), fragmentSrc.GetSize()),
											sdl3::gpu_shader_stage::FRAGMENT, StringView("ShaderBuilder.frag"),
											fragmentSamplers, fragmentUniformBuffers);
		if (!fragmentShader)
			return Err(std::move(fragmentShader.Error()));

		return Ok(ShaderProgram::FromShaders(std::move(vertexShader.Value()), std::move(fragmentShader.Value())));
	}
};

} // namespace render3d
