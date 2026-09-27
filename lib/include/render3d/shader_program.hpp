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
																 const StageInfo &fragmentInfo);

	/// Construit un ShaderProgram à partir de shaders déjà créés — utilisé par
	/// ShaderBuilder, qui compile son propre GLSL au lieu de charger des
	/// fichiers précompilés (voir Load() ci-dessus).
	[[nodiscard]] static ShaderProgram FromShaders(sdl3::GpuShader vertex, sdl3::GpuShader fragment) noexcept;

	[[nodiscard]] sdl3::GpuShader &Vertex() noexcept { return m_vertex; }
	[[nodiscard]] sdl3::GpuShader &Fragment() noexcept { return m_fragment; }
};

} // namespace render3d
