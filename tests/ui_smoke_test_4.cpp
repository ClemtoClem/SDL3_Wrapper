// Smoke test : nouveaux widgets du module ui/ — ComboBox (overlay + clic
// consommé), ListBox (sélection + molette interne), Expander (replier),
// TabView (bascule d'onglet), Spinner (animation), Badge, Tooltip,
// état désactivé (UiDisabled), slider à pas, knob à plage, ImageFit.
#include "sdl3/sdl3.hpp"
#include "ui/ui.hpp"
#include <cassert>
#include <cstdlib>
#include <iostream>

static sdl3::FRect RectOf(ecs::ArchetypeRegistry &ar, const char *name) {
	auto e = ui::FindByName(ar, String(name));
	assert(e.IsSome());
	auto c = ar.GetComponent<ui::UiComputed>(e.Unwrap());
	assert(c.IsSome());
	return c.Unwrap()->screen;
}

static ecs::Entity EntOf(ecs::ArchetypeRegistry &ar, const char *name) {
	auto e = ui::FindByName(ar, String(name));
	assert(e.IsSome());
	return e.Unwrap();
}

// ── Fabrique de sdl3::Event synthétiques ────────────────────────────────────
// InputSystem consomme directement des sdl3::Event (plus d'InputState
// agrégée à construire) ; ces helpers simulent la boucle de poll SDL.

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
static sdl3::Event MouseMotionEv(float x, float y) {
	sdl3::Event ev{};
	ev.raw.type = SDL_EVENT_MOUSE_MOTION;
	ev.raw.motion.x = x;
	ev.raw.motion.y = y;
	return ev;
}
static sdl3::Event WheelEv(float y) {
	sdl3::Event ev{};
	ev.raw.type = SDL_EVENT_MOUSE_WHEEL;
	ev.raw.wheel.y = y;
	return ev;
}

