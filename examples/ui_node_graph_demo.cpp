/**
 * ui_node_graph_demo — démonstration de ui::nodegraph (éditeur de node-graph
 * réutilisable, cf. src/ui/nodegraph.hpp), sous forme d'une petite
 * "calculatrice visuelle" façon Blueprint Unreal : deux nœuds d'opération
 * (Add/Multiply) connectés à un nœud de sortie (Output).
 *
 * Démontre toute la checklist d'acceptation du plan (section 11) :
 *   - grille de fond, pan (clic-milieu), zoom (molette vers le curseur)
 *   - nœuds déplaçables/redimensionnables, sélection simple/multiple (Ctrl),
 *     rectangle de sélection (marquee), déplacement de groupe
 *   - auto-scroll en glissant un nœud près du bord
 *   - pins de formes différentes (cercle/triangle), couleurs personnalisées
 *   - connexions Bézier ET orthogonale, épaisseur/couleur/flèches personnalisées
 *   - point intermédiaire sur une connexion (double-clic sur le tracé)
 *   - inner-zoom indépendant sur le nœud "Output" (contenu agrandi)
 *   - couper/copier/coller/supprimer (clavier) + menu contextuel (clic droit)
 *   - sauvegarde/chargement (Ctrl+S/Ctrl+O) via serializeGraph()/data::JsonDocument
 *   - rendu 100% personnalisable : le contenu des nœuds est fourni ici par
 *     l'application (inputNumber, labels), l'éditeur ne connaît que la boîte
 *     et l'en-tête.
 *
 *   make examples && ./build/examples/ui_node_graph_demo
 */
#include <format>
#include <iostream>
#include <memory>

#include "core/core.hpp"
#include "data/data.hpp"
#include "sdl3/sdl3.hpp"
#include "ui/ui.hpp"

static constexpr int WIN_W = 1100;
static constexpr int WIN_H = 700;
static constexpr float FONT_PT = 14.f;
static const char *savePath = "node_graph_demo.json";

