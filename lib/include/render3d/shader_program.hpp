#pragma once
#include "../core/core.hpp"
#include "../sdl3/gpu.hpp"
#include "../sdl3/iostream.hpp"

namespace render3d {

// Paire vertex+fragment GpuShader, chargée depuis assets/shaders/bin/. Le
// format binaire (SPIR-V vs MSL) et le nom du point d'entrée ("main" vs
// "main0", ce dernier imposé par spirv-cross côté Metal) sont choisis
// automatiquement d'après ce que GpuDevice::ShaderFormats() annonce.
class ShaderProgram {
	sdl3::GpuShader m_vertex;
	sdl3::GpuShader m_fragment;

public:
	ShaderProgram() = default;

	struct StageInfo {
		uint32_t numSamplers = 0;
		uint32_t numUniformBuffers = 0;
	};

	/// `basePath` est le chemin sans extension de stage/format, p.ex.
	/// "assets/shaders/bin/mesh_phong" — les fichiers réels sont
	/// "<basePath>.vert.{spv,msl}" et "<basePath>.frag.{spv,msl}".
	[[nodiscard]] static Result<ShaderProgram, StringView> Load(sdl3::GpuDevice &device, const String &basePath,
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

	/// Construit un ShaderProgram à partir de shaders déjà créés — utilisé par
	/// ShaderBuilder, qui compile son propre GLSL au lieu de charger des
	/// fichiers précompilés (voir Load() ci-dessus).
	[[nodiscard]] static ShaderProgram FromShaders(sdl3::GpuShader vertex, sdl3::GpuShader fragment) noexcept {
		ShaderProgram program;
		program.m_vertex = std::move(vertex);
		program.m_fragment = std::move(fragment);
		return program;
	}

	[[nodiscard]] sdl3::GpuShader &Vertex() noexcept { return m_vertex; }
	[[nodiscard]] sdl3::GpuShader &Fragment() noexcept { return m_fragment; }
};

} // namespace render3d
