#pragma once
#include <cstring>
#include <vector>

#include "../core/core.hpp"
#include "../math/math.hpp"
#include "../sdl3/gpu.hpp"
#include "hdr_loader.hpp"
#include "shader_compiler.hpp"
#include "shader_program.hpp"
#include "vertex.hpp"

namespace render3d {

// Infrastructure de capture de cubemap (M11) : convertir une texture 2D
// équirectangulaire (chargée depuis un .hdr, voir hdr_loader.hpp) en cubemap
// en rendant un cube unité (caméra à l'origine) 6 fois, une face par passe —
// réutilisée telle quelle par l'irradiance diffuse (M12) et le préfiltrage
// spéculaire (M13), seul le fragment shader change.

/// Matrice vue-projection pour la face `faceIndex` (0-5, ordre standard
/// +X,-X,+Y,-Y,+Z,-Z) d'une capture cubemap depuis l'origine — FOV 90°,
/// near/far arbitrairement serrés (la géométrie capturée est le cube unité
/// lui-même, voir CubeVertices()). Duplique la logique de
/// PointLight::ShadowViewProjection (light.hpp) plutôt que de la partager :
/// light.hpp ne dépend d'aucun module GPU, cubemap.hpp ne doit pas l'y forcer.
[[nodiscard]] inline math::FMatrix4 CubeFaceViewProjection(int faceIndex) noexcept {
	static constexpr math::FVector3 DIRS[6] = {{1.f, 0.f, 0.f},  {-1.f, 0.f, 0.f}, {0.f, 1.f, 0.f},
											   {0.f, -1.f, 0.f}, {0.f, 0.f, 1.f},  {0.f, 0.f, -1.f}};
	static constexpr math::FVector3 UPS[6] = {{0.f, -1.f, 0.f}, {0.f, -1.f, 0.f}, {0.f, 0.f, 1.f},
											  {0.f, 0.f, -1.f}, {0.f, -1.f, 0.f}, {0.f, -1.f, 0.f}};
	math::FMatrix4 view = math::FMatrix4::LookAt({0.f, 0.f, 0.f}, DIRS[faceIndex], UPS[faceIndex]);
	math::FMatrix4 proj = math::FMatrix4::Perspective(1.57079632679f, 1.f, 0.1f, 10.f);
	return proj * view;
}

/// Vertex shader partagé par toutes les passes de capture/dessin de cube
/// unité (conversion équirectangulaire M11, convolution d'irradiance M12,
/// préfiltrage spéculaire M13, skybox) — le sommet fait toujours office de
/// direction (`outDir`), seul le fragment shader change d'une passe à l'autre.
inline constexpr const char *CUBE_CAPTURE_VERTEX_SHADER = R"(#version 450
layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec3 inNormal;
layout(location = 2) in vec2 inUV;
layout(location = 3) in vec4 inColor;
layout(set = 1, binding = 0) uniform PerFrameUBO { mat4 uViewProjection; };
layout(location = 0) out vec3 outDir;
void main() {
	outDir = inPosition;
	gl_Position = uViewProjection * vec4(inPosition, 1.0);
}
)";

/// Cube unité (36 sommets non indexés, deux triangles par face) centré à
/// l'origine, winding CCW-vu-de-l'extérieur (même convention que Mesh::Cube(),
/// voir canvas.hpp) — la position du sommet EST la direction de capture
/// (cube unité, caméra à l'origine), le fragment shader n'a besoin de rien
/// d'autre. Vertex3D est réutilisé (normal/uv/color ignorés) pour garder le
/// même layout de pipeline que le reste du module.
[[nodiscard]] inline std::vector<Vertex3D> CubeVertices() {
	auto v = [](float x, float y, float z) { return Vertex3D{{x, y, z}, {0, 1, 0}, {0, 0}, sdl3::Color::WHITE()}; };
	return {
		// +X
		v(1, -1, -1), v(1, 1, -1), v(1, 1, 1), v(1, -1, -1), v(1, 1, 1), v(1, -1, 1),
		// -X
		v(-1, -1, 1), v(-1, 1, 1), v(-1, 1, -1), v(-1, -1, 1), v(-1, 1, -1), v(-1, -1, -1),
		// +Y
		v(-1, 1, -1), v(-1, 1, 1), v(1, 1, 1), v(-1, 1, -1), v(1, 1, 1), v(1, 1, -1),
		// -Y
		v(-1, -1, 1), v(-1, -1, -1), v(1, -1, -1), v(-1, -1, 1), v(1, -1, -1), v(1, -1, 1),
		// +Z
		v(1, -1, 1), v(1, 1, 1), v(-1, 1, 1), v(1, -1, 1), v(-1, 1, 1), v(-1, -1, 1),
		// -Z
		v(-1, -1, -1), v(-1, 1, -1), v(1, 1, -1), v(-1, -1, -1), v(1, 1, -1), v(1, -1, -1),
	};
}

