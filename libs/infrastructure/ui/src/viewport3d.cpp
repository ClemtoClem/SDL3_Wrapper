// Définitions de ui/viewport3d.hpp
#include <ui/systems.hpp>
#include <ui/viewport3d.hpp>

namespace ui {

WidgetBuilder Viewport3D(render3d::Object3D &root, render3d::Camera camera, bool continuous,
						 Option<ViewportLighting> lighting) {
	WidgetBuilder b("viewport3d");
	render3d::Object3D *rootPtr = &root;
	// L'état GPU n'est pas copiable : construit à la matérialisation.
	b.SetExpand([rootPtr, camera, continuous, lighting](WidgetSpec &s) {
		s.components.push_back([rootPtr, camera, continuous, lighting](ecs::ArchetypeRegistry &w, ecs::Entity e) {
			Viewport3DState v;
			v.root = rootPtr;
			v.camera = camera;
			v.continuous = continuous;
			v.lighting = lighting;
			w.AddComponent(e, std::move(v));
		});
	});
	return b;
}

void RequestViewportRender(Widget view) {
	if (auto v = view.Get<Viewport3DState>(); v.IsSome())
		v.Unwrap()->needsRender = true;
}

namespace {

/// Rend la scène de `v` dans `target` (caméra à l'aspect de la cible,
/// éclairage propre à la vue s'il y en a un).
bool RenderInto(render3d::Canvas &canvas, Viewport3DState &v, render3d::OffscreenTarget &target, render3d::OffscreenSync sync) {
	render3d::Camera camera = v.camera;
	camera.aspect = float(target.Width()) / float(target.Height());
	if (v.lighting.IsNone())
		return bool(canvas.RenderObjectOffscreen(*v.root, camera, target, sync));
	const render3d::DirectionalLight sun = canvas.Directional();
	const render3d::AmbientLight ambient = canvas.Ambient();
	const sdl3::Color background = canvas.BackgroundColor();
	const std::vector<render3d::PointLight> points = canvas.ActivePointLights();
	const std::vector<render3d::SpotLight> spots = canvas.ActiveSpotLights();
	canvas.SetLighting(v.lighting.Value().sun, v.lighting.Value().ambient);
	canvas.SetBackgroundColor(v.lighting.Value().background);
	canvas.SetLights({}, {});
	const bool rendered = bool(canvas.RenderObjectOffscreen(*v.root, camera, target, sync));
	canvas.SetLighting(sun, ambient);
	canvas.SetBackgroundColor(background);
	canvas.SetLights(points, spots);
	return rendered;
}

/// Copie `v.pixels` (w × h, RGBA) dans la texture affichée.
void ShowPixels(Viewport3DState &v, Context &ctx, uint32_t w, uint32_t h) {
	if (v.displayTexture.IsNone() || v.textureWidth != w || v.textureHeight != h) {
		// RGBA32 : ordre mémoire [R,G,B,A] des pixels téléchargés (RGBA8888
		// est un format empaqueté, inversé sur une machine petit-boutiste).
		v.displayTexture = NONE;
		auto created = sdl3::Texture::Create(*ctx.renderer, int(w), int(h), sdl3::PixelFormat::RGBA32);
		if (!created)
			return;
		v.displayTexture = Some(std::move(created.Value()));
		v.textureWidth = w;
		v.textureHeight = h;
	}
	(void)v.displayTexture.Value().Update(v.pixels.data(), int(w) * 4);
}

bool RenderReadback(render3d::Canvas &canvas, Viewport3DState &v, Context &ctx, uint32_t w, uint32_t h) {
	if (!v.target.EnsureSize(canvas.Device(), w, h, VIEWPORT3D_COLOR_FORMAT, VIEWPORT3D_DEPTH_FORMAT))
		return false;
	if (!RenderInto(canvas, v, v.target, render3d::OffscreenSync::WAIT) ||
		!render3d::detail::DownloadColorTexture(canvas, v.target, v.pixels))
		return false;
	ShowPixels(v, ctx, w, h);
	return true;
}

bool RenderDirect(render3d::Canvas &canvas, Viewport3DState &v, Context &ctx, uint32_t w, uint32_t h) {
	const bool resized = v.textureWidth != w || v.textureHeight != h;
	if (resized)
		v.displayTexture = NONE; // elle affiche la texture que EnsureSize va remplacer
	if (!v.target.EnsureSize(canvas.Device(), w, h, VIEWPORT3D_COLOR_FORMAT, VIEWPORT3D_DEPTH_FORMAT))
		return false;
	// Soumis sans attente : le renderer 2D, qui partage le device, soumet son
	// image APRÈS (à la présentation), la file GPU ordonne le reste.
	if (!RenderInto(canvas, v, v.target, render3d::OffscreenSync::SUBMIT))
		return false;
	if (v.displayTexture.IsNone()) {
		auto created = sdl3::Texture::FromGpuTexture(*ctx.renderer, v.target.ColorTexture().Get(), int(w), int(h));
		if (!created) {
			v.directFailed = true; // renderer sans SDL_GPU partagé : repli définitif
			return false;
		}
		v.displayTexture = Some(std::move(created.Value()));
		v.textureWidth = w;
		v.textureHeight = h;
	}
	return true;
}

bool RenderPipelined(render3d::Canvas &canvas, Viewport3DState &v, Context &ctx, uint32_t w, uint32_t h) {
	constexpr size_t RING = 3;
	if (v.ring.size() != RING)
		v.ring.resize(RING);
	// 1. L'image la plus récente que le GPU a finie (rendue et téléchargée).
	Viewport3DState::InFlight *done = nullptr;
	for (Viewport3DState::InFlight &slot : v.ring)
		if (slot.fence.IsSome() && slot.fence.Value().Query() && (!done || slot.frame > done->frame))
			done = &slot;
	if (done && render3d::ReadColorDownload(done->target, v.pixels)) {
		v.shownFrame = done->frame;
		ShowPixels(v, ctx, done->target.Width(), done->target.Height());
	}
	// Les cibles terminées (affichées ou dépassées) sont libres.
	for (Viewport3DState::InFlight &slot : v.ring)
		if (slot.fence.IsSome() && slot.frame <= v.shownFrame && slot.fence.Value().Query())
			slot.fence = NONE;
	// 2. Une cible libre ; sinon le GPU a trois images de retard : on attend la plus ancienne.
	Viewport3DState::InFlight *slot = nullptr;
	for (Viewport3DState::InFlight &s : v.ring)
		if (s.fence.IsNone()) {
			slot = &s;
			break;
		}
	if (!slot) {
		for (Viewport3DState::InFlight &s : v.ring)
			if (!slot || s.frame < slot->frame)
				slot = &s;
		Ref<sdl3::GpuFence> fenceRef = MakeRef(slot->fence.Value());
		(void)canvas.Device().WaitForFences(true, std::span<const Ref<sdl3::GpuFence>>(&fenceRef, 1));
		slot->fence = NONE;
	}
	if (!slot->target.EnsureSize(canvas.Device(), w, h, VIEWPORT3D_COLOR_FORMAT, VIEWPORT3D_DEPTH_FORMAT))
		return false;
	if (!RenderInto(canvas, v, slot->target, render3d::OffscreenSync::SUBMIT))
		return false;
	Result<sdl3::GpuFence, StringView> fence = render3d::SubmitColorDownload(canvas, slot->target);
	if (fence.IsError())
		return false;
	slot->fence = Some(std::move(fence.Value()));
	slot->frame = ++v.ringFrame;
	return true;
}

void RenderViewports(ecs::ArchetypeRegistry &world, Context &ctx) {
	if (!ctx.canvas3d || !ctx.renderer)
		return;
	render3d::Canvas &canvas = *ctx.canvas3d;
	for (ecs::Entity e : world.EntitiesWith<Viewport3DState>()) {
		auto vp = world.GetComponent<Viewport3DState>(e);
		auto rect = world.GetComponent<Rect>(e);
		if (vp.IsNone() || rect.IsNone() || !rect.Unwrap()->visible)
			continue; // masqué (onglet inactif…) : aucun rendu 3D inutile
		Viewport3DState &v = *vp.Unwrap();
		if (!v.root)
			continue;
		const sdl3::FRect box = rect.Unwrap()->content;
		const uint32_t w = uint32_t(math::Max(0.f, box.w)), h = uint32_t(math::Max(0.f, box.h));
		if (w == 0 || h == 0)
			continue;
		if (!v.continuous && !v.needsRender && v.displayTexture.IsSome() && v.textureWidth == w && v.textureHeight == h)
			continue;
		bool rendered = false;
		if (v.transfer == ViewportTransfer::DIRECT && !v.directFailed)
			rendered = RenderDirect(canvas, v, ctx, w, h);
		else if (v.transfer == ViewportTransfer::PIPELINED)
			rendered = RenderPipelined(canvas, v, ctx, w, h);
		else
			rendered = RenderReadback(canvas, v, ctx, w, h);
		if (rendered)
			v.needsRender = false;
	}
}

void DrawViewport(ecs::ArchetypeRegistry &world, Context &ctx, ecs::Entity e, const Rect &rect, const ComputedStyle &) {
	auto vp = world.GetComponent<Viewport3DState>(e);
	if (vp.IsNone() || vp.Unwrap()->displayTexture.IsNone())
		return;
	ctx.painter.Image(vp.Unwrap()->displayTexture.Value(), rect.content, ObjectFit::Fill);
}

} // namespace

void RegisterViewport3DExtension(Context &ctx) {
	Extension ext;
	ext.name = "viewport3d";
	ext.prepare = RenderViewports;
	ext.draw = DrawViewport;
	RegisterExtension(ctx, std::move(ext));
}

} // namespace ui
