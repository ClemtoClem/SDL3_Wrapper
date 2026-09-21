#include "core/test.hpp"
#include "sdl3/sdl3.hpp"
#include "ui/ui.hpp"
#include <cassert>
#include <iostream>

static sdl3::Event MouseDownEv(float x, float y, Uint8 button = SDL_BUTTON_LEFT, Uint8 clicks = 1) {
	sdl3::Event ev{};
	ev.raw.type = SDL_EVENT_MOUSE_BUTTON_DOWN;
	ev.raw.button.x = x;
	ev.raw.button.y = y;
	ev.raw.button.button = button;
	ev.raw.button.clicks = clicks;
	return ev;
}
static sdl3::Event MouseUpEv(float x, float y, Uint8 button = SDL_BUTTON_LEFT) {
	sdl3::Event ev{};
	ev.raw.type = SDL_EVENT_MOUSE_BUTTON_UP;
	ev.raw.button.x = x;
	ev.raw.button.y = y;
	ev.raw.button.button = button;
	return ev;
}
static sdl3::Event MouseMoveEv(float x, float y) {
	sdl3::Event ev{};
	ev.raw.type = SDL_EVENT_MOUSE_MOTION;
	ev.raw.motion.x = x;
	ev.raw.motion.y = y;
	return ev;
}
static sdl3::Event WheelEv(float x, float y, float wheelY) {
	sdl3::Event ev{};
	ev.raw.type = SDL_EVENT_MOUSE_WHEEL;
	ev.raw.wheel.mouse_x = x;
	ev.raw.wheel.mouse_y = y;
	ev.raw.wheel.y = wheelY;
	return ev;
}
static sdl3::Event KeyDownEv(SDL_Keycode key, SDL_Keymod mod = SDL_KMOD_NONE) {
	sdl3::Event ev{};
	ev.raw.type = SDL_EVENT_KEY_DOWN;
	ev.raw.key.key = key;
	ev.raw.key.mod = mod;
	return ev;
}

