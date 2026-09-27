#pragma once
#include "../core/core.hpp"
#include "../sdl3/gpu.hpp"
#include "shader_chunks.hpp"
#include "shader_compiler.hpp"
#include "shader_program.hpp"
#include "vertex.hpp"

namespace render3d {

// Résolution (carrée) des shadow maps directionnelle/spot — partagée entre
// Canvas (création des depth textures) et ShaderBuilder (#define
// SHADOW_MAP_SIZE injecté dans le GLSL généré, voir shader_chunks::SHADOW_REAL_FUNC).
inline constexpr uint32_t SHADOW_MAP_RESOLUTION = 1024;

// Infrastructure de shadow mapping (voir le plan, M7) : une depth texture
// 2D (directionnelle/spot) ou cubemap (point, M9), un sampler de comparaison
// matériel (PCF via `texture(sampler2DShadow, ...)`), et un pipeline
// "depth-only" minimal (position seule, aucune sortie fragment) qui rend les
// draw calls déjà en file dans Canvas::m_drawList depuis le point de vue de
// la lumière — voir DirectionalLight/SpotLight/PointLight::ShadowViewProjection
// (light.hpp) pour les matrices vue-projection correspondantes.

/// Crée une depth texture 2D carrée utilisable à la fois comme cible de
/// rendu (DEPTH_STENCIL_TARGET) et comme source pour le sampler de
/// comparaison du shader (SAMPLER) — même repli de format que
/// Canvas::Create() (D32_FLOAT -> D24_UNORM -> D16_UNORM selon le backend).
[[nodiscard]] Result<sdl3::GpuTexture, StringView> CreateShadowDepthTexture(sdl3::GpuDevice &device,
																				   uint32_t size,
																				   sdl3::GpuTextureFormat format);

/// Résout le format de depth texture supporté par le backend, dans le même
/// ordre de repli que Canvas::Create() — dupliqué plutôt que partagé car
/// Canvas ne s'initialise pas forcément avant que ce module en ait besoin.
[[nodiscard]] sdl3::GpuTextureFormat ResolveShadowDepthFormat(sdl3::GpuDevice &device);

/// Sampler de comparaison matériel (PCF) partagé par toutes les shadow maps
/// 2D (directionnelle + spot) — `enable_compare`/`compare_op` sont câblés par
/// SDL_GPU mais n'étaient utilisés par aucun site d'appel avant cette phase.
[[nodiscard]] Result<sdl3::GpuSampler, StringView> CreateShadowComparisonSampler(sdl3::GpuDevice &device);

/// Shader "depth-only" : vertex identique au vertex Lit/Unlit habituel
/// (position + PerFrame/PerObject UBO, mêmes bindings) mais sans varyings ;
/// fragment sans aucune sortie (le render pass n'a pas de cible couleur).
[[nodiscard]] Result<ShaderProgram, String> BuildShadowCasterShaderProgram(sdl3::GpuDevice &device);

/// Pipeline "depth-only" (aucune cible couleur) pour rendre les projeteurs
/// d'ombre depuis le point de vue d'une lumière — réutilise le même layout
/// de sommet (Vertex3D) que Canvas::GetOrCreatePipeline() pour dessiner les
/// Mesh existants sans conversion.
[[nodiscard]] Result<sdl3::GpuGraphicsPipeline, StringView>
CreateShadowCasterPipeline(sdl3::GpuDevice &device, ShaderProgram &program, sdl3::GpuTextureFormat depthFormat);

// ── Point light (cubemap de distance, M9) ───────────────────────────────────
// SDL_GPU expose bien un type de texture CUBE, mais échantillonner une DEPTH
// cubemap n'a aucun précédent testé dans ce dépôt (ni ailleurs sur le web
// pour ce wrapper) ; on retient donc la technique three.js — une cubemap
// COULEUR (R32_FLOAT) stockant la distance linéaire lumière->fragment par
// texel, comparée manuellement à la distance réelle du fragment courant, pas
// de sampler de comparaison matériel ici (comparaison faite dans le GLSL).

/// Résolution (carrée, par face) de la cubemap de distance du point light —
/// plus petite que SHADOW_MAP_RESOLUTION : 6 faces à rendre par frame contre
/// 1 pour directionnelle/spot, garder le coût raisonnable.
inline constexpr uint32_t POINT_SHADOW_MAP_RESOLUTION = 512;

[[nodiscard]] Result<sdl3::GpuTexture, StringView> CreatePointShadowCubeTexture(sdl3::GpuDevice &device,
																					   uint32_t size);

/// Sampler standard (pas de comparaison matérielle, voir note ci-dessus) pour
/// échantillonner la cubemap de distance.
[[nodiscard]] Result<sdl3::GpuSampler, StringView> CreatePointShadowSampler(sdl3::GpuDevice &device);

/// Shader "distance-only" : le vertex ne diffère du dépth-only ci-dessus que
/// par la sortie de la position monde (nécessaire au fragment pour calculer
/// la distance à la lumière) ; le fragment écrit `length(worldPos - lightPos)`
/// dans le canal .r de la cible R32_FLOAT via un petit UBO dédié à cette
/// passe (set=3 binding=0 — pipeline indépendant du pipeline principal de
/// Canvas, son propre budget de 4 UBO fragment repart de zéro).
[[nodiscard]] Result<ShaderProgram, String> BuildPointShadowCasterShaderProgram(sdl3::GpuDevice &device);

/// Pipeline "distance-only" — une cible couleur R32_FLOAT (une face de la
/// cubemap) + une depth target 2D classique pour un test de profondeur
/// correct entre casters (voir CreatePointShadowFaceDepthTexture).
[[nodiscard]] Result<sdl3::GpuGraphicsPipeline, StringView>
CreatePointShadowCasterPipeline(sdl3::GpuDevice &device, ShaderProgram &program, sdl3::GpuTextureFormat depthFormat);

} // namespace render3d
