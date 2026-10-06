// Définitions de ui/viewport3d.hpp
#include "ui/viewport3d.hpp"

namespace ui {

// ── Viewport3DSystem ─────────────────────────────────────────────────────────

void Viewport3DSystem::Update(ecs::ArchetypeRegistry &world, render3d::Canvas &canvas, IUiRenderBackend &ren) {
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

} // namespace ui
