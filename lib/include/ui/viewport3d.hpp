#pragma once
/**
 * ui::Viewport3D — widget qui embarque une scène render3d:: indépendante
 * dans l'arbre UI 2D (M22, Phase 3 du plan d'expansion moteur
 * `/home/clement/.claude/plans/optimized-humming-toast.md`). Suit le
 * précédent NodeGraphSystem (nodegraph.hpp) : un widget "complexe" reçoit son
 * propre système dédié (Viewport3DSystem), appelé explicitement par
 * ui::Ui::Render() (ou l'appli, en usage sans façade), plutôt que noyé dans
 * LayoutSystem/InputSystem générique.
 *
 * PAS `ui::Scene` — collision volontairement évitée avec le `ui::Scene`/
 * `ui::SceneManager` existant (scene.hpp), un tout autre concept (navigation
 * façon GtkStack entre écrans 2D).
 *
 * Pipeline complet (cf. ui.hpp::Ui::Render()) :
 *
 *   1. Viewport3DSystem::Update(world, canvas, ren) — AVANT tout dessin 2D :
 *      pour chaque UiViewport3D, lit UiComputed.screen (taille résolue par
 *      LayoutSystem, PEUT changer chaque frame — redimensionnement de
 *      fenêtre, layout réactif...), redimensionne son OffscreenTarget
 *      GPU (EnsureSize) si besoin, rend la scène offscreen
 *      (RenderObjectToTexture, M21), puis crée/rafraîchit sa sdl3::Texture
 *      d'affichage à partir des pixels téléchargés.
 *   2. RenderSystem::DrawWidget (systems.hpp) — dispatch normal : si le
 *      widget porte un UiViewport3D avec displayTexture posé, blit la
 *      texture déjà rendue via DrawImageFit (même primitive que UiImage) ;
 *      chrome (fond/bordure/radius) suit la cascade UiStyle normale, seul le
 *      contenu intérieur est spécial.
 *
 * Ownership de la racine de scène (cf. rapport de tâche pour le
 * raisonnement) : Object3D est non-copiable/non-déplaçable (object3d.hpp:38-
 * 41, un déplacement invaliderait le pointeur `parent` que tiennent les
 * enfants existants) — UiViewport3D référence donc une Object3D& possédée
 * par l'APPELANT (comme root.Add(...) dans render3d_showcase.cpp), jamais
 * par valeur. Chaque UiViewport3D possède en revanche sa propre Camera (type
 * simple, camera.hpp) et son propre OffscreenTarget (M21) : aucun état GPU
 * partagé entre deux Viewport3D, plusieurs scènes/caméras indépendantes
 * "gratuites" par construction.
 *
 * Hors scope M22 (cf. plan) : ombres/IBL à l'intérieur d'un Viewport3D
 * (limite héritée de RenderObjectToTexture, M21 — pas contournée ici) ;
 * interaction avec la scène 3D (orbite caméra, sélection au clic...) —
 * purement un widget d'AFFICHAGE pour l'instant, l'interaction est un souci
 * du niveau éditeur (M30+).
 */
#include <vector>

#include "../render3d/camera.hpp"
#include "../render3d/canvas.hpp"
#include "../render3d/object3d.hpp"
#include "../render3d/offscreen.hpp"
#include "../sdl3/render.hpp"
#include "components.hpp"
#include "render_backend.hpp"

namespace ui {

/// Formats couleur/profondeur de l'OffscreenTarget interne de chaque
/// UiViewport3D — mêmes formats larges (couverts par tous les backends
/// SDL_GPU de cet environnement) que tests/offscreen_smoke_test.cpp, choisis
/// sans sondage de support pour la même raison (cf. offscreen.hpp).
constexpr sdl3::GpuTextureFormat VIEWPORT3D_COLOR_FORMAT = SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM;
constexpr sdl3::GpuTextureFormat VIEWPORT3D_DEPTH_FORMAT = SDL_GPU_TEXTUREFORMAT_D32_FLOAT;

/// Widget qui affiche une scène render3d:: indépendante. `root` est une
/// référence NON POSSÉDÉE (cf. en-tête du fichier) — l'appelant doit garder
/// la Object3D racine en vie au moins aussi longtemps que ce composant.
struct UiViewport3D {
	render3d::Object3D *root = nullptr;
	render3d::Camera camera{};

	/// Cible offscreen GPU (couleur+profondeur) — redimensionnée par
	/// Viewport3DSystem::Update quand UiComputed.screen change de taille.
	render3d::OffscreenTarget target{};