/// Format couleur HDR pour les cubemaps d'environnement — R16G16B16A16_FLOAT
/// préféré (supporté universellement en cible de rendu), repli sur
/// R32G32B32A32_FLOAT (même stratégie de sondage que Canvas::Create() pour
/// la depth texture, voir shadow.hpp::ResolveShadowDepthFormat).
[[nodiscard]] inline sdl3::GpuTextureFormat ResolveHdrColorFormat(sdl3::GpuDevice &device) {
	sdl3::GpuTextureFormat format = SDL_GPU_TEXTUREFORMAT_R16G16B16A16_FLOAT;
	if (!device.TextureSupportsFormat(format, sdl3::gpu_texture_type::CUBE,
									  sdl3::gpu_texture_usage::COLOR_TARGET | sdl3::gpu_texture_usage::SAMPLER))
		format = SDL_GPU_TEXTUREFORMAT_R32G32B32A32_FLOAT;
	return format;
}

[[nodiscard]] inline Result<sdl3::GpuTexture, StringView> CreateEnvironmentCubemap(sdl3::GpuDevice &device,
																				   uint32_t size,
																				   sdl3::GpuTextureFormat format,
																				   uint32_t numLevels = 1) {
	sdl3::GpuTextureCreateInfo info{};
	info.type = sdl3::gpu_texture_type::CUBE;
	info.format = format;
	info.usage = sdl3::gpu_texture_usage::COLOR_TARGET | sdl3::gpu_texture_usage::SAMPLER;
	info.width = size;
	info.height = size;
	info.layer_count_or_depth = 6;
	info.num_levels = numLevels;
	info.sample_count = sdl3::gpu_sample_count::SAMPLE1;
	return device.CreateTexture(info);
}

/// Upload l'image équirectangulaire décodée (RGB, voir hdr_loader.hpp) comme
/// texture 2D source (RGBA32F — complète le canal alpha à 1, toujours
/// supporté en lecture SAMPLER contrairement aux cibles de rendu HDR).
[[nodiscard]] inline Result<sdl3::GpuTexture, String> UploadEquirectTexture(sdl3::GpuDevice &device,
																			const HdrImage &image) {
	sdl3::GpuTextureCreateInfo info{};
	info.type = sdl3::gpu_texture_type::TEXTURE2_D;
	info.format = SDL_GPU_TEXTUREFORMAT_R32G32B32A32_FLOAT;
	info.usage = sdl3::gpu_texture_usage::SAMPLER;
	info.width = uint32_t(image.width);
	info.height = uint32_t(image.height);
	info.layer_count_or_depth = 1;
	info.num_levels = 1;
	info.sample_count = sdl3::gpu_sample_count::SAMPLE1;
	auto textureResult = device.CreateTexture(info);
	if (!textureResult)
		return Err(String(textureResult.Error()));

	std::vector<float> rgba(size_t(image.width) * size_t(image.height) * 4);
	for (size_t i = 0; i < size_t(image.width) * size_t(image.height); ++i) {
		rgba[i * 4 + 0] = image.pixels[i * 3 + 0];
		rgba[i * 4 + 1] = image.pixels[i * 3 + 1];
		rgba[i * 4 + 2] = image.pixels[i * 3 + 2];
		rgba[i * 4 + 3] = 1.f;
	}
	uint32_t byteSize = uint32_t(rgba.size() * sizeof(float));
	auto transferResult = device.CreateTransferBuffer(sdl3::gpu_transfer_buffer_usage::UPLOAD, byteSize);
	if (!transferResult)
		return Err(String(transferResult.Error()));
	sdl3::GpuTransferBuffer transfer = std::move(transferResult.Value());
	{
		auto mapped = transfer.Map(false);
		if (!mapped)
			return Err(String("UploadEquirectTexture: échec du mapping du transfer buffer"));
		std::memcpy(mapped.GetData(), rgba.data(), byteSize);
	}

	auto cmd = device.AcquireCommandBuffer();
	{
		sdl3::GpuCopyPass copyPass = cmd.BeginCopyPass();
		sdl3::GpuTextureTransferInfo src{transfer.Get(), 0, uint32_t(image.width), uint32_t(image.height)};
		sdl3::GpuTextureRegion dst{
			textureResult.Value().Get(), 0, 0, 0, 0, 0, uint32_t(image.width), uint32_t(image.height), 1};
		copyPass.UploadToTexture(src, dst, false);
	}
	if (!cmd.Submit())
		return Err(String(sdl3::GetError()));

	return Ok(std::move(textureResult.Value()));
}

