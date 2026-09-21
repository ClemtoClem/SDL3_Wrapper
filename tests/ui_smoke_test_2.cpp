#include "sdl3/sdl3.hpp"
#include "ui/ui.hpp"
#include <cstdlib>
#include <iostream>

int main() {
	setenv("SDL_VIDEODRIVER", "dummy", 0);
	ecs::ArchetypeRegistry ar;
	ui::LayoutSystem layout;
	ui::UiFactory f(ar, layout);
	ui::SceneManager scenes(f);

	scenes.Add("menu", [&](ui::UiFactory &fac) {
		ecs::Entity root = fac.Panel()
							  .Pad(16)
							  .Gap(10)
							  .Offset(50, 50)
							  .W(ui::Dimension::Px(300))
							  .HAuto()
							  .Children(fac.Label("Titre"), fac.Button("Jouer").Name("play"))
							  .Spawn();
		return std::vector{root};
	});

	std::cout << "switchTo=" << scenes.SwitchTo("menu") << "\n";
	std::cout << "ran=" << layout.RunIfNeeded(ar, 800, 600) << "\n";

	ar.Query<ui::UiRect, ui::UiComputed>([&](ecs::Entity e, ui::UiRect &, ui::UiComputed &c) {
		std::cout << "e" << e.id << " screen=" << c.screen.x << "," << c.screen.y << " " << c.screen.w << "x"
				  << c.screen.h << " hidden=" << ar.HasComponent<ui::UiHidden>(e) << "\n";
	});
	auto play = ui::FindByName(ar, "play");
	std::cout << "play found=" << play.IsSome() << "\n";
	return 0;
}