	/// Texture SDL_Renderer affichée par RenderSystem::DrawWidget — NONE tant
	/// qu'aucune frame n'a encore pu être rendue (backend sans
	/// sdl3::Renderer réel, ou tout premier appel avant layout) ; le widget
	/// ne dessine alors simplement rien ce frame-là (dégradation cohérente
	/// avec le reste du dépôt, cf. EnsureShadowResources/EnsureIblSamplers).
	Option<sdl3::Texture> displayTexture = NONE;

	/// Dernière taille pixel pour laquelle displayTexture a été CRÉÉE (pas
	/// seulement mise à jour) — permet à Viewport3DSystem::Update de savoir
	/// s'il doit appeler Texture::Create (taille changée / première fois) ou
	/// juste Texture::Update (même taille, juste un nouveau contenu) : une
	/// texture SDL_Renderer ne peut pas être redimensionnée en place.
	uint32_t textureWidth = 0;
	uint32_t textureHeight = 0;

	/// Pixels CPU tightly-packed réutilisés chaque frame comme buffer
	/// intermédiaire (RenderObjectToTexture -> Texture::Update) — membre
	/// plutôt que local à Update() pour éviter une réallocation par frame et
	/// par viewport.
	std::vector<uint8_t> pixels;

	/// Rendu continu (défaut : chaque image) ou À LA DEMANDE : avec `false`,
	/// la scène n'est rendue que lorsque `needsRender` est vrai ou que la
	/// taille change — une vignette de modèle, un aperçu figé ne coûtent
	/// alors qu'un rendu, pas un par image.
	bool continuous = true;
	bool needsRender = true;

