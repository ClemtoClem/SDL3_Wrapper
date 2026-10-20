#pragma once
/**
 * Sauvegardes de mondes : un DOSSIER par monde.
 *
 *   <saves>/<monde>/
 *     world.json            métadonnées et paramètres (nom, graine,
 *                           générateur, mods utilisés, temps de jeu,
 *                           point d'apparition, réglages libres…)
 *     chunks/x_y_z.chunk    chunks MODIFIÉS (binaire compact : palette
 *                           d'identifiants TEXTUELS puis plages répétées —
 *                           un mod ajouté ou retiré ne décale aucun bloc)
 *     entities.json         état des entités persistantes (créatures,
 *                           objets au sol…) : position, vitesse, santé,
 *                           propriétés libres, inventaire
 *     players/<nom>.json    joueurs : position, regard, santé, inventaire
 *
 * Versions : SAVE_VERSION est la version écrite ; une sauvegarde plus
 * ancienne est MIGRÉE à l'ouverture, étape par étape (RegisterMigration),
 * avant toute lecture — puis réécrite au format courant à la sauvegarde
 * suivante. Un bloc inconnu (mod retiré) devient de l'air, signalé.
 */
#include <functional>
#include <map>
#include <vector>

#include <ecs/ecs.hpp>

#include "components.hpp"
#include "inventory.hpp"
#include "world.hpp"

namespace voxel {

inline constexpr int SAVE_VERSION = 2;
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

	[[nodiscard]] const String &Directory() const noexcept { return m_dir; }
	[[nodiscard]] bool Exists() const;

	/// Lit world.json (migré si besoin) ; NONE : nouveau monde.
	Result<Option<WorldMetadata>, String> LoadMetadata();
	Result<bool, String> SaveMetadata(const WorldMetadata &metadata);

	// ChunkStorage
	bool LoadChunk(Chunk &chunk) override;
	void SaveChunk(const Chunk &chunk) override;

	/// Encodage d'un chunk (palette + plages) — exposé pour les tests.
	[[nodiscard]] std::vector<uint8_t> EncodeChunk(const Chunk &chunk) const;
	bool DecodeChunk(const std::vector<uint8_t> &bytes, Chunk &chunk);

	/// Entités persistantes et joueurs.
	Result<bool, String> SaveEntities(ecs::ArchetypeRegistry &registry);
	/// Recrée les entités enregistrées ; rend leur nombre.
	Result<size_t, String> LoadEntities(ecs::ArchetypeRegistry &registry);
	Result<bool, String> SavePlayer(ecs::ArchetypeRegistry &registry, ecs::Entity player);
	/// Applique l'état enregistré au joueur ; faux s'il n'existe pas encore.
	bool LoadPlayer(ecs::ArchetypeRegistry &registry, ecs::Entity player);

	/// Migration de world.json de `fromVersion` à `fromVersion + 1` (les
	/// autres fichiers peuvent être réécrits par la fonction).
	using Migration = std::function<Option<String>(NodePtr &world, const String &directory)>;
	void RegisterMigration(int fromVersion, Migration migration);

	/// Avertissements (blocs inconnus…) depuis l'ouverture.
	[[nodiscard]] const std::vector<String> &Warnings() const noexcept { return m_warnings; }

	/// Mondes présents dans un dossier de sauvegardes.
	[[nodiscard]] static std::vector<String> ListWorlds(const String &savesDirectory);

private:
	[[nodiscard]] String ChunkPath(const ChunkPos &p) const;

	const ContentRegistry &m_content;
	String m_dir;
	std::map<int, Migration> m_migrations;
	std::vector<String> m_warnings;
	std::map<std::string, bool> m_reportedUnknown;
};

/// État d'une entité ↔ JSON (persistance, scripts).
[[nodiscard]] NodePtr EntityToJson(ecs::ArchetypeRegistry &registry, ecs::Entity e);
[[nodiscard]] ecs::Entity EntityFromJson(ecs::ArchetypeRegistry &registry, const ContentRegistry &content, const NodePtr &json);

} // namespace voxel
