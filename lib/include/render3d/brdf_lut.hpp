#pragma once
#include <cstring>

#include "../core/core.hpp"
#include "../sdl3/gpu.hpp"
#include "shader_compiler.hpp"
#include "shader_program.hpp"
#include "vertex.hpp"

namespace render3d {

// BRDF LUT (M13) — approximation split-sum (Karis/Epic) précalculée une fois
// en 2D : axes (NdotV, roughness) -> sortie (scale, bias) appliqués au F0 du
// matériau à l'usage (voir le plan). Rendu plein cadre (un quad NDC direct,
// pas de matrice — contrairement aux passes de capture de cube.hpp) sur une
// texture RG (2 canaux suffisent, pas besoin d'un format RGBA complet).

/// Format 2 canaux flottant — R16G16_FLOAT préféré (universellement supporté
/// en cible de rendu), repli R32G32_FLOAT, même stratégie de sondage que
/// cubemap.hpp::ResolveHdrColorFormat.
[[nodiscard]] sdl3::GpuTextureFormat ResolveBrdfLutFormat(sdl3::GpuDevice &device);

[[nodiscard]] Result<sdl3::GpuTexture, StringView> CreateBrdfLutTexture(sdl3::GpuDevice &device, uint32_t size,
																			   sdl3::GpuTextureFormat format);

/// Quad NDC plein cadre (position.xy déjà en clip space, uv en [0,1]) — pas
/// de matrice de projection nécessaire, réutilise Vertex3D (normal/color
/// ignorés) pour garder le même vertex_input_state que le reste du module.
[[nodiscard]] std::vector<Vertex3D> BrdfLutQuadVertices();

/// GeometrySmith spécifique à l'intégration IBL split-sum : k=roughness²/2,
/// différent du k=(roughness+1)²/8 utilisé en éclairage direct (voir
/// shader_chunks::GeometrySchlickGGX) — deux formules Karis distinctes pour
/// deux usages distincts, ne pas les confondre/fusionner.
[[nodiscard]] Result<ShaderProgram, String> BuildBrdfLutShaderProgram(sdl3::GpuDevice &device);

[[nodiscard]] Result<sdl3::GpuGraphicsPipeline, StringView>
CreateBrdfLutPipeline(sdl3::GpuDevice &device, ShaderProgram &program, sdl3::GpuTextureFormat colorFormat);

} // namespace render3d