/// Shader de conversion équirectangulaire -> cubemap : le sommet du cube
/// unité EST la direction de capture (voir CubeVertices()), le fragment la
/// convertit en UV équirectangulaire (atan2/asin, convention standard) et
/// échantillonne la texture source.
[[nodiscard]] inline Result<ShaderProgram, String> BuildEquirectToCubemapShaderProgram(sdl3::GpuDevice &device) {
	String vertexSrc = CUBE_CAPTURE_VERTEX_SHADER;
	String fragmentSrc = R"(#version 450
layout(location = 0) in vec3 inDir;
layout(location = 0) out vec4 outFragColor;
layout(set = 2, binding = 0) uniform sampler2D uEquirect;
const float PI = 3.14159265359;
void main() {
	vec3 dir = normalize(inDir);
	float u = atan(dir.z, dir.x) / (2.0 * PI) + 0.5;
	float v = asin(clamp(dir.y, -1.0, 1.0)) / PI + 0.5;
	outFragColor = texture(uEquirect, vec2(u, v));
}
)";
	auto vertexShader = CompileShader(device, StringView(vertexSrc.CStr(), vertexSrc.GetSize()),
									  sdl3::gpu_shader_stage::VERTEX, StringView("EquirectToCube.vert"), 0, 1);
	if (!vertexShader)
		return Err(std::move(vertexShader.Error()));
	auto fragmentShader = CompileShader(device, StringView(fragmentSrc.CStr(), fragmentSrc.GetSize()),
										sdl3::gpu_shader_stage::FRAGMENT, StringView("EquirectToCube.frag"), 1, 0);
	if (!fragmentShader)
		return Err(std::move(fragmentShader.Error()));
	return Ok(ShaderProgram::FromShaders(std::move(vertexShader.Value()), std::move(fragmentShader.Value())));
}

/// Pipeline générique "capture de face de cube" — réutilisé par l'irradiance
/// (M12) et le préfiltrage spéculaire (M13) avec un autre ShaderProgram/format.
[[nodiscard]] inline Result<sdl3::GpuGraphicsPipeline, StringView>
CreateCubeCapturePipeline(sdl3::GpuDevice &device, ShaderProgram &program, sdl3::GpuTextureFormat colorFormat) {
	sdl3::GpuVertexBufferDescription vertexBufferDesc{0, uint32_t(sizeof(Vertex3D)),
													  sdl3::gpu_vertex_input_rate::VERTEX, 0};
	sdl3::GpuVertexAttribute vertexAttributes[4] = {
		{0, 0, sdl3::gpu_vertex_element_format::FLOAT3, uint32_t(offsetof(Vertex3D, position))},
		{1, 0, sdl3::gpu_vertex_element_format::FLOAT3, uint32_t(offsetof(Vertex3D, normal))},
		{2, 0, sdl3::gpu_vertex_element_format::FLOAT2, uint32_t(offsetof(Vertex3D, uv))},
		{3, 0, sdl3::gpu_vertex_element_format::UBYTE4_NORM, uint32_t(offsetof(Vertex3D, color))},
	};
	sdl3::GpuColorTargetDescription colorTargetDesc{};
	colorTargetDesc.format = colorFormat;

	sdl3::GpuGraphicsPipelineCreateInfo info{};
	info.vertex_shader = program.Vertex().Get();
	info.fragment_shader = program.Fragment().Get();
	info.vertex_input_state.vertex_buffer_descriptions = &vertexBufferDesc;
	info.vertex_input_state.num_vertex_buffers = 1;
	info.vertex_input_state.vertex_attributes = vertexAttributes;
	info.vertex_input_state.num_vertex_attributes = 4;
	info.primitive_type = sdl3::gpu_primitive_type::TRIANGLE_LIST;
	info.rasterizer_state.fill_mode = sdl3::gpu_fill_mode::FILL;
	info.rasterizer_state.cull_mode = sdl3::gpu_cull_mode::NONE; // capture depuis l'intérieur du cube unité
	info.rasterizer_state.front_face = sdl3::gpu_front_face::CLOCKWISE;
	info.multisample_state.sample_count = sdl3::gpu_sample_count::SAMPLE1;
	info.depth_stencil_state.enable_depth_test = false;
	info.depth_stencil_state.enable_depth_write = false;
	info.target_info.color_target_descriptions = &colorTargetDesc;
	info.target_info.num_color_targets = 1;
	info.target_info.has_depth_stencil_target = false;

	return device.CreateGraphicsPipeline(info);
}

