#pragma once
#include <algorithm>

#include "brdf_lut.hpp"
#include "cubemap.hpp"
#include "hdr_loader.hpp"

namespace render3d {

// Environnement IBL/skybox (M11-M13 complet) : cubemap "brute" convertie
// depuis l'équirectangulaire source, irradiance diffuse convoluée (M12),
// cubemap spéculaire préfiltrée (mips par rugosité) et BRDF LUT (M13) —
// toutes calculées une fois à LoadEnvironmentFromHdr() et réutilisées
// telles quelles ensuite (voir Canvas::SetEnvironment).
struct Environment {
	sdl3::GpuTexture cubemap;
	sdl3::GpuTexture irradianceCubemap;
	sdl3::GpuTexture prefilteredCubemap;
	sdl3::GpuTexture brdfLut;
};

namespace detail {

/// Rend les 6 faces d'une cubemap cible en dessinant le cube unité
/// (CubeVertices()) avec `pipeline`, une texture/sampler source commune à
/// toutes les faces (équirectangulaire 2D pour la conversion M11, cubemap
/// source pour la convolution M12/M13) — factorisé car identique dans les
/// deux cas, seul le ShaderProgram/pipeline en amont diffère.
[[nodiscard]] Result<bool, String>
RenderCubeFaces(sdl3::GpuDevice &device, sdl3::GpuGraphicsPipeline &pipeline, sdl3::GpuBuffer &vertexBuffer,
				uint32_t vertexCount, sdl3::GpuTextureSamplerBinding sourceSampler, sdl3::GpuTexture &target,
				uint32_t targetSize);

/// Rend les 5 niveaux de mip x 6 faces du préfiltrage spéculaire — variante
/// de RenderCubeFaces ci-dessus avec deux différences : la cible de rendu
/// change de taille/mip par niveau (`mip_level`, `GpuColorTargetInfo`, voir
/// cubemap.hpp::PREFILTER_MIP_LEVELS), et un uniform fragment (roughness) est
/// poussé à chaque passe — assez différent pour ne pas réutiliser RenderCubeFaces.
[[nodiscard]] Result<bool, String>
RenderPrefilteredMips(sdl3::GpuDevice &device, sdl3::GpuGraphicsPipeline &pipeline, sdl3::GpuBuffer &vertexBuffer,
					  uint32_t vertexCount, sdl3::GpuTextureSamplerBinding sourceSampler, sdl3::GpuTexture &target,
					  uint32_t baseSize);

} // namespace detail

/// Charge un fichier .hdr équirectangulaire (voir hdr_loader.hpp), le
/// convertit en cubemap (M11), calcule son irradiance diffuse (M12), sa
/// cubemap spéculaire préfiltrée et sa BRDF LUT (M13) — tout le travail GPU
/// se fait ici, une seule fois ; Canvas::SetEnvironment() ne fait ensuite que
/// retenir une référence vers le résultat.
[[nodiscard]] Result<Environment, String>
LoadEnvironmentFromHdr(sdl3::GpuDevice &device, const String &path, uint32_t cubemapSize = 512,
					   uint32_t irradianceSize = 32, uint32_t prefilterBaseSize = 128, uint32_t brdfLutSize = 256);

} // namespace render3d