int main() {
	// ── Init SDL / TTF ───────────────────────────────────────────────────────
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
	auto fontRes = sdl3::Font::FindLocal({"DejaVuSans", "FreeSans", "Arial"}, FONT_PT);
	if (!fontRes) {
		std::cerr << "Font: " << fontRes.Error().CStr() << "\n";
		return 1;
	}
	auto &font = fontRes.Value();

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
	ui::SdlRendererBackend renBackend(ren);
	auto engRes = sdl3::TextEngine::Create(ren);
	if (!engRes) {
		std::cerr << "TextEngine: " << engRes.Error().CStr() << "\n";
		return 1;
	}
	auto &eng = engRes.Value();

	// ── Pipeline UI — systèmes manuels (pas la façade ui::Ui) car
	// NodeGraphSystem doit s'intercaler entre layout et rendu (cf. en-tête de
	// src/ui/nodegraph.hpp pour l'ordre exact d'intégration). ──────────────
	ecs::ArchetypeRegistry ar;
	ui::LayoutSystem layout;
	ui::InputSystem input;
	ui::RenderSystem render;
	ui::StyleSystem style;
	ui::UiFactory f(ar, layout);
	ui::NodeGraphSystem nodeGraph;
	style.sheet = &f.sheet;

	window.StartTextInput();
	render.SetTextEngine(&eng, &font);
	nodeGraph.SetTextEngine(eng, font);
	layout.measureText = [&font](const String &s, float fs) -> sdl3::FPoint {
		if (auto sz = font.Measure(s); sz.IsSome())
			return {float(sz.Unwrap().x) * (fs / FONT_PT), fs * 1.35f};
		return {float(s.size()) * fs * 0.55f, fs * 1.3f};
	};

	// ── Encadrement de fenêtre ; l'aide clavier/souris va dans la barre d'état
	// Pas de fond sur la racine : la grille du graphe est dessinée AVANT
	// render.Run() et doit rester visible.
	ui::WindowFrame frame;
	frame.Build(f, layout, render, window,
				{.title = "ui::nodegraph - calculatrice visuelle", .appIcon = Some(ui::MaterialIcons::ACCOUNT_TREE),
				 .status = "Molette : zoom · Clic-milieu : pan · Ctrl+clic : multi-sélection · Glisser sur le vide : "
						   "marquee · Suppr/Ctrl+C/X/V : presse-papiers · Clic droit : menu · Ctrl+S/O : sauver/charger"});

	// ── Canvas : toute la zone de contenu ────────────────────────────────────
	auto canvas = ui::CanvasBuilder(f);
	canvas.GrowW().GrowH().Parent(frame.Content());
	ecs::Entity canvasE = canvas.Spawn();
	ui::AttachNodeGraphCanvas(ar, canvasE); // thème par défaut (Phase 7, style Blueprint) déjà appliqué

	// ── Nœuds "Add"/"Multiply" (une opération, deux entrées, une sortie) ────
	auto makeOpNode = [&](const char *title, sdl3::FPoint pos, sdl3::FColor header) {
		auto content = f.Column();
		content.Gap(4.f).Children(f.Label(title).FontSize(15.f),
								  f.InputNumber(0.f, 100.f, 1.f).GrowW(),
								  f.InputNumber(0.f, 100.f, 2.f).GrowW());
		ecs::Entity e = ui::AddGraphNode(f, canvasE, title, pos, {190.f, 130.f}, std::move(content), header);
		(void)ui::AddGraphPin(ar, e, ui::PinSide::Left, 0.3f, ui::PinShape::CIRCLE, {235, 235, 235, 255});
		(void)ui::AddGraphPin(ar, e, ui::PinSide::Left, 0.7f, ui::PinShape::CIRCLE, {235, 235, 235, 255});
		ecs::Entity out = ui::AddGraphPin(ar, e, ui::PinSide::Right, 0.5f, ui::PinShape::TRIANGLE, header);
		return std::pair{e, out};
	};
	auto [addNode, addOut] = makeOpNode("Add", {60.f, 60.f}, sdl3::FColor{52/255.f, 120/255.f, 90/255.f, 255/255.f});
	auto [mulNode, mulOut] = makeOpNode("Multiply", {60.f, 260.f}, sdl3::FColor{150/255.f, 90/255.f, 40/255.f, 255/255.f});

	// ── Nœud "Output" — contenu agrandi via inner-zoom (spec section 7) ─────
	auto outContent = f.Column();
	outContent.Gap(6.f).Children(f.Label("Output").FontSize(15.f), f.Label("= ?").FontSize(18.f));
	ecs::Entity outNode = ui::AddGraphNode(f, canvasE, "Output", {430.f, 150.f}, {170.f, 110.f}, std::move(outContent),
										  sdl3::FColor::UI_NODE_HEADER_NEUTRAL(), /*innerZoom=*/1.3f);
	ecs::Entity outInA = ui::AddGraphPin(ar, outNode, ui::PinSide::Left, 0.3f, ui::PinShape::CIRCLE, {235, 235, 235, 255});
	ecs::Entity outInB = ui::AddGraphPin(ar, outNode, ui::PinSide::Left, 0.7f, ui::PinShape::CIRCLE, {235, 235, 235, 255});

	style.Resolve(ar);
	nodeGraph.Prepass(ar);
	layout.RunIfNeeded(ar, float(WIN_W), float(WIN_H));
	nodeGraph.UpdatePins(ar);

	// Connexion Add -> Output : Bézier par défaut, flèche, un point intermédiaire.
	if (auto *c1 = ui::ConnectPins(ar, addOut, outInA)) {
		c1->color = sdl3::FColor{130/255.f, 220/255.f, 170/255.f, 255/255.f};
		ui::InsertWaypoint(*c1, 0, {380.f, 90.f});
	}
	// Connexion Multiply -> Output : orthogonale, plus épaisse, couleur assortie.
	if (auto *c2 = ui::ConnectPins(ar, mulOut, outInB)) {
		c2->style = ui::ConnectionStyle::ORTHOGONAL;
		c2->color = sdl3::FColor{230/255.f, 160/255.f, 90/255.f, 255/255.f};
		c2->thickness = 3.5f;
	}

	// ── Menu contextuel (clic droit) : Supprimer / Dupliquer ────────────────
	// cf. NodeGraphCallbacks::onContextMenu — l'éditeur détecte seulement le
	// clic droit et le nœud visé ; ce popup est entièrement construit ici,
	// côté application (Décision d'architecture, spec section 9).
	auto rightClicked = std::make_shared<ecs::Entity>();

	auto delBtn = f.Button("Supprimer");
	delBtn.OnClick([&ar, &layout, rightClicked] {
		if (rightClicked->Valid()) {
			ui::DestroyGraphNode(ar, *rightClicked);
			*rightClicked = ecs::Entity{};
			layout.MarkDirty();
		}
	});
	auto dupBtn = f.Button("Dupliquer");
	dupBtn.OnClick([&ar, &f, &nodeGraph, &canvasE, rightClicked] {
		if (!rightClicked->Valid())
			return;
		nodeGraph.SelectNode(ar, *rightClicked);
		nodeGraph.CopySelection(ar, canvasE);
		if (auto n = ar.GetComponent<ui::UiGraphNode>(*rightClicked); n.IsSome()) {
			sdl3::FPoint p = n.Unwrap()->canvasPos;
			nodeGraph.PasteClipboard(f, canvasE, {p.x + 40.f, p.y + 40.f});
		}
	});
	auto ctxMenu = f.Popup();
	ctxMenu.Gap(2.f).Pad(4.f).Children(std::move(delBtn), std::move(dupBtn));
	ecs::Entity ctxMenuE = ctxMenu.Spawn();

	ui::NodeGraphCallbacks cb;
	cb.onContextMenu = [&ar, &layout, ctxMenuE, rightClicked](ecs::Entity node, ecs::Entity, sdl3::FPoint screenPos) {
		*rightClicked = node;
		if (auto rect = ar.GetComponent<ui::UiRect>(ctxMenuE); rect.IsSome())
			rect.Unwrap()->offset = {screenPos.x, screenPos.y};
		ui::OpenPopup(ar, layout, ctxMenuE, ctxMenuE);
	};
	ar.AddComponent(canvasE, std::move(cb));

	// ── Sauvegarde / chargement (Ctrl+S / Ctrl+O) ───────────────────────────
	auto saveGraph = [&ar, canvasE] {
		data::JsonDocument json;
		json.SetRoot(ui::SerializeGraph(ar, canvasE));
		if (auto io = sdl3::IOStream::FromFile(savePath, "wb"); io) {
			if (json.Encode(io.Value()))
				std::cout << "Graphe sauvegarde: " << savePath << "\n";
			else
				std::cerr << "Echec d'ecriture: " << savePath << "\n";
		} else {
			std::cerr << "Echec sauvegarde: " << io.Error().CStr() << "\n";
		}
	};
	auto loadGraph = [&ar, &f, &nodeGraph, &layout, canvasE] {
		auto io = sdl3::IOStream::FromFile(savePath, "rb");
		if (!io) {
			std::cerr << "Echec chargement: " << io.Error().CStr() << "\n";
			return;
		}
		data::JsonDocument json;
		if (json.Decode(io.Value()).IsSome()) {
			std::cerr << "JSON invalide dans " << savePath << "\n";
			return;
		}
		ui::DeserializeGraph(f, nodeGraph, layout, canvasE, json.GetRoot());
		std::cout << "Graphe charge: " << savePath << "\n";
	};

	// ── Boucle ───────────────────────────────────────────────────────────────
	bool running = true;
	uint64_t lastTick = sdl3::GetTicksMS();

	while (running) {
		uint64_t now = sdl3::GetTicksMS();
		float dt = float(now - lastTick) / 1000.f;
		lastTick = now;

		nodeGraph.Prepass(ar);
		layout.RunIfNeeded(ar, float(window.GetSize().x), float(window.GetSize().y));
		nodeGraph.UpdatePins(ar);

		while (auto ev = sdl3::PollEvent()) {
			auto &e = ev.Value();
			if (e.IsQuit()) {
				running = false;
				break;
			}
			if (e.IsKeyDown(SDLK_ESCAPE) && !input.HasOpenModal()) {
				running = false;
				break;
			}
			bool ctrl = (sdl3::keyboard::Mods() & SDL_KMOD_CTRL) != 0;
			if (ctrl && e.IsKeyDown(SDLK_S)) {
				saveGraph();
			} else if (ctrl && e.IsKeyDown(SDLK_O)) {
				loadGraph();
			} else if (ctrl && e.IsKeyDown(SDLK_V)) {
				nodeGraph.PasteClipboard(f, canvasE, {200.f, 400.f});
			}
			input.HandleEvent(ar, e, layout);
			nodeGraph.HandleEvent(ar, e, layout);
		}
		input.Tick(ar, layout, dt);
		nodeGraph.Tick(ar, layout, dt);
		frame.Update();
		if (frame.CloseRequested())
			running = false;

		style.Resolve(ar);
		layout.RunIfNeeded(ar, float(window.GetSize().x), float(window.GetSize().y));

		ren.SetDrawColor(sdl3::FColor{16 / 255.f, 17 / 255.f, 22 / 255.f, 1.f});
		ren.Clear();
		nodeGraph.RenderGrid(ar, renBackend);
		nodeGraph.RenderConnections(ar, renBackend);
		render.Run(ar, renBackend, &input.tooltip);
		nodeGraph.RenderHeaders(ar, renBackend);
		nodeGraph.RenderPins(ar, renBackend);
		nodeGraph.RenderMarquee(renBackend);
		ren.Present();
	}
	return 0;
}
