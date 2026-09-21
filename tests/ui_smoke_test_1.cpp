#include "sdl3/sdl3.hpp"
#include "ui/ui.hpp"
#include <cstdlib>
#include <iostream>

int main() {
    setenv("SDL_VIDEODRIVER", "dummy", 0);
    ecs::ArchetypeRegistry ar;
    ui::LayoutSystem layout;
    ui::UiFactory f(ar, layout);

    ecs::Entity root = f.Panel()
                          .Pad(16)
                          .Gap(10)
                          .Offset(50, 50)
                          .W(ui::Dimension::Px(300))
                          .HAuto()
                          .Children(f.Label("Titre").Name("title"), f.Button("Jouer").Name("play"))
                          .Spawn();

    std::cout << "root id=" << root.id << "\n";
    auto kids = ar.GetComponent<ui::UiChildren>(root);
    std::cout << "root has UiChildren=" << kids.IsSome();
    if (kids.IsSome())
        std::cout << " count=" << kids.Unwrap()->list.size();
    std::cout << "\n";

    layout.Run(ar, 800, 600);

    ar.Query<ui::UiRect, ui::UiComputed>([&](ecs::Entity e, ui::UiRect &r, ui::UiComputed &c) {
        std::cout << "e" << e.id << " screen=" << c.screen.x << "," << c.screen.y << " " << c.screen.w << "x"
                  << c.screen.h << " rectsize=" << r.size.x << "x" << r.size.y
                  << " hasParent=" << ar.HasComponent<ui::UiParent>(e) << "\n";
    });
    return 0;
}