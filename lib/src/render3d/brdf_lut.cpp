// Définitions de render3d/brdf_lut.hpp
#include "render3d/brdf_lut.hpp"

namespace render3d {

sdl3::GpuTextureFormat ResolveBrdfLutFormat(sdl3::GpuDevice &device) {
	sdl3::GpuTextureFormat format = SDL_GPU_TEXTUREFORMAT_R16G16_FLOAT;
	if (!device.TextureSupportsFormat(format, sdl3::gpu_texture_type::TEXTURE2_D,
									  sdl3::gpu_texture_usage::COLOR_TARGET | sdl3::gpu_texture_usage::SAMPLER))
		format = SDL_GPU_TEXTUREFORMAT_R32G32_FLOAT;
	return format;
}

Result<sdl3::GpuTexture, StringView> CreateBrdfLutTexture(sdl3::GpuDevice &device, uint32_t size,
		sdl3::GpuTextureFormat format) {
	sdl3::GpuTextureCreateInfo info{};
	info.type = sdl3::gpu_texture_type::TEXTURE2_D;
	info.format = format;
	info.usage = sdl3::gpu_texture_usage::COLOR_TARGET | sdl3::gpu_texture_usage::SAMPLER;
	info.width = size;
	info.height = size;
	info.layer_count_or_depth = 1;
	info.num_levels = 1;
	info.sample_count = sdl3::gpu_sample_count::SAMPLE1;
	return device.CreateTexture(info);
}

std::vector<Vertex3D> BrdfLutQuadVertices() {
	auto v = [](float x, float y, float u, float t) {
		return Vertex3D{{x, y, 0.f}, {0, 0, 1}, {u, t}, sdl3::Color::WHITE()};
	};
	return {v(-1, -1, 0, 0), v(1, -1, 1, 0), v(1, 1, 1, 1), v(-1, -1, 0, 0), v(1, 1, 1, 1), v(-1, 1, 0, 1)};
}

Result<ShaderProgram, String> BuildBrdfLutShaderProgram(sdl3::GpuDevice &device) {
	String vertexSrc = R"(#version 450
layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec3 inNormal;
layout(location = 2) in vec2 inUV;
layout(location = 3) in vec4 inColor;
layout(location = 0) out vec2 outUV;
void main() {
	outUV = inUV;
	gl_Position = vec4(inPosition.xy, 0.0, 1.0);
}
)";
	String fragmentSrc = R"(#version 450
layout(location = 0) in vec2 inUV;
layout(location = 0) out vec4 outFragColor;
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
float GeometrySchlickGGXIBL(float NdotV, float roughness) {
	float k = (roughness * roughness) / 2.0;
	return NdotV / max(NdotV * (1.0 - k) + k, 0.0001);
}
float GeometrySmithIBL(float NdotV, float NdotL, float roughness) {
	return GeometrySchlickGGXIBL(NdotV, roughness) * GeometrySchlickGGXIBL(NdotL, roughness);
}

vec2 IntegrateBRDF(float NdotV, float roughness) {
	vec3 V = vec3(sqrt(1.0 - NdotV * NdotV), 0.0, NdotV);
	float A = 0.0;
	float B = 0.0;
	vec3 N = vec3(0.0, 0.0, 1.0);
	const uint SAMPLE_COUNT = 1024u;
	for (uint i = 0u; i < SAMPLE_COUNT; ++i) {
		vec2 xi = Hammersley(i, SAMPLE_COUNT);
		vec3 H = ImportanceSampleGGX(xi, N, roughness);
		vec3 L = normalize(2.0 * dot(V, H) * H - V);
		float NdotL = max(L.z, 0.0);
		float NdotH = max(H.z, 0.0);
		float VdotH = max(dot(V, H), 0.0);
		if (NdotL > 0.0) {
			float G = GeometrySmithIBL(NdotV, NdotL, roughness);
			float G_Vis = (G * VdotH) / max(NdotH * NdotV, 0.0001);
			float Fc = pow(1.0 - VdotH, 5.0);
			A += (1.0 - Fc) * G_Vis;
			B += Fc * G_Vis;
		}
	}
	return vec2(A / float(SAMPLE_COUNT), B / float(SAMPLE_COUNT));
}

void main() {
	vec2 result = IntegrateBRDF(max(inUV.x, 0.001), max(inUV.y, 0.001));
	outFragColor = vec4(result, 0.0, 1.0);
}
)";
	auto vertexShader = CompileShader(device, StringView(vertexSrc.CStr(), vertexSrc.GetSize()),
									  sdl3::gpu_shader_stage::VERTEX, StringView("BrdfLut.vert"), 0, 0);
	if (!vertexShader)
		return Err(std::move(vertexShader.Error()));
	auto fragmentShader = CompileShader(device, StringView(fragmentSrc.CStr(), fragmentSrc.GetSize()),
										sdl3::gpu_shader_stage::FRAGMENT, StringView("BrdfLut.frag"), 0, 0);
	if (!fragmentShader)
		return Err(std::move(fragmentShader.Error()));
	return Ok(ShaderProgram::FromShaders(std::move(vertexShader.Value()), std::move(fragmentShader.Value())));
}

Result<sdl3::GpuGraphicsPipeline, StringView> CreateBrdfLutPipeline(sdl3::GpuDevice &device, ShaderProgram &program, sdl3::GpuTextureFormat colorFormat) {
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
	info.target_info.has_depth_stencil_target = false;

	return device.CreateGraphicsPipeline(info);
}

} // namespace render3d