	/// Éclairage PROPRE au widget. Sans lui, un viewport rend avec l'éclairage
	/// courant du Canvas partagé — celui de la scène principale : une vignette
	/// sortirait noire dans une scène de nuit. Avec lui, l'éclairage du Canvas
	/// est remplacé le temps de CE rendu, puis restauré.
	struct Lighting {
		render3d::DirectionalLight sun;
		render3d::AmbientLight ambient;
		sdl3::Color background{0, 0, 0, 255};
	};
	Option<Lighting> lighting = NONE;
};

/// Système dédié (précédent : NodeGraphSystem, nodegraph.hpp) — pas intégré
/// au dispatch générique de LayoutSystem/InputSystem : appelé explicitement,
/// une fois par frame, AVANT le passage de dessin 2D (cf. en-tête du fichier
/// pour l'ordre complet).
class Viewport3DSystem {
public:
	/// Rend la scène de chaque UiViewport3D dans son OffscreenTarget puis
	/// rafraîchit sa texture d'affichage SDL_Renderer. `canvas` est PARTAGÉ
	/// entre tous les viewports (un seul render3d::GpuDevice par appli, cf.
	/// ui::Ui::Initialize(render3d::Canvas&)) — chaque UiViewport3D garde en
	/// revanche son propre OffscreenTarget/Camera, donc aucun état de scène
	/// n'est partagé, seulement le device GPU sous-jacent.
	///
	/// Hazard ECS (cf. memory/project_ui_ecs_module.md — StyleSystem::resolve
	/// s'y est fait piéger deux fois) : COLLECTE d'abord la liste des
	/// entités via EntitiesWith<UiViewport3D>() (copie, cf. ecs.hpp:899-901),
	/// PUIS mute leurs composants dans une boucle séparée — jamais
	/// AddComponent/RemoveComponent pendant une itération de Query, même si
	/// ce système lui-même n'en appelle aucun (défensif : reste valide même
	/// combiné, la même frame, à du code applicatif qui, lui, en ajoute/
	/// retire pendant que ce Update() tourne).
	void Update(ecs::ArchetypeRegistry &world, render3d::Canvas &canvas, IUiRenderBackend &ren) {
		std::vector<ecs::Entity> entities = world.EntitiesWith<UiViewport3D>();

		for (ecs::Entity e : entities) {
			auto vp = world.GetComponent<UiViewport3D>(e);
			if (vp.IsNone())
				continue; // parti entre la collecte et ici (despawn applicatif) : ignore-le simplement
			UiViewport3D &v = *vp.Unwrap();
			if (!v.root)
				continue; // widget spawné sans racine de scène : rien à dessiner
			// Masqué (onglet inactif, panneau caché, mode plein écran qui
			// cache l'interface) : sa mise en page est figée, son image
			// invisible — la rendre quand même coûtait un rendu 3D complet
			// par image pour rien.
			if (IsHiddenRecursive(world, e))
				continue;

			auto computed = world.GetComponent<UiComputed>(e);
			if (computed.IsNone())
				continue; // pas encore passé par LayoutSystem une seule fois

			uint32_t w = uint32_t(sdl3::Max(0.f, computed.Unwrap()->screen.w));
			uint32_t h = uint32_t(sdl3::Max(0.f, computed.Unwrap()->screen.h));
			if (w == 0 || h == 0)
				continue; // widget replié/masqué ce frame : pas de taille à rendre

			// Rendu à la demande : rien à refaire tant que ni le contenu ni la
			// taille n'ont changé.
			if (!v.continuous && !v.needsRender && v.displayTexture.IsSome() && v.textureWidth == w &&
				v.textureHeight == h)
				continue;

			auto sized = v.target.EnsureSize(canvas.Device(), w, h, VIEWPORT3D_COLOR_FORMAT, VIEWPORT3D_DEPTH_FORMAT);
			if (!sized)
				continue; // best-effort (cf. en-tête du fichier) : échec GPU, on garde la texture précédente

			bool rendered = false;
			if (v.lighting.IsSome()) {
				// Éclairage propre : celui du Canvas est sauvegardé, remplacé
				// le temps de ce rendu, puis restauré à l'identique.
				const render3d::DirectionalLight savedSun = canvas.Directional();
				const render3d::AmbientLight savedAmbient = canvas.Ambient();
				const sdl3::Color savedBackground = canvas.BackgroundColor();
				const std::vector<render3d::PointLight> savedPoints = canvas.ActivePointLights();
				const std::vector<render3d::SpotLight> savedSpots = canvas.ActiveSpotLights();
				canvas.SetLighting(v.lighting.Value().sun, v.lighting.Value().ambient);
				canvas.SetBackgroundColor(v.lighting.Value().background);
				canvas.SetLights({}, {});
				rendered = bool(render3d::RenderObjectToTexture(canvas, *v.root, v.camera, v.target, v.pixels));
				canvas.SetLighting(savedSun, savedAmbient);
				canvas.SetBackgroundColor(savedBackground);
				canvas.SetLights(savedPoints, savedSpots);
			} else {
				rendered = bool(render3d::RenderObjectToTexture(canvas, *v.root, v.camera, v.target, v.pixels));
			}
			if (!rendered)
				continue;
			v.needsRender = false;

			// Pas de sdl3::Renderer réel sous ce backend (futur backend
			// non-SDL) : rien de plus à faire, RenderSystem::DrawWidget
			// sautera simplement le blit tant que displayTexture reste NONE.
			sdl3::Renderer *nativeRenderer = ren.NativeRenderer();
			if (!nativeRenderer)
				continue;

			bool needsCreate = v.displayTexture.IsNone() || v.textureWidth != w || v.textureHeight != h;
			if (needsCreate) {
				// `PixelFormat::RGBA32`, PAS le défaut `RGBA8888` : les pixels
				// téléchargés depuis la cible GPU sont en R8G8B8A8_UNORM,
				// c'est-à-dire l'ordre LITTÉRAL [R,G,B,A] en mémoire, alors
				// que RGBA8888 est un format PACKED dont l'ordre mémoire sur
				// little-endian est l'inverse ([A,B,G,R]). Le défaut faisait
				// donc échanger le rouge et le bleu dans TOUT widget
				// Viewport3D — un fond bleu nuit ressortait brun-rouge, et un
				// cube bleu s'affichait rouge. Le même piège est documenté en
				// long sur `Renderer::ReadPixels` (render.hpp) : RGBA32 est
				// l'alias qui garantit [R,G,B,A] quelle que soit
				// l'endianness. Trouvé en construisant
				// examples/game_editor/, dont la vitrine de matériaux rend
				// l'inversion évidente à l'œil.
				auto created = sdl3::Texture::Create(*nativeRenderer, int(w), int(h), sdl3::PixelFormat::RGBA32);
				if (!created)
					continue; // best-effort : réessaiera au prochain frame
				v.displayTexture = Some(std::move(created.Value()));
				v.textureWidth = w;
				v.textureHeight = h;
			}
			// Pixels contigus, 4 octets par pixel : pitch = largeur * 4.
			v.displayTexture->Update(v.pixels.data(), int(w) * 4);
		}
	}
};

} // namespace ui
