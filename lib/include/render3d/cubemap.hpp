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
[[nodiscard]] math::FMatrix4 CubeFaceViewProjection(int faceIndex) noexcept;

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
[[nodiscard]] std::vector<Vertex3D> CubeVertices();

/// Format couleur HDR pour les cubemaps d'environnement — R16G16B16A16_FLOAT
/// préféré (supporté universellement en cible de rendu), repli sur
/// R32G32B32A32_FLOAT (même stratégie de sondage que Canvas::Create() pour
/// la depth texture, voir shadow.hpp::ResolveShadowDepthFormat).
[[nodiscard]] sdl3::GpuTextureFormat ResolveHdrColorFormat(sdl3::GpuDevice &device);

[[nodiscard]] Result<sdl3::GpuTexture, StringView> CreateEnvironmentCubemap(sdl3::GpuDevice &device,
																				   uint32_t size,
																				   sdl3::GpuTextureFormat format,
																				   uint32_t numLevels = 1);

/// Upload l'image équirectangulaire décodée (RGB, voir hdr_loader.hpp) comme
/// texture 2D source (RGBA32F — complète le canal alpha à 1, toujours
/// supporté en lecture SAMPLER contrairement aux cibles de rendu HDR).
[[nodiscard]] Result<sdl3::GpuTexture, String> UploadEquirectTexture(sdl3::GpuDevice &device,
																			const HdrImage &image);

/// Shader de conversion équirectangulaire -> cubemap : le sommet du cube
/// unité EST la direction de capture (voir CubeVertices()), le fragment la
/// convertit en UV équirectangulaire (atan2/asin, convention standard) et
/// échantillonne la texture source.
[[nodiscard]] Result<ShaderProgram, String> BuildEquirectToCubemapShaderProgram(sdl3::GpuDevice &device);

/// Pipeline générique "capture de face de cube" — réutilisé par l'irradiance
/// (M12) et le préfiltrage spéculaire (M13) avec un autre ShaderProgram/format.
[[nodiscard]] Result<sdl3::GpuGraphicsPipeline, StringView>
CreateCubeCapturePipeline(sdl3::GpuDevice &device, ShaderProgram &program, sdl3::GpuTextureFormat colorFormat);

// ── Irradiance diffuse (M12) — convolution hémisphère cosinus-pondérée de la
// cubemap d'environnement (M11), technique standard (voir learnopengl.com/PBR/IBL) :
// pour chaque texel de sortie (direction N = sommet du cube unité), intègre
// la radiance entrante sur l'hémisphère autour de N par un double balayage
// phi/theta à pas fixe — approche explicite délibérément préférée à un calcul
// analytique (SH) ou au compute shader (voir le plan : aucun précédent de
// compute dans ce dépôt, une passe fragment supplémentaire reste cohérente
// avec le reste du module).
[[nodiscard]] Result<ShaderProgram, String> BuildIrradianceConvolutionShaderProgram(sdl3::GpuDevice &device);

// ── Préfiltrage spéculaire (M13) — convolution GGX importance-sampled de la
// cubemap d'environnement, une passe par niveau de mip (roughness croissante,
// voir PREFILTER_MIP_LEVELS/PrefilterMipSize ci-dessous) ; RadicalInverse_VdC/
// Hammersley/ImportanceSampleGGX sont l'approximation split-sum standard
// (Karis/Epic, voir learnopengl.com/PBR/IBL/Specular-IBL) — la roughness du
// niveau varie par uniform fragment (set=3 binding=0), pas par #define : un
// seul ShaderProgram/pipeline sert aux 5 niveaux x 6 faces.
inline constexpr uint32_t PREFILTER_MIP_LEVELS = 5;

[[nodiscard]] Result<ShaderProgram, String> BuildPrefilterShaderProgram(sdl3::GpuDevice &device);

// ── Skybox (M11) — dessine le cube unité de CubeVertices() en échantillonnant
// directement une cubemap d'environnement déjà prête (samplerCube, pas de
// conversion ici) ; le sommet du cube fait à nouveau office de direction.
[[nodiscard]] Result<ShaderProgram, String> BuildSkyboxShaderProgram(sdl3::GpuDevice &device);

/// Pipeline skybox : dessiné en premier dans la passe couleur principale
/// (voir Canvas::End()), test/écriture de profondeur désactivés — la
/// géométrie normale dessinée après le recouvre naturellement par ordre de
/// dessin, aucune astuce de profondeur nécessaire.
[[nodiscard]] Result<sdl3::GpuGraphicsPipeline, StringView>
CreateSkyboxPipeline(sdl3::GpuDevice &device, ShaderProgram &program, sdl3::GpuTextureFormat colorFormat,
					 sdl3::GpuTextureFormat depthFormat);

} // namespace render3d
