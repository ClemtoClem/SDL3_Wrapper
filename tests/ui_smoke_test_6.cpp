// Smoke test 6 (Phase 10, cf. plan mossy-forging-fog.md — Vérification) :
// popup/modale (ouverture, Échap, clic extérieur), sélection multiple
// (Ctrl/Maj+clic), redimensionnement de splitter/colonne de tableau — pour
// fixer le comportement des primitives introduites en Phases 1/2/4/6/8, comme
// ui_smoke_test_1..5 le font déjà pour le pipeline existant.
#include "sdl3/sdl3.hpp"
#include "ui/ui.hpp"
#include <cassert>
#include <cstdlib>
#include <iostream>

static sdl3::Event MouseDownEv(float x, float y) {
	sdl3::Event ev{};
	ev.raw.type = SDL_EVENT_MOUSE_BUTTON_DOWN;
	ev.raw.button.x = x;
	ev.raw.button.y = y;
	ev.raw.button.button = SDL_BUTTON_LEFT;
	return ev;
}
static sdl3::Event MouseUpEv(float x, float y) {
	sdl3::Event ev{};
	ev.raw.type = SDL_EVENT_MOUSE_BUTTON_UP;
	ev.raw.button.x = x;
	ev.raw.button.y = y;
	ev.raw.button.button = SDL_BUTTON_LEFT;
	return ev;
}
static sdl3::Event MouseMoveEv(float x, float y) {
	sdl3::Event ev{};
	ev.raw.type = SDL_EVENT_MOUSE_MOTION;
	ev.raw.motion.x = x;
	ev.raw.motion.y = y;
	return ev;
}
static sdl3::Event KeyDownEv(SDL_Keycode key) {
	sdl3::Event ev{};
	ev.raw.type = SDL_EVENT_KEY_DOWN;
	ev.raw.key.key = key;
	return ev;
}
static void Click(ui::InputSystem &input, ecs::ArchetypeRegistry &ar, ui::LayoutSystem &layout, float x, float y) {
	input.HandleEvent(ar, MouseDownEv(x, y), layout);
	input.HandleEvent(ar, MouseUpEv(x, y), layout);
}