// ── Irradiance diffuse (M12) — convolution hémisphère cosinus-pondérée de la
// cubemap d'environnement (M11), technique standard (voir learnopengl.com/PBR/IBL) :
// pour chaque texel de sortie (direction N = sommet du cube unité), intègre
// la radiance entrante sur l'hémisphère autour de N par un double balayage
// phi/theta à pas fixe — approche explicite délibérément préférée à un calcul
// analytique (SH) ou au compute shader (voir le plan : aucun précédent de
// compute dans ce dépôt, une passe fragment supplémentaire reste cohérente
// avec le reste du module).
[[nodiscard]] inline Result<ShaderProgram, String> BuildIrradianceConvolutionShaderProgram(sdl3::GpuDevice &device) {
	String vertexSrc = CUBE_CAPTURE_VERTEX_SHADER;
	String fragmentSrc = R"(#version 450
layout(location = 0) in vec3 inDir;
layout(location = 0) out vec4 outFragColor;
layout(set = 2, binding = 0) uniform samplerCube uEnvironment;
const float PI = 3.14159265359;
void main() {
	vec3 N = normalize(inDir);
	vec3 up = abs(N.y) < 0.999 ? vec3(0.0, 1.0, 0.0) : vec3(1.0, 0.0, 0.0);
	vec3 right = normalize(cross(up, N));
	up = normalize(cross(N, right));

	vec3 irradiance = vec3(0.0);
	float sampleDelta = 0.05;
	float samples = 0.0;
	for (float phi = 0.0; phi < 2.0 * PI; phi += sampleDelta) {
		for (float theta = 0.0; theta < 0.5 * PI; theta += sampleDelta) {
			vec3 tangentSample = vec3(sin(theta) * cos(phi), sin(theta) * sin(phi), cos(theta));
			vec3 sampleVec = tangentSample.x * right + tangentSample.y * up + tangentSample.z * N;
			irradiance += texture(uEnvironment, sampleVec).rgb * cos(theta) * sin(theta);
			samples += 1.0;
		}
	}
	irradiance = PI * irradiance / samples;
	outFragColor = vec4(irradiance, 1.0);
}
)";
	auto vertexShader = CompileShader(device, StringView(vertexSrc.CStr(), vertexSrc.GetSize()),
									  sdl3::gpu_shader_stage::VERTEX, StringView("IrradianceConvolution.vert"), 0, 1);
	if (!vertexShader)
		return Err(std::move(vertexShader.Error()));
	auto fragmentShader = CompileShader(device, StringView(fragmentSrc.CStr(), fragmentSrc.GetSize()),
										sdl3::gpu_shader_stage::FRAGMENT, StringView("IrradianceConvolution.frag"), 1,
										0);
	if (!fragmentShader)
		return Err(std::move(fragmentShader.Error()));
	return Ok(ShaderProgram::FromShaders(std::move(vertexShader.Value()), std::move(fragmentShader.Value())));
}

