/**
 * sandbox — éditeur de niveau 3D construit UNIQUEMENT à partir du
 * wrapper C++23 de ce dépôt : `ui::` (interface retenue sur ECS), `ecs::`,
 * `render3d::` (rendu SDL_GPU, portails, animation), `physics::` (solveur à
 * impulsions séquentielles), `data::` (JSON pour les projets) et
 * `script` — le langage de script interprété écrit pour cette démo.
 *
 * Ce fichier est volontairement minuscule : il ne fait qu'analyser la ligne de
 * commande et lancer `sandbox::App`. Tout le reste vit dans
 * sandbox/ :
 *
 *   cli.hpp            options de ligne de commande
 *   project.hpp        modèle de document (projet → scènes → nœuds, scripts)
 *   project_files.hpp  stockage sur disque : un dossier par projet (manifeste
 *                      .json, scenes/<nom>.scene, scripts/<nom>.script, assets/)
 *   runtime.hpp        document → scène vivante (Object3D / ECS / corps physiques) + API hôte des scripts
 *   panels.hpp         l'interface : page « Aucun projet ouvert », menus, docks, boîtes de fichiers
 *   scenarios.hpp      scénarios scriptés qui pilotent l'éditeur et prennent les captures
 *   report.hpp         rapport d'exécution (texte ou JSON)
 *   app.hpp            fenêtre, boucle d'images, captures, mode sans écran
 *
 * Le CONTENU (scènes, scripts) n'est plus dans le code : il vit dans les
 * projets de data/sandbox/projects/. Sans `--project`, l'éditeur
 * s'ouvre sur « Aucun projet ouvert » et propose d'en créer ou d'en ouvrir un.
 *
 * Quelques commandes utiles :
 *   ./cpp-maker build sandbox
 *   ./cpp-maker test sandbox
 *   ./cpp-maker run sandbox --project=project1
 *   ./cpp-maker run sandbox --help
 *   ./cpp-maker run sandbox --list-scenarios
 *   xvfb-run -a ./cpp-maker run sandbox --project=project1 --scenario=tour \
 *       --report=captures/rapport.json --report-format=json
 *   ./cpp-maker run sandbox --headless --project=project1 --scenario=smoke --report=/dev/stdout
 */
#include <cstdio>

#include <sandbox/app/app.hpp>

int main(int argc, char **argv) {
	auto options = sandbox::CommandLine::Parse(argc, argv);
	if (options.IsError()) {
		std::fprintf(stderr, "%s\n", options.Error().CStr());
		return 2;
	}

	sandbox::App app(std::move(options).Unwrap(), sandbox::CommandLine::Rebuild(argc, argv));
	return app.Run();
}
