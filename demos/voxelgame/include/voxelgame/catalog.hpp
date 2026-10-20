#pragma once
/**
 * Catalogues de l'application : mondes enregistrés et mods (installés ou
 * disponibles dans le dépôt local).
 *
 *  - Monde : dossier de sauvegarde (world.json, chunks, joueurs, vignette).
 *    Réglages du monde dans world.json « settings » : description, mode,
 *    difficulté, règles, mods activés, réglages des mods, ordre de chargement.
 *  - Mod : dossier avec mod.json (voxel::ModInfo) ; « installé » = présent
 *    dans le dossier des mods du jeu ; « disponible » = dans le dépôt
 *    (demos/voxelgame/repository), installable par copie.
 */
#include <vector>

#include <core/core.hpp>
#include <dataio/node.hpp>
#include <voxel/mods.hpp>

namespace voxelgame {

/// Problème de compatibilité d'un monde avec les mods installés.
struct WorldIssue {
	enum class Kind { MISSING_MOD, VERSION_CHANGED, NEWER_SAVE, CONFLICT };
	Kind kind;
	String modId, detail;
};

struct WorldEntry {
	String folder;        ///< nom du dossier (identifiant stable)
	String path;          ///< dossier complet
	String name, description;
	String created, lastPlayed; ///< ISO
	double playTime = 0.0;      ///< secondes
	String gameMode = "survival", difficulty = "normal";
	String generator;
	uint32_t seed = 0;
	int version = 0;
	uint64_t sizeBytes = 0;
	String thumbnail;     ///< vignette (vide si absente)
	std::vector<std::pair<String, String>> mods; ///< mods de la dernière session (id, version)
	std::vector<String> enabledMods;             ///< mods choisis pour ce monde (vide : tous)
	std::vector<WorldIssue> issues;
	dataio::NodePtr settings;                    ///< réglages complets (world.json « settings »)
};

class WorldCatalog {
public:
	explicit WorldCatalog(String savesDirectory) : m_dir(std::move(savesDirectory)) {}
	[[nodiscard]] const String &Directory() const noexcept { return m_dir; }

	/// Mondes, du plus récemment joué au plus ancien ; compatibilité évaluée
	/// par rapport à `installed`.
	[[nodiscard]] std::vector<WorldEntry> List(const std::vector<voxel::ModInfo> &installed) const;
	[[nodiscard]] Option<WorldEntry> Find(const String &folder, const std::vector<voxel::ModInfo> &installed) const;

	/// Nom de dossier libre dérivé d'un nom affiché (« Mon monde » → « mon_monde_2 »).
	[[nodiscard]] String FreeFolder(const String &name) const;
	/// Crée world.json (le terrain est généré à la première ouverture).
	Result<String, String> Create(const String &name, uint32_t seed, const String &generator, const dataio::NodePtr &settings) const;
	/// Remplace le nom affiché et/ou les réglages (world.json).
	Result<bool, String> Update(const String &folder, const Option<String> &name, const dataio::NodePtr &settings) const;
	Result<bool, String> Remove(const String &folder) const;
	/// Copie de sécurité dans `<saves>/../backups/<dossier>-<date>` ; rend son chemin.
	Result<String, String> Backup(const String &folder) const;
	Result<String, String> Duplicate(const String &folder) const;

private:
	[[nodiscard]] Option<WorldEntry> Read(const String &folder, const std::vector<voxel::ModInfo> &installed) const;
	String m_dir;
};

/// Mod du catalogue : installé, disponible dans le dépôt, ou les deux.
struct ModEntry {
	voxel::ModInfo info;          ///< version installée (sinon celle du dépôt)
	Option<voxel::ModInfo> available; ///< version du dépôt
	bool installed = false;
	bool builtin = false;         ///< « core » : ne peut pas être désinstallé
	[[nodiscard]] bool UpdateAvailable() const;
	[[nodiscard]] String Category() const;
};

class ModCatalog {
public:
	ModCatalog(std::vector<String> modDirectories, String repository)
		: m_modDirs(std::move(modDirectories)), m_repository(std::move(repository)) {}

	void Refresh();
	[[nodiscard]] const std::vector<ModEntry> &Entries() const noexcept { return m_entries; }
	[[nodiscard]] const ModEntry *Find(const String &id) const;
	[[nodiscard]] std::vector<voxel::ModInfo> Installed() const;
	[[nodiscard]] const std::vector<String> &ModDirectories() const noexcept { return m_modDirs; }

	/// Copie le mod du dépôt dans le premier dossier de mods (installation ou mise à jour).
	Result<bool, String> Install(const String &id);
	Result<bool, String> Uninstall(const String &id);

	/// Catégories reconnues (id → nom affiché).
	[[nodiscard]] static const std::vector<std::pair<String, String>> &Categories();
	[[nodiscard]] static String CategoryName(const String &id);

private:
	std::vector<String> m_modDirs;
	String m_repository;
	std::vector<ModEntry> m_entries;
};

/// Date ISO « 2026-10-20T14:21:05 » → « 20/10/2026 14:21 ».
[[nodiscard]] String FormatDate(const String &iso);
/// Durée en secondes → « 3 h 12 min ».
[[nodiscard]] String FormatDuration(double seconds);
[[nodiscard]] String FormatBytes(uint64_t bytes);
/// Nom lisible d'un mode et d'une difficulté.
[[nodiscard]] String GameModeName(const String &id);
[[nodiscard]] String DifficultyName(const String &id);

} // namespace voxelgame
