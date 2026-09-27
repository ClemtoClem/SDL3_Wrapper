#pragma once
/**
 * ui::ShaderEffectSystem — post-traitement GPU du sous-arbre d'un widget
 * (M23, Phase 4 du plan d'expansion moteur
 * `/home/clement/.claude/plans/optimized-humming-toast.md`). Précédent :
 * NodeGraphSystem/Viewport3DSystem — un widget "complexe" reçoit son propre
 * système dédié, appelé explicitement par ui::Ui::Render() (ou l'appli, en
 * usage sans façade), plutôt que noyé dans LayoutSystem/InputSystem
 * générique.
 *
 * Direction-INVERSÉE de Viewport3D (M22, qui composite du contenu 3D DANS
 * l'UI 2D) : ici on part de contenu 2D déjà rendu (le sous-arbre normal du
 * widget), on le fait repasser par un pipeline GPU dédié (glow/tint-shift),
 * puis on recomposite le résultat À LA PLACE du rendu normal — direction-
 * inversée de UploadSurfaceToGpuTexture (M21, jamais consommé avant ce
 * jalon, cf. offscreen.hpp).
 *
 * Correction de plan (cf. rapport de tâche M23) : le texte original du plan
 * mentionnait un "render3d::ShaderBuilder-composed fragment shader" —
 * inexact. ShaderBuilder (shader_builder.hpp) est un DSL de shading de
 * MAILLAGE (.Lit/.Metallic/.Roughness/.PointLights/.Environment...), aucune
 * notion de vignette/glow/blur/tint. Ce fichier écrit à la main de petites
 * sources GLSL par preset, compilées via l'infra GÉNÉRIQUE déjà existante
 * (render3d::CompileShader, shader_compiler.hpp — GLSL->SPIR-V->MSL via
 * shaderc+spirv-cross, déjà éprouvée Phase 2-3), avec son PROPRE petit cache
 * de pipeline (2 presets = 2 pipelines, cf. m_tintPipeline/m_glowPipeline
 * ci-dessous) — JAMAIS Canvas::m_pipelineCache/PipelineKey (mesh-shading
 * uniquement, hors scope ici par construction).
 *
 * Pipeline complet, par UiShaderEffect, par frame (cf. UpdateOne ci-dessous
 * pour le détail ligne à ligne) :
 *
 *   (a) DrawTree(world, ren, e) du sous-arbre ENTIER (widget + descendants)
 *       dans une texture cible sdl3::Renderer dédiée (SDL_TEXTUREACCESS_
 *       TARGET) — PAS le rendu normal de l'appli, un rendu SÉPARÉ,
 *       spécifiquement pour capturer les pixels sources.
 *   (b) ReadPixels (nouveau : Renderer::ReadPixels, render.hpp) + Convert
 *       vers un format connu, upload CPU->GPU via UploadSurfaceToGpuTexture
 *       (M21, offscreen.hpp).
 *   (c) Un quad plein-écran (triangle unique, sans vertex buffer — cf.
 *       FULLSCREEN_TRIANGLE_VERT) à travers le pipeline dédié à ce preset,
 *       rendu dans une DEUXIÈME texture GPU (couleur seule, pas de depth :
 *       ce n'est pas un rendu 3D).
 *   (d) Download GPU->CPU du résultat, upload dans une sdl3::Texture
 *       affichée — composée par RenderSystem::DrawWidget via DrawImageFit,
 *       exactement comme UiViewport3D/UiImage.
 *
 * Coût réel à retenir (cf. rapport de tâche) : 2 aller-retours CPU<->GPU
 * complets par widget-à-effet PAR FRAME (upload en (b), download+re-upload
 * en (d)), plus une texture GPU source entièrement RECRÉÉE chaque frame
 * (UploadSurfaceToGpuTexture ne fait jamais d'update-in-place) — nettement
 * plus cher qu'un Viewport3D (M22, un seul aller-retour GPU->CPU->
 * sdl3::Texture). Acceptable pour le scope de ce jalon (quelques widgets à
 * effet, pas un usage systématique) ; une optimisation future pourrait
 * partager le pipeline glow/tint avec un compositing GPU natif si ui::
 * migre un jour vers un backend de rendu 2D non-SDL_Renderer.
 *
 * Subtilité de repère de coordonnées (piège réel rencontré pendant ce
 * jalon, cf. rapport de tâche) : DrawTree dessine avec les coordonnées
 * ABSOLUES de fenêtre déjà résolues par LayoutSystem (UiComputed.screen/
 * clip), PAS relatives à l'entité capturée — un widget positionné à
 * (300,200) dessinerait donc à (300,200) dans notre petite texture cible
 * w×h, hors de ses bornes, si rien ne compensait. Compensé via
 * Renderer::SetViewport (SDL : "the top left of the area will become
 * coordinate (0,0) for future drawing commands") avec un rect décalé de
 * (-screen.x, -screen.y) juste avant DrawTree, remis à plat (ClearViewport)
 * avant ReadPixels (qui lit sinon "le viewport courant", pas forcément
 * [0,largeur)x[0,hauteur) de la cible réelle).
 *
 * Dégradation (best-effort, même convention que EnsureShadowResources/
 * Viewport3DSystem::Update) : à tout échec GPU au-delà de la capture (a),
 * ce widget garde simplement la DERNIÈRE texture d'effet valide connue (cf.
 * RestoreGuard ci-dessous) — si aucune n'a jamais existé (premier frame, ou
 * échec permanent), RenderSystem::DrawTree retombe naturellement sur le
 * rendu NORMAL du sous-arbre (aucune entrée dans RenderSystem::
 * effectTextures => la branche UiShaderEffect de DrawWidget/DrawTree ne
 * s'active simplement pas) — dégradation "gratuite", sans code spécial.
 *
 * Piège anti-boucle-de-rétroaction (cf. RestoreGuard) : la capture (a)
 * appelle DrawTree SUR L'ENTITÉ ELLE-MÊME (elle porte UiShaderEffect) — sans
 * précaution, DrawWidget(e) blitterait la texture d'effet du FRAME
 * PRÉCÉDENT au lieu du contenu normal du widget, un effet composant sa
 * propre sortie d'avant plutôt que le contenu réel. RestoreGuard retire
 * temporairement l'entrée de RenderSystem::effectTextures avant la capture,
 * la réinsère si ce widget échoue plus loin ce frame (fallback "dernière
 * texture connue"), ou la laisse remplacée par la fraîche en cas de succès.
 */
