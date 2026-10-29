#pragma once
/**
 * voxelia — le jeu voxel de démonstration (« Voxelia »).
 *
 * Tout le CONTENU vient des mods (demos/voxelia/mods : JSON, images,
 * sons, modèles, scripts) ; ce programme ajoute à voxel::Game :
 *  - l'affichage (WorldRenderer : chunks, entités articulées, ciel, ombres),
 *  - l'interface complète (Shell + écrans, cf. screens.hpp) : menus, gestion
 *    des mondes et des mods, réglages, HUD, inventaire, fabrication,
 *    discussion, terminal, commerce…
 *  - les commandes du joueur (touches configurables),
 *  - le son (audio::AudioEngine, voix spatialisées, volumes par catégorie).
 *
 * Ligne de commande : voir Usage().
 */
#include <memory>
#include <vector>

#include <core/core.hpp>
#include <voxel/game.hpp>

namespace voxelia {

struct Options {
	String world;                                     ///< vide : menu principal (fenêtre) / « Monde » (headless)
	Option<uint32_t> seed = NONE;
	String generator;
	std::vector<String> mods;
	String saves = "data/voxelia/saves";
	String structures;                                ///< échange de structures entre mondes (vide : à côté de `saves`)
	String repository = "demos/voxelia/repository"; ///< mods disponibles à l'installation
	String cache = "data/voxelia/cache";           ///< monde de fond des menus
	String settingsPath = "data/voxelia/settings.json";
	String screenshots = "data/voxelia/screenshots";
	int view = 6;
	bool creative = false;
	bool headless = false;
	/// Simulation et maillage sur leurs fils (« voxel-update », « voxel-render ») ;
	/// faux (--threads=off) : tout sur le fil principal, l'un après l'autre.
	bool threaded = true;
	/// Fils de rendu qui maillent les chunks (--mesh-threads=N, avec --threads=on).
	/// Deux : le meilleur compromis mesuré sur 4 cœurs (un troisième dispute
	/// le processeur au fil principal et au pilote GPU).
	int meshThreads = 2;
	/// Passage de la vue 3D à l'interface (--viewport=direct|pipelined|readback).
	String viewport = "direct";
	long frames = 0;                                  ///< 0 : jusqu'à la fermeture
	int width = 1280, height = 760;
	std::vector<std::pair<long, String>> screenshotsAt; ///< image → fichier
	std::vector<std::pair<long, String>> commands;      ///< image → commande (/…) ou commande d'application (!…)
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
	int RunHeadless();
	int RunWindowed();

	Options m_options;
};

} // namespace voxelia
