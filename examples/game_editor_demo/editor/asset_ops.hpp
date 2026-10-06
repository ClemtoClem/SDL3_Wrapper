#pragma once
/**
 * game_editor::AssetOperations — modifier ce que montre le navigateur de
 * ressources : créer un dossier, renommer, dupliquer, supprimer, déplacer.
 *
 * Deux natures d'entrées, deux façons d'agir :
 *
 *  - les ÉLÉMENTS DU PROJET (`AssetEntry::managed` : scènes, scripts de la
 *    bibliothèque, scripts de jeu) — l'enregistrement du projet écrit
 *    lui-même leurs fichiers et efface ceux qu'il ne possède plus. On agit
 *    donc sur le projet (Runtime::RenameScene, RemoveScript…) ; le disque
 *    suit au prochain enregistrement. Ils restent dans leur dossier : les
 *    déplacer n'aurait pas de sens pour le projet, qui les range lui-même.
 *  - le reste, FICHIERS ET DOSSIERS du disque (pièces `.scene`, modules,
 *    textures du projet…) — opérations directes sur le disque.
 *
 * Seul le dossier du projet se modifie : les ressources partagées
 * (`assets/` du dépôt) sont en lecture seule, et la structure du projet
 * (son manifeste, ses dossiers `scenes/`, `scripts/`, `assets/`) ne se
 * renomme, ne se déplace ni ne se supprime.
 *
 * Chaque opération groupée traite ce qu'elle peut et rend, élément par
 * élément, ce qu'elle a refusé : une sélection mêlée n'échoue pas en bloc.
 * Sans widget : testée sans fenêtre.
 */
#include "assets.hpp"
#include "core/core.hpp"

#include <vector>

namespace game_editor {

class Runtime;

/// Bilan d'une opération groupée.
struct AssetOpReport {
	int done = 0;
	std::vector<String> errors; ///< « nom : raison », un par élément refusé
	std::vector<String> created; ///< emplacements créés (copies, déplacés) — à sélectionner ensuite

	[[nodiscard]] bool Ok() const noexcept { return errors.empty(); }
	/// Une ligne pour la barre d'état.
	[[nodiscard]] String Summary(const char* verb) const;
};

class AssetOperations {
public:
	AssetOperations(Runtime& runtime, const AssetBrowserModel& model)
		: m_runtime(runtime), m_model(model) {}

	/// Le dossier `location` (emplacement du navigateur) accepte-t-il des
	/// créations et des dépôts ?
	[[nodiscard]] bool IsWritableFolder(const String& location) const;

	/// Pourquoi `entry` ne peut pas subir `operation` (« rename », « delete »,
	/// « duplicate », « move ») — NONE : permis.
	[[nodiscard]] Option<String> WhyLocked(const AssetEntry& entry, const char* operation) const;

	/// Crée un dossier dans `parent` ; rend son emplacement.
	[[nodiscard]] Result<String, String> CreateFolder(const String& parent, const String& name);

	/// Renomme ; pour un fichier, l'extension est gardée si `name` n'en donne
	/// pas. Rend le nouvel emplacement.
	[[nodiscard]] Result<String, String> Rename(const AssetEntry& entry, const String& name);

	AssetOpReport Delete(const std::vector<AssetEntry>& entries);

	AssetOpReport Duplicate(const std::vector<AssetEntry>& entries);

	/// Déplace dans le dossier `destination` (emplacement du navigateur).
	AssetOpReport Move(const std::vector<AssetEntry>& entries, const String& destination);

	/// Nom valide pour un fichier ou un dossier (ni vide, ni `.`/`..`, ni
	/// séparateur, ni caractère réservé) ; NONE si valide, la raison sinon.
	[[nodiscard]] static Option<String> InvalidName(const String& name);

	/// « nom (copie).ext », puis « nom (copie 2).ext »… libre dans `folder`.
	[[nodiscard]] static String FreeCopyName(const String& folder, const String& name);

	/// Extension (point compris) d'un nom de fichier — `.gameplay.script`,
	/// `.script`, `.png` — ou vide.
	[[nodiscard]] static String ExtensionOf(const String& name);

private:
	/// Sur le disque, dans le dossier du projet (hors ressources partagées).
	[[nodiscard]] bool InsideProject(const String& path) const;
	/// Manifeste et dossiers `scenes/`, `scripts/`, `assets/` du projet.
	[[nodiscard]] bool IsProjectStructure(const String& path) const;

	Runtime& m_runtime;
	const AssetBrowserModel& m_model;
};

} // namespace game_editor