#include <cstring>
#include <unordered_map>
#include <vector>

#include "../render3d/canvas.hpp"
#include "../render3d/offscreen.hpp"
#include "../render3d/shader_compiler.hpp"
#include "../sdl3/render.hpp"
#include "components.hpp"
#include "render_backend.hpp"
#include "systems.hpp"

namespace ui {

/// Format couleur des deux textures GPU intermédiaires (sortie du pipeline
/// d'effet) — même format large que VIEWPORT3D_COLOR_FORMAT/
/// OFFSCREEN_COLOR_FORMAT (viewport3d.hpp/offscreen.hpp), tightly-packed
/// RGBA8 côté octets, littéralement [R,G,B,A] par texel comme tout format GPU
/// (aucune notion de "packed 32-bit int endian-dépendant" côté GPU — cf.
/// PixelFormat::RGBA32, PAS RGBA8888, côté CPU pour la même garantie
/// d'ordre d'octets littéral : cf. doc de Renderer::ReadPixels, render.hpp,
/// pour le piège découvert pendant ce jalon).
constexpr sdl3::GpuTextureFormat SHADER_EFFECT_COLOR_FORMAT = SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM;

// ----------------------------------------------------------------------------
// Sources GLSL — un triangle plein-écran sans vertex buffer (technique
// standard, id généré par gl_VertexIndex : id0->clip(-1,-1)/uv(0,0),
// id1->clip(3,-1)/uv(2,0), id2->clip(-1,3)/uv(0,2) — la portion visible,
// clip in [-1,1], correspond exactement à uv in [0,1]). Conventions
// set/binding identiques au reste du dépôt (cf. assets/shaders/src/
// mesh_phong.frag / shape2d_textured.frag) : sampler fragment = set 2,
// uniform buffer fragment = set 3.
// ----------------------------------------------------------------------------

constexpr const char *FULLSCREEN_TRIANGLE_VERT = R"(#version 450

layout(location = 0) out vec2 outUV;

void main() {
	vec2 pos = vec2(float((gl_VertexIndex << 1) & 2), float(gl_VertexIndex & 2));
	outUV = pos;
	gl_Position = vec4(pos * 2.0 - 1.0, 0.0, 1.0);
}
)";

