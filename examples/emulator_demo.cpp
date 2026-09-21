/**
 * emulator_demo — émulateur Nintendo DS / Game Boy Advance / Game Boy Color
 * construit sur le wrapper C++23 de ce dépôt : `sdl3::` (fenêtre, rendu,
 * audio, réseau, manettes, journal), `ui::` (interface retenue sur ECS) et
 * `data::` (rapport JSON). Tout vit dans l'espace de noms `emulator_demo`.
 *
 * Ce fichier ne fait qu'analyser la ligne de commande et lancer
 * `emulator_demo::Demo`. Le reste est sous examples/emulator_demo/ :
 *
 *   emulator/              le cœur d'émulation (NDS/GBA dérivé de NooDS, GB/GBC)
 *   app/cli.hpp            options de ligne de commande
 *   app/demo.*             point d'entrée : modes, réglages, rapport, code de sortie
 *   app/emulator_session.* une partie : cœur, fil d'émulation, entrées, états
 *   app/application.*      la coquille fenêtrée « EmulOS »
 *   app/modals.*           boîtes de dialogue (ROMs, états, configuration…)
 *   app/game_view.*        affichage du framebuffer émulé
 *   app/report.hpp         rapport d'exécution (texte ou JSON)
 *
 * Quelques commandes utiles :
 *
 *   make emulator_demo && ./build/bin/emulator_demo --help
 *   ./build/bin/emulator_demo --list-roms
 *   ./build/bin/emulator_demo --headless --rom=assets/roms/jeu.gbc --frames=600 \
 *       --press=200:START --screenshot=600:captures/jeu.png --report=/dev/stdout
 *   xvfb-run -a ./build/bin/emulator_demo --rom=assets/roms/jeu.gba --frames=300 \
 *       --no-audio --screenshot=300:captures/fenetre.png --report=rapport.json --report-format=json
 */
#include <cstdio>

#include "emulator_demo/app/demo.hpp"

int main(int argc, char **argv) {
	auto options = emulator_demo::CommandLine::Parse(argc, argv);
	if (options.IsError()) {
		std::fprintf(stderr, "%s\n", options.Error().CStr());
		return 2;
	}
	emulator_demo::Demo demo(std::move(options).Unwrap(), emulator_demo::CommandLine::Rebuild(argc, argv));
	return demo.Run();
}
