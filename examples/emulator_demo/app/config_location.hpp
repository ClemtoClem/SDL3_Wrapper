#pragma once
/**
 * Où lire le fichier de réglages.
 *
 * Il ne peut pas désigner son propre emplacement : quand l'utilisateur en
 * choisit un autre dans la fenêtre de configuration, le chemin est retenu dans
 * un petit fichier du dossier de préférences de l'application
 * (`config_location.txt`, à côté du cache des ROMs extraites). Ordre de
 * résolution au lancement :
 *   1. `--config=CHEMIN` en ligne de commande ;
 *   2. le chemin retenu, s'il y en a un ;
 *   3. `Settings::DEFAULT_CONFIG_PATH` (./saves/emulator_demo/config.ini).
 */
#include "core/core.hpp"
#include "sdl3/filesystem.hpp"
#include "sdl3/iostream.hpp"

#include "../emulator/settings.hpp"

namespace emulator_demo::app {

/// Fichier qui retient l'emplacement choisi (vide sans dossier de préférences).
[[nodiscard]] inline String ConfigLocationFile() {
	if (Option<String> pref = sdl3::filesystem::PrefPath("EmulOS", "emulator_demo"); pref.IsSome())
		return pref.Value() + "config_location.txt";
	return String();
}

/// Chemin retenu par la fenêtre de configuration, vide s'il n'y en a pas.
[[nodiscard]] inline String RememberedConfigPath() {
	const String file = ConfigLocationFile();
	if (file.IsEmpty())
		return String();
	auto bytes = sdl3::ReadFile(file);
	if (bytes.IsError())
		return String();
	const std::vector<uint8_t> &data = bytes.Value();
	return String(reinterpret_cast<const char *>(data.data()), data.size()).Trim();
}

/// Retient `path` pour les prochains lancements ; vide = revenir au défaut.
inline bool RememberConfigPath(const String &path) {
	const String file = ConfigLocationFile();
	if (file.IsEmpty())
		return false;
	if (path.IsEmpty() || path == Settings::DEFAULT_CONFIG_PATH) {
		(void)sdl3::filesystem::Remove(file);
		return true;
	}
	return sdl3::WriteFile(file, path.c_str(), path.GetSize());
}

/// Fichier de réglages à utiliser (cf. l'ordre en tête de fichier).
[[nodiscard]] inline String ResolveConfigPath(const String &commandLinePath) {
	if (!commandLinePath.IsEmpty())
		return commandLinePath;
	if (String remembered = RememberedConfigPath(); !remembered.IsEmpty())
		return remembered;
	return String(Settings::DEFAULT_CONFIG_PATH);
}

} // namespace emulator_demo::app
