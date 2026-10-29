#pragma once
/**
 * Sauvegardes de mondes : un DOSSIER par monde.
 *
 *   <saves>/<monde>/
 *     world.json      métadonnées et paramètres (nom, graine, générateur,
 *                     mods utilisés, temps de jeu, point d'apparition,
 *                     réglages libres…) — lisible, lu par les menus
 *     world.db        base sql:: (format binaire SQLB, somme de contrôle) :
 *       chunks    (pos PK « x,y,z » — « dimension|x,y,z » hors du monde
 *                 ordinaire —, x, y, z, data) chunks MODIFIÉS
 *                 (palette d'identifiants TEXTUELS puis plages répétées —
 *                 un mod ajouté ou retiré ne décale aucun bloc)
 *       entities  (uuid PK, kind, x, y, z, data JSON) entités persistantes
 *                 (« dimension » dans data : chacune reste dans la sienne)
 *       players   (name PK, data JSON, saved) joueurs
 *       containers (pos PK « x,y,z » — « dimension|x,y,z » —, data JSON)
 *                 contenu des rangements (coffres, armoires…)
 *       info      (key PK, value) format et version de la base
 *     world.db.bak    base précédente (repli si world.db est illisible)
 *
 * Écriture : les changements (chunks déchargés, entités, joueurs) restent
 * en mémoire jusqu'à Flush(), qui écrit TOUTE la base d'un coup, de façon
 * atomique (fichier temporaire puis renommage) : un arrêt brutal laisse la
 * sauvegarde précédente intacte, jamais un mélange d'états.
 *
 * Versions : SAVE_VERSION est la version écrite ; une sauvegarde plus
 * ancienne est MIGRÉE à l'ouverture, étape par étape (RegisterMigration),
 * avant toute lecture. La version 2 (un fichier par chunk, entities.json,
 * players/<nom>.json) est importée dans world.db puis ses fichiers supprimés.
 * Un bloc inconnu (mod retiré) devient de l'air, signalé.
 */
#include <functional>
#include <map>
#include <vector>

#include <ecs/ecs.hpp>
#include <sql/database.hpp>

#include "components.hpp"
#include "inventory.hpp"
#include "world.hpp"

namespace voxel {

inline constexpr int SAVE_VERSION = 3;
inline constexpr const char *SAVE_FORMAT = "voxel.world";

struct WorldMetadata {
	String name = "Monde";
	uint32_t seed = 1337;
	String generator;
	int version = SAVE_VERSION;
	String created, lastPlayed;  ///< dates ISO (heure locale)
	double playTime = 0.0;       ///< secondes cumulées
	double gameTime = 0.0;       ///< horloge du jeu (jour/nuit)
	math::FVector3 spawn{0.f, 64.f, 0.f};
	bool spawnKnown = false;
	std::vector<std::pair<String, String>> mods; ///< id, version
	NodePtr settings;            ///< paramètres libres du monde (mods, règles)
};

class WorldSave final : public ChunkStorage {
public:
	WorldSave(const ContentRegistry &content, String directory);
	/// Écrit les changements encore en mémoire (filet de sécurité : le jeu
	/// appelle Flush() lui-même à chaque sauvegarde).
	~WorldSave() override;
	WorldSave(const WorldSave &) = delete;
	WorldSave &operator=(const WorldSave &) = delete;

	[[nodiscard]] const String &Directory() const noexcept { return m_dir; }
	/// Dimension dont les chunks et les entités sont lus et écrits (vide :
	/// le monde ordinaire, clés d'origine).
	void SetDimension(const String &dimension) { m_dimension = dimension; }
	[[nodiscard]] const String &Dimension() const noexcept { return m_dimension; }
	[[nodiscard]] bool Exists() const;

	/// Lit world.json (migré si besoin) et ouvre world.db ; NONE : nouveau
	/// monde.
	Result<Option<WorldMetadata>, String> LoadMetadata();
	Result<bool, String> SaveMetadata(const WorldMetadata &metadata);

	/// Écrit world.db si quelque chose a changé depuis la dernière écriture.
	Result<bool, String> Flush();
	[[nodiscard]] bool HasUnsavedChanges() const noexcept { return m_dirty; }
	/// Nombre de chunks modifiés enregistrés.
	[[nodiscard]] size_t StoredChunkCount();
	/// Base sous-jacente (outils, diagnostics) ; ouverte au besoin.
	[[nodiscard]] sql::Database &Database();

	// ChunkStorage
	bool LoadChunk(Chunk &chunk) override;
	void SaveChunk(const Chunk &chunk) override;

	/// Encodage d'un chunk (palette + plages) — exposé pour les tests.
	[[nodiscard]] std::vector<uint8_t> EncodeChunk(const Chunk &chunk) const;
	bool DecodeChunk(const std::vector<uint8_t> &bytes, Chunk &chunk);

	/// Entités persistantes de la dimension courante (celles des autres
	/// dimensions restent dans la base) et joueurs.
	Result<bool, String> SaveEntities(ecs::ArchetypeRegistry &registry);
	/// Recrée les entités enregistrées de la dimension courante ; rend leur nombre.
	Result<size_t, String> LoadEntities(ecs::ArchetypeRegistry &registry);
	Result<bool, String> SavePlayer(ecs::ArchetypeRegistry &registry, ecs::Entity player);
	/// Applique l'état enregistré au joueur ; faux s'il n'existe pas encore.
	bool LoadPlayer(ecs::ArchetypeRegistry &registry, ecs::Entity player);
	/// Contenu d'un rangement de la dimension courante (Inventory::ToJson) ;
	/// nul s'il n'a jamais été enregistré. `contents` nul : supprimé.
	[[nodiscard]] NodePtr LoadContainer(const BlockPos &p);
	Result<bool, String> SaveContainer(const BlockPos &p, const NodePtr &contents);

	/// Migration de world.json de `fromVersion` à `fromVersion + 1` (les
	/// autres fichiers peuvent être réécrits par la fonction).
	using Migration = std::function<Option<String>(NodePtr &world, const String &directory)>;
	void RegisterMigration(int fromVersion, Migration migration);

	/// Avertissements (blocs inconnus…) depuis l'ouverture.
	[[nodiscard]] const std::vector<String> &Warnings() const noexcept { return m_warnings; }

	/// Mondes présents dans un dossier de sauvegardes.
	[[nodiscard]] static std::vector<String> ListWorlds(const String &savesDirectory);

private:
	[[nodiscard]] String DatabasePath() const;
	[[nodiscard]] String ChunkKey(const ChunkPos &p) const;
	[[nodiscard]] String ContainerKey(const BlockPos &p) const;
	/// Charge world.db (ou world.db.bak, ou une base neuve) une seule fois.
	Result<bool, String> OpenDatabase();
	void CreateSchema(sql::Database &db) const;
	/// Version 2 → 3 : chunks/, entities.json et players/ → world.db.
	Option<String> ImportLegacyFiles();

	const ContentRegistry &m_content;
	String m_dir;
	String m_dimension;
	sql::Database m_db;
	bool m_dbOpen = false;
	bool m_dirty = false;
	std::map<int, Migration> m_migrations;
	std::vector<String> m_warnings;
	std::map<std::string, bool> m_reportedUnknown;
};

/// État d'une entité ↔ JSON (persistance, scripts).
[[nodiscard]] NodePtr EntityToJson(ecs::ArchetypeRegistry &registry, ecs::Entity e);
[[nodiscard]] ecs::Entity EntityFromJson(ecs::ArchetypeRegistry &registry, const ContentRegistry &content, const NodePtr &json);

} // namespace voxel
