#pragma once
/**
 * Mods : du CONTENU sans recompiler le moteur.
 *
 *   mods/<mod>/
 *     mod.json          {"id", "name", "version", "description",
 *                        "dependencies": [...], "optionalDependencies": [...]}
 *     **.json           définitions (blocs, objets, créatures, recettes,
 *                       biomes, structures, générateurs, configuration…),
 *                       un objet ou un tableau d'objets par fichier
 *     textures/…/x.png   enregistrées d'office : id « mod:nom_du_fichier »
 *     sounds/…/x.wav|ogg idem (sons), sauf ceux déjà définis en JSON
 *     models/…/x.obj|b3d idem (modèles)
 *     shaders/x.vert + x.frag   idem (shader « mod:x »)
 *     scripts/…/x.script, init.script   exécutés APRÈS les JSON du mod
 *
 * Les mods sont chargés dans l'ordre de leurs DÉPENDANCES (puis par nom) ;
 * une dépendance manquante ou circulaire écarte le mod (erreur). Un mod
 * peut SURCHARGER une définition d'un mod dont il dépend (même id).
 */
#include <vector>

#include <core/core.hpp>

#include "content.hpp"

namespace voxel {

struct ModInfo {
	String id, name, version, description, path;
	std::vector<String> dependencies, optionalDependencies;
	std::vector<String> scripts; ///< à exécuter (ordre : init.script puis scripts/ triés)
	int definitions = 0;
	bool enabled = true;
};

struct ModReport {
	std::vector<ModInfo> mods; ///< dans l'ordre de chargement
	std::vector<String> errors, warnings;
};

class ModLoader {
public:
	/// Mods trouvés dans ces dossiers (un sous-dossier par mod).
	[[nodiscard]] static std::vector<ModInfo> Discover(const std::vector<String> &directories);
	/// Ordre de chargement (dépendances d'abord) ; les mods impossibles à
	/// charger sont retirés avec une erreur.
	[[nodiscard]] static std::vector<ModInfo> Order(std::vector<ModInfo> mods, std::vector<String> &errors);
	/// Charge les définitions JSON et les ressources d'un mod.
	static void LoadDefinitions(ContentRegistry &content, ModInfo &mod, ModReport &report);
	/// Tout : découverte, ordre, définitions (les scripts restent à exécuter).
	[[nodiscard]] static ModReport LoadAll(ContentRegistry &content, const std::vector<String> &directories,
										   const std::vector<String> &disabled = {});
};

} // namespace voxel
