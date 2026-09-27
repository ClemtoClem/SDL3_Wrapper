// Définitions de render3d/shader_builder.hpp — fichier généré par splitter.py : le code
// vient tel quel de l'en-tête (seules les signatures sont réécrites).

#include "render3d/shader_builder.hpp"

namespace render3d {

// ── ShaderBuilder ────────────────────────────────────────────────────────────

ShaderBuilder & ShaderBuilder::Color(sdl3::FColor c) noexcept {
	m_color = c;
	return *this;
}

ShaderBuilder & ShaderBuilder::Texture(Ref<sdl3::GpuTexture> texture) noexcept {
	m_texture = Some(texture);
	return *this;
}

ShaderBuilder & ShaderBuilder::Lit(LightingModel model) noexcept {
	m_lightingModel = model;
	return *this;
}

ShaderBuilder & ShaderBuilder::Metallic(float value) noexcept {
	m_metallic = value;
	return *this;
}

ShaderBuilder & ShaderBuilder::Roughness(float value) noexcept {
	m_roughness = value;
	return *this;
}

ShaderBuilder & ShaderBuilder::Emissive(sdl3::FColor c) noexcept {
	m_emissive = c;
	return *this;
}

ShaderBuilder & ShaderBuilder::PointLights(int count) noexcept {
	m_pointLights = count;
	return *this;
}

ShaderBuilder & ShaderBuilder::SpotLights(int count) noexcept {
	m_spotLights = count;
	return *this;
}

ShaderBuilder & ShaderBuilder::Shadow(int samples) noexcept {
	m_shadowSamples = samples;
	return *this;
}

ShaderBuilder & ShaderBuilder::Environment(bool value) noexcept {
	m_environment = value;
	return *this;
}

ShaderBuilder & ShaderBuilder::Instanced(bool value) noexcept {
	m_instanced = value;
	return *this;
}

ShaderBuilder & ShaderBuilder::Skinned(bool value) noexcept {
	m_skinned = value;
	return *this;
}

ShaderBuilder & ShaderBuilder::DoubleSided(bool value) noexcept {
	m_doubleSided = value;
	return *this;
}

ShaderBuilder & ShaderBuilder::Wireframe(bool value) noexcept {
	m_wireframe = value;
	return *this;
}

uint32_t ShaderBuilder::FragmentUniformBufferCount() const noexcept {
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

uint32_t ShaderBuilder::FragmentSamplerCount() const noexcept {
	uint32_t count = 1;
	if (HasShadows())
		count += 4;
	if (HasEnvironment())
		count += 3;
	return count;
}

Result<ShaderProgram, String> ShaderBuilder::Build(sdl3::GpuDevice &device) const {
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

} // namespace render3d
