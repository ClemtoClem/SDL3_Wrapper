#pragma once
/**
 * Fabriques des écrans de l'interface (un fichier source par famille).
 *
 *  menus      : accueil, sélection / création / réglages / renommage d'un monde
 *  mods       : navigateur, fiche, mods d'un monde, configuration,
 *               organisation, confirmation des changements
 *  réglages   : général, commandes, graphismes, audio, jeu, accessibilité ;
 *               saisie d'une touche, choix du skin
 *  système    : chargement, erreur, confirmation
 *  partie     : pause, inventaire, fabrication, discussion, terminal,
 *               informations du monde, commerce, dialogue
 */
#include <functional>
#include <memory>

#include <voxel/game.hpp>

#include "shell.hpp"

namespace voxelgame {

// Menus
std::unique_ptr<Screen> MakeMainMenu(Shell &shell);
std::unique_ptr<Screen> MakeWorldSelect(Shell &shell);
std::unique_ptr<Screen> MakeCreateWorld(Shell &shell);
/// `folder` vide : le monde de la partie en cours.
std::unique_ptr<Screen> MakeWorldSettings(Shell &shell, String folder);
std::unique_ptr<Screen> MakeRenameWorld(Shell &shell, String folder);

// Mods
/// Changements de mods d'un monde, résumés avant application.
struct ModChanges {
	String worldFolder, worldName;
	std::vector<String> added, removed, updated;
	std::vector<String> affectedDependencies, risks;
};
std::unique_ptr<Screen> MakeModBrowser(Shell &shell);
std::unique_ptr<Screen> MakeModDetails(Shell &shell, String id);
std::unique_ptr<Screen> MakeWorldMods(Shell &shell, String folder);
/// `worldFolder` vide : réglages globaux du mod.
std::unique_ptr<Screen> MakeModConfig(Shell &shell, String modId, String worldFolder);
std::unique_ptr<Screen> MakeModOrganize(Shell &shell);
std::unique_ptr<Screen> MakeModChanges(Shell &shell, ModChanges changes, std::function<void(bool backup)> apply);

// Réglages
enum class SettingsTab { GENERAL, CONTROLS, GRAPHICS, AUDIO, GAMEPLAY, ACCESSIBILITY };
std::unique_ptr<Screen> MakeSettings(Shell &shell, SettingsTab tab = SettingsTab::GENERAL);
std::unique_ptr<Screen> MakeKeyCapture(Shell &shell, String label, std::function<void(int)> done);
std::unique_ptr<Screen> MakeSkinPicker(Shell &shell);

// Système
std::unique_ptr<Screen> MakeConfirm(Shell &shell, ConfirmSpec spec);
std::unique_ptr<Screen> MakeLoading(Shell &shell, String folder);
std::unique_ptr<Screen> MakeError(Shell &shell, String title, String message, String details);

// Partie
std::unique_ptr<Screen> MakePause(Shell &shell);
std::unique_ptr<Screen> MakeInventory(Shell &shell);
std::unique_ptr<Screen> MakeCrafting(Shell &shell);
std::unique_ptr<Screen> MakeChat(Shell &shell, String prefix);
std::unique_ptr<Screen> MakeConsole(Shell &shell, String prefix);
std::unique_ptr<Screen> MakeWorldInfo(Shell &shell);
std::unique_ptr<Screen> MakeTrade(Shell &shell, voxel::UiRequest request);
std::unique_ptr<Screen> MakeDialog(Shell &shell, voxel::UiRequest request);

} // namespace voxelgame
