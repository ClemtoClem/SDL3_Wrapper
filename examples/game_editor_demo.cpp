/**
 * game_editor_demo — éditeur de niveau 3D construit UNIQUEMENT à partir du
 * wrapper C++23 de ce dépôt : `ui::` (interface retenue sur ECS), `ecs::`,
 * `render3d::` (rendu SDL_GPU, portails, animation), `physics::` (solveur à
 * impulsions séquentielles), `data::` (JSON pour les projets) et
 * `data::script` — le langage de script interprété écrit pour cette démo.
 *
 * Ce fichier est volontairement minuscule : il ne fait qu'analyser la ligne de
 * commande et lancer `game_editor::App`. Tout le reste vit dans
 * examples/game_editor_demo/ :
 *
 *   cli.hpp            options de ligne de commande
 *   project.hpp        modèle de document (projet → scènes → nœuds, scripts)
 *   project_files.hpp  stockage sur disque : un dossier par projet (manifeste
 *                      .json, scenes/*.scene, scripts/*.script, assets/)
 *   runtime.hpp        document → scène vivante (Object3D / ECS / corps physiques) + API hôte des scripts
 *   panels.hpp         l'interface : page « Aucun projet ouvert », menus, docks, boîtes de fichiers
 *   scenarios.hpp      scénarios scriptés qui pilotent l'éditeur et prennent les captures
 *   report.hpp         rapport d'exécution (texte ou JSON)
 *   app.hpp            fenêtre, boucle d'images, captures, mode sans écran
 *
 * Le CONTENU (scènes, scripts) n'est plus dans le code : il vit dans les
 * projets de saves/game_editor_demo/projects/. Sans `--project`, l'éditeur
 * s'ouvre sur « Aucun projet ouvert » et propose d'en créer ou d'en ouvrir un.
 *
 * Quelques commandes utiles :
 *
 *   make game_editor_demo && ./build/debug/game_editor_demo
 *   ./build/debug/game_editor_demo --project=project1
 *   ./build/debug/game_editor_demo --help
 *   ./build/debug/game_editor_demo --list-scenarios
 *   xvfb-run -a ./build/debug/game_editor_demo --project=project1 --scenario=tour \
 *       --report=captures/rapport.json --report-format=json
 *   ./build/debug/game_editor_demo --headless --project=project1 --scenario=smoke --report=/dev/stdout
 */
#include <cstdio>

#include "game_editor_demo/app/app.hpp"

int main(int argc, char **argv) {
	auto options = game_editor::CommandLine::Parse(argc, argv);
	if (options.IsError()) {
		std::fprintf(stderr, "%s\n", options.Error().CStr());
		return 2;
	}

	game_editor::App app(std::move(options).Unwrap(), game_editor::CommandLine::Rebuild(argc, argv));
	return app.Run();
}