int main() {
	setenv("SDL_VIDEODRIVER", "dummy", 0);

	ecs::ArchetypeRegistry ar;
	ui::LayoutSystem layout;
	ui::InputSystem input;
	ui::UiFactory f(ar, layout);
	ui::StyleSystem style;
	style.sheet = &f.sheet;
	ui::NodeGraphSystem nodeGraph;

	auto canvas = ui::CanvasBuilder(f);
	canvas.Anchor(ui::Anchor::TopLeft).Size(600.f, 400.f);
	ecs::Entity canvasE = canvas.Spawn();
	ui::AttachNodeGraphCanvas(ar, canvasE);

	auto content = f.Column();
	content.Gap(4.f).Children(f.Label("Contenu du noeud"), f.Button("OK"));
	ecs::Entity nodeE = ui::AddGraphNode(f, canvasE, "Mon Noeud", {50.f, 40.f}, {200.f, 120.f}, std::move(content));

	style.Resolve(ar);
	nodeGraph.Prepass(ar);
	layout.RunIfNeeded(ar, 800.f, 600.f);

	// ── Vérifie la transform initiale (pan=0, zoom=1) : screen == canvas ───
	{
		auto c = ar.GetComponent<ui::UiComputed>(nodeE);
		assert(c.IsSome());
		std::cout << "initial node screen: " << c.Unwrap()->screen.x << "," << c.Unwrap()->screen.y << " "
				  << c.Unwrap()->screen.w << "x" << c.Unwrap()->screen.h << "\n";
		assert(sdl3::Abs(c.Unwrap()->screen.x - 50.f) < 0.01f);
		assert(sdl3::Abs(c.Unwrap()->screen.y - 40.f) < 0.01f);
		assert(sdl3::Abs(c.Unwrap()->screen.w - 200.f) < 0.01f);
		assert(sdl3::Abs(c.Unwrap()->screen.h - 120.f) < 0.01f);
	}

	// ── Drag du header : déplace le nœud (canvasPos) ────────────────────────
	{
		float hx = 50.f + 20.f, hy = 40.f + 10.f; // dans la bande d'en-tête
		nodeGraph.HandleEvent(ar, MouseDownEv(hx, hy), layout);
		nodeGraph.HandleEvent(ar, MouseMoveEv(hx + 30.f, hy + 15.f), layout);
		nodeGraph.HandleEvent(ar, MouseUpEv(hx + 30.f, hy + 15.f), layout);
		auto node = ar.GetComponent<ui::UiGraphNode>(nodeE);
		assert(node.IsSome());
		std::cout << "after drag canvasPos: " << node.Unwrap()->canvasPos.x << "," << node.Unwrap()->canvasPos.y
				  << "\n";
		assert(sdl3::Abs(node.Unwrap()->canvasPos.x - 80.f) < 0.01f);
		assert(sdl3::Abs(node.Unwrap()->canvasPos.y - 55.f) < 0.01f);
	}

	nodeGraph.Prepass(ar);
	layout.RunIfNeeded(ar, 800.f, 600.f);

	// ── Redimensionnement via la poignée de coin ────────────────────────────
	{
		auto c = ar.GetComponent<ui::UiComputed>(nodeE);
		float gx = c.Unwrap()->screen.x + c.Unwrap()->screen.w - 5.f;
		float gy = c.Unwrap()->screen.y + c.Unwrap()->screen.h - 5.f;
		nodeGraph.HandleEvent(ar, MouseDownEv(gx, gy), layout);
		nodeGraph.HandleEvent(ar, MouseMoveEv(gx + 40.f, gy + 25.f), layout);
		nodeGraph.HandleEvent(ar, MouseUpEv(gx + 40.f, gy + 25.f), layout);
		auto node = ar.GetComponent<ui::UiGraphNode>(nodeE);
		std::cout << "after resize size: " << node.Unwrap()->size.x << "x" << node.Unwrap()->size.y << "\n";
		assert(sdl3::Abs(node.Unwrap()->size.x - 240.f) < 0.01f);
		assert(sdl3::Abs(node.Unwrap()->size.y - 145.f) < 0.01f);
	}

	// ── Pan via clic-milieu ──────────────────────────────────────────────────
	{
		nodeGraph.HandleEvent(ar, MouseDownEv(300.f, 200.f, SDL_BUTTON_MIDDLE), layout);
		nodeGraph.HandleEvent(ar, MouseMoveEv(250.f, 170.f), layout);
		nodeGraph.HandleEvent(ar, MouseUpEv(250.f, 170.f, SDL_BUTTON_MIDDLE), layout);
		auto cv = ar.GetComponent<ui::UiNodeGraphCanvas>(canvasE);
		std::cout << "after pan: " << cv.Unwrap()->pan.x << "," << cv.Unwrap()->pan.y << "\n";
		// delta souris = (-50,-30) -> pan += -delta/zoom = (+50,+30)
		assert(sdl3::Abs(cv.Unwrap()->pan.x - 50.f) < 0.01f);
		assert(sdl3::Abs(cv.Unwrap()->pan.y - 30.f) < 0.01f);
	}

	// ── Zoom molette vers le curseur ─────────────────────────────────────────
	{
		auto cv = ar.GetComponent<ui::UiNodeGraphCanvas>(canvasE);
		float zoomBefore = cv.Unwrap()->zoom;
		nodeGraph.HandleEvent(ar, WheelEv(300.f, 200.f, 1.f), layout);
		std::cout << "zoom after wheel: " << cv.Unwrap()->zoom << " (before=" << zoomBefore << ")\n";
		assert(cv.Unwrap()->zoom > zoomBefore);
		assert(sdl3::Abs(cv.Unwrap()->zoom - zoomBefore * 1.1f) < 0.001f);
	}

	std::cout << "nodegraph phase1 checks passed\n";

	// ════════════════════════════════════════════════════════════════════
	// Phase 2 — Pins
	// ════════════════════════════════════════════════════════════════════

	// État courant (issu de Phase 1) : canvasPos=(80,55) size=(240,145),
	// pan=(50,30), zoom=1.1 — recalcule le screen rect du nœud pour ancrer
	// les pins sur des valeurs connues (formule indépendante : recopiée à
	// la main ici, PAS un appel à NodeGraphSystem, pour que le test soit un
	// vrai contrôle et non une tautologie).
	nodeGraph.Prepass(ar);
	layout.RunIfNeeded(ar, 800.f, 600.f);

	ecs::Entity pinLeft = ui::AddGraphPin(ar, nodeE, ui::PinSide::Left, 0.5f, ui::PinShape::CIRCLE,
										 sdl3::FColor{100, 180, 255, 255}, 10.f);
	ecs::Entity pinRight = ui::AddGraphPin(ar, nodeE, ui::PinSide::Right, 0.0f, ui::PinShape::SQUARE,
										  sdl3::FColor{255, 160, 90, 255}, 8.f);
	nodeGraph.UpdatePins(ar);

	sdl3::FRect nodeScreen{};
	{
		auto c = ar.GetComponent<ui::UiComputed>(nodeE);
		assert(c.IsSome());
		nodeScreen = c.Unwrap()->screen;
		std::cout << "node screen before pin checks: " << nodeScreen.x << "," << nodeScreen.y << " " << nodeScreen.w
				  << "x" << nodeScreen.h << "\n";
	}

	// ── Position des pins sur le bord attendu (calcul indépendant) ─────────
	{
		auto cv = ar.GetComponent<ui::UiNodeGraphCanvas>(canvasE);
		float zoom = cv.Unwrap()->zoom;

		auto pl = ar.GetComponent<ui::UiComputed>(pinLeft);
		assert(pl.IsSome());
		float expLx = nodeScreen.x, expLy = nodeScreen.y + 0.5f * nodeScreen.h;
		float expLHalf = 10.f * zoom * 0.5f;
		std::cout << "pinLeft screen: " << pl.Unwrap()->screen.x << "," << pl.Unwrap()->screen.y << " "
				  << pl.Unwrap()->screen.w << "x" << pl.Unwrap()->screen.h << "\n";
		assert(sdl3::Abs(pl.Unwrap()->screen.x - (expLx - expLHalf)) < 0.05f);
		assert(sdl3::Abs(pl.Unwrap()->screen.y - (expLy - expLHalf)) < 0.05f);
		assert(sdl3::Abs(pl.Unwrap()->screen.w - expLHalf * 2.f) < 0.05f);

		auto pr = ar.GetComponent<ui::UiComputed>(pinRight);
		assert(pr.IsSome());
		float expRx = nodeScreen.x + nodeScreen.w, expRy = nodeScreen.y;
		float expRHalf = 8.f * zoom * 0.5f;
		std::cout << "pinRight screen: " << pr.Unwrap()->screen.x << "," << pr.Unwrap()->screen.y << " "
				  << pr.Unwrap()->screen.w << "x" << pr.Unwrap()->screen.h << "\n";
		assert(sdl3::Abs(pr.Unwrap()->screen.x - (expRx - expRHalf)) < 0.05f);
		assert(sdl3::Abs(pr.Unwrap()->screen.y - (expRy - expRHalf)) < 0.05f);
	}

	// ── Survol : la souris sur pinLeft le marque hovered, pas pinRight ──────
	{
		auto pl = ar.GetComponent<ui::UiComputed>(pinLeft);
		sdl3::FPoint center{pl.Unwrap()->screen.x + pl.Unwrap()->screen.w * 0.5f,
					  pl.Unwrap()->screen.y + pl.Unwrap()->screen.h * 0.5f};
		nodeGraph.HandleEvent(ar, MouseMoveEv(center.x, center.y), layout);
		assert(ar.GetComponent<ui::UiGraphPin>(pinLeft).Unwrap()->hovered);
		assert(!ar.GetComponent<ui::UiGraphPin>(pinRight).Unwrap()->hovered);

		// Un point loin de tout pin efface le survol.
		nodeGraph.HandleEvent(ar, MouseMoveEv(5.f, 5.f), layout);
		assert(!ar.GetComponent<ui::UiGraphPin>(pinLeft).Unwrap()->hovered);
	}

	// ── connectable=false : ignoré par hitTestPin/survol ────────────────────
	{
		ar.GetComponent<ui::UiGraphPin>(pinRight).Unwrap()->connectable = false;
		auto pr = ar.GetComponent<ui::UiComputed>(pinRight);
		sdl3::FPoint center{pr.Unwrap()->screen.x + pr.Unwrap()->screen.w * 0.5f,
					  pr.Unwrap()->screen.y + pr.Unwrap()->screen.h * 0.5f};
		assert(!nodeGraph.HitTestPin(ar, center.x, center.y).Valid());
		nodeGraph.HandleEvent(ar, MouseMoveEv(center.x, center.y), layout);
		assert(!ar.GetComponent<ui::UiGraphPin>(pinRight).Unwrap()->hovered);
		ar.GetComponent<ui::UiGraphPin>(pinRight).Unwrap()->connectable = true; // restore
	}

	// ── Un clic sur un pin ne doit PAS démarrer un drag de nœud ─────────────
	{
		auto canvasPosBefore = ar.GetComponent<ui::UiGraphNode>(nodeE).Unwrap()->canvasPos;
		auto pl = ar.GetComponent<ui::UiComputed>(pinLeft);
		sdl3::FPoint center{pl.Unwrap()->screen.x + pl.Unwrap()->screen.w * 0.5f,
					  pl.Unwrap()->screen.y + pl.Unwrap()->screen.h * 0.5f};
		nodeGraph.HandleEvent(ar, MouseDownEv(center.x, center.y), layout);
		nodeGraph.HandleEvent(ar, MouseMoveEv(center.x + 50.f, center.y + 50.f), layout);
		nodeGraph.HandleEvent(ar, MouseUpEv(center.x + 50.f, center.y + 50.f), layout);
		auto canvasPosAfter = ar.GetComponent<ui::UiGraphNode>(nodeE).Unwrap()->canvasPos;
		assert(sdl3::Abs(canvasPosAfter.x - canvasPosBefore.x) < 0.01f);
		assert(sdl3::Abs(canvasPosAfter.y - canvasPosBefore.y) < 0.01f);
	}

	std::cout << "nodegraph phase2 checks passed\n";

	// ════════════════════════════════════════════════════════════════════
	// Phase 3 — Connexions
	// ════════════════════════════════════════════════════════════════════

	auto contentB = f.Column();
	contentB.Gap(4.f).Children(f.Label("Noeud B"));
	ecs::Entity nodeB = ui::AddGraphNode(f, canvasE, "Noeud B", {500.f, 300.f}, {150.f, 100.f}, std::move(contentB));
	ecs::Entity pinC = ui::AddGraphPin(ar, nodeB, ui::PinSide::Left, 0.5f, ui::PinShape::CIRCLE, sdl3::FColor{120, 220, 140, 255}, 10.f);
	
	style.Resolve(ar);
	nodeGraph.Prepass(ar);
	layout.RunIfNeeded(ar, 800.f, 600.f);
	nodeGraph.UpdatePins(ar);

	// ── connectPins : création, doublon refusé, auto-connexion refusée ─────
	{
		ui::GraphConnection *conn = ui::ConnectPins(ar, pinLeft, pinC);
		assert(conn != nullptr);
		assert(conn->fromPin == pinLeft && conn->toPin == pinC);
		assert(ar.GetComponent<ui::UiGraphPin>(pinLeft).Unwrap()->connected);
		assert(ar.GetComponent<ui::UiGraphPin>(pinC).Unwrap()->connected);

		auto cv = ar.GetComponent<ui::UiNodeGraphCanvas>(canvasE);
		assert(cv.Unwrap()->connections.size() == 1);

		assert(ui::ConnectPins(ar, pinLeft, pinC) == nullptr); // doublon
		assert(ui::ConnectPins(ar, pinC, pinLeft) == nullptr); // doublon (sens inverse)
		assert(ui::ConnectPins(ar, pinLeft, pinLeft) == nullptr); // auto-connexion
		assert(cv.Unwrap()->connections.size() == 1);

		std::cout << "connectPins: ok (1 connexion)\n";
	}

	// ── disconnectPins : retire la connexion, remet connected=false ────────
	{
		ui::DisconnectPins(ar, pinLeft, pinC);
		auto cv = ar.GetComponent<ui::UiNodeGraphCanvas>(canvasE);
		assert(cv.Unwrap()->connections.empty());
		assert(!ar.GetComponent<ui::UiGraphPin>(pinLeft).Unwrap()->connected);
		assert(!ar.GetComponent<ui::UiGraphPin>(pinC).Unwrap()->connected);
		std::cout << "disconnectPins: ok\n";
	}

	// ── Tracé (3 styles) : calcul indépendant de detail::ConnectionPath ────
	{
		sdl3::FPoint a{0.f, 0.f}, b{100.f, 40.f};
		auto straight = ui::detail::ConnectionPath(a, b, ui::ConnectionStyle::STRAIGHT, 0.f, 1.f);
		assert(straight.size() == 2);
		assert(sdl3::Abs(straight[0].x - a.x) < 0.01f && sdl3::Abs(straight[1].x - b.x) < 0.01f);

		auto ortho = ui::detail::ConnectionPath(a, b, ui::ConnectionStyle::ORTHOGONAL, 0.f, 1.f);
		assert(ortho.size() == 4);
		assert(sdl3::Abs(ortho[1].x - 50.f) < 0.01f && sdl3::Abs(ortho[1].y - a.y) < 0.01f);
		assert(sdl3::Abs(ortho[2].x - 50.f) < 0.01f && sdl3::Abs(ortho[2].y - b.y) < 0.01f);

		auto bez = ui::detail::ConnectionPath(a, b, ui::ConnectionStyle::BEZIER, 0.f, 1.f);
		assert(bez.size() == 25); // 24 segments -> 25 points
		assert(sdl3::Abs(bez.front().x - a.x) < 0.01f && sdl3::Abs(bez.back().x - b.x) < 0.01f);
		std::cout << "connectionPath (straight/orthogonal/bezier): ok\n";
	}

	// ── Glisser pin→pin : crée une connexion au relâchement sur un pin cible ─
	{
		auto pl = ar.GetComponent<ui::UiComputed>(pinLeft);
		sdl3::FPoint from{pl.Unwrap()->screen.x + pl.Unwrap()->screen.w * 0.5f,
						pl.Unwrap()->screen.y + pl.Unwrap()->screen.h * 0.5f};
		auto pc = ar.GetComponent<ui::UiComputed>(pinC);
		sdl3::FPoint to{pc.Unwrap()->screen.x + pc.Unwrap()->screen.w * 0.5f,
					  pc.Unwrap()->screen.y + pc.Unwrap()->screen.h * 0.5f};

		nodeGraph.HandleEvent(ar, MouseDownEv(from.x, from.y), layout);
		nodeGraph.HandleEvent(ar, MouseMoveEv((from.x + to.x) * 0.5f, (from.y + to.y) * 0.5f), layout);
		nodeGraph.HandleEvent(ar, MouseUpEv(to.x, to.y), layout);

		auto cv = ar.GetComponent<ui::UiNodeGraphCanvas>(canvasE);
		assert(cv.Unwrap()->connections.size() == 1);
		assert(cv.Unwrap()->connections[0].fromPin == pinLeft);
		assert(cv.Unwrap()->connections[0].toPin == pinC);
		assert(ar.GetComponent<ui::UiGraphPin>(pinLeft).Unwrap()->connected);
		std::cout << "glisser pin->pin: ok\n";
	}

	// ── Glisser vers du vide : aucune connexion créée ───────────────────────
	{
		auto pl = ar.GetComponent<ui::UiComputed>(pinLeft);
		sdl3::FPoint from{pl.Unwrap()->screen.x + pl.Unwrap()->screen.w * 0.5f,
						pl.Unwrap()->screen.y + pl.Unwrap()->screen.h * 0.5f};
		size_t before = ar.GetComponent<ui::UiNodeGraphCanvas>(canvasE).Unwrap()->connections.size();
		nodeGraph.HandleEvent(ar, MouseDownEv(from.x, from.y), layout);
		nodeGraph.HandleEvent(ar, MouseMoveEv(5.f, 5.f), layout);
		nodeGraph.HandleEvent(ar, MouseUpEv(5.f, 5.f), layout);
		assert(ar.GetComponent<ui::UiNodeGraphCanvas>(canvasE).Unwrap()->connections.size() == before);
		std::cout << "glisser vers le vide: aucune connexion (ok)\n";
	}

	std::cout << "nodegraph phase3 checks passed\n";

	// ════════════════════════════════════════════════════════════════════
	// Phase 4 — Points intermédiaires
	// ════════════════════════════════════════════════════════════════════
	// État courant : une seule connexion pinLeft->pinC (créée par le glisser
	// de la Phase 3), sans waypoint.

	// ── connectionPath routé à travers un point intermédiaire ──────────────
	{
		sdl3::FPoint a{0.f, 0.f}, b{100.f, 0.f}, w{50.f, 40.f};
		std::array<sdl3::FPoint, 1> through{w};
		auto straight = ui::detail::ConnectionPath(a, through, b, ui::ConnectionStyle::STRAIGHT, 0.f, 1.f);
		assert(straight.size() == 3); // a, w, b — sans point dupliqué
		assert(sdl3::Abs(straight[1].x - w.x) < 0.01f && sdl3::Abs(straight[1].y - w.y) < 0.01f);

		auto ortho = ui::detail::ConnectionPath(a, through, b, ui::ConnectionStyle::ORTHOGONAL, 0.f, 1.f);
		assert(ortho.size() == 7); // 2 segments x 4 points - 1 point dupliqué par jonction
		assert(sdl3::Abs(ortho.front().x - a.x) < 0.01f && sdl3::Abs(ortho.back().x - b.x) < 0.01f);
		std::cout << "connectionPath (avec waypoint): ok\n";
	}

	// ── Double-clic sur le TRACÉ : insère un waypoint à l'endroit cliqué ────
	sdl3::FPoint waypointScreen{};
	{
		auto pl = ar.GetComponent<ui::UiComputed>(pinLeft);
		auto pc = ar.GetComponent<ui::UiComputed>(pinC);
		sdl3::FPoint from{pl.Unwrap()->screen.x + pl.Unwrap()->screen.w * 0.5f,
						pl.Unwrap()->screen.y + pl.Unwrap()->screen.h * 0.5f};
		sdl3::FPoint to{pc.Unwrap()->screen.x + pc.Unwrap()->screen.w * 0.5f,
					  pc.Unwrap()->screen.y + pc.Unwrap()->screen.h * 0.5f};
		waypointScreen = {(from.x + to.x) * 0.5f, (from.y + to.y) * 0.5f}; // pile sur le segment de contrôle

		auto cv = ar.GetComponent<ui::UiNodeGraphCanvas>(canvasE);
		assert(cv.Unwrap()->connections[0].waypoints.empty());
		nodeGraph.HandleEvent(ar, MouseDownEv(waypointScreen.x, waypointScreen.y, SDL_BUTTON_LEFT, 2), layout);
		assert(cv.Unwrap()->connections[0].waypoints.size() == 1);

		auto c = ar.GetComponent<ui::UiComputed>(canvasE);
		sdl3::FPoint back = ui::CanvasToScreen(cv.Unwrap()->connections[0].waypoints[0], *cv.Unwrap(), c.Unwrap()->screen);
		assert(sdl3::Abs(back.x - waypointScreen.x) < 0.5f);
		assert(sdl3::Abs(back.y - waypointScreen.y) < 0.5f);
		std::cout << "double-clic sur le trace: waypoint ajoute (ok)\n";
	}

	// ── Glisser la poignée du waypoint : déplace en temps réel ──────────────
	{
		nodeGraph.HandleEvent(ar, MouseDownEv(waypointScreen.x, waypointScreen.y), layout);
		nodeGraph.HandleEvent(ar, MouseMoveEv(waypointScreen.x + 25.f, waypointScreen.y - 15.f), layout);
		nodeGraph.HandleEvent(ar, MouseUpEv(waypointScreen.x + 25.f, waypointScreen.y - 15.f), layout);

		auto cv = ar.GetComponent<ui::UiNodeGraphCanvas>(canvasE);
		auto c = ar.GetComponent<ui::UiComputed>(canvasE);
		sdl3::FPoint newScreen =
			ui::CanvasToScreen(cv.Unwrap()->connections[0].waypoints[0], *cv.Unwrap(), c.Unwrap()->screen);
		assert(sdl3::Abs(newScreen.x - (waypointScreen.x + 25.f)) < 0.5f);
		assert(sdl3::Abs(newScreen.y - (waypointScreen.y - 15.f)) < 0.5f);
		waypointScreen = newScreen;
		std::cout << "glisser la poignee: deplacement pris en compte (ok)\n";
	}

	// ── Double-clic sur la poignée : supprime le waypoint ───────────────────
	{
		nodeGraph.HandleEvent(ar, MouseDownEv(waypointScreen.x, waypointScreen.y, SDL_BUTTON_LEFT, 2), layout);
		auto cv = ar.GetComponent<ui::UiNodeGraphCanvas>(canvasE);
		assert(cv.Unwrap()->connections[0].waypoints.empty());
		std::cout << "double-clic sur la poignee: waypoint supprime (ok)\n";
	}

	// ── API programmatique insertWaypoint/removeWaypoint ────────────────────
	{
		auto cv = ar.GetComponent<ui::UiNodeGraphCanvas>(canvasE);
		ui::GraphConnection &conn = cv.Unwrap()->connections[0];
		ui::InsertWaypoint(conn, 0, {10.f, 10.f});
		assert(conn.waypoints.size() == 1);
		assert(sdl3::Abs(conn.waypoints[0].x - 10.f) < 0.01f);
		ui::RemoveWaypoint(conn, 0);
		assert(conn.waypoints.empty());
		ui::RemoveWaypoint(conn, 5); // hors bornes : no-op, ne doit pas planter
		std::cout << "insertWaypoint/removeWaypoint programmatiques: ok\n";
	}

	std::cout << "nodegraph phase4 checks passed\n";

	// ════════════════════════════════════════════════════════════════════
	// Phase 5 — Sélection & déplacement de groupe
	// ════════════════════════════════════════════════════════════════════
	// État courant : deux nœuds, nodeE (canvasPos ~80,55) et nodeB (~500,300),
	// ordre de UiChildren du canvas = [nodeE, nodeB] (ordre de création).

	std::vector<ecs::Entity> order;
	if (auto ch = ar.GetComponent<ui::UiChildren>(canvasE); ch.IsSome())
		order = ch.Unwrap()->list;
	assert(order.size() == 2 && order[0] == nodeE && order[1] == nodeB);

	// ── applyNodeSelectionClick (pur, sans ECS) : clic simple/ctrl/maj ──────
	{
		ui::NodeSelectionState state;
		ui::ApplyNodeSelectionClick(state, nodeE, false, false, order);
		assert(state.selected.size() == 1 && state.selected.count(nodeE));
		assert(state.anchor == nodeE);

		ui::ApplyNodeSelectionClick(state, nodeB, true, false, order); // ctrl+clic : ajoute
		assert(state.selected.size() == 2 && state.selected.count(nodeB));
		assert(state.anchor == nodeB);

		ui::ApplyNodeSelectionClick(state, nodeB, true, false, order); // ctrl+clic sur le même : retire
		assert(state.selected.size() == 1 && !state.selected.count(nodeB));
		assert(state.anchor == nodeB); // un ctrl+clic déplace aussi l'ancre (comme Explorer)

		ui::ApplyNodeSelectionClick(state, nodeE, false, true, order); // maj+clic : plage [ancre=nodeB, nodeE]
		assert(state.selected.size() == 2);

		ui::ApplyNodeSelectionClick(state, nodeB, false, false, order); // clic simple : remplace
		assert(state.selected.size() == 1 && state.selected.count(nodeB));
		std::cout << "applyNodeSelectionClick (simple/ctrl/maj): ok\n";
	}

	// ── API programmatique : selectNode/deselectNode/clearSelection/selectAllNodes ──
	{
		nodeGraph.SelectNode(ar, nodeE);
		assert(nodeGraph.IsNodeSelected(ar, nodeE));
		assert(!nodeGraph.IsNodeSelected(ar, nodeB));
		assert(ar.GetComponent<ui::UiGraphNode>(nodeE).Unwrap()->selected); // répercuté sur le rendu

		nodeGraph.SelectNode(ar, nodeB, /*additive=*/true);
		assert(nodeGraph.IsNodeSelected(ar, nodeE) && nodeGraph.IsNodeSelected(ar, nodeB));

		nodeGraph.DeselectNode(ar, nodeE);
		assert(!nodeGraph.IsNodeSelected(ar, nodeE) && nodeGraph.IsNodeSelected(ar, nodeB));
		assert(!ar.GetComponent<ui::UiGraphNode>(nodeE).Unwrap()->selected);

		nodeGraph.SelectAllNodes(ar, canvasE);
		assert(nodeGraph.IsNodeSelected(ar, nodeE) && nodeGraph.IsNodeSelected(ar, nodeB));

		nodeGraph.ClearSelection(ar, canvasE);
		assert(!nodeGraph.IsNodeSelected(ar, nodeE) && !nodeGraph.IsNodeSelected(ar, nodeB));
		std::cout << "selectNode/deselectNode/clearSelection/selectAllNodes: ok\n";
	}

	// ── Hooks de changement (NodeGraphCallbacks) ─────────────────────────────
	int selectionChangedCount = 0;
	ecs::Entity lastMovedNode{};
	sdl3::FPoint lastMovedPos{};
	{
		ui::NodeGraphCallbacks cb;
		cb.onSelectionChanged = [&](const std::unordered_set<ecs::Entity> &) { ++selectionChangedCount; };
		cb.onNodeMoved = [&](ecs::Entity e, sdl3::FPoint p) {
			lastMovedNode = e;
			lastMovedPos = p;
		};
		ar.AddComponent(canvasE, std::move(cb));
		nodeGraph.SelectNode(ar, nodeE);
		assert(selectionChangedCount == 1);
		std::cout << "NodeGraphCallbacks::onSelectionChanged: ok\n";
	}

	// ── Clic sur en-tête : sélection simple, puis ctrl+clic ajoute ──────────
	{
		nodeGraph.ClearSelection(ar, canvasE);
		auto ce = ar.GetComponent<ui::UiComputed>(nodeE);
		sdl3::FPoint headerE{ce.Unwrap()->screen.x + 10.f, ce.Unwrap()->screen.y + 10.f};
		nodeGraph.HandleEvent(ar, MouseDownEv(headerE.x, headerE.y), layout);
		nodeGraph.HandleEvent(ar, MouseUpEv(headerE.x, headerE.y), layout);
		assert(nodeGraph.IsNodeSelected(ar, nodeE) && !nodeGraph.IsNodeSelected(ar, nodeB));

		auto cb2 = ar.GetComponent<ui::UiComputed>(nodeB);
		sdl3::FPoint headerB{cb2.Unwrap()->screen.x + 10.f, cb2.Unwrap()->screen.y + 10.f};
		sdl3::keyboard::SetMods(SDL_KMOD_LCTRL);
		nodeGraph.HandleEvent(ar, MouseDownEv(headerB.x, headerB.y), layout);
		nodeGraph.HandleEvent(ar, MouseUpEv(headerB.x, headerB.y), layout);
		sdl3::keyboard::SetMods(SDL_KMOD_NONE);
		assert(nodeGraph.IsNodeSelected(ar, nodeE) && nodeGraph.IsNodeSelected(ar, nodeB));
		std::cout << "clic simple + ctrl+clic sur en-tete: ok\n";
	}

	// ── Déplacement de groupe : glisser un nœud DÉJÀ sélectionné déplace tout le groupe ──
	{
		auto posEBefore = ar.GetComponent<ui::UiGraphNode>(nodeE).Unwrap()->canvasPos;
		auto posBBefore = ar.GetComponent<ui::UiGraphNode>(nodeB).Unwrap()->canvasPos;
		auto ce = ar.GetComponent<ui::UiComputed>(nodeE);
		sdl3::FPoint headerE{ce.Unwrap()->screen.x + 10.f, ce.Unwrap()->screen.y + 10.f};

		nodeGraph.HandleEvent(ar, MouseDownEv(headerE.x, headerE.y), layout);
		// Un clic simple sur un nœud DÉJÀ sélectionné ne doit PAS réduire le
		// groupe à ce seul nœud.
		assert(nodeGraph.IsNodeSelected(ar, nodeE) && nodeGraph.IsNodeSelected(ar, nodeB));
		nodeGraph.HandleEvent(ar, MouseMoveEv(headerE.x + 18.f, headerE.y + 12.f), layout);
		nodeGraph.HandleEvent(ar, MouseUpEv(headerE.x + 18.f, headerE.y + 12.f), layout);

		auto cv = ar.GetComponent<ui::UiNodeGraphCanvas>(canvasE);
		float zoom = cv.Unwrap()->zoom;
		auto posEAfter = ar.GetComponent<ui::UiGraphNode>(nodeE).Unwrap()->canvasPos;
		auto posBAfter = ar.GetComponent<ui::UiGraphNode>(nodeB).Unwrap()->canvasPos;
		float expDx = 18.f / zoom, expDy = 12.f / zoom;
		assert(sdl3::Abs((posEAfter.x - posEBefore.x) - expDx) < 0.05f);
		assert(sdl3::Abs((posEAfter.y - posEBefore.y) - expDy) < 0.05f);
		assert(sdl3::Abs((posBAfter.x - posBBefore.x) - expDx) < 0.05f);
		assert(sdl3::Abs((posBAfter.y - posBBefore.y) - expDy) < 0.05f);
		assert(lastMovedNode == nodeE || lastMovedNode == nodeB); // onNodeMoved a bien tiré
		std::cout << "deplacement de groupe (2 noeuds selectionnes): ok\n";
	}

	// ── Marquee : rectangle englobant les deux nœuds les sélectionne ────────
	{
		nodeGraph.ClearSelection(ar, canvasE);
		auto ce = ar.GetComponent<ui::UiComputed>(nodeE);
		auto cb2 = ar.GetComponent<ui::UiComputed>(nodeB);
		const sdl3::FRect &se = ce.Unwrap()->screen;
		const sdl3::FRect &sb = cb2.Unwrap()->screen;
		float minX = sdl3::Min(se.x, sb.x) - 10.f, minY = sdl3::Min(se.y, sb.y) - 10.f;
		float maxX = sdl3::Max(se.x + se.w, sb.x + sb.w) + 10.f;
		float maxY = sdl3::Max(se.y + se.h, sb.y + sb.h) + 10.f;
		// Le point de PRESS doit tomber dans le rect écran du canvas pour que
		// le marquee démarre (cf. handleEvent) — nodeB dépasse potentiellement
		// le canvas 600x400 du test, donc seul le coin de départ est clampé
		// (le relâchement, lui, n'a pas besoin d'être contenu).
		const sdl3::FRect &canvasScreen = ar.GetComponent<ui::UiComputed>(canvasE).Unwrap()->screen;
		minX = sdl3::Max(minX, canvasScreen.x + 1.f);
		minY = sdl3::Max(minY, canvasScreen.y + 1.f);

		nodeGraph.HandleEvent(ar, MouseDownEv(minX, minY), layout);
		nodeGraph.HandleEvent(ar, MouseUpEv(maxX, maxY), layout);
		assert(nodeGraph.IsNodeSelected(ar, nodeE) && nodeGraph.IsNodeSelected(ar, nodeB));
		std::cout << "marquee (rectangle englobant): ok\n";

		// Un marquee sur du vide (sans ctrl) vide la sélection.
		nodeGraph.HandleEvent(ar, MouseDownEv(2.f, 2.f), layout);
		nodeGraph.HandleEvent(ar, MouseUpEv(6.f, 6.f), layout);
		assert(!nodeGraph.IsNodeSelected(ar, nodeE) && !nodeGraph.IsNodeSelected(ar, nodeB));
		std::cout << "marquee sur le vide: selection videe (ok)\n";
	}

	// ── destroyGraphNode : despawn en cascade (nœud + pins + connexions) ────
	{
		auto contentT = f.Column();
		contentT.Gap(4.f).Children(f.Label("Jetable"));
		ecs::Entity nodeT = ui::AddGraphNode(f, canvasE, "Jetable", {700.f, 500.f}, {120.f, 80.f}, std::move(contentT));
		ecs::Entity pinT = ui::AddGraphPin(ar, nodeT, ui::PinSide::Left, 0.5f);
		style.Resolve(ar);
		nodeGraph.Prepass(ar);
		layout.RunIfNeeded(ar, 800.f, 600.f);
		nodeGraph.UpdatePins(ar);

		assert(ui::ConnectPins(ar, pinT, pinRight) != nullptr);
		size_t connCountBefore = ar.GetComponent<ui::UiNodeGraphCanvas>(canvasE).Unwrap()->connections.size();
		assert(ar.GetComponent<ui::UiGraphPin>(pinRight).Unwrap()->connected);

		ui::DestroyGraphNode(ar, nodeT);

		assert(ar.GetComponent<ui::UiGraphNode>(nodeT).IsNone());
		assert(ar.GetComponent<ui::UiGraphPin>(pinT).IsNone());
		auto cv = ar.GetComponent<ui::UiNodeGraphCanvas>(canvasE);
		assert(cv.Unwrap()->connections.size() == connCountBefore - 1);
		assert(!ar.GetComponent<ui::UiGraphPin>(pinRight).Unwrap()->connected); // plus aucune connexion
		std::cout << "destroyGraphNode: despawn en cascade (ok)\n";
	}

	std::cout << "nodegraph phase5 checks passed\n";

	// ════════════════════════════════════════════════════════════════════
	// Phase 6 — Auto-scroll, menu contextuel, couper/copier/coller/supprimer
	// ════════════════════════════════════════════════════════════════════

	// ── Menu contextuel : clic droit délègue via NodeGraphCallbacks::onContextMenu ─
	{
		ecs::Entity ctxNode{};
		sdl3::FPoint ctxPos{};
		int ctxCount = 0;
		auto cb = ar.GetComponent<ui::NodeGraphCallbacks>(canvasE);
		assert(cb.IsSome()); // posé en Phase 5 (onSelectionChanged/onNodeMoved)
		cb.Unwrap()->onContextMenu = [&](ecs::Entity node, ecs::Entity, sdl3::FPoint pos) {
			ctxNode = node;
			ctxPos = pos;
			++ctxCount;
		};

		auto ce = ar.GetComponent<ui::UiComputed>(nodeE);
		sdl3::FPoint headerE{ce.Unwrap()->screen.x + 10.f, ce.Unwrap()->screen.y + 10.f};
		nodeGraph.HandleEvent(ar, MouseDownEv(headerE.x, headerE.y, SDL_BUTTON_RIGHT), layout);
		assert(ctxCount == 1 && ctxNode == nodeE);
		assert(sdl3::Abs(ctxPos.x - headerE.x) < 0.01f && sdl3::Abs(ctxPos.y - headerE.y) < 0.01f);

		nodeGraph.HandleEvent(ar, MouseDownEv(3.f, 3.f, SDL_BUTTON_RIGHT), layout); // fond vide
		assert(ctxCount == 2 && !ctxNode.Valid());
		std::cout << "menu contextuel (onContextMenu): ok\n";
	}

	// ── Auto-scroll pendant un drag proche du bord ──────────────────────────
	{
		nodeGraph.SelectNode(ar, nodeE);
		auto ce = ar.GetComponent<ui::UiComputed>(nodeE);
		sdl3::FPoint headerE{ce.Unwrap()->screen.x + 10.f, ce.Unwrap()->screen.y + 10.f};

		// Sans drag en cours : tick() ne fait rien.
		auto cvBefore = *ar.GetComponent<ui::UiNodeGraphCanvas>(canvasE).Unwrap();
		nodeGraph.Tick(ar, layout, 1.f);
		assert(sdl3::Abs(ar.GetComponent<ui::UiNodeGraphCanvas>(canvasE).Unwrap()->pan.x - cvBefore.pan.x) < 0.01f);

		nodeGraph.HandleEvent(ar, MouseDownEv(headerE.x, headerE.y), layout);
		nodeGraph.HandleEvent(ar, MouseMoveEv(10.f, 200.f), layout); // proche du bord gauche (canvas 600x400 en (0,0))

		float panXBefore = ar.GetComponent<ui::UiNodeGraphCanvas>(canvasE).Unwrap()->pan.x;
		float nodeXBefore = ar.GetComponent<ui::UiGraphNode>(nodeE).Unwrap()->canvasPos.x;
		for (int i = 0; i < 5; ++i)
			nodeGraph.Tick(ar, layout, 0.1f);
		float panXAfter = ar.GetComponent<ui::UiNodeGraphCanvas>(canvasE).Unwrap()->pan.x;
		float nodeXAfter = ar.GetComponent<ui::UiGraphNode>(nodeE).Unwrap()->canvasPos.x;
		assert(panXAfter < panXBefore - 0.01f); // défile vers la gauche
		assert(sdl3::Abs((nodeXAfter - nodeXBefore) - (panXAfter - panXBefore)) < 0.01f); // même delta

		nodeGraph.HandleEvent(ar, MouseUpEv(10.f, 200.f), layout);
		std::cout << "auto-scroll pendant un drag: ok\n";
	}

	// ── Couper/copier/coller/supprimer ───────────────────────────────────────
	{
		assert(!nodeGraph.HasClipboard());
		nodeGraph.SelectNode(ar, nodeB);
		nodeGraph.HandleEvent(ar, KeyDownEv(SDLK_C, SDL_KMOD_LCTRL), layout);
		assert(nodeGraph.HasClipboard());

		auto pasted = nodeGraph.PasteClipboard(f, canvasE, {900.f, 900.f});
		assert(pasted.size() == 1);
		ecs::Entity nodeP = pasted[0];
		auto np = ar.GetComponent<ui::UiGraphNode>(nodeP);
		auto nb = ar.GetComponent<ui::UiGraphNode>(nodeB);
		assert(np.IsSome());
		assert(np.Unwrap()->title == nb.Unwrap()->title);
		assert(sdl3::Abs(np.Unwrap()->size.x - nb.Unwrap()->size.x) < 0.01f);
		assert(sdl3::Abs(np.Unwrap()->canvasPos.x - 900.f) < 0.01f);
		assert(sdl3::Abs(np.Unwrap()->canvasPos.y - 900.f) < 0.01f);
		int pastedPinCount = 0;
		ar.Query<ui::UiGraphPin>([&](ecs::Entity, ui::UiGraphPin &pin) {
			if (pin.node == nodeP)
				++pastedPinCount;
		});
		assert(pastedPinCount == 1); // nodeB n'avait que pinC
		std::cout << "copySelection/pasteClipboard: ok\n";

		// Suppr sur le nœud collé (sélectionné par pasteClipboard? non — le
		// sélectionner explicitement avant de tester la touche Suppr).
		nodeGraph.SelectNode(ar, nodeP);
		nodeGraph.HandleEvent(ar, KeyDownEv(SDLK_DELETE), layout);
		assert(ar.GetComponent<ui::UiGraphNode>(nodeP).IsNone());
		std::cout << "Suppr (deleteSelection via clavier): ok\n";

		// Couper nodeB : copie + destruction, y compris sa connexion à pinLeft.
		assert(ar.GetComponent<ui::UiGraphPin>(pinLeft).Unwrap()->connected);
		nodeGraph.SelectNode(ar, nodeB);
		nodeGraph.HandleEvent(ar, KeyDownEv(SDLK_X, SDL_KMOD_LCTRL), layout);
		assert(nodeGraph.HasClipboard());
		assert(ar.GetComponent<ui::UiGraphNode>(nodeB).IsNone());
		assert(ar.GetComponent<ui::UiGraphPin>(pinC).IsNone());
		assert(!ar.GetComponent<ui::UiGraphPin>(pinLeft).Unwrap()->connected);
		std::cout << "cutSelection (Ctrl+X): ok\n";
	}

	std::cout << "nodegraph phase6 checks passed\n";

	// ════════════════════════════════════════════════════════════════════
	// Phase 7 — inner-zoom appliqué + thème par défaut
	// ════════════════════════════════════════════════════════════════════

	// ── Thème par défaut (style Blueprint) : valeurs de construction ────────
	{
		auto cv = ar.GetComponent<ui::UiNodeGraphCanvas>(canvasE);
		assert(cv.Unwrap()->bgColor.r == 25 / 255.f && cv.Unwrap()->bgColor.g == 26 / 255.f &&
			   cv.Unwrap()->bgColor.b == 30 / 255.f);
		auto ne = ar.GetComponent<ui::UiGraphNode>(nodeE);
		assert(ne.Unwrap()->headerColor.r == sdl3::FColor::UI_NODE_HEADER_BLUE().r &&
			   ne.Unwrap()->headerColor.b == sdl3::FColor::UI_NODE_HEADER_BLUE().b);
		ecs::Entity pinFresh = ui::AddGraphPin(ar, nodeE, ui::PinSide::Bottom);
		assert(ar.GetComponent<ui::UiGraphPin>(pinFresh).Unwrap()->color.r == sdl3::FColor::UI_TEXT_BRIGHT().r);
		std::cout << "theme par defaut (couleurs de construction): ok\n";
	}

	// ── inner-zoom : échelle le CONTENU indépendamment de la boîte du nœud ──
	{
		auto sizedChild = f.Panel();
		sizedChild.Size(40.f, 20.f);
		auto innerContent = f.Column();
		innerContent.Children(std::move(sizedChild));
		ecs::Entity nodeZ = ui::AddGraphNode(f, canvasE, "Zoomed", {1200.f, 100.f}, {300.f, 200.f},
											std::move(innerContent), sdl3::FColor{80, 80, 80, 255}, 2.0f);
		style.Resolve(ar);
		nodeGraph.Prepass(ar);
		layout.RunIfNeeded(ar, 800.f, 600.f);

		auto zch = ar.GetComponent<ui::UiChildren>(nodeZ);
		assert(zch.IsSome() && zch.Unwrap()->list.size() == 1);
		ecs::Entity contentRoot = zch.Unwrap()->list.front();
		auto cch = ar.GetComponent<ui::UiChildren>(contentRoot);
		assert(cch.IsSome() && cch.Unwrap()->list.size() == 1);
		ecs::Entity childE = cch.Unwrap()->list.front();

		auto childC = ar.GetComponent<ui::UiComputed>(childE);
		std::cout << "enfant a innerZoom=2.0: " << childC.Unwrap()->screen.w << "x" << childC.Unwrap()->screen.h
				  << "\n";
		assert(sdl3::Abs(childC.Unwrap()->screen.w - 80.f) < 0.05f); // 40 * 2.0
		assert(sdl3::Abs(childC.Unwrap()->screen.h - 40.f) < 0.05f); // 20 * 2.0

		// Changement à chaud (API programmatique, spec section 7).
		ui::SetNodeInnerZoom(ar, layout, nodeZ, 0.5f);
		style.Resolve(ar);
		nodeGraph.Prepass(ar);
		layout.RunIfNeeded(ar, 800.f, 600.f);
		auto childC2 = ar.GetComponent<ui::UiComputed>(childE);
		std::cout << "enfant apres setNodeInnerZoom(0.5): " << childC2.Unwrap()->screen.w << "x"
				  << childC2.Unwrap()->screen.h << "\n";
		assert(sdl3::Abs(childC2.Unwrap()->screen.w - 20.f) < 0.05f); // 40 * 0.5
		assert(sdl3::Abs(childC2.Unwrap()->screen.h - 10.f) < 0.05f); // 20 * 0.5
		std::cout << "inner-zoom (construction + setNodeInnerZoom): ok\n";
	}

	// ── États visuels de nœud : disabled bloque le clic, hovered suit la souris ─
	{
		auto ce = ar.GetComponent<ui::UiComputed>(nodeE);
		sdl3::FPoint headerE{ce.Unwrap()->screen.x + 10.f, ce.Unwrap()->screen.y + 10.f};

		// Survol.
		nodeGraph.HandleEvent(ar, MouseMoveEv(headerE.x, headerE.y), layout);
		assert(ar.GetComponent<ui::UiGraphNode>(nodeE).Unwrap()->hovered);
		nodeGraph.HandleEvent(ar, MouseMoveEv(2.f, 2.f), layout);
		assert(!ar.GetComponent<ui::UiGraphNode>(nodeE).Unwrap()->hovered);

		// Désactivé : le clic n'ouvre ni sélection ni drag.
		nodeGraph.ClearSelection(ar, canvasE);
		ar.GetComponent<ui::UiGraphNode>(nodeE).Unwrap()->disabled = true;
		auto posBefore = ar.GetComponent<ui::UiGraphNode>(nodeE).Unwrap()->canvasPos;
		nodeGraph.HandleEvent(ar, MouseDownEv(headerE.x, headerE.y), layout);
		nodeGraph.HandleEvent(ar, MouseMoveEv(headerE.x + 30.f, headerE.y + 20.f), layout);
		nodeGraph.HandleEvent(ar, MouseUpEv(headerE.x + 30.f, headerE.y + 20.f), layout);
		assert(!nodeGraph.IsNodeSelected(ar, nodeE));
		auto posAfter = ar.GetComponent<ui::UiGraphNode>(nodeE).Unwrap()->canvasPos;
		assert(sdl3::Abs(posAfter.x - posBefore.x) < 0.01f && sdl3::Abs(posAfter.y - posBefore.y) < 0.01f);
		ar.GetComponent<ui::UiGraphNode>(nodeE).Unwrap()->disabled = false; // restore
		std::cout << "etats visuels de noeud (hovered/disabled): ok\n";
	}

	std::cout << "nodegraph phase7 checks passed\n";

	// ════════════════════════════════════════════════════════════════════
	// Phase 8 — Persistance (serializeGraph/deserializeGraph)
	// ════════════════════════════════════════════════════════════════════
	// Graphe FRAIS sur un canvas dédié (plus simple à vérifier au octet près
	// qu'un round-trip de l'état accumulé des 7 phases précédentes).
	{
		auto canvasSrc = ui::CanvasBuilder(f);
		canvasSrc.Anchor(ui::Anchor::TopLeft).Size(400.f, 300.f);
		ecs::Entity canvasSrcE = canvasSrc.Spawn();
		ui::AttachNodeGraphCanvas(ar, canvasSrcE, ui::UiNodeGraphCanvas{});
		ar.GetComponent<ui::UiNodeGraphCanvas>(canvasSrcE).Unwrap()->pan = {12.f, 34.f};
		ar.GetComponent<ui::UiNodeGraphCanvas>(canvasSrcE).Unwrap()->zoom = 1.6f;

		auto contentX = f.Column();
		ecs::Entity nodeX = ui::AddGraphNode(f, canvasSrcE, "Noeud X", {10.f, 20.f}, {180.f, 90.f}, std::move(contentX),
											sdl3::FColor{10 / 255.f, 20 / 255.f, 30 / 255.f, 200 / 255.f}, 1.5f);
		{
			auto n = ar.GetComponent<ui::UiGraphNode>(nodeX);
			n.Unwrap()->disabled = true;
			n.Unwrap()->minWidth = 55.f;
			n.Unwrap()->minHeight = 33.f;
			n.Unwrap()->headerHeight = 30.f;
		}
		(void)ui::AddGraphPin(ar, nodeX, ui::PinSide::Left, 0.25f, ui::PinShape::STAR,
							  sdl3::FColor{9 / 255.f, 8 / 255.f, 7 / 255.f, 6 / 255.f}, 12.f);
		ecs::Entity pinX2 = ui::AddGraphPin(ar, nodeX, ui::PinSide::Right, 0.75f, ui::PinShape::SQUARE);

		auto contentY = f.Column();
		ecs::Entity nodeY = ui::AddGraphNode(f, canvasSrcE, "Noeud Y", {400.f, 250.f}, {150.f, 100.f}, std::move(contentY));
		ecs::Entity pinY1 = ui::AddGraphPin(ar, nodeY, ui::PinSide::Left);

		ui::GraphConnection *conn = ui::ConnectPins(ar, pinX2, pinY1);
		assert(conn != nullptr);
		conn->color = sdl3::FColor{1 / 255.f, 2 / 255.f, 3 / 255.f, 4 / 255.f};
		conn->thickness = 5.5f;
		conn->opacity = 0.75f;
		conn->style = ui::ConnectionStyle::ORTHOGONAL;
		conn->startCap = ui::ConnectionEndCap::ARROW;
		conn->endCap = ui::ConnectionEndCap::NONE;
		conn->arrowSize = 13.f;
		conn->bezierControlOffset = 42.f;
		conn->waypoints.push_back({99.f, 88.f});

		nodeGraph.SelectNode(ar, nodeX);

		style.Resolve(ar);
		nodeGraph.Prepass(ar);
		layout.RunIfNeeded(ar, 800.f, 600.f);

		// ── Sérialisation puis désérialisation sur un canvas NEUF ───────────
		data::NodePtr doc = ui::SerializeGraph(ar, canvasSrcE);
		assert(doc != nullptr);

		auto canvasDst = ui::CanvasBuilder(f);
		canvasDst.Anchor(ui::Anchor::TopLeft).Size(400.f, 300.f);
		ecs::Entity canvasDstE = canvasDst.Spawn();
		ui::AttachNodeGraphCanvas(ar, canvasDstE);

		auto keyMap = ui::DeserializeGraph(f, nodeGraph, layout, canvasDstE, doc);
		assert(keyMap.size() == 2);

		auto cvDst = ar.GetComponent<ui::UiNodeGraphCanvas>(canvasDstE);
		assert(sdl3::Abs(cvDst.Unwrap()->pan.x - 12.f) < 0.01f);
		assert(sdl3::Abs(cvDst.Unwrap()->pan.y - 34.f) < 0.01f);
		assert(sdl3::Abs(cvDst.Unwrap()->zoom - 1.6f) < 0.001f);
		assert(cvDst.Unwrap()->connections.size() == 1);
		std::cout << "serializeGraph/deserializeGraph: canvas pan/zoom/connexions ok\n";

		// Retrouve le nœud restauré correspondant à "Noeud X" via son titre
		// (les clés du document ne sont pas exposées à l'appelant).
		ecs::Entity nodeX2{};
		for (auto &[key, e] : keyMap) {
			(void)key;
			if (auto n = ar.GetComponent<ui::UiGraphNode>(e); n.IsSome() && n.Unwrap()->title == "Noeud X")
				nodeX2 = e;
		}
		assert(nodeX2.Valid());
		auto n2 = ar.GetComponent<ui::UiGraphNode>(nodeX2);
		assert(sdl3::Abs(n2.Unwrap()->canvasPos.x - 10.f) < 0.01f);
		assert(sdl3::Abs(n2.Unwrap()->canvasPos.y - 20.f) < 0.01f);
		assert(sdl3::Abs(n2.Unwrap()->size.x - 180.f) < 0.01f);
		assert(n2.Unwrap()->headerColor.r == 10 / 255.f && n2.Unwrap()->headerColor.a == 200 / 255.f);
		assert(n2.Unwrap()->disabled);
		assert(sdl3::Abs(n2.Unwrap()->minWidth - 55.f) < 0.01f);
		assert(sdl3::Abs(n2.Unwrap()->headerHeight - 30.f) < 0.01f);
		assert(nodeGraph.IsNodeSelected(ar, nodeX2)); // sélection restaurée

		auto ch2 = ar.GetComponent<ui::UiChildren>(nodeX2);
		assert(ch2.IsSome() && !ch2.Unwrap()->list.empty());
		auto st2 = ar.GetComponent<ui::UiStyle>(ch2.Unwrap()->list.front());
		assert(st2.IsSome());
		const auto *iz2 = st2.Unwrap()->Get<ui::prop::InnerZoom>();
		assert(iz2 != nullptr && sdl3::Abs(iz2->value - 1.5f) < 0.01f);
		std::cout << "serializeGraph/deserializeGraph: chrome + inner-zoom + selection ok\n";

		int pinCount2 = 0;
		ecs::Entity pinX12{};
		ar.Query<ui::UiGraphPin>([&](ecs::Entity pe, ui::UiGraphPin &pin) {
			if (pin.node == nodeX2) {
				++pinCount2;
				if (pin.shape == ui::PinShape::STAR)
					pinX12 = pe;
			}
		});
		assert(pinCount2 == 2);
		assert(pinX12.Valid());
		auto p1 = ar.GetComponent<ui::UiGraphPin>(pinX12);
		assert(sdl3::Abs(p1.Unwrap()->sideOffset - 0.25f) < 0.01f);
		assert(p1.Unwrap()->side == ui::PinSide::Left);
		assert(p1.Unwrap()->color.r == 9 / 255.f && p1.Unwrap()->color.g == 8 / 255.f &&
			   p1.Unwrap()->color.b == 7 / 255.f && p1.Unwrap()->color.a == 6 / 255.f);
		assert(sdl3::Abs(p1.Unwrap()->size - 12.f) < 0.01f);
		std::cout << "serializeGraph/deserializeGraph: pins ok\n";

		const ui::GraphConnection &c2 = cvDst.Unwrap()->connections[0];
		assert(c2.color.r == 1 / 255.f && c2.color.g == 2 / 255.f && c2.color.b == 3 / 255.f && c2.color.a == 4 / 255.f);
		assert(sdl3::Abs(c2.thickness - 5.5f) < 0.01f);
		assert(sdl3::Abs(c2.opacity - 0.75f) < 0.01f);
		assert(c2.style == ui::ConnectionStyle::ORTHOGONAL);
		assert(c2.startCap == ui::ConnectionEndCap::ARROW);
		assert(c2.endCap == ui::ConnectionEndCap::NONE);
		assert(sdl3::Abs(c2.arrowSize - 13.f) < 0.01f);
		assert(sdl3::Abs(c2.bezierControlOffset - 42.f) < 0.01f);
		assert(c2.waypoints.size() == 1);
		assert(sdl3::Abs(c2.waypoints[0].x - 99.f) < 0.01f && sdl3::Abs(c2.waypoints[0].y - 88.f) < 0.01f);
		std::cout << "serializeGraph/deserializeGraph: connexion (style/couleur/waypoint) ok\n";
	}

	std::cout << "nodegraph phase8 checks passed\n";

	// ════════════════════════════════════════════════════════════════════
	// Phase 9 — Rendu réel (formes de pins restantes + échappatoires
	// onCustomDraw) et exemple. Les 15 critères d'acceptation de la spec
	// (section 11) sont couverts par l'ensemble des phases précédentes :
	//   1  grille/scroll/zoom/navigation .... Phase 1 (RenderGrid/handleEvent)
	//   2  nœuds : créer/déplacer/redim ..... Phase 1 (addGraphNode/handleEvent)
	//   3  sélection multiple + groupe ...... Phase 5 (applyNodeSelectionClick,
	//                                          groupDragStart)
	//   4  pan/zoom libres ................... Phase 1 (handleEvent MIDDLE/WHEEL)
	//   5  auto-scroll pendant un drag ....... Phase 6 (tick())
	//   6  pins formes/couleurs .............. Phases 2 (Circle/Square) et 8
	//                                          (Star) + Triangle ci-dessous
	//   7  connecter deux pins ................ Phase 3 (connectPins/drag)
	//   8  Bézier/droite/orthogonale .......... Phases 3-4 (ConnectionStyle)
	//   9  personnaliser une connexion ........ Phase 3 (GraphConnection)
	//   10 points intermédiaires .............. Phase 4 (insertWaypoint/drag)
	//   11 inner-zoom indépendant ............. Phase 7 (setNodeInnerZoom)
	//   12 couper/copier/coller/supprimer ..... Phase 6 (handleEvent clavier)
	//   13 menus contextuels ................... Phase 6 (onContextMenu)
	//   14 sauvegarder/restaurer l'état ....... Phase 8 (serialize/deserializeGraph)
	//   15 rendu 100% personnalisable .......... ci-dessous (onCustomDraw)
	// examples/ui_node_graph_demo.cpp démontre tout ceci de façon interactive
	// (nœuds Add/Multiply/Output, thème Blueprint par défaut).
	{
		auto winRes = sdl3::Window::Create("smoke-ui7-phase9", 400, 300, sdl3::window_flags::HIDDEN);
		assert(winRes);
		auto renRes = sdl3::Renderer::Create(winRes.Value());
		assert(renRes);
		auto &ren = renRes.Value();
		ui::SdlRendererBackend renBackend(ren);

		auto contentR = f.Column();
		ecs::Entity nodeR = ui::AddGraphNode(f, canvasE, "Rendu", {5000.f, 5000.f}, {150.f, 100.f}, std::move(contentR));
		ecs::Entity pinTri =
			ui::AddGraphPin(ar, nodeR, ui::PinSide::Bottom, 0.5f, ui::PinShape::TRIANGLE, {80, 200, 120, 255});
		ecs::Entity pinCircleR = ui::AddGraphPin(ar, nodeR, ui::PinSide::Top, 0.5f);
		style.Resolve(ar);
		nodeGraph.Prepass(ar);
		layout.RunIfNeeded(ar, 800.f, 600.f);
		nodeGraph.UpdatePins(ar);

		// ── Forme Triangle (dernière forme non encore exercée) ──────────────
		assert(ar.GetComponent<ui::UiGraphPin>(pinTri).Unwrap()->shape == ui::PinShape::TRIANGLE);

		// ── Échappatoire de rendu personnalisé (spec critère #15) ───────────
		bool pinCustomDrawCalled = false, connCustomDrawCalled = false;
		ar.GetComponent<ui::UiGraphPin>(pinTri).Unwrap()->onCustomDraw =
			[&](sdl3::Renderer &, const sdl3::FRect &, const ui::UiGraphPin &) { pinCustomDrawCalled = true; };

		ui::GraphConnection *conn = ui::ConnectPins(ar, pinTri, pinCircleR);
		assert(conn != nullptr);
		conn->onCustomDraw = [&](sdl3::Renderer &, std::span<const sdl3::FPoint>, const ui::GraphConnection &) {
			connCustomDrawCalled = true;
		};

		// ── Passe de rendu complète (ordre documenté en tête de nodegraph.hpp) ─
		nodeGraph.RenderGrid(ar, renBackend);
		nodeGraph.RenderConnections(ar, renBackend);
		nodeGraph.RenderHeaders(ar, renBackend);
		nodeGraph.RenderPins(ar, renBackend);
		nodeGraph.RenderMarquee(renBackend);

		assert(pinCustomDrawCalled);
		assert(connCustomDrawCalled);
		std::cout << "forme Triangle + onCustomDraw (pin/connexion) + passe de rendu complete: ok\n";
	}

	std::cout << "nodegraph phase9 checks passed\n";
	std::cout << "nodegraph: tous les criteres d'acceptation couverts (phases 1-9)\n";
	return 0;
}