// TINT_SHIFT : outColor.rgb = lerp(src.rgb, src.rgb * uColor.rgb, uParam) —
// formule exactement hand-computable par pixel connaissant src/uColor/uParam
// (cf. tests/ui_shader_effects_smoke_test.cpp).
constexpr const char *TINT_SHIFT_FRAG = R"(#version 450

layout(location = 0) in vec2 inUV;
layout(location = 0) out vec4 outColor;

layout(set = 2, binding = 0) uniform sampler2D uSource;

layout(set = 3, binding = 0) uniform EffectUBO {
	vec4 uColor;
	float uParam;
	float uPad0;
	float uPad1;
	float uPad2;
};

void main() {
	vec4 src = texture(uSource, inUV);
	vec3 tinted = mix(src.rgb, src.rgb * uColor.rgb, uParam);
	outColor = vec4(tinted, src.a);
}
)";

// GLOW : halo radial — outColor.rgb = lerp(src.rgb, uColor.rgb,
// smoothstep(uParam, 0.5, distance(uv, (0.5,0.5)))). distance UV au centre
// varie de 0 (centre) à sqrt(0.5) (coin) ; smoothstep(edge0=uParam,
// edge1=0.5, x) est la formule GLSL standard t=clamp((x-edge0)/(edge1-
// edge0),0,1), smoothstep=t*t*(3-2t) — hand-computable pour tout (uv,
// uParam) connus, cf. tests/ui_shader_effects_smoke_test.cpp.
constexpr const char *GLOW_FRAG = R"(#version 450

layout(location = 0) in vec2 inUV;
layout(location = 0) out vec4 outColor;

layout(set = 2, binding = 0) uniform sampler2D uSource;

layout(set = 3, binding = 0) uniform EffectUBO {
	vec4 uColor;
	float uParam;
	float uPad0;
	float uPad1;
	float uPad2;
};

void main() {
	vec4 src = texture(uSource, inUV);
	float dist = distance(inUV, vec2(0.5, 0.5));
	float t = smoothstep(uParam, 0.5, dist);
	vec3 glowed = mix(src.rgb, uColor.rgb, t);
	outColor = vec4(glowed, src.a);
}
)";

/// Struct C++ poussée telle quelle (PushFragmentUniformData<T>, memcpy brut)
/// — layout std140-compatible par construction (vec4 @0, 4 floats packés
/// ensuite, taille totale 32 octets, multiple de 16), même style de padding
/// explicite que MaterialUBO/LightUBO (offscreen.hpp/canvas.hpp).
struct ShaderEffectUBO {
	float color[4];
	float param;
	float pad0 = 0.f, pad1 = 0.f, pad2 = 0.f;
};

