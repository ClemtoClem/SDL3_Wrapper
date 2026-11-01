#pragma once
/**
 * Documents des éditeurs de biomes, de générateurs et de dimensions — leur
 * définition telle qu'écrite dans le mod (DefinitionDocument), plus :
 *
 *  - validation À BLANC (copie du contenu) : chargement, avertissements du
 *    contenu, références (blocs, biomes, générateur, structures, textures) ;
 *  - réglages de mod référencés (« {"setting": id, "fallback": …} ») et leur
 *    chemin, à montrer avec la valeur figée dans le monde (Game::ResolveSettings) ;
 *  - APERÇU sans toucher la partie : un monde autonome (sa propre copie du
 *    contenu, document chargé) de N × N chunks générés par le générateur que
 *    la dimension aurait (Game::DimensionGenerator, même graine), coupé à une
 *    hauteur au besoin (plafonds des labyrinthes et des cavernes) ;
 *  - graphe des dimensions : morts qui y envoient (« enterOnDeath »),
 *    ascenseurs (configuration « elevators.destinations », « from »), portails
 *    (blocs « portal.dimension »).
 */
#include <memory>
#include <vector>

#include <voxel/game.hpp>
#include <voxel/worldgen.hpp>

#include "definition_document.hpp"

namespace voxelia::studio {

enum class WorldgenKind : uint8_t { Biome, Generator, Dimension };
[[nodiscard]] const char *WorldgenKindId(WorldgenKind kind) noexcept; ///< "biome", "generator", "dimension"

struct WorldgenDiagnostic {
	enum class Severity : uint8_t { Warning, Error } severity = Severity::Warning;
	String message;
};

/// Réglage de mod référencé par une définition.
struct SettingRef {
	String path;     ///< chemin pointé dans le document (« maze.floors.count »)
	String id;       ///< id du réglage
	String fallback; ///< valeur de repli (JSON compact)
};

class WorldgenDocument : public DefinitionDocument {
public:
	WorldgenDocument() = default;
	[[nodiscard]] static Option<WorldgenDocument> FromContent(const voxel::ContentRegistry &content, WorldgenKind kind, const String &id);
	[[nodiscard]] static WorldgenDocument New(WorldgenKind kind, const String &id, const String &mod, const String &baseDir,
											  const String &file);

	[[nodiscard]] WorldgenKind Kind() const noexcept { return m_kind; }
	/// Le contenu `live` avec ce document chargé (aperçus, validation).
	[[nodiscard]] Result<voxel::ContentRegistry, String> Scratch(const voxel::ContentRegistry &live) const;
	[[nodiscard]] std::vector<WorldgenDiagnostic> Validate(const voxel::ContentRegistry &live) const;
	[[nodiscard]] std::vector<SettingRef> Settings() const;
	/// Dimension à montrer pour ce document : la dimension elle-même ; celle du
	/// biome (« dimension »), sinon celle dont le générateur liste le biome, sinon
	/// le monde ordinaire ; pour un générateur, la première dimension qui
	/// l'utilise, sinon le monde ordinaire.
	[[nodiscard]] String PreviewDimension(const voxel::ContentRegistry &content) const;

private:
	WorldgenKind m_kind = WorldgenKind::Biome;
};

// ── Aperçu ───────────────────────────────────────────────────────────────────

struct WorldgenPreview {
	std::unique_ptr<voxel::ContentRegistry> content; ///< la copie (le monde la référence)
	std::shared_ptr<voxel::TerrainGenerator> generator;
	std::unique_ptr<voxel::World> world;
	std::vector<voxel::ChunkPos> chunks;
	String dimension;
	voxel::BlockPos centre{0, 0, 0};
	int cutY = 0;             ///< au-dessus : vidé (plafonds)
	int minY = 0, maxY = 0;   ///< tranche générée
	double milliseconds = 0.0;
};

/// `side` × `side` chunks autour de (x, z) de `centre`, tranche verticale
/// choisie d'après le générateur (sol des étages d'un labyrinthe, surface
/// sinon) ; `cutY` NONE : le plafond du premier étage d'un labyrinthe, rien
/// sinon. Graine et générateur de `dimension` dans la partie `game`.
[[nodiscard]] Result<WorldgenPreview, String> BuildPreview(voxel::Game &game, const voxel::ContentRegistry &content, const String &dimension,
														   const voxel::BlockPos &centre, int side, Option<int> cutY);

// ── Graphe des dimensions ────────────────────────────────────────────────────

struct DimensionLink {
	String from, to; ///< « * » : de partout
	String kind;     ///< "death", "elevator", "portal"
	String label;
};
struct DimensionGraph {
	std::vector<String> nodes; ///< dimensions (le monde ordinaire d'abord)
	std::vector<DimensionLink> links;
};
[[nodiscard]] DimensionGraph BuildDimensionGraph(const voxel::ContentRegistry &content);

} // namespace voxelia::studio
