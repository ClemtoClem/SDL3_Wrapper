/**
 * ui_node_graph_demo — démonstration de ui::nodegraph (éditeur de graphes de
 * nœuds), sous forme d'une petite « calculatrice visuelle » façon Blueprint :
 * deux nœuds d'opération (Add / Multiply) reliés à un nœud de sortie.
 *
 *   - grille de fond, pan (clic milieu), zoom (molette vers le curseur)
 *   - nœuds déplaçables / redimensionnables, sélection simple / multiple
 *     (Ctrl), rectangle de sélection, déplacement de groupe
 *   - pins de formes différentes, couleurs personnalisées
 *   - connexions Bézier ET orthogonale, épaisseur / couleur / flèches
 *   - point intermédiaire sur une connexion (double clic sur le tracé)
 *   - zoom intérieur indépendant sur le nœud « Output »
 *   - couper / copier / coller / supprimer (clavier, canevas focalisé) et
 *     menu contextuel (clic droit sur un nœud)
 *   - sauvegarde / chargement (Ctrl+S / Ctrl+O) en JSON
 *   - le contenu des nœuds est un arbre de widgets ordinaire fourni par
 *     l'application ; en-têtes et corps se stylent en CSS.
 *
 *   cpp-maker run ui_node_graph_demo
 */
#include <iostream>

#include <core/core.hpp>
#include <dataio/dataio.hpp>
#include <sdl3/sdl3.hpp>
#include <ui/ui.hpp>

static constexpr int WIN_W = 1100;
static constexpr int WIN_H = 700;
static constexpr const char *SAVE_PATH = "node_graph_demo.json";

