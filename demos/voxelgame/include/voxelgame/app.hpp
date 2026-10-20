#pragma once
/**
 * voxelgame — le jeu voxel de démonstration.
 *
 * Tout le CONTENU vient des mods (demos/voxelgame/mods : JSON, images,
 * sons, modèles, scripts) ; ce programme n'ajoute à voxel::Game que :
 *  - l'affichage (WorldRenderer : chunks, entités, ciel),
 *  - l'interface : viseur, barre d'objets (icônes de l'atlas), santé,
 *    progression du minage, messages, console (T ou /), inventaire et
 *    fabrication (E), menu (Échap), informations (F3),
 *  - les commandes du joueur (souris + clavier, touches PHYSIQUES : ZQSD
 *    et WASD au même endroit),
 *  - le son (audio::AudioEngine, voix spatialisées).
 *
 * Ligne de commande : --world=NOM --seed=N --generator=ID --mods=DIR (répétable)
 * --saves=DIR --view=N --creative --frames=N --screenshot=N:FICHIER
 * --exec=N:/commande (répétable) --headless --width=W --height=H.
 */
#include <memory>
#include <vector>

#include <core/core.hpp>
#include <voxel/game.hpp>

namespace voxelgame {

struct Options {
	String world = "Monde";
	Option<uint32_t> seed = NONE;
	String generator;
	std::vector<String> mods;
	String saves = "data/voxelgame/saves";
	int view = 6;
	bool creative = false;
	bool headless = false;
	long frames = 0;                                  ///< 0 : jusqu'à la fermeture
	int width = 1280, height = 760;
	std::vector<std::pair<long, String>> screenshots; ///< image → fichier
	std::vector<std::pair<long, String>> commands;    ///< image → commande de console
	bool help = false;
};

/// Rend NONE si les options sont valides, sinon le message d'erreur.
[[nodiscard]] Option<String> ParseOptions(int argc, char **argv, Options &options);
[[nodiscard]] const char *Usage();

class App {
public:
	explicit App(Options options);
	~App();
	int Run();

private:
	int RunHeadless(voxel::Game &game);
	int RunWindowed(voxel::Game &game);
	void LoadGame(voxel::Game &game);

	Options m_options;
};

} // namespace voxelgame