// ── Préfiltrage spéculaire (M13) — convolution GGX importance-sampled de la
// cubemap d'environnement, une passe par niveau de mip (roughness croissante,
// voir PREFILTER_MIP_LEVELS/PrefilterMipSize ci-dessous) ; RadicalInverse_VdC/
// Hammersley/ImportanceSampleGGX sont l'approximation split-sum standard
// (Karis/Epic, voir learnopengl.com/PBR/IBL/Specular-IBL) — la roughness du
// niveau varie par uniform fragment (set=3 binding=0), pas par #define : un
// seul ShaderProgram/pipeline sert aux 5 niveaux x 6 faces.
inline constexpr uint32_t PREFILTER_MIP_LEVELS = 5;

[[nodiscard]] inline Result<ShaderProgram, String> BuildPrefilterShaderProgram(sdl3::GpuDevice &device) {
	String vertexSrc = CUBE_CAPTURE_VERTEX_SHADER;
	String fragmentSrc = R"(#version 450
layout(location = 0) in vec3 inDir;
layout(location = 0) out vec4 outFragColor;
layout(set = 2, binding = 0) uniform samplerCube uEnvironment;
layout(set = 3, binding = 0) uniform PrefilterUBO { float uRoughness; };
const float PI = 3.14159265359;

float RadicalInverseVdC(uint bits) {
	bits = (bits << 16u) | (bits >> 16u);
	bits = ((bits & 0x55555555u) << 1u) | ((bits & 0xAAAAAAAAu) >> 1u);
	bits = ((bits & 0x33333333u) << 2u) | ((bits & 0xCCCCCCCCu) >> 2u);
	bits = ((bits & 0x0F0F0F0Fu) << 4u) | ((bits & 0xF0F0F0F0u) >> 4u);
	bits = ((bits & 0x00FF00FFu) << 8u) | ((bits & 0xFF00FF00u) >> 8u);
	return float(bits) * 2.3283064365386963e-10;
}
vec2 Hammersley(uint i, uint n) {
	return vec2(float(i) / float(n), RadicalInverseVdC(i));
}
vec3 ImportanceSampleGGX(vec2 xi, vec3 N, float roughness) {
	float a = roughness * roughness;
	float phi = 2.0 * PI * xi.x;
	float cosTheta = sqrt((1.0 - xi.y) / (1.0 + (a * a - 1.0) * xi.y));
	float sinTheta = sqrt(1.0 - cosTheta * cosTheta);
	vec3 H = vec3(cos(phi) * sinTheta, sin(phi) * sinTheta, cosTheta);
	vec3 up = abs(N.z) < 0.999 ? vec3(0.0, 0.0, 1.0) : vec3(1.0, 0.0, 0.0);
	vec3 tangent = normalize(cross(up, N));
	vec3 bitangent = cross(N, tangent);
	return normalize(tangent * H.x + bitangent * H.y + N * H.z);
}

void main() {
	vec3 N = normalize(inDir);
	vec3 R = N;
	vec3 V = R;
	const uint SAMPLE_COUNT = 512u;
	float totalWeight = 0.0;
	vec3 prefiltered = vec3(0.0);
	for (uint i = 0u; i < SAMPLE_COUNT; ++i) {
		vec2 xi = Hammersley(i, SAMPLE_COUNT);
		vec3 H = ImportanceSampleGGX(xi, N, uRoughness);
		vec3 L = normalize(2.0 * dot(V, H) * H - V);
		float NdotL = max(dot(N, L), 0.0);
		if (NdotL > 0.0) {
			prefiltered += texture(uEnvironment, L).rgb * NdotL;
			totalWeight += NdotL;
		}
	}
	prefiltered = totalWeight > 0.0 ? prefiltered / totalWeight : prefiltered;
	outFragColor = vec4(prefiltered, 1.0);
}
)";
	auto vertexShader = CompileShader(device, StringView(vertexSrc.CStr(), vertexSrc.GetSize()),
									  sdl3::gpu_shader_stage::VERTEX, StringView("Prefilter.vert"), 0, 1);
	if (!vertexShader)
		return Err(std::move(vertexShader.Error()));
	auto fragmentShader = CompileShader(device, StringView(fragmentSrc.CStr(), fragmentSrc.GetSize()),
										sdl3::gpu_shader_stage::FRAGMENT, StringView("Prefilter.frag"), 1, 1);
	if (!fragmentShader)
		return Err(std::move(fragmentShader.Error()));
	return Ok(ShaderProgram::FromShaders(std::move(vertexShader.Value()), std::move(fragmentShader.Value())));
}