int main() {
	setenv("SDL_VIDEODRIVER", "dummy", 0);
	ecs::ArchetypeRegistry ar;
	ui::LayoutSystem layout;
	ui::InputSystem input;
	ui::UiFactory f(ar, layout);
	ui::StyleSystem style;
	style.sheet = &f.sheet;

	// ── §1 : Popup ancré — ouverture au clic déclencheur, fermeture au clic
	// extérieur ─────────────────────────────────────────────────────────────
	{
		auto trigger = f.Button("Ouvrir");
		trigger.Anchor(ui::Anchor::TopLeft).Offset(10.f, 10.f);
		ecs::Entity triggerE = trigger.Spawn();

		auto pop = f.Popup();
		pop.Children(f.Label("Contenu du popup"));
		ecs::Entity popE = pop.Spawn();

		style.Resolve(ar);
		layout.RunIfNeeded(ar, 800.f, 600.f);

		ui::OpenPopup(ar, layout, popE, triggerE);
		layout.RunIfNeeded(ar, 800.f, 600.f);
		{
			auto ps = ar.GetComponent<ui::UiPopupState>(popE);
			assert(ps.IsSome() && ps.Unwrap()->open);
		}

		// Clic loin de tout (ni popup, ni trigger) : ferme le popup non-modal.
		Click(input, ar, layout, 700.f, 500.f);
		{
			auto ps = ar.GetComponent<ui::UiPopupState>(popE);
			assert(ps.IsSome() && !ps.Unwrap()->open);
		}
		std::cout << "§1 popup outside-click close: OK\n";
	}

	// ── §2 : Modale — ouverture, Échap la ferme, bloque le reste de l'arbre
	// pendant qu'elle est ouverte ────────────────────────────────────────────
	{
		auto behindBtn = f.Button("Derrière");
		behindBtn.Anchor(ui::Anchor::TopLeft).Offset(10.f, 10.f);
		bool behindClicked = false;
		behindBtn.OnClick([&behindClicked] { behindClicked = true; });
		ecs::Entity behindE = behindBtn.Spawn();

		auto dialog = f.Panel();
		dialog.Size(200.f, 100.f).Children(f.Label("Modale"));
		auto modalW = f.Modal(std::move(dialog));
		ecs::Entity modalE = modalW.Spawn();

		style.Resolve(ar);
		layout.RunIfNeeded(ar, 800.f, 600.f);

		ui::InputSystem modalInput; // pile modale propre à ce sous-test
		modalInput.OpenModal(ar, layout, modalE);
		assert(modalInput.HasOpenModal());
		layout.RunIfNeeded(ar, 800.f, 600.f);

		// Clic sur le bouton "derrière" la modale : bloqué.
		sdl3::FRect behindScreen{};
		if (auto c = ar.GetComponent<ui::UiComputed>(behindE); c.IsSome())
			behindScreen = c.Unwrap()->screen;
		Click(modalInput, ar, layout, behindScreen.x + 5.f, behindScreen.y + 5.f);
		assert(!behindClicked);

		// Échap ferme la modale.
		modalInput.HandleEvent(ar, KeyDownEv(SDLK_ESCAPE), layout);
		assert(!modalInput.HasOpenModal());
		{
			auto ps = ar.GetComponent<ui::UiPopupState>(modalE);
			assert(ps.IsSome() && !ps.Unwrap()->open);
		}
		std::cout << "§2 modal escape + blocking: OK\n";
	}

	// ── §3 : Sélection multiple — Ctrl+clic bascule, Maj+clic étend la plage
	// (cf. applySelectionClick, interaction.hpp) ────────────────────────────
	{
		ui::SelectionState state;
		ui::ApplySelectionClick(state, 2, false, false, true); // clic simple
		assert(state.selected.size() == 1 && state.selected.contains(2));
		ui::ApplySelectionClick(state, 5, true, false, true); // ctrl+clic : ajoute
		assert(state.selected.size() == 2 && state.selected.contains(2) && state.selected.contains(5));
		ui::ApplySelectionClick(state, 5, true, false, true); // ctrl+clic sur le même : retire
		assert(state.selected.size() == 1 && !state.selected.contains(5));
		// Un ctrl+clic déplace aussi l'ancre (comme Explorer) — l'ancre est
		// donc 5 ici (pas 2), la plage Maj+clic suivante part bien de LÀ.
		assert(state.anchor == 5);
		ui::ApplySelectionClick(state, 8, false, true, true); // maj+clic : plage [ancre=5, 8]
		assert(state.selected.size() == 4);
		for (int i = 5; i <= 8; ++i)
			assert(state.selected.contains(i));
		std::cout << "§3 applySelectionClick ctrl/shift: OK\n";
	}

	// ── §4 : Splitter — glisser la poignée redimensionne le panneau avant
	// (cf. UiResizeHandle, Phase 2/8) ────────────────────────────────────────
	{
		ui::WidgetBuilder before = f.Panel();
		before.Size(150.f, 200.f);
		ui::WidgetBuilder after = f.Panel();
		ecs::Entity containerE = f.Splitter(std::move(before), std::move(after), ui::Orientation::Horizontal, 150.f);

		style.Resolve(ar);
		layout.RunIfNeeded(ar, 800.f, 600.f);

		ecs::Entity handleE{};
		ar.Query<ui::UiResizeHandle, ui::UiComputed>(
			[&](ecs::Entity e, ui::UiResizeHandle &, ui::UiComputed &) { handleE = e; });
		assert(handleE.Valid());
		sdl3::FRect handleScreen{};
		if (auto c = ar.GetComponent<ui::UiComputed>(handleE); c.IsSome())
			handleScreen = c.Unwrap()->screen;

		float hx = handleScreen.x + 3.f, hy = handleScreen.y + 5.f;
		input.HandleEvent(ar, MouseDownEv(hx, hy), layout);
		input.HandleEvent(ar, MouseMoveEv(hx + 80.f, hy), layout);
		input.HandleEvent(ar, MouseUpEv(hx + 80.f, hy), layout);
		layout.RunIfNeeded(ar, 800.f, 600.f);

		ecs::Entity beforeE{};
		if (auto ch = ar.GetComponent<ui::UiChildren>(containerE); ch.IsSome())
			beforeE = ch.Unwrap()->list.front();
		auto it = ar.GetComponent<ui::UiItem>(beforeE);
		assert(it.IsSome() && it.Unwrap()->width.value > 220.f);
		std::cout << "§4 splitter drag resize: OK\n";
	}

	// ── §5 : Table — colonne redimensionnable au glisser de bordure d'en-tête
	// (cf. UiTable, Phase 6) ─────────────────────────────────────────────────
	{
		std::vector<ui::UiTableColumn> cols = {{"Nom", 100.f, 40.f, true}, {"Valeur", 100.f, 40.f, true}};
		std::vector<std::vector<String>> rows = {{"A", "1"}, {"B", "2"}};
		auto tbl = f.Table(cols, rows, true, false);
		tbl.Anchor(ui::Anchor::TopLeft).Offset(0.f, 0.f).Size(300.f, 150.f);
		ecs::Entity tblE = tbl.Spawn();

		style.Resolve(ar);
		layout.RunIfNeeded(ar, 800.f, 600.f);

		sdl3::FRect tblScreen{};
		if (auto c = ar.GetComponent<ui::UiComputed>(tblE); c.IsSome())
			tblScreen = c.Unwrap()->screen;

		// Bordure droite de la 1re colonne (largeur 100, zone de résize ±6px
		// autour de x=100) : viser x=97 pour rester dans la zone de résize ET
		// dans le survol de la colonne 0 elle-même (relX < 100, intervalle
		// demi-ouvert — x=100 pile tomberait déjà dans la colonne 1).
		float edgeX = tblScreen.x + 97.f, edgeY = tblScreen.y + 10.f;
		input.HandleEvent(ar, MouseDownEv(edgeX, edgeY), layout);
		input.HandleEvent(ar, MouseMoveEv(edgeX + 40.f, edgeY), layout);
		input.HandleEvent(ar, MouseUpEv(edgeX + 40.f, edgeY), layout);

		auto t = ar.GetComponent<ui::UiTable>(tblE);
		assert(t.IsSome());
		// `ColumnX(n)` est le DÉCALAGE cumulé du DÉBUT de la colonne n :
		// `ColumnX(0)` vaut donc 0 par construction, et l'assertion d'origine
		// (`ColumnX(0) > 130`) ne pouvait jamais passer. La largeur de la
		// première colonne, c'est `ColumnX(1)` — ou `columns[0].width`.
		std::cout << "column0 width after drag=" << t.Unwrap()->ColumnX(1) << std::endl;
		assert(t.Unwrap()->ColumnX(0) == 0.f);
		assert(t.Unwrap()->ColumnX(1) > 130.f);
		assert(t.Unwrap()->columns[0].width == t.Unwrap()->ColumnX(1));
		std::cout << "§5 table column resize: OK\n";
	}

	std::cout << "ui_smoke_test_6: all sections passed\n";
	return 0;
}
