// Définitions de render3d/shader_compiler.hpp
#include "render3d/shader_compiler.hpp"

namespace render3d {

Result<std::vector<uint32_t>, String> CompileGlslToSpirv(StringView source, sdl3::GpuShaderStage stage, StringView debugName) {
	shaderc::Compiler compiler;
	shaderc::CompileOptions options;
	options.SetOptimizationLevel(shaderc_optimization_level_performance);
	options.SetTargetEnvironment(shaderc_target_env_vulkan, shaderc_env_version_vulkan_1_1);

	shaderc_shader_kind kind =
		stage == sdl3::gpu_shader_stage::VERTEX ? shaderc_glsl_vertex_shader : shaderc_glsl_fragment_shader;

	String name(debugName);
	auto result = compiler.CompileGlslToSpv(source.GetData(), source.GetSize(), kind, name.CStr(), options);
	if (result.GetCompilationStatus() != shaderc_compilation_status_success)
		return Err(String(result.GetErrorMessage()));

	return Ok(std::vector<uint32_t>(result.cbegin(), result.cend()));
}

Result<String, String> CrossCompileSpirvToMsl(std::span<const uint32_t> spirv) {
	try {
		spirv_cross::CompilerMSL msl(spirv.data(), spirv.size());
		spirv_cross::CompilerMSL::Options options;
		options.set_msl_version(2, 3, 0);
		msl.set_msl_options(options);
		return Ok(String(msl.compile().c_str()));
	} catch (const std::exception &e) {
		return Err(String(e.what()));
	}
}

Result<sdl3::GpuShader, String> CompileShader(sdl3::GpuDevice &device, StringView glslSource, sdl3::GpuShaderStage stage, StringView debugName,
		uint32_t numSamplers, uint32_t numUniformBuffers, uint32_t numStorageBuffers) {
	auto spirv = CompileGlslToSpirv(glslSource, stage, debugName);
	if (!spirv)
		return Err(std::move(spirv.Error()));

	sdl3::GpuShaderFormat available = device.ShaderFormats();
	sdl3::GpuShaderCreateInfo info{};
	info.stage = stage;
	info.num_samplers = numSamplers;
	info.num_uniform_buffers = numUniformBuffers;
	info.num_storage_buffers = numStorageBuffers;

	// Conservé en vie jusqu'à l'appel CreateShader : code_size/code pointent dedans.
	std::vector<uint32_t> spirvBytes = std::move(spirv.Value());
	String mslSource;

	if (available & sdl3::gpu_shader_format::SPIR_V) {
		info.format = sdl3::gpu_shader_format::SPIR_V;
		info.entrypoint = "main";
		info.code_size = spirvBytes.size() * sizeof(uint32_t);
		info.code = reinterpret_cast<const uint8_t *>(spirvBytes.data());
	} else if (available & sdl3::gpu_shader_format::MSL) {
		auto msl = CrossCompileSpirvToMsl(spirvBytes);
		if (!msl)
			return Err(std::move(msl.Error()));
		mslSource = std::move(msl.Value());
		info.format = sdl3::gpu_shader_format::MSL;
		info.entrypoint = "main0";
		info.code_size = mslSource.GetSize();
		info.code = reinterpret_cast<const uint8_t *>(mslSource.CStr());
	} else {
		return Err(String("CompileShader: no supported shader format on this GPU device"));
	}

	auto shader = device.CreateShader(info);
	if (!shader)
		return Err(String(shader.Error()));
	return Ok(std::move(shader.Value()));
}

} // namespace render3d