int main() {
	setenv("SDL_VIDEODRIVER", "dummy", 0);

	auto sdl = sdl3::SdlContext::Create(sdl3::init_flags::VIDEO);
	if (!sdl) {
		std::cerr << "sdl init failed\n";
		return 1;
	}
	auto winRes = sdl3::Window::Create("smoke-ui4", 800, 600, sdl3::window_flags::HIDDEN);
	if (!winRes) {
		std::cerr << "window failed\n";
		return 1;
	}
	auto renRes = sdl3::Renderer::Create(winRes.Value());
	if (!renRes) {
		std::cerr << "renderer failed\n";
		return 1;
	}
	auto &ren = renRes.Value();
	ui::SdlRendererBackend renBackend(ren);

	ecs::ArchetypeRegistry ar;
	ui::LayoutSystem layout;
	ui::InputSystem input;
	ui::RenderSystem render;
	ui::UiFactory f(ar, layout);

	// Petite texture pour exercer les 4 modes ImageFit au rendu.
	if (auto tex = sdl3::Texture::Create(ren, 8, 8))
		render.textures.insert_or_assign(String("tex"), std::move(tex.Value()));

	// ── Construction ─────────────────────────────────────────────────────────
	int comboSel = -1, listSel = -1, tabSel = -1;
	int disClicks = 0;
	float sliderVal = -1.f, knobVal = -1.f;
	bool expState = true;

	// Colonne 1 : le dropdown du combo (ouvert) recouvrira la listbox → test
	// de consommation du clic.
	f.Panel()
		.Offset(20, 20)
		.W(ui::Dimension::Px(360))
		.HAuto()
		.Pad(12)
		.Gap(10)
		.Children(f.Combo({String("Alpha"), String("Beta"), String("Gamma")}, 0)
					  .Name("combo")
					  .Tooltip("Choisir la langue")
					  .OnChange([&](float v) { comboSel = int(v); }),
				  f.Listbox({String("i0"), String("i1"), String("i2"), String("i3"), String("i4"), String("i5"),
							 String("i6"), String("i7"), String("i8"), String("i9")})
					  .Name("list")
					  .H(ui::Dimension::Px(120))
					  .OnChange([&](float v) { listSel = int(v); }),
				  f.Slider(0.f, 100.f, 0.f, 10.f).Name("sld").H(ui::Dimension::Px(20)).OnChange([&](float v) {
					  sliderVal = v;
				  }),
				  f.Knob(0.f, 100.f, 50.f).Name("knb").Size(48, 48).OnChange([&](float v) { knobVal = v; }),
				  f.Button("Désactivé").Name("dis").Disabled().OnClick([&] { disClicks++; }),
				  f.Spinner().Name("spin").Size(28, 28), f.Badge(String("7")).Name("badge"),
				  f.Image(String("tex"), 40, 40).Fit(ui::ImageFit::CONTAIN).Name("img"))
		.Spawn();

	// Colonne 2 : expander + tabview.
	f.Panel()
		.Offset(420, 20)
		.W(ui::Dimension::Px(340))
		.HAuto()
		.Pad(12)
		.Gap(10)
		.Children(f.Expander("Réglages", true)
					  .Name("exp1")
					  .OnToggle([&](bool b) { expState = b; })
					  .Children(f.Label("corps A").Name("exp1_a"), f.Label("corps B").Name("exp1_b")),
				  f.Expander("Fermé", false).Name("exp2").Children(f.Label("caché").Name("exp2_a")),
				  f.Tabview({String("Un"), String("Deux"), String("Trois")}, 0)
					  .Name("tabs")
					  .H(ui::Dimension::Px(140))
					  .OnChange([&](float v) { tabSel = int(v); })
					  .Children(f.Label("page 0").Name("tab0"), f.Label("page 1").Name("tab1"),
								f.Label("page 2").Name("tab2")))
		.Spawn();

	layout.Run(ar, 800, 600);

	auto press = [&](float x, float y) {
		input.HandleEvent(ar, MouseDownEv(x, y), layout);
		input.HandleEvent(ar, MouseUpEv(x, y), layout);
		if (layout.Dirty())
			layout.Run(ar, 800, 600);
	};

	// ── 1. Expander "fermé au spawn" : enfant caché ──────────────────────────
	assert(ar.HasComponent<ui::UiHidden>(EntOf(ar, "exp2_a")));
	std::cout << "expander replié au spawn: enfant caché ok\n";

	// ── 2. TabView au spawn : seul l'onglet 0 visible ────────────────────────
	assert(!ar.HasComponent<ui::UiHidden>(EntOf(ar, "tab0")));
	assert(ar.HasComponent<ui::UiHidden>(EntOf(ar, "tab1")));
	assert(ar.HasComponent<ui::UiHidden>(EntOf(ar, "tab2")));
	std::cout << "tabview au spawn: seul l'onglet actif visible ok\n";

	// ── 3. ComboBox : ouvrir, choisir l'item 2 en overlay, clic consommé ─────
	sdl3::FRect cr = RectOf(ar, "combo");
	press(cr.x + cr.w / 2, cr.y + cr.h / 2); // ouvre
	{
		auto cb = ar.GetComponent<ui::UiComboBox>(EntOf(ar, "combo"));
		assert(cb.IsSome() && cb.Unwrap()->open);
	}
	// Item 2 du dropdown — il recouvre la listbox : le clic ne doit PAS
	// sélectionner d'item de liste.
	float itemH = ar.GetComponent<ui::UiComboBox>(EntOf(ar, "combo")).Unwrap()->itemHeight;
	float iy = cr.y + cr.h + 2.f + 2.f * itemH + itemH / 2;
	press(cr.x + cr.w / 2, iy);
	{
		auto cb = ar.GetComponent<ui::UiComboBox>(EntOf(ar, "combo"));
		assert(cb.IsSome() && !cb.Unwrap()->open);
		assert(cb.Unwrap()->selected == 2);
	}
	assert(comboSel == 2);
	assert(listSel == -1); // clic consommé par l'overlay
	std::cout << "combo: selection overlay item 2 + clic consomme ok\n";

	// ── 4. ListBox : clic sur l'item 1, molette pour scroller ────────────────
	sdl3::FRect lr = RectOf(ar, "list");
	float lItemH = ar.GetComponent<ui::ListBox>(EntOf(ar, "list")).Unwrap()->itemHeight;
	press(lr.x + 20, lr.y + 1.5f * lItemH);
	assert(listSel == 1);
	{
		input.HandleEvent(ar, MouseMotionEv(lr.x + 20, lr.y + 30), layout);
		input.HandleEvent(ar, WheelEv(-2.f), layout); // vers le bas
		auto lb = ar.GetComponent<ui::ListBox>(EntOf(ar, "list"));
		std::cout << "listbox scroll=" << lb.Unwrap()->scroll << "\n";
		assert(lb.Unwrap()->scroll > 0.f);
	}
	std::cout << "listbox: selection + molette interne ok\n";

	// ── 5. Slider à pas : drag à 57 % → 60 (pas de 10) ───────────────────────
	sdl3::FRect sr = RectOf(ar, "sld");
	press(sr.x + sr.w * 0.57f, sr.y + sr.h / 2);
	std::cout << "slider (pas 10) = " << sliderVal << "\n";
	assert(sliderVal == 60.f);

	// ── 6. Knob à plage : drag -40 px → 50 + 40*0.5 = 70 ; molette +2 ────────
	sdl3::FRect kr = RectOf(ar, "knb");
	float kcx = kr.x + kr.w / 2, kcy = kr.y + kr.h / 2;
	{
		input.HandleEvent(ar, MouseDownEv(kcx, kcy), layout);
		input.HandleEvent(ar, MouseMotionEv(kcx, kcy - 40.f), layout);
		input.HandleEvent(ar, MouseUpEv(kcx, kcy - 40.f), layout);
	}
	std::cout << "knob apres drag = " << knobVal << "\n";
	assert(knobVal > 69.9f && knobVal < 70.1f);
	{
		input.HandleEvent(ar, MouseMotionEv(kcx, kcy), layout);
		input.HandleEvent(ar, WheelEv(1.f), layout);
	}
	std::cout << "knob apres molette = " << knobVal << "\n";
	assert(knobVal > 71.9f && knobVal < 72.1f);
	std::cout << "slider a pas + knob plage/molette ok\n";

	// ── 7. Désactivé : le clic ne fait rien ──────────────────────────────────
	sdl3::FRect dr = RectOf(ar, "dis");
	press(dr.x + dr.w / 2, dr.y + dr.h / 2);
	assert(disClicks == 0);
	{
		auto b = ar.GetComponent<ui::UiButton>(EntOf(ar, "dis"));
		assert(b.IsSome() && !b.Unwrap()->hovered);
	}
	std::cout << "bouton desactive inerte ok\n";

	// ── 8. Expander : replier via l'en-tête ──────────────────────────────────
	sdl3::FRect xr = RectOf(ar, "exp1");
	float oldH = xr.h;
	float headerH = ar.GetComponent<ui::UiExpander>(EntOf(ar, "exp1")).Unwrap()->headerHeight;
	press(xr.x + xr.w / 2, xr.y + headerH / 2);
	assert(!expState);
	assert(ar.HasComponent<ui::UiHidden>(EntOf(ar, "exp1_a")));
	assert(ar.HasComponent<ui::UiHidden>(EntOf(ar, "exp1_b")));
	sdl3::FRect xr2 = RectOf(ar, "exp1");
	std::cout << "expander h: " << oldH << " -> " << xr2.h << "\n";
	assert(xr2.h < oldH);
	// Re-déplier.
	press(xr2.x + xr2.w / 2, xr2.y + headerH / 2);
	assert(expState);
	assert(!ar.HasComponent<ui::UiHidden>(EntOf(ar, "exp1_a")));
	std::cout << "expander replier/deplier ok\n";

	// ── 9. TabView : activer l'onglet 1 ──────────────────────────────────────
	sdl3::FRect tvr = RectOf(ar, "tabs");
	float tabH = ar.GetComponent<ui::UiTabView>(EntOf(ar, "tabs")).Unwrap()->tabHeight;
	press(tvr.x + tvr.w * 0.5f, tvr.y + tabH / 2); // milieu = onglet 1 (sur 3)
	assert(tabSel == 1);
	assert(ar.HasComponent<ui::UiHidden>(EntOf(ar, "tab0")));
	assert(!ar.HasComponent<ui::UiHidden>(EntOf(ar, "tab1")));
	assert(ar.HasComponent<ui::UiHidden>(EntOf(ar, "tab2")));
	std::cout << "tabview bascule d'onglet ok\n";

	// ── 10. Spinner : l'angle avance ─────────────────────────────────────────
	{
		float a0 = ar.GetComponent<ui::UiSpinner>(EntOf(ar, "spin")).Unwrap()->angle;
		input.Tick(ar, layout, 0.1f);
		float a1 = ar.GetComponent<ui::UiSpinner>(EntOf(ar, "spin")).Unwrap()->angle;
		std::cout << "spinner angle " << a0 << " -> " << a1 << "\n";
		assert(a1 != a0);
	}

	// ── 11. Badge : taille intrinsèque non nulle ─────────────────────────────
	sdl3::FRect br = RectOf(ar, "badge");
	assert(br.w > 0.f && br.h > 0.f);
	std::cout << "badge " << br.w << "x" << br.h << " ok\n";

	// ── 12. Tooltip : survol prolongé du combo ───────────────────────────────
	{
		cr = RectOf(ar, "combo");
		input.HandleEvent(ar, MouseMotionEv(cr.x + cr.w / 2, cr.y + cr.h / 2), layout);
		for (int i = 0; i < 4; ++i)
			input.Tick(ar, layout, 0.3f);
		assert(input.tooltip.visible);
		assert(input.tooltip.text == String("Choisir la langue"));
		// La souris part → l'infobulle disparaît.
		input.HandleEvent(ar, MouseMotionEv(1.f, 599.f), layout);
		input.Tick(ar, layout, 0.016f);
		assert(!input.tooltip.visible);
	}
	std::cout << "tooltip apres survol ok\n";

	// ── 13. Rendu : combo rouvert (overlay) + tooltip + ImageFit ─────────────
	{
		cr = RectOf(ar, "combo");
		input.HandleEvent(ar, MouseDownEv(cr.x + 5, cr.y + 5), layout);
		assert(ar.GetComponent<ui::UiComboBox>(EntOf(ar, "combo")).Unwrap()->open);
		ren.SetDrawColor(sdl3::Color{0, 0, 0});
		ren.Clear();
		render.Run(ar, renBackend, &input.tooltip);
		ren.Present();
	}
	std::cout << "rendu overlay (dropdown) + widgets ok\n";

	std::cout << "ui smoke test 4 done\n";
	return 0;
}
