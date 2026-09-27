// Définitions de render3d/shadow.hpp — fichier généré par splitter.py : le code
// vient tel quel de l'en-tête (seules les signatures sont réécrites).

#include "render3d/shadow.hpp"

namespace render3d {

Result<sdl3::GpuTexture, StringView> CreateShadowDepthTexture(sdl3::GpuDevice &device,
		uint32_t size,
		sdl3::GpuTextureFormat format) {
	sdl3::GpuTextureCreateInfo info{};
	info.type = sdl3::gpu_texture_type::TEXTURE2_D;
	info.format = format;
	info.usage = sdl3::gpu_texture_usage::DEPTH_STENCIL_TARGET | sdl3::gpu_texture_usage::SAMPLER;
	info.width = size;
	info.height = size;
	info.layer_count_or_depth = 1;
	info.num_levels = 1;
	info.sample_count = sdl3::gpu_sample_count::SAMPLE1;
	return device.CreateTexture(info);
}

sdl3::GpuTextureFormat ResolveShadowDepthFormat(sdl3::GpuDevice &device) {
	sdl3::GpuTextureFormat format = SDL_GPU_TEXTUREFORMAT_D32_FLOAT;
	if (!device.TextureSupportsFormat(format, sdl3::gpu_texture_type::TEXTURE2_D,
									  sdl3::gpu_texture_usage::DEPTH_STENCIL_TARGET)) {
		format = SDL_GPU_TEXTUREFORMAT_D24_UNORM;
		if (!device.TextureSupportsFormat(format, sdl3::gpu_texture_type::TEXTURE2_D,
										  sdl3::gpu_texture_usage::DEPTH_STENCIL_TARGET))
			format = SDL_GPU_TEXTUREFORMAT_D16_UNORM;
	}
	return format;
}

Result<sdl3::GpuSampler, StringView> CreateShadowComparisonSampler(sdl3::GpuDevice &device) {
	sdl3::GpuSamplerCreateInfo info{};
	info.min_filter = sdl3::gpu_filter::LINEAR;
	info.mag_filter = sdl3::gpu_filter::LINEAR;
	info.mipmap_mode = sdl3::gpu_sampler_mipmap_mode::NEAREST;
	info.address_mode_u = sdl3::gpu_sampler_address_mode::CLAMP_TO_EDGE;
	info.address_mode_v = sdl3::gpu_sampler_address_mode::CLAMP_TO_EDGE;
	info.address_mode_w = sdl3::gpu_sampler_address_mode::CLAMP_TO_EDGE;
	info.enable_compare = true;
	info.compare_op = sdl3::gpu_compare_op::LESS;
	return device.CreateSampler(info);
}

Result<ShaderProgram, String> BuildShadowCasterShaderProgram(sdl3::GpuDevice &device) {
	using namespace shader_chunks;

	String vertexSrc = "#version 450\n";
	vertexSrc += VERTEX_ATTRIBUTES;
	vertexSrc += PER_FRAME_UBO;
	vertexSrc += PER_OBJECT_UBO;
	vertexSrc += R"(
void main() {
	gl_Position = uViewProjection * uModel * vec4(inPosition, 1.0);
}
)";

	String fragmentSrc = "#version 450\nvoid main() {}\n";

	auto vertexShader = CompileShader(device, StringView(vertexSrc.CStr(), vertexSrc.GetSize()),
									  sdl3::gpu_shader_stage::VERTEX, StringView("ShadowCaster.vert"), 0, 2);
	if (!vertexShader)
		return Err(std::move(vertexShader.Error()));

	auto fragmentShader = CompileShader(device, StringView(fragmentSrc.CStr(), fragmentSrc.GetSize()),
										sdl3::gpu_shader_stage::FRAGMENT, StringView("ShadowCaster.frag"), 0, 0);
	if (!fragmentShader)
		return Err(std::move(fragmentShader.Error()));

	return Ok(ShaderProgram::FromShaders(std::move(vertexShader.Value()), std::move(fragmentShader.Value())));
}