// ── Skybox (M11) — dessine le cube unité de CubeVertices() en échantillonnant
// directement une cubemap d'environnement déjà prête (samplerCube, pas de
// conversion ici) ; le sommet du cube fait à nouveau office de direction.
[[nodiscard]] inline Result<ShaderProgram, String> BuildSkyboxShaderProgram(sdl3::GpuDevice &device) {
	String vertexSrc = CUBE_CAPTURE_VERTEX_SHADER;
	String fragmentSrc = R"(#version 450
layout(location = 0) in vec3 inDir;
layout(location = 0) out vec4 outFragColor;
layout(set = 2, binding = 0) uniform samplerCube uEnvironment;
void main() {
	outFragColor = texture(uEnvironment, normalize(inDir));
}
)";
	auto vertexShader = CompileShader(device, StringView(vertexSrc.CStr(), vertexSrc.GetSize()),
									  sdl3::gpu_shader_stage::VERTEX, StringView("Skybox.vert"), 0, 1);
	if (!vertexShader)
		return Err(std::move(vertexShader.Error()));
	auto fragmentShader = CompileShader(device, StringView(fragmentSrc.CStr(), fragmentSrc.GetSize()),
										sdl3::gpu_shader_stage::FRAGMENT, StringView("Skybox.frag"), 1, 0);
	if (!fragmentShader)
		return Err(std::move(fragmentShader.Error()));
	return Ok(ShaderProgram::FromShaders(std::move(vertexShader.Value()), std::move(fragmentShader.Value())));
}

/// Pipeline skybox : dessiné en premier dans la passe couleur principale
/// (voir Canvas::End()), test/écriture de profondeur désactivés — la
/// géométrie normale dessinée après le recouvre naturellement par ordre de
/// dessin, aucune astuce de profondeur nécessaire.
[[nodiscard]] inline Result<sdl3::GpuGraphicsPipeline, StringView>
CreateSkyboxPipeline(sdl3::GpuDevice &device, ShaderProgram &program, sdl3::GpuTextureFormat colorFormat,
					 sdl3::GpuTextureFormat depthFormat) {
	sdl3::GpuVertexBufferDescription vertexBufferDesc{0, uint32_t(sizeof(Vertex3D)),
													  sdl3::gpu_vertex_input_rate::VERTEX, 0};
	sdl3::GpuVertexAttribute vertexAttributes[4] = {
		{0, 0, sdl3::gpu_vertex_element_format::FLOAT3, uint32_t(offsetof(Vertex3D, position))},
		{1, 0, sdl3::gpu_vertex_element_format::FLOAT3, uint32_t(offsetof(Vertex3D, normal))},
		{2, 0, sdl3::gpu_vertex_element_format::FLOAT2, uint32_t(offsetof(Vertex3D, uv))},
		{3, 0, sdl3::gpu_vertex_element_format::UBYTE4_NORM, uint32_t(offsetof(Vertex3D, color))},
	};
	sdl3::GpuColorTargetDescription colorTargetDesc{};
	colorTargetDesc.format = colorFormat;

	sdl3::GpuGraphicsPipelineCreateInfo info{};
	info.vertex_shader = program.Vertex().Get();
	info.fragment_shader = program.Fragment().Get();
	info.vertex_input_state.vertex_buffer_descriptions = &vertexBufferDesc;
	info.vertex_input_state.num_vertex_buffers = 1;
	info.vertex_input_state.vertex_attributes = vertexAttributes;
	info.vertex_input_state.num_vertex_attributes = 4;
	info.primitive_type = sdl3::gpu_primitive_type::TRIANGLE_LIST;
	info.rasterizer_state.fill_mode = sdl3::gpu_fill_mode::FILL;
	info.rasterizer_state.cull_mode = sdl3::gpu_cull_mode::NONE;
	info.rasterizer_state.front_face = sdl3::gpu_front_face::CLOCKWISE;
	info.multisample_state.sample_count = sdl3::gpu_sample_count::SAMPLE1;
	info.depth_stencil_state.enable_depth_test = false;
	info.depth_stencil_state.enable_depth_write = false;
	info.target_info.color_target_descriptions = &colorTargetDesc;
	info.target_info.num_color_targets = 1;
	info.target_info.has_depth_stencil_target = true;
	info.target_info.depth_stencil_format = depthFormat;

	return device.CreateGraphicsPipeline(info);
}

} // namespace render3d
