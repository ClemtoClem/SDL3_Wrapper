// Widgets « autonomes » (extensions : graphe, graphe de nœuds, effets), chrome
// de fenêtre, scènes, et un rendu réel sur renderer logiciel.
#include <ui/ui.hpp>

#include <sdl3/ttf.hpp>

#include "harness.hpp"
#include "test.hpp"

TEST(Plot, MeasureHoverAndWheelZoom) {
	Harness h;
	const float ys[] = {1.f, 4.f, 2.f, 8.f, 5.f, 7.f};
	ui::Widget plot = h.Mount(ui::PlotLines(ys, "Signal").XRange(0.f, 5.f).YRange(0.f, 10.f));
	const sdl3::FRect r = plot.Bounds();
	CHECK(r.w >= 199.f); // taille intrinsèque de l'extension
	CHECK(r.h >= 119.f);

	ui::Widget sized = h.Mount(ui::PlotLines(ys).Size(400.f, 240.f).XRange(0.f, 5.f).YRange(0.f, 10.f));
	auto model = sized.Get<ui::PlotModel>();
	REQUIRE(model.IsSome());
	CHECK_EQ(model.Unwrap()->series.size(), size_t(1));
	const float span0 = model.Unwrap()->xAxis.max - model.Unwrap()->xAxis.min;
	const sdl3::FPoint c = Harness::Center(sized);
	h.Wheel(c.x, c.y, 1.f);
	auto after = sized.Get<ui::PlotModel>();
	const float span1 = after.Unwrap()->xAxis.max - after.Unwrap()->xAxis.min;
	CHECK(span1 < span0); // molette : zoom vers le curseur
	h.Move(c.x, c.y);
	CHECK(sized.Get<ui::PlotModel>().Unwrap()->hovered);
}

TEST(NodeGraph, NodesPinsConnectionsAndSerialization) {
	Harness h;
	ui::Widget canvas = h.Mount(ui::NodeGraph().Size(600.f, 400.f));
	ui::Widget a = ui::AddGraphNode(canvas, "A", {40.f, 40.f}, {180.f, 100.f}, ui::Text("contenu A"));
	ui::Widget b = ui::AddGraphNode(canvas, "B", {320.f, 160.f}, {180.f, 100.f}, ui::Text("contenu B"));
	ui::Widget out = ui::AddGraphPin(a, ui::PinSide::Right);
	ui::Widget in = ui::AddGraphPin(b, ui::PinSide::Left);
	h.Advance(0.016f);
	CHECK_EQ(ui::NodesOf(h.world, canvas.Entity()).size(), size_t(2));
	CHECK(ui::NodeOfPin(h.world, out.Entity()) == a.Entity());
	CHECK(ui::CanvasOfNode(h.world, b.Entity()) == canvas.Entity());
	// Le nœud est placé d'après sa position dans le canevas.
	const sdl3::FRect cb = canvas.Bounds();
	CHECK(Near(a.Bounds().x, cb.x + 40.f, 1.f));
	CHECK(Near(a.Bounds().w, 180.f, 1.f));

	REQUIRE(ui::ConnectPins(h.world, out.Entity(), in.Entity()) != nullptr);
	CHECK(ui::ConnectPins(h.world, out.Entity(), in.Entity()) == nullptr); // déjà relié
	CHECK_EQ(canvas.Get<ui::GraphCanvas>().Unwrap()->connections.size(), size_t(1));

	// Glisser l'en-tête déplace le nœud.
	ui::Widget header = a.FindType("graph-node-header").Unwrap();
	const sdl3::FPoint hc = Harness::Center(header);
	h.Drag(hc.x, hc.y, hc.x + 60.f, hc.y + 30.f);
	h.Advance(0.016f);
	const sdl3::FPoint pos = a.Get<ui::GraphNode>().Unwrap()->canvasPos;
	CHECK(Near(pos.x, 100.f, 2.f));
	CHECK(Near(pos.y, 70.f, 2.f));
	CHECK(ui::IsNodeSelected(h.world, a.Entity()));

	// Sérialisation aller-retour dans un autre canevas.
	dataio::NodePtr doc = ui::SerializeGraph(h.world, canvas.Entity());
	REQUIRE(doc != nullptr);
	ui::Widget copy = h.Mount(ui::NodeGraph().Size(600.f, 400.f));
	auto keys = ui::DeserializeGraph(copy, doc);
	h.Advance(0.016f);
	CHECK_EQ(keys.size(), size_t(2));
	CHECK_EQ(ui::NodesOf(h.world, copy.Entity()).size(), size_t(2));
	CHECK_EQ(copy.Get<ui::GraphCanvas>().Unwrap()->connections.size(), size_t(1));

	// Supprimer la sélection retire le nœud et sa connexion.
	ui::ClearGraphSelection(h.world, canvas.Entity());
	ui::SelectNode(h.world, a.Entity());
	ui::DeleteGraphSelection(h.world, canvas.Entity());
	h.Advance(0.016f);
	CHECK(!a.Valid());
	CHECK_EQ(ui::NodesOf(h.world, canvas.Entity()).size(), size_t(1));
	CHECK(canvas.Get<ui::GraphCanvas>().Unwrap()->connections.empty());
}