Result<sdl3::GpuGraphicsPipeline, StringView> CreateShadowCasterPipeline(sdl3::GpuDevice &device, ShaderProgram &program, sdl3::GpuTextureFormat depthFormat) {
	sdl3::GpuVertexBufferDescription vertexBufferDesc{0, uint32_t(sizeof(Vertex3D)),
													  sdl3::gpu_vertex_input_rate::VERTEX, 0};
	sdl3::GpuVertexAttribute vertexAttributes[4] = {
		{0, 0, sdl3::gpu_vertex_element_format::FLOAT3, uint32_t(offsetof(Vertex3D, position))},
		{1, 0, sdl3::gpu_vertex_element_format::FLOAT3, uint32_t(offsetof(Vertex3D, normal))},
		{2, 0, sdl3::gpu_vertex_element_format::FLOAT2, uint32_t(offsetof(Vertex3D, uv))},
		{3, 0, sdl3::gpu_vertex_element_format::UBYTE4_NORM, uint32_t(offsetof(Vertex3D, color))},
	};

	sdl3::GpuGraphicsPipelineCreateInfo info{};
	info.vertex_shader = program.Vertex().Get();
	info.fragment_shader = program.Fragment().Get();
	info.vertex_input_state.vertex_buffer_descriptions = &vertexBufferDesc;
	info.vertex_input_state.num_vertex_buffers = 1;
	info.vertex_input_state.vertex_attributes = vertexAttributes;
	info.vertex_input_state.num_vertex_attributes = 4;
	info.primitive_type = sdl3::gpu_primitive_type::TRIANGLE_LIST;

	info.rasterizer_state.fill_mode = sdl3::gpu_fill_mode::FILL;
	info.rasterizer_state.cull_mode = sdl3::gpu_cull_mode::BACK;
	info.rasterizer_state.front_face = sdl3::gpu_front_face::CLOCKWISE; // même convention que Canvas, voir canvas.hpp
	info.rasterizer_state.enable_depth_clip = true;

	info.multisample_state.sample_count = sdl3::gpu_sample_count::SAMPLE1;

	info.depth_stencil_state.enable_depth_test = true;
	info.depth_stencil_state.enable_depth_write = true;
	info.depth_stencil_state.compare_op = sdl3::gpu_compare_op::LESS_OR_EQUAL;

	info.target_info.num_color_targets = 0; // depth-only : aucune cible couleur
	info.target_info.has_depth_stencil_target = true;
	info.target_info.depth_stencil_format = depthFormat;

	return device.CreateGraphicsPipeline(info);
}

Result<sdl3::GpuTexture, StringView> CreatePointShadowCubeTexture(sdl3::GpuDevice &device, uint32_t size) {
	sdl3::GpuTextureCreateInfo info{};
	info.type = sdl3::gpu_texture_type::CUBE;
	info.format = SDL_GPU_TEXTUREFORMAT_R32_FLOAT;
	info.usage = sdl3::gpu_texture_usage::COLOR_TARGET | sdl3::gpu_texture_usage::SAMPLER;
	info.width = size;
	info.height = size;
	info.layer_count_or_depth = 6; // 6 faces (une "layer" par face pour SDL_GPU_TEXTURETYPE_CUBE)
	info.num_levels = 1;
	info.sample_count = sdl3::gpu_sample_count::SAMPLE1;
	return device.CreateTexture(info);
}

Result<sdl3::GpuSampler, StringView> CreatePointShadowSampler(sdl3::GpuDevice &device) {
	sdl3::GpuSamplerCreateInfo info{};
	info.min_filter = sdl3::gpu_filter::LINEAR;
	info.mag_filter = sdl3::gpu_filter::LINEAR;
	info.mipmap_mode = sdl3::gpu_sampler_mipmap_mode::NEAREST;
	info.address_mode_u = sdl3::gpu_sampler_address_mode::CLAMP_TO_EDGE;
	info.address_mode_v = sdl3::gpu_sampler_address_mode::CLAMP_TO_EDGE;
	info.address_mode_w = sdl3::gpu_sampler_address_mode::CLAMP_TO_EDGE;
	return device.CreateSampler(info);
}

Result<ShaderProgram, String> BuildPointShadowCasterShaderProgram(sdl3::GpuDevice &device) {
	using namespace shader_chunks;

	String vertexSrc = "#version 450\n";
	vertexSrc += VERTEX_ATTRIBUTES;
	vertexSrc += PER_FRAME_UBO;
	vertexSrc += PER_OBJECT_UBO;
	vertexSrc += R"(
layout(location = 0) out vec3 outWorldPos;
void main() {
	vec4 worldPos = uModel * vec4(inPosition, 1.0);
	outWorldPos = worldPos.xyz;
	gl_Position = uViewProjection * worldPos;
}
)";

	String fragmentSrc = R"(#version 450
