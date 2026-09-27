#pragma once
#include <shaderc/shaderc.hpp>
#include <spirv_cross/spirv_msl.hpp>
#include <vector>

#include "../core/core.hpp"
#include "../sdl3/gpu.hpp"

namespace render3d {

// Compilateur GLSL embarqué : GLSL (texte) -> SPIR-V (shaderc) -> MSL (spirv-cross),
// utilisé par ShaderBuilder pour générer des shaders à l'exécution plutôt que
// de dépendre uniquement des binaires précompilés d'assets/shaders/bin/. Les
// erreurs contiennent du texte généré dynamiquement (message de shaderc/
// spirv-cross) : String (propriétaire), jamais StringView, pour éviter toute
// référence pendante une fois le compilateur/résultat local détruit.

/// Compile un source GLSL (`#version 450`, point d'entrée `main` — imposé par
/// GLSL) vers un module SPIR-V.
[[nodiscard]] Result<std::vector<uint32_t>, String>
CompileGlslToSpirv(StringView source, sdl3::GpuShaderStage stage, StringView debugName);

/// Cross-compile un module SPIR-V vers MSL (point d'entrée "main0", imposé par
/// spirv-cross pour les fonctions avec arguments — même convention que
/// assets/shaders/compiler.sh en pré-compilation).
[[nodiscard]] Result<String, String> CrossCompileSpirvToMsl(std::span<const uint32_t> spirv);

/// Compile un source GLSL et construit directement un sdl3::GpuShader prêt à
/// l'emploi, en choisissant SPIR-V ou MSL selon ce que le device annonce
/// supporter (même logique de sélection que ShaderProgram::Load pour les
/// shaders précompilés).
[[nodiscard]] Result<sdl3::GpuShader, String>
CompileShader(sdl3::GpuDevice &device, StringView glslSource, sdl3::GpuShaderStage stage, StringView debugName,
			  uint32_t numSamplers, uint32_t numUniformBuffers, uint32_t numStorageBuffers = 0);

} // namespace render3d
