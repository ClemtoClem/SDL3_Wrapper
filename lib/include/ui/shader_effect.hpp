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
				RenderSystem &renderSystem) {
		sdl3::Renderer *nativeRenderer = ren.NativeRenderer();
		if (!nativeRenderer)
			return; // backend sans sdl3::Renderer réel : rien à faire ce frame (cf. Viewport3DSystem)

		std::vector<ecs::Entity> entities = world.EntitiesWith<UiShaderEffect>();
		for (ecs::Entity e : entities)
			UpdateOne(world, canvas, ren, renderSystem, *nativeRenderer, e);
	}

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
		~RestoreGuard() {
			if (!committed && saved.IsSome())
				rs.effectTextures.insert_or_assign(e, std::move(saved.Value()));
		}
	};

	[[nodiscard]] Option<Ref<sdl3::GpuSampler>> GetOrBuildSampler(sdl3::GpuDevice &device) {
		if (m_sampler.IsSome())
			return Some(MakeRef(m_sampler.Value()));
		// NEAREST (pas LINEAR) : le sous-arbre capturé est déjà à la
		// résolution exacte du widget (source == destination en taille), un
		// filtrage LINEAR ne ferait qu'introduire du flou d'interpolation
		// entre texels voisins — indésirable ici puisque les tests vérifient
		// des valeurs EXACTES par pixel (cf. tests/ui_shader_effects_smoke_
		// test.cpp).
		sdl3::GpuSamplerCreateInfo info{};
		info.min_filter = sdl3::gpu_filter::NEAREST;
		info.mag_filter = sdl3::gpu_filter::NEAREST;
		info.mipmap_mode = sdl3::gpu_sampler_mipmap_mode::NEAREST;
		info.address_mode_u = sdl3::gpu_sampler_address_mode::CLAMP_TO_EDGE;
		info.address_mode_v = sdl3::gpu_sampler_address_mode::CLAMP_TO_EDGE;
		info.address_mode_w = sdl3::gpu_sampler_address_mode::CLAMP_TO_EDGE;
		auto result = device.CreateSampler(info);
		if (!result)
			return NONE;
		m_sampler = Some(std::move(result.Value()));
		return Some(MakeRef(m_sampler.Value()));
	}

	[[nodiscard]] Option<Ref<sdl3::GpuGraphicsPipeline>> GetOrBuildPipeline(sdl3::GpuDevice &device,
																			UiShaderEffectKind kind) {
		Option<sdl3::GpuGraphicsPipeline> &slot = (kind == UiShaderEffectKind::GLOW) ? m_glowPipeline : m_tintPipeline;
		if (slot.IsSome())
			return Some(MakeRef(slot.Value()));

		const char *fragSource = (kind == UiShaderEffectKind::GLOW) ? GLOW_FRAG : TINT_SHIFT_FRAG;
		const char *debugName = (kind == UiShaderEffectKind::GLOW) ? "shader_effect_glow" : "shader_effect_tint";

		// Vertex : aucun sampler/uniform (le triangle plein-écran est généré
		// depuis gl_VertexIndex seul, cf. FULLSCREEN_TRIANGLE_VERT). Fragment :
		// 1 sampler (source) + 1 uniform buffer (EffectUBO).
		auto vs = render3d::CompileShader(device, StringView(FULLSCREEN_TRIANGLE_VERT), sdl3::gpu_shader_stage::VERTEX,
										  StringView("shader_effect.vert"), 0, 0);
		if (!vs)
			return NONE;
		auto fs = render3d::CompileShader(device, StringView(fragSource), sdl3::gpu_shader_stage::FRAGMENT,
										  StringView(debugName), 1, 1);
		if (!fs)
			return NONE;

		sdl3::GpuGraphicsPipelineCreateInfo info{};
		info.vertex_shader = vs.Value().Get();
		info.fragment_shader = fs.Value().Get();
		// vertex_input_state reste zero-initialisé (aucun vertex buffer, cf.
		// ci-dessus) — pas de depth (post-traitement 2D, pas de rendu 3D).
		info.primitive_type = sdl3::gpu_primitive_type::TRIANGLE_LIST;
		info.rasterizer_state.fill_mode = sdl3::gpu_fill_mode::FILL;
		info.rasterizer_state.cull_mode = sdl3::gpu_cull_mode::NONE; // triangle unique ad hoc, pas un maillage authored
		info.rasterizer_state.front_face = sdl3::gpu_front_face::CLOCKWISE;
		info.multisample_state.sample_count = sdl3::gpu_sample_count::SAMPLE1;

		sdl3::GpuColorTargetDescription colorDesc{};
		colorDesc.format = SHADER_EFFECT_COLOR_FORMAT;
		info.target_info.color_target_descriptions = &colorDesc;
		info.target_info.num_color_targets = 1;

		auto pipelineResult = device.CreateGraphicsPipeline(info);
		if (!pipelineResult)
			return NONE;
		slot = Some(std::move(pipelineResult.Value()));
		return Some(MakeRef(slot.Value()));
	}

	/// (Re)crée `state.effectColorTexture` si la taille demandée diffère —
	/// même schéma que OffscreenTarget::EnsureSize (offscreen.hpp) mais SANS
	/// texture de profondeur : ce pipeline ne fait aucun test de profondeur
	/// (un seul triangle plein-écran, jamais de superposition à départager).
	[[nodiscard]] bool EnsureEffectColorTexture(sdl3::GpuDevice &device, WidgetState &state, uint32_t w, uint32_t h) {
		if (state.effectColorTexture.IsSome() && state.effectWidth == w && state.effectHeight == h)
			return true;
		sdl3::GpuTextureCreateInfo info{};
		info.type = sdl3::gpu_texture_type::TEXTURE2_D;
		info.format = SHADER_EFFECT_COLOR_FORMAT;
		info.usage = sdl3::gpu_texture_usage::COLOR_TARGET | sdl3::gpu_texture_usage::SAMPLER;
		info.width = w;
		info.height = h;
		info.layer_count_or_depth = 1;
		info.num_levels = 1;
		info.sample_count = sdl3::gpu_sample_count::SAMPLE1;
		auto result = device.CreateTexture(info);
		if (!result)
			return false;
		state.effectColorTexture = Some(std::move(result.Value()));
		state.effectWidth = w;
		state.effectHeight = h;
		return true;
	}

	void UpdateOne(ecs::ArchetypeRegistry &world, render3d::Canvas &canvas, IUiRenderBackend &ren,
				   RenderSystem &renderSystem, sdl3::Renderer &nativeRenderer, ecs::Entity e) {
		auto fx = world.GetComponent<UiShaderEffect>(e);
		if (fx.IsNone())
			return; // parti entre la collecte et ici (despawn applicatif) : ignore-le simplement
		auto computed = world.GetComponent<UiComputed>(e);
		if (computed.IsNone())
			return; // pas encore passé par LayoutSystem une seule fois

		uint32_t w = uint32_t(sdl3::Max(0.f, computed.Unwrap()->screen.w));
		uint32_t h = uint32_t(sdl3::Max(0.f, computed.Unwrap()->screen.h));
		if (w == 0 || h == 0)
			return; // widget replié/masqué ce frame : pas de taille à rendre

		// Cf. en-tête du fichier, "piège anti-boucle-de-rétroaction" : retire
		// l'entrée existante AVANT de capturer, la restaure au pire (RAII) si
		// ce widget échoue plus loin ce frame.
		RestoreGuard guard{renderSystem, e, NONE, false};
		if (auto it = renderSystem.effectTextures.find(e); it != renderSystem.effectTextures.end()) {
			guard.saved = Some(std::move(it->second));
			renderSystem.effectTextures.erase(it);
		}

		WidgetState &state = m_state[e];

		// ── (a) Capture du sous-arbre dans une texture cible dédiée ────────
		bool needsSourceCreate = state.sourceTarget.IsNone() || state.sourceWidth != w || state.sourceHeight != h;
		if (needsSourceCreate) {
			// Format de CRÉATION de cette texture cible : sans importance ici
			// (simple canevas de dessin 2D via SetDrawColor/FillRect/DrawTree,
			// jamais nourri d'octets bruts directement) — RGBA8888 par défaut
			// suffit ; ReadPixels ci-dessous lit le format NATIF réel puis
			// Convert() explicitement vers RGBA32 (cf. sa propre note).
			auto created =
				nativeRenderer.CreateTexture(sdl3::PixelFormat::RGBA8888, sdl3::TextureAccess::TARGET, int(w), int(h));
			if (!created)
				return; // best-effort : réessaiera au prochain frame
			state.sourceTarget = Some(std::move(created.Value()));
			state.sourceWidth = w;
			state.sourceHeight = h;
		}
		if (!nativeRenderer.SetTarget(*state.sourceTarget))
			return;
		nativeRenderer.SetDrawColor(sdl3::FColor{0.f, 0.f, 0.f, 0.f});
		nativeRenderer.Clear();

		// Décalage de repère (cf. en-tête du fichier) : DrawTree dessine en
		// coordonnées ABSOLUES de fenêtre (UiComputed.screen/clip), pas
		// relatives à `e` — sans ce viewport décalé, le contenu se
		// dessinerait hors des bornes de notre petite texture w×h dès que le
		// widget n'est pas au coin (0,0) de la fenêtre.
		int ox = int(computed.Unwrap()->screen.x);
		int oy = int(computed.Unwrap()->screen.y);
		nativeRenderer.SetViewport(sdl3::Rect{-ox, -oy, ox + int(w) + 1, oy + int(h) + 1});
		renderSystem.DrawTree(world, ren, e, /*overlayEntry=*/false);
		ren.ClearClipRect(); // hygiène (même geste que RenderSystem::Run après son propre parcours d'arbre)
		nativeRenderer.ClearViewport(); // remis à plat AVANT ReadPixels (cf. doc Renderer::ReadPixels, render.hpp)

		auto readResult = nativeRenderer.ReadPixels();
		nativeRenderer.ResetTarget(); // dans tous les cas, avant tout retour anticipé qui suit
		if (!readResult)
			return;
		// RGBA32, PAS RGBA8888 (piège réel, cf. la note détaillée sur
		// Renderer::ReadPixels dans render.hpp) : RGBA8888 est un format
		// PACKED dont l'ordre en mémoire sur little-endian est l'INVERSE de
		// son nom — un octet-par-octet naïf en RGBA8888 lirait donc des
		// couleurs corrompues (canaux permutés) une fois uploadées côté GPU
		// ci-dessous. RGBA32 seul garantit l'ordre littéral [R,G,B,A].
		auto converted = readResult.Value().Convert(sdl3::PixelFormat::RGBA32);
		if (!converted)
			return;
		SDL_Surface *surf = converted.Value().Get();
		if (!surf || uint32_t(surf->w) != w || uint32_t(surf->h) != h)
			return; // ReadPixels peut renvoyer moins que demandé (clip au viewport courant) : abandonne plutôt que lire hors-buffer

		// Compaction tightly-packed (pitch de Convert() non garanti ==
		// largeur*4 par l'API SDL, cf. doc Renderer::ReadPixels) — même
		// discipline "ne jamais deviner le layout" que le reste de ce dépôt.
		state.readbackPixels.resize(size_t(w) * h * 4);
		const auto *srcPixels = static_cast<const uint8_t *>(surf->pixels);
		for (uint32_t row = 0; row < h; ++row)
			std::memcpy(state.readbackPixels.data() + size_t(row) * w * 4, srcPixels + size_t(row) * size_t(surf->pitch),
						size_t(w) * 4);

		// ── (b) upload CPU -> GPU (M21, offscreen.hpp) ──────────────────────
		auto uploaded = render3d::UploadSurfaceToGpuTexture(canvas.Device(), state.readbackPixels.data(), w, h,
															 SHADER_EFFECT_COLOR_FORMAT);
		if (!uploaded)
			return;
		sdl3::GpuTexture sourceGpuTexture = std::move(uploaded.Value());

		// ── (c) pipeline d'effet dédié ───────────────────────────────────
		if (!EnsureEffectColorTexture(canvas.Device(), state, w, h))
			return;
		auto pipeline = GetOrBuildPipeline(canvas.Device(), fx.Unwrap()->kind);
		if (!pipeline)
			return;
		auto sampler = GetOrBuildSampler(canvas.Device());
		if (!sampler)
			return;

		sdl3::FColor c = fx.Unwrap()->color;
		ShaderEffectUBO ubo{{c.r, c.g, c.b, c.a}, fx.Unwrap()->param};

		auto cmd = canvas.Device().AcquireCommandBuffer();
		{
			sdl3::GpuColorTargetInfo colorTarget{};
			colorTarget.texture = state.effectColorTexture.Value().Get();
			colorTarget.clear_color = sdl3::FColor{0.f, 0.f, 0.f, 0.f};
			colorTarget.load_op = sdl3::gpu_load_op::CLEAR;
			colorTarget.store_op = sdl3::gpu_store_op::STORE;

			sdl3::GpuRenderPass pass = cmd.BeginRenderPass(colorTarget);
			sdl3::GpuViewport gpuViewport{0.f, 0.f, float(w), float(h), 0.f, 1.f};
			pass.SetViewport(gpuViewport);
			pass.BindPipeline(pipeline.Value());
			sdl3::GpuTextureSamplerBinding samplerBinding{};
			samplerBinding.texture = sourceGpuTexture.Get();
			samplerBinding.sampler = sampler.Value()->Get();
			pass.BindFragmentSamplers(0, {&samplerBinding, 1});
			cmd.PushFragmentUniformData(0, ubo);
			pass.DrawPrimitives(3, 1, 0, 0);
		}
		if (!cmd.Submit())
			return;
		if (!canvas.Device().WaitIdle())
			return;

		// ── (d) download GPU -> CPU, ré-upload en sdl3::Texture affichée ───
		uint32_t bytesPerTexel = sdl3::GpuTexelBlockSize(SHADER_EFFECT_COLOR_FORMAT);
		uint32_t byteSize = w * h * bytesPerTexel;
		auto downloadResult = canvas.Device().CreateTransferBuffer(sdl3::gpu_transfer_buffer_usage::DOWNLOAD, byteSize);
		if (!downloadResult)
			return;
		sdl3::GpuTransferBuffer downloadBuffer = std::move(downloadResult.Value());
		auto downloadCmd = canvas.Device().AcquireCommandBuffer();
		{
			sdl3::GpuCopyPass copyPass = downloadCmd.BeginCopyPass();
			sdl3::GpuTextureRegion src{state.effectColorTexture.Value().Get(), 0, 0, 0, 0, 0, w, h, 1};
			sdl3::GpuTextureTransferInfo dst{downloadBuffer.Get(), 0, w, h};
			copyPass.DownloadFromTexture(src, dst);
		}
		if (!downloadCmd.Submit())
			return;
		if (!canvas.Device().WaitIdle())
			return;
		auto mapped = downloadBuffer.Map(false);
		if (!mapped)
			return;
		state.downloadPixels.resize(byteSize);
		std::memcpy(state.downloadPixels.data(), mapped.GetData(), byteSize);

		bool needsDisplayCreate = state.displayWidth != w || state.displayHeight != h ||
								  renderSystem.effectTextures.find(e) == renderSystem.effectTextures.end();
		if (needsDisplayCreate) {
			// RGBA32 EXPLICITE (pas le défaut RGBA8888 de Texture::Create,
			// cf. piège documenté sur Renderer::ReadPixels dans render.hpp) :
			// state.downloadPixels ci-dessous est en ordre littéral
			// [R,G,B,A] (sortie GPU R8G8B8A8_UNORM) — une texture taguée
			// RGBA8888 nourrie de ces octets s'afficherait totalement
			// transparente (premier octet, R, relu comme alpha), confirmé
			// empiriquement pendant ce jalon.
			auto created = sdl3::Texture::Create(nativeRenderer, int(w), int(h), sdl3::PixelFormat::RGBA32);
			if (!created)
				return;
			renderSystem.effectTextures.insert_or_assign(e, std::move(created.Value()));
			state.displayWidth = w;
			state.displayHeight = h;
		}
		renderSystem.effectTextures.at(e).Update(state.downloadPixels.data(), int(w) * 4);
		guard.committed = true; // succès : la texture fraîche remplace définitivement l'ancienne (RestoreGuard n'agit plus)
	}
};

} // namespace ui