layout(location = 0) in vec3 inWorldPos;
layout(location = 0) out vec4 outFragColor;
layout(set = 3, binding = 0) uniform PointShadowCasterUBO {
	vec3 uLightPos;
	float uPadding0;
};
void main() {
	float dist = length(inWorldPos - uLightPos);
	outFragColor = vec4(dist, dist, dist, 1.0);
}
)";

	auto vertexShader = CompileShader(device, StringView(vertexSrc.CStr(), vertexSrc.GetSize()),
									  sdl3::gpu_shader_stage::VERTEX, StringView("PointShadowCaster.vert"), 0, 2);
	if (!vertexShader)
		return Err(std::move(vertexShader.Error()));

	auto fragmentShader =
		CompileShader(device, StringView(fragmentSrc.CStr(), fragmentSrc.GetSize()), sdl3::gpu_shader_stage::FRAGMENT,
					  StringView("PointShadowCaster.frag"), 0, 1);
	if (!fragmentShader)
		return Err(std::move(fragmentShader.Error()));

	return Ok(ShaderProgram::FromShaders(std::move(vertexShader.Value()), std::move(fragmentShader.Value())));
}

Result<sdl3::GpuGraphicsPipeline, StringView> CreatePointShadowCasterPipeline(sdl3::GpuDevice &device, ShaderProgram &program, sdl3::GpuTextureFormat depthFormat) {
	sdl3::GpuVertexBufferDescription vertexBufferDesc{0, uint32_t(sizeof(Vertex3D)),
													  sdl3::gpu_vertex_input_rate::VERTEX, 0};
	sdl3::GpuVertexAttribute vertexAttributes[4] = {
		{0, 0, sdl3::gpu_vertex_element_format::FLOAT3, uint32_t(offsetof(Vertex3D, position))},
		{1, 0, sdl3::gpu_vertex_element_format::FLOAT3, uint32_t(offsetof(Vertex3D, normal))},
		{2, 0, sdl3::gpu_vertex_element_format::FLOAT2, uint32_t(offsetof(Vertex3D, uv))},
		{3, 0, sdl3::gpu_vertex_element_format::UBYTE4_NORM, uint32_t(offsetof(Vertex3D, color))},
	};
	sdl3::GpuColorTargetDescription colorTargetDesc{};
	colorTargetDesc.format = SDL_GPU_TEXTUREFORMAT_R32_FLOAT;

	sdl3::GpuGraphicsPipelineCreateInfo info{};
	info.vertex_shader = program.Vertex().Get();
	info.fragment_shader = program.Fragment().Get();
	info.vertex_input_state.vertex_buffer_descriptions = &vertexBufferDesc;
	info.vertex_input_state.num_vertex_buffers = 1;
	info.vertex_input_state.vertex_attributes = vertexAttributes;
	info.vertex_input_state.num_vertex_attributes = 4;
	info.primitive_type = sdl3::gpu_primitive_type::TRIANGLE_LIST;

	info.rasterizer_state.fill_mode = sdl3::gpu_fill_mode::FILL;
	info.rasterizer_state.cull_mode = sdl3::gpu_cull_mode::BACK;
	info.rasterizer_state.front_face = sdl3::gpu_front_face::CLOCKWISE;
	info.rasterizer_state.enable_depth_clip = true;

	info.multisample_state.sample_count = sdl3::gpu_sample_count::SAMPLE1;

	info.depth_stencil_state.enable_depth_test = true;
	info.depth_stencil_state.enable_depth_write = true;
	info.depth_stencil_state.compare_op = sdl3::gpu_compare_op::LESS_OR_EQUAL;

	info.target_info.color_target_descriptions = &colorTargetDesc;
	info.target_info.num_color_targets = 1;
	info.target_info.has_depth_stencil_target = true;
	info.target_info.depth_stencil_format = depthFormat;

	return device.CreateGraphicsPipeline(info);
}

} // namespace render3d
