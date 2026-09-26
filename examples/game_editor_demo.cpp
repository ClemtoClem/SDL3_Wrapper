/**
 * game_editor_demo — éditeur de niveau 3D construit UNIQUEMENT à partir du
 * wrapper C++23 de ce dépôt : `ui::` (interface retenue sur ECS), `ecs::`,
 * `render3d::` (rendu SDL_GPU, portails, animation), `physics::` (solveur à
 * impulsions séquentielles), `data::` (JSON pour les projets) et
 * `data::script` — le langage de script interprété écrit pour cette démo.
 *
 * Ce fichier est volontairement minuscule : il ne fait qu'analyser la ligne de
 * commande et lancer `game_editor::App`. Tout le reste vit dans
 * examples/game_editor/ :
 *
 *   cli.hpp        options de ligne de commande
 *   project.hpp    modèle de document (projet → scènes → objets), sérialisé en JSON
 *   content.hpp    les trois scènes livrées + leurs scripts de gameplay
 *   runtime.hpp    document → scène vivante (Object3D / ECS / corps physiques) + API hôte des scripts
 *   panels.hpp     l'interface : menus, barre d'outils, docks à onglets, inspecteur, console, profileur
 *   scenarios.hpp  scénarios scriptés qui pilotent l'éditeur et prennent les captures
 *   report.hpp     rapport d'exécution (texte ou JSON)
 *   app.hpp        fenêtre, boucle d'images, captures, mode sans écran
 *
 * Quelques commandes utiles :
 *
 *   make game_editor_demo && ./build/bin/game_editor_demo
 *   ./build/bin/game_editor_demo --help
 *   ./build/bin/game_editor_demo --list-scenarios
 *   xvfb-run -a ./build/bin/game_editor_demo --scenario=tour \
 *       --report=captures/rapport.json --report-format=json
 *   ./build/bin/game_editor_demo --headless --scenario=smoke --report=/dev/stdout
 */
#include <cstdio>

#include "game_editor_demo/app.hpp"

int main(int argc, char **argv) {
	auto options = game_editor::CommandLine::Parse(argc, argv);
	if (options.IsError()) {
		std::fprintf(stderr, "%s\n", options.Error().CStr());
		return 2;
	}

	game_editor::App app(std::move(options).Unwrap(), game_editor::CommandLine::Rebuild(argc, argv));
	return app.Run();
}
