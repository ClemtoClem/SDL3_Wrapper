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
		if (!v.target.EnsureSize(canvas.Device(), w, h, VIEWPORT3D_COLOR_FORMAT, VIEWPORT3D_DEPTH_FORMAT))
			continue;
		bool rendered = false;
		if (v.lighting.IsSome()) {
			const render3d::DirectionalLight sun = canvas.Directional();
			const render3d::AmbientLight ambient = canvas.Ambient();
			const sdl3::Color background = canvas.BackgroundColor();
			const std::vector<render3d::PointLight> points = canvas.ActivePointLights();
			const std::vector<render3d::SpotLight> spots = canvas.ActiveSpotLights();
			canvas.SetLighting(v.lighting.Value().sun, v.lighting.Value().ambient);
			canvas.SetBackgroundColor(v.lighting.Value().background);
			canvas.SetLights({}, {});
			rendered = bool(render3d::RenderObjectToTexture(canvas, *v.root, v.camera, v.target, v.pixels));
			canvas.SetLighting(sun, ambient);
			canvas.SetBackgroundColor(background);
			canvas.SetLights(points, spots);
		} else {
			rendered = bool(render3d::RenderObjectToTexture(canvas, *v.root, v.camera, v.target, v.pixels));
		}
		if (!rendered)
			continue;
		v.needsRender = false;
		if (v.displayTexture.IsNone() || v.textureWidth != w || v.textureHeight != h) {
			// RGBA32 : ordre mémoire [R,G,B,A] des pixels téléchargés (RGBA8888
			// est un format empaqueté, inversé sur une machine petit-boutiste).
			auto created = sdl3::Texture::Create(*ctx.renderer, int(w), int(h), sdl3::PixelFormat::RGBA32);
			if (!created)
				continue;
			v.displayTexture = Some(std::move(created.Value()));
			v.textureWidth = w;
			v.textureHeight = h;
		}
		(void)v.displayTexture.Value().Update(v.pixels.data(), int(w) * 4);
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
