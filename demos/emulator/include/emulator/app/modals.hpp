#pragma once
/**
 * Boîtes de dialogue de la coquille : ROMs, états sauvegardés, configuration,
 * codes de triche, réseau, à propos. Chacune est reconstruite à chaque
 * ouverture (son contenu dépend de l'état du disque ou de la session).
 */
#include <core/core.hpp>
#include <ui/widget.hpp>

namespace emulator::app {

class Application;

/// Construit la boîte `name` (roms | saves | config | cheats | network |
/// about) et rend sa racine modale, ou un widget invalide si le nom est
/// inconnu. Ne l'OUVRE pas (cf. Application::OpenModal).
[[nodiscard]] ui::Widget BuildModal(Application &app, const String &name);

/// Sélecteur de fichier natif pour lancer une ROM.
void OpenRomFileDialog(Application &app);

} // namespace emulator::app
