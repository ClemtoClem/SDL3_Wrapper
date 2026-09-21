// Smoke test : régression — un callback (onClick/onToggle/...) qui mute
// l'ECS de façon STRUCTURELLE (changement de scène, despawn) alors qu'il est
// invoqué depuis l'intérieur d'un world.Query<>() est un comportement
// indéfini : Query() itère `for (const auto& arch_ptr : archetypes)` sur un
// std::vector<unique_ptr<Archetype>>, et toute migration d'entité qui crée un
// nouvel archétype (get_or_create_archetype -> archetypes.push_back) peut
// réallouer ce vecteur et invalider la référence `arch_ptr` en cours
// d'itération — d'où des plantages aléatoires en usage réel (ex: bouton de
// navigation d'en-tête dont l'onClick appelle SceneManager::SwitchTo, qui
// ajoute/retire UiHidden sur des dizaines d'entités).
//
// InputSystem::dispatch() copie désormais chaque callback dans un vecteur
// `pending` exécuté APRÈS la fin de toutes les queries de l'évènement : ce
// test clique un bouton dont l'onClick fait exactement ce changement de
// scène structurel, et vérifie qu'aucune corruption n'en résulte.
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

int main() {
	setenv("SDL_VIDEODRIVER", "dummy", 0);

	auto sdl = sdl3::SdlContext::Create(sdl3::init_flags::VIDEO);
	if (!sdl) {
		std::cerr << "sdl init failed\n";
		return 1;
	}

	ecs::ArchetypeRegistry ar;
	ui::LayoutSystem layout;
	ui::InputSystem input;
	ui::UiFactory f(ar, layout);
	ui::SceneManager scenes(f);

	// Chaque scène ressemble à une page de ui_showcase : une grosse poignée
	// de widgets variés (donc de nombreux archétypes différents créés lors du
	// premier build), pour maximiser les chances de réallocation du vecteur
	// d'archétypes pendant le clic si le bug de ré-entrance était présent.
	auto buildPage = [&](ui::UiFactory &fac, const char *tag) {
		auto page = fac.Column();
		page.Pad(8).Gap(4).Offset(0, 60);
		for (int i = 0; i < 15; ++i) {
			page.Children(fac.Label(String(tag).Append(i)), fac.Button(String("b").Append(i)), fac.Toggle(i % 2 == 0),
						  fac.Checkbox(i % 3 == 0), fac.Slider(0.f, 1.f, 0.5f));
		}
		return std::vector{page.Spawn()};
	};
	scenes.Add("pageA", [&](ui::UiFactory &fac) { return buildPage(fac, "A"); });
	scenes.Add("pageB", [&](ui::UiFactory &fac) { return buildPage(fac, "B"); });
	scenes.Add("pageC", [&](ui::UiFactory &fac) { return buildPage(fac, "C"); });

	// Barre de nav : 3 boutons dont l'onClick appelle switchTo — EXACTEMENT
	// le schéma qui causait le plantage original (callback structurel
	// invoqué depuis l'intérieur de world.Query<UiButton, UiComputed>()).
	int switches = 0;
	auto nav = f.Row();
	nav.Gap(8).Offset(0, 0);
	nav.Children(f.Button("A").Name("navA").Size(80, 32).OnClick([&] {
		scenes.SwitchTo("pageA");
		++switches;
	}),
				 f.Button("B").Name("navB").Size(80, 32).OnClick([&] {
					 scenes.SwitchTo("pageB");
					 ++switches;
				 }),
				 f.Button("C").Name("navC").Size(80, 32).OnClick([&] {
					 scenes.SwitchTo("pageC");
					 ++switches;
				 }));
	nav.Spawn();

	scenes.SwitchTo("pageA");
	layout.Run(ar, 800, 600);

	auto click = [&](const char *navName) {
		auto e = ui::FindByName(ar, String(navName));
		assert(e.IsSome());
		auto c = ar.GetComponent<ui::UiComputed>(e.Unwrap());
		assert(c.IsSome());
		sdl3::FRect r = c.Unwrap()->screen;
		float cx = r.x + r.w / 2, cy = r.y + r.h / 2;
		input.HandleEvent(ar, MouseDownEv(cx, cy), layout);
		input.HandleEvent(ar, MouseUpEv(cx, cy), layout);
		if (layout.Dirty())
			layout.Run(ar, 800, 600);
	};

	// Bascule rapidement entre les 3 pages plusieurs fois : chaque clic
	// déclenche un changement de scène DEPUIS l'intérieur de la query du
	// bouton. Avant le correctif (callback appelé en direct dans la query),
	// ceci provoquait un segfault aléatoire ou un layout corrompu.
	for (int round = 0; round < 20; ++round) {
		click("navB");
		click("navC");
		click("navA");
	}
	std::cout << "60 clics de navigation (callback -> changement de scene structurel): ok, switches=" << switches
			  << "\n";
	assert(switches == 60);

	// L'ECS doit être dans un état cohérent : seule la scène active a ses
	// racines visibles, les deux autres sont cachées.
	assert(scenes.Get("pageA")->Visible());
	assert(!scenes.Get("pageB")->Visible());
	assert(!scenes.Get("pageC")->Visible());

	// L'ECS doit rester pleinement fonctionnel après toute cette réentrance
	// (pas de composant/table corrompus) : un nouveau bouton spawné et cliqué
	// après coup doit se comporter normalement.
	//
	// Placé à DROITE de la page active (qui fait ~176 px de large et descend
	// bien plus bas que 500 px) : depuis le blocage des évènements par ordre
	// d'affichage (cf. ui::HitTestTopMost), un bouton posé SOUS un widget de
	// la page ne recevrait plus le clic — et c'est voulu, l'utilisateur ne le
	// verrait pas. Le chevauchement était ici accidentel et sans rapport avec
	// ce que ce test vérifie (l'intégrité de l'ECS après réentrance).
	int postClicks = 0;
	ecs::Entity postBtn =
		f.Button("post").Name("postBtn").Size(80, 32).Offset(600, 500).OnClick([&] { ++postClicks; }).Spawn();
	layout.Run(ar, 800, 600);
	{
		auto c = ar.GetComponent<ui::UiComputed>(postBtn);
		assert(c.IsSome());
		sdl3::FRect r = c.Unwrap()->screen;
		input.HandleEvent(ar, MouseDownEv(r.x + r.w / 2, r.y + r.h / 2), layout);
		input.HandleEvent(ar, MouseUpEv(r.x + r.w / 2, r.y + r.h / 2), layout);
	}
	assert(postClicks == 1);
	std::cout << "widget spawne apres reentrance toujours fonctionnel: ok\n";

	std::cout << "ui smoke test 5 done\n";
	return 0;
}