TEST(NodeGraph, SelectionClickRules) {
	ui::NodeSelectionState s;
	const ecs::Entity e1{1, 0}, e2{2, 0}, e3{3, 0};
	const std::vector<ecs::Entity> order{e1, e2, e3};
	ui::ApplyNodeSelectionClick(s, e1, false, false, order);
	CHECK_EQ(s.selected.size(), size_t(1));
	ui::ApplyNodeSelectionClick(s, e3, false, true, order); // Maj : plage
	CHECK_EQ(s.selected.size(), size_t(3));
	ui::ApplyNodeSelectionClick(s, e2, true, false, order); // Ctrl : bascule
	CHECK_EQ(s.selected.size(), size_t(2));
	CHECK(!s.selected.contains(e2));
}

TEST(Scene, LazyBuildHideKeepsState) {
	Harness h;
	ui::SceneManager scenes(h.stage);
	int builds = 0;
	scenes.Add("menu", [&] {
		++builds;
		return ui::Column().Id("menu").Children({ui::Input("nom").Id("name")});
	});
	scenes.Add("options", [&] {
		++builds;
		return ui::Column().Id("options").Children({ui::Checkbox("son")});
	});
	CHECK_EQ(builds, 0);
	CHECK(scenes.SwitchTo("menu"));
	h.Frame();
	CHECK_EQ(builds, 1);
	ui::Widget name = h.gui.Find("#name").Unwrap();
	name.SetText("Alice");
	CHECK(scenes.SwitchTo("options"));
	h.Frame();
	CHECK_EQ(builds, 2);
	CHECK(!scenes.RootOf("menu").IsVisible());
	CHECK(scenes.Current() == "options");
	CHECK(scenes.SwitchTo("menu"));
	h.Frame();
	CHECK_EQ(builds, 2); // aucune reconstruction
	CHECK(name.Text() == "Alice");
	CHECK_EQ(scenes.VisibleScenes().size(), size_t(1));
	CHECK(!scenes.SwitchTo("inconnue"));
	CHECK(scenes.Destroy("menu"));
	CHECK(!name.Valid());
	CHECK(scenes.Show("menu"));
	CHECK_EQ(builds, 3);
}

TEST(Chrome, TitleBarStatusBarAndHitTest) {
	Harness h;
	sdl3::Window window; // sans fenêtre réelle : seules la structure et la géométrie sont testées
	bool closed = false;
	ui::TitleBarOptions opts;
	opts.title = "Titre";
	opts.icons = false;
	opts.onClose = [&] { closed = true; };
	ui::Widget page = h.Mount(ui::Column().Width(640.f).Children({
		ui::TitleBar(window, opts).Width(640.f),
		ui::Box().Height(200.f),
		ui::StatusBar({"Prêt", true, false}).Width(640.f),
	}));
	ui::TitleBarWidgets tb = ui::TitleBarWidgets::From(page);
	ui::StatusBarWidgets sb = ui::StatusBarWidgets::From(page);
	REQUIRE(tb.root.Valid());
	CHECK(tb.title.Text() == "Titre");
	CHECK(tb.minimizeBtn.Valid() && tb.maximizeBtn.Valid() && tb.closeBtn.Valid());
	CHECK(!tb.appIcon.Valid());
	CHECK_EQ(tb.Buttons().size(), size_t(3));
	CHECK(Near(tb.root.Bounds().h, 36.f)); // hauteur du thème
	// Boutons poussés à droite par l'espace extensible.
	CHECK(tb.closeBtn.Bounds().x + tb.closeBtn.Bounds().w > 600.f);
	h.Click(tb.closeBtn);
	CHECK(closed);
	tb.SetTitle("Autre");
	CHECK(tb.title.Text() == "Autre");

	REQUIRE(sb.label.Valid());
	CHECK(sb.label.Text() == "Prêt");
	sb.SetText("Chargé", Some(ui::Rgba(0, 255, 0)));
	CHECK(sb.label.Text() == "Chargé");
	REQUIRE(sb.grip.Valid());

	ui::WindowChrome chrome;
	chrome.Attach(window, tb, &sb);
	const ui::HitTestCache &cache = chrome.Cache();
	CHECK(Near(cache.titleBar.w, 640.f));
	CHECK_EQ(cache.excluded.size(), size_t(3));
	CHECK_EQ(cache.grips.size(), size_t(1));
	const sdl3::FPoint close = Harness::Center(tb.closeBtn);
	CHECK(chrome.HitTest({int(close.x), int(close.y)}) == SDL_HITTEST_NORMAL);
	const sdl3::FRect title = tb.title.Bounds();
	CHECK(chrome.HitTest({int(title.x + 2.f), int(title.y + title.h * 0.5f)}) == SDL_HITTEST_DRAGGABLE);
	const sdl3::FPoint grip = Harness::Center(sb.grip);
	CHECK(chrome.HitTest({int(grip.x), int(grip.y)}) == SDL_HITTEST_RESIZE_BOTTOMRIGHT);
	chrome.Detach();
}

