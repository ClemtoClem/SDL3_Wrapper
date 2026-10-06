// Définitions de render3d/shader_program.hpp
#include "render3d/shader_program.hpp"

namespace render3d {

// ── ShaderProgram ────────────────────────────────────────────────────────────

Result<ShaderProgram, StringView> ShaderProgram::Load(sdl3::GpuDevice &device, const String &basePath,
		const StageInfo &vertexInfo,
		const StageInfo &fragmentInfo) {
	sdl3::GpuShaderFormat available = device.ShaderFormats();

	const char *ext = nullptr;
	const char *entrypoint = nullptr;
	sdl3::GpuShaderFormat format = sdl3::gpu_shader_format::INVALID;
	if (available & sdl3::gpu_shader_format::SPIR_V) {
		ext = ".spv";
		entrypoint = "main";
		format = sdl3::gpu_shader_format::SPIR_V;
	} else if (available & sdl3::gpu_shader_format::MSL) {
		ext = ".msl";
		entrypoint = "main0";
		format = sdl3::gpu_shader_format::MSL;
	} else {
		return Err(StringView("ShaderProgram::Load: no supported shader format on this GPU device"));
	}

	auto vertexBytes = sdl3::ReadFile(basePath + ".vert" + ext);
	if (!vertexBytes)
		return Err(vertexBytes.Error());
	auto fragmentBytes = sdl3::ReadFile(basePath + ".frag" + ext);
	if (!fragmentBytes)
		return Err(fragmentBytes.Error());

	sdl3::GpuShaderCreateInfo vertexCreateInfo{};
	vertexCreateInfo.code_size = vertexBytes.Value().size();
	vertexCreateInfo.code = vertexBytes.Value().data();
	vertexCreateInfo.entrypoint = entrypoint;
	vertexCreateInfo.format = format;
	vertexCreateInfo.stage = sdl3::gpu_shader_stage::VERTEX;
	vertexCreateInfo.num_samplers = vertexInfo.numSamplers;
	vertexCreateInfo.num_uniform_buffers = vertexInfo.numUniformBuffers;

	auto vertexShader = device.CreateShader(vertexCreateInfo);
	if (!vertexShader)
		return Err(vertexShader.Error());

	sdl3::GpuShaderCreateInfo fragmentCreateInfo{};
	fragmentCreateInfo.code_size = fragmentBytes.Value().size();
	fragmentCreateInfo.code = fragmentBytes.Value().data();
	fragmentCreateInfo.entrypoint = entrypoint;
	fragmentCreateInfo.format = format;
	fragmentCreateInfo.stage = sdl3::gpu_shader_stage::FRAGMENT;
	fragmentCreateInfo.num_samplers = fragmentInfo.numSamplers;
	fragmentCreateInfo.num_uniform_buffers = fragmentInfo.numUniformBuffers;

	auto fragmentShader = device.CreateShader(fragmentCreateInfo);
	if (!fragmentShader)
		return Err(fragmentShader.Error());

	ShaderProgram program;
	program.m_vertex = std::move(vertexShader.Value());
	program.m_fragment = std::move(fragmentShader.Value());
	return Ok(std::move(program));
}

ShaderProgram ShaderProgram::FromShaders(sdl3::GpuShader vertex, sdl3::GpuShader fragment) noexcept {
	ShaderProgram program;
	program.m_vertex = std::move(vertex);
	program.m_fragment = std::move(fragment);
	return program;
}

} // namespace render3d