int main() {
	auto sdl = sdl3::SdlContext::Create(sdl3::init_flags::VIDEO | sdl3::init_flags::EVENTS);
	if (!sdl) {
		std::cerr << "SDL init: " << sdl.Error().CStr() << "\n";
		return 1;
	}
	auto ttf = sdl3::TtfContext::Create();
	if (!ttf) {
		std::cerr << "TTF init: " << ttf.Error().CStr() << "\n";
		return 1;
	}
	auto winRes =
		sdl3::Window::Create(u8"ui::nodegraph - calculatrice visuelle", WIN_W, WIN_H, ui::WindowFrame::WINDOW_FLAGS);
	if (!winRes) {
		std::cerr << "Window: " << winRes.Error().CStr() << "\n";
		return 1;
	}
	auto &window = winRes.Value();
	auto renRes = sdl3::Renderer::Create(window);
	if (!renRes) {
		std::cerr << "Renderer: " << renRes.Error().CStr() << "\n";
		return 1;
	}
	auto &ren = renRes.Value();

	ecs::ArchetypeRegistry world;
	ui::Ui gui(world, window, ren);

	ui::WindowFrame frame;
	frame.Build(gui, window,
				{.title = "ui::nodegraph - calculatrice visuelle", .appIcon = Some(ui::MaterialIcons::ACCOUNT_TREE),
				 .status = "Molette : zoom · Clic milieu : pan · Ctrl+clic : multi-sélection · Glisser sur le vide : "
						   "rectangle · Suppr/Ctrl+C/X/V : presse-papiers · Clic droit : menu · Ctrl+S/O : "
						   "sauver/charger"});

	// ── Canevas : toute la zone de contenu ───────────────────────────────────
	ui::Widget canvas = frame.Content().Append(ui::NodeGraph().Grow().FillWidth());

	const ui::Color pinWhite = ui::Rgba(235, 235, 235);
	auto makeOpNode = [&](const char *title, sdl3::FPoint pos, ui::Color header) {
		ui::Widget node = ui::AddGraphNode(canvas, title, pos, {190.f, 130.f},
										   ui::Column().Gap(4.f).Children({
											   ui::Label(title).FontSize(15.f),
											   ui::NumberInput(0.f, 100.f, 1.f).FillWidth(),
											   ui::NumberInput(0.f, 100.f, 2.f).FillWidth(),
										   }),
										   header);
		ui::AddGraphPin(node, ui::PinSide::Left, 0.3f, ui::PinShape::CIRCLE, pinWhite);
		ui::AddGraphPin(node, ui::PinSide::Left, 0.7f, ui::PinShape::CIRCLE, pinWhite);
		return ui::AddGraphPin(node, ui::PinSide::Right, 0.5f, ui::PinShape::TRIANGLE, header);
	};
	ui::Widget addOut = makeOpNode("Add", {60.f, 60.f}, ui::Rgba(52, 120, 90));
	ui::Widget mulOut = makeOpNode("Multiply", {60.f, 260.f}, ui::Rgba(150, 90, 40));

	// Nœud « Output » : contenu agrandi par le zoom intérieur.
	ui::Widget outNode = ui::AddGraphNode(canvas, "Output", {430.f, 150.f}, {170.f, 110.f},
										  ui::Column().Gap(6.f).Children({
											  ui::Label("Output").FontSize(15.f),
											  ui::Label("= ?").FontSize(18.f),
										  }),
										  ui::Rgba(70, 74, 86), 1.3f);
	ui::Widget outInA = ui::AddGraphPin(outNode, ui::PinSide::Left, 0.3f, ui::PinShape::CIRCLE, pinWhite);
	ui::Widget outInB = ui::AddGraphPin(outNode, ui::PinSide::Left, 0.7f, ui::PinShape::CIRCLE, pinWhite);

	// Add -> Output : Bézier, flèche, un point intermédiaire.
	if (ui::GraphConnection *c1 = ui::ConnectPins(world, addOut.Entity(), outInA.Entity())) {
		c1->color = ui::Rgba(130, 220, 170);
		ui::InsertWaypoint(*c1, 0, {380.f, 90.f});
	}
	// Multiply -> Output : orthogonale, plus épaisse.
	if (ui::GraphConnection *c2 = ui::ConnectPins(world, mulOut.Entity(), outInB.Entity())) {
		c2->style = ui::ConnectionStyle::ORTHOGONAL;
		c2->color = ui::Rgba(230, 160, 90);
		c2->thickness = 3.5f;
	}

	// ── Menu contextuel (clic droit sur un nœud) : construit par l'application ─
	ecs::Entity rightClicked{};
	ui::Widget menu = gui.Mount(ui::ContextMenu().Children({
		ui::MenuItem("Supprimer", "Suppr").OnClick([&] {
			if (world.IsAlive(rightClicked))
				ui::DestroyGraphNode(world, rightClicked);
			rightClicked = ecs::Entity{};
		}),
		ui::MenuItem("Dupliquer").OnClick([&] {
			auto node = world.GetComponent<ui::GraphNode>(rightClicked);
			if (node.IsNone())
				return;
			const sdl3::FPoint p = node.Unwrap()->canvasPos;
			ui::ClearGraphSelection(world, canvas.Entity());
			ui::SelectNode(world, rightClicked);
			ui::CopyGraphSelection(world, canvas.Entity());
			ui::PasteGraphClipboard(canvas, {p.x + 40.f, p.y + 40.f});
		}),
	}));
	ui::GraphCallbacks callbacks;
	callbacks.onContextMenu = [&](ecs::Entity node, ecs::Entity, sdl3::FPoint at) {
		if (!world.IsAlive(node))
			return;
		rightClicked = node;
		menu.OpenAt(at);
	};
	ui::SetGraphCallbacks(canvas, std::move(callbacks));

	// ── Sauvegarde / chargement ─────────────────────────────────────────────
	auto saveGraph = [&] {
		dataio::JsonDocument json;
		json.SetRoot(ui::SerializeGraph(world, canvas.Entity()));
		auto io = sdl3::IOStream::FromFile(SAVE_PATH, "wb");
		if (io && json.Encode(io.Value()))
			frame.SetStatus(String("Graphe sauvegardé : ") + SAVE_PATH);
		else
			frame.SetStatus(String("Échec de la sauvegarde : ") + SAVE_PATH);
	};
	auto loadGraph = [&] {
		auto io = sdl3::IOStream::FromFile(SAVE_PATH, "rb");
		if (!io) {
			frame.SetStatus(String("Échec du chargement : ") + io.Error().CStr());
			return;
		}
		dataio::JsonDocument json;
		if (json.Decode(io.Value()).IsSome()) {
			frame.SetStatus(String("JSON invalide : ") + SAVE_PATH);
			return;
		}
		for (ecs::Entity n : ui::NodesOf(world, canvas.Entity()))
			ui::DestroyGraphNode(world, n);
		ui::DeserializeGraph(canvas, json.GetRoot());
		frame.SetStatus(String("Graphe chargé : ") + SAVE_PATH);
	};

	bool running = true;
	uint64_t lastTick = sdl3::GetTicksMS();
	while (running) {
		const uint64_t now = sdl3::GetTicksMS();
		const float dt = float(now - lastTick) / 1000.f;
		lastTick = now;

		while (auto ev = sdl3::PollEvent()) {
			auto &e = ev.Value();
			if (e.IsQuit()) {
				running = false;
				break;
			}
			const bool ctrl = (sdl3::keyboard::Mods() & SDL_KMOD_CTRL) != 0;
			if (ctrl && e.IsKeyDown(SDLK_S)) {
				saveGraph();
				continue;
			}
			if (ctrl && e.IsKeyDown(SDLK_O)) {
				loadGraph();
				continue;
			}
			if (gui.HandleEvent(e))
				continue;
			if (e.IsKeyDown(SDLK_ESCAPE))
				running = false;
		}
		gui.Update(dt);
		frame.Update();
		if (frame.CloseRequested())
			running = false;

		ren.SetDrawColor(gui.CurrentPalette().appBg);
		ren.Clear();
		gui.Render();
		ren.Present();
	}
	return 0;
}