TEST(Chrome, FloatingPanelMovesAndResizes) {
	Harness h;
	ui::Widget panel, header, grip;
	h.Mount(ui::Panel().Absolute().Left(100.f).Top(80.f).Size(240.f, 160.f).Ref(panel).Children({
		ui::Row().Height(24.f).FillWidth().Ref(header).Children({ui::Label("Panneau")}),
		ui::Spacer(),
		ui::Label("⇲").Ref(grip),
	}));
	ui::MakeFloatingPanel(header, Some(grip));
	h.Frame();
	const sdl3::FRect r0 = panel.Bounds();
	const sdl3::FPoint hc = Harness::Center(header);
	h.Drag(hc.x, hc.y, hc.x + 50.f, hc.y + 20.f);
	const sdl3::FRect r1 = panel.Bounds();
	CHECK(Near(r1.x, r0.x + 50.f, 1.f));
	CHECK(Near(r1.y, r0.y + 20.f, 1.f));
	const sdl3::FPoint gc = Harness::Center(grip);
	h.Drag(gc.x, gc.y, gc.x + 40.f, gc.y + 30.f);
	const sdl3::FRect r2 = panel.Bounds();
	CHECK(Near(r2.w, r1.w + 40.f, 1.f));
	CHECK(Near(r2.h, r1.h + 30.f, 1.f));
}

TEST(ShaderEffect, FallsBackToNormalDrawingWithoutGpu) {
	Harness h;
	ui::Widget card = h.Mount(ui::Panel().Size(80.f, 40.f).With(ui::TintShift(ui::Rgba(255, 0, 0), 0.5f)));
	REQUIRE(card.Has<ui::ShaderEffect>());
	CHECK(card.Get<ui::ShaderEffect>().Unwrap()->kind == ui::ShaderEffectKind::TINT_SHIFT);
	ui::SetShaderEffect(card, Some(ui::GlowEffect(ui::Rgba(0, 0, 255), 0.2f)));
	CHECK(card.Get<ui::ShaderEffect>().Unwrap()->kind == ui::ShaderEffectKind::GLOW);
	h.gui.Render(); // sans renderer ni Canvas : aucun effet, aucun plantage
	ui::SetShaderEffect(card, NONE);
	CHECK(!card.Has<ui::ShaderEffect>());
}

TEST(Render, SoftwareRendererPaintsBoxesAndText) {
	auto ttf = sdl3::TtfContext::Create();
	auto surface = sdl3::Surface::Create(240, 120, sdl3::PixelFormat::RGBA32);
	REQUIRE(bool(surface));
	auto renderer = sdl3::Renderer::CreateSoftware(surface.Value());
	REQUIRE(bool(renderer));
	ecs::ArchetypeRegistry world;
	ui::Ui gui(world, renderer.Value(), {240.f, 120.f});
	gui.Mount(ui::Column().AlignItems(ui::Align::Start).Gap(0.f).Children({
		ui::Box().Size(100.f, 50.f).Background(ui::Rgba(255, 0, 0)),
		ui::Box().Size(100.f, 50.f).Background(ui::Rgba(0, 0, 255)).BorderRadius(8.f).Children({
			ui::Text("Ab").Color(ui::Rgba(255, 255, 255)),
		}),
	}));
	(void)renderer.Value().SetDrawColor(sdl3::Color{0, 0, 0, 255});
	(void)renderer.Value().Clear();
	gui.Render();
	(void)renderer.Value().Present();
	auto pixel = [&](int x, int y) {
		Uint8 r = 0, g = 0, b = 0, a = 0;
		SDL_ReadSurfacePixel(surface.Value().Get(), x, y, &r, &g, &b, &a);
		return sdl3::Color{r, g, b, a};
	};
	CHECK_EQ(int(pixel(50, 25).r), 255);
	CHECK_EQ(int(pixel(50, 25).b), 0);
	CHECK_EQ(int(pixel(90, 90).b), 255); // intérieur du second bloc
	CHECK_EQ(int(pixel(200, 25).r), 0);	 // hors des blocs : fond
	CHECK_EQ(int(pixel(0, 50).b), 0);	 // coin arrondi : fond
	if (ttf && gui.GetFonts().HasFamily("dejavu sans")) {
		// Le texte allume quelques pixels clairs dans le bloc bleu.
		int light = 0;
		for (int y = 50; y < 70; ++y)
			for (int x = 0; x < 30; ++x)
				if (pixel(x, y).r > 128)
					++light;
		CHECK(light > 5);
	}
}

TEST(Plot, PaletteColorsAreNormalized) {
	for (size_t i = 0; i < 10; ++i) {
		const sdl3::FColor c = ui::PlotPaletteColor(i);
		CHECK(c.r <= 1.f && c.g <= 1.f && c.b <= 1.f && c.a <= 1.f);
		CHECK(!(c.r == 1.f && c.g == 1.f && c.b == 1.f)); // aucune série blanche
	}
}