class ShaderEffectSystem {
public:
	/// Post-traite chaque UiShaderEffect (cf. en-tête du fichier pour le
	/// pipeline complet). `renderSystem` DOIT être celui dont
	/// RenderSystem::DrawWidget/DrawTree sont ensuite utilisés pour le rendu
	/// 2D normal (ui::Ui::Render() garantit cela : même instance `render`
	/// passée aux deux) — c'est là que vit RenderSystem::effectTextures, la
	/// seule copie possédée de chaque texture d'effet (cf. en-tête :
	/// UiShaderEffect reste un composant pur, aucun état GPU dessus).
	///
	/// Hazard ECS (cf. memory/project_ui_ecs_module.md) : collecte d'abord
	/// (EntitiesWith, copie), mute ensuite — même précaution que
	/// Viewport3DSystem::Update.
	void Update(ecs::ArchetypeRegistry &world, render3d::Canvas &canvas, IUiRenderBackend &ren,
				RenderSystem &renderSystem);

private:
	/// État GPU-adjacent par widget-à-effet — cf. en-tête du fichier pour la
	/// raison pour laquelle ceci vit ICI (système) et pas sur UiShaderEffect
	/// (composant). La texture AFFICHÉE finale n'est PAS possédée ici : elle
	/// vit dans RenderSystem::effectTextures (seule lue par DrawWidget) ;
	/// displayWidth/displayHeight ci-dessous ne sont que des métadonnées pour
	/// savoir si Texture::Create (taille changée) ou juste Texture::Update
	/// suffit, sans dupliquer la propriété de la texture elle-même.
	struct WidgetState {
		Option<sdl3::Texture> sourceTarget = NONE; ///< (a) cible sdl3::Renderer de capture du sous-arbre
		uint32_t sourceWidth = 0, sourceHeight = 0;
		std::vector<uint8_t> readbackPixels; ///< [R,G,B,A] tightly-packed, réutilisé chaque frame (pas de réalloc)

		Option<sdl3::GpuTexture> effectColorTexture = NONE; ///< (c) sortie GPU du pipeline d'effet (couleur seule)
		uint32_t effectWidth = 0, effectHeight = 0;
		std::vector<uint8_t> downloadPixels; ///< [R,G,B,A] tightly-packed, réutilisé chaque frame

		uint32_t displayWidth = 0, displayHeight = 0; ///< cf. doc de la struct : la Texture elle-même est ailleurs
	};
	std::unordered_map<ecs::Entity, WidgetState> m_state;

	// 2 presets = 2 pipelines fixes (cf. en-tête du fichier : pas de
	// PipelineKey générique façon Canvas, hors scope ici par construction).
	Option<sdl3::GpuGraphicsPipeline> m_tintPipeline = NONE;
	Option<sdl3::GpuGraphicsPipeline> m_glowPipeline = NONE;
	Option<sdl3::GpuSampler> m_sampler = NONE;

	/// Réinsère l'ancienne texture d'effet (si ce widget en avait déjà une)
	/// dans RenderSystem::effectTextures à la destruction — SAUF si
	/// `committed` a été posé (succès de ce frame) : cf. en-tête du fichier,
	/// "piège anti-boucle-de-rétroaction". RAII plutôt que dupliquer la
	/// restauration à chaque `return` best-effort de UpdateOne.
	struct RestoreGuard {
		RenderSystem &rs;
		ecs::Entity e;
		Option<sdl3::Texture> saved = NONE;
		bool committed = false;
		~RestoreGuard();
	};

	[[nodiscard]] Option<Ref<sdl3::GpuSampler>> GetOrBuildSampler(sdl3::GpuDevice &device);

	[[nodiscard]] Option<Ref<sdl3::GpuGraphicsPipeline>> GetOrBuildPipeline(sdl3::GpuDevice &device,
																			UiShaderEffectKind kind);

	/// (Re)crée `state.effectColorTexture` si la taille demandée diffère —
	/// même schéma que OffscreenTarget::EnsureSize (offscreen.hpp) mais SANS
	/// texture de profondeur : ce pipeline ne fait aucun test de profondeur
	/// (un seul triangle plein-écran, jamais de superposition à départager).
	[[nodiscard]] bool EnsureEffectColorTexture(sdl3::GpuDevice &device, WidgetState &state, uint32_t w, uint32_t h);

	void UpdateOne(ecs::ArchetypeRegistry &world, render3d::Canvas &canvas, IUiRenderBackend &ren,
				   RenderSystem &renderSystem, sdl3::Renderer &nativeRenderer, ecs::Entity e);
};

} // namespace ui
