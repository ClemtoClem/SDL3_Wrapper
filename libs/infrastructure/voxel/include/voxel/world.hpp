#pragma once
/**
 * World — le monde voxel : chunks cubiques, blocs, chargement/déchargement
 * des zones, génération, persistance des modifications et événements.
 *
 *  - Les chunks (16³) naissent par CHARGEMENT (ChunkStorage : la sauvegarde
 *    les connaît, ils ont été modifiés) ou par GÉNÉRATION (ChunkGenerator).
 *  - UpdateLoadedArea charge les chunks autour des ancres (joueurs) — les
 *    plus proches d'abord, `budget` au plus par appel — et décharge ceux qui
 *    sont trop loin, en ENREGISTRANT ceux qui ont été modifiés.
 *  - Une structure générée qui déborde sur un chunk pas encore là y est
 *    écrite plus tard (écritures en attente), sans dépendre de l'ordre de
 *    génération.
 *  - Toute modification émet `onBlockChanged` (cause, ancien/nouveau bloc) ;
 *    chaque chunk porte une RÉVISION (le maillage d'affichage se reconstruit
 *    quand elle change ; une modification au bord d'un chunk fait aussi
 *    avancer les voisins, dont les faces visibles changent).
 *  - Un chunk qui ARRIVE (ou un bloc qui change d'opacité) fait avancer la
 *    révision de ses voisins et des chunks en dessous : leurs faces au bord,
 *    leur occlusion et leur lumière du ciel (cf. meshing.hpp) sont recalculées
 *    au fil du déplacement du joueur.
 *  - Les blocs qui tombent (sable…) et ceux qui exigent un support sont
 *    signalés à la simulation (`TakeUnstableBlocks`).
 */
#include <deque>
#include <memory>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include <events/signal.hpp>

#include "content.hpp"

namespace voxel {

class Chunk {
public:
	explicit Chunk(ChunkPos pos) : m_pos(pos) {}

	[[nodiscard]] ChunkPos Pos() const noexcept { return m_pos; }
	[[nodiscard]] BlockId Get(int x, int y, int z) const noexcept {
		return m_blocks.empty() ? m_uniform : m_blocks[size_t(LocalIndex(x, y, z))];
	}
	[[nodiscard]] uint8_t Param(int x, int y, int z) const noexcept {
		return m_params.empty() ? 0 : m_params[size_t(LocalIndex(x, y, z))];
	}
	void Set(int x, int y, int z, BlockId id, uint8_t param = 0);
	/// Remplit tout le chunk (génération).
	void Fill(BlockId id);

	/// Chunk d'un seul bloc (stockage compact : aucun tableau alloué).
	[[nodiscard]] bool IsUniform() const noexcept { return m_blocks.empty(); }
	[[nodiscard]] BlockId UniformBlock() const noexcept { return m_uniform; }
	[[nodiscard]] bool IsEmpty() const noexcept { return m_blocks.empty() ? m_uniform == AIR : m_nonAir == 0; }

	/// Avance à chaque changement (affichage à reconstruire).
	[[nodiscard]] uint64_t Revision() const noexcept { return m_revision; }
	void Touch() noexcept { ++m_revision; }
	/// Modifié depuis sa génération : à enregistrer.
	[[nodiscard]] bool IsModified() const noexcept { return m_modified; }
	void SetModified(bool modified) noexcept { m_modified = modified; }

	/// Données brutes (sauvegarde) : CHUNK_VOLUME ids, et params (vide = 0).
	[[nodiscard]] std::vector<BlockId> Blocks() const;
	[[nodiscard]] const std::vector<uint8_t> &Params() const noexcept { return m_params; }
	void Assign(std::vector<BlockId> blocks, std::vector<uint8_t> params);

private:
	ChunkPos m_pos;
	BlockId m_uniform = AIR;
	std::vector<BlockId> m_blocks; ///< vide : uniforme
	std::vector<uint8_t> m_params; ///< orientation, état (vide : tout à 0)
	int m_nonAir = 0;
	uint64_t m_revision = 1;
	bool m_modified = false;
};

enum class ChangeCause : uint8_t { GENERATION, PLAYER, ENTITY, SCRIPT, SIMULATION, COMMAND };

struct BlockChange {
	BlockPos pos;
	BlockId previous = AIR, current = AIR;
	uint8_t param = 0;
	ChangeCause cause = ChangeCause::SCRIPT;
};

class World;

/// Fabrique de chunks (cf. worldgen.hpp).
class ChunkGenerator {
public:
	virtual ~ChunkGenerator() = default;
	virtual void Generate(Chunk &chunk, World &world) = 0;
	/// Hauteur de surface approximative (apparition des joueurs).
	[[nodiscard]] virtual int SurfaceHeight(int /*x*/, int /*z*/) const { return 0; }
	/// Biome d'une colonne (nullptr : aucun).
	[[nodiscard]] virtual const BiomeDef *BiomeAt(int /*x*/, int /*z*/) const { return nullptr; }
	/// Hauteur estimée sous laquelle la colonne est couverte (terrain, voûte)
	/// quand les chunks au-dessus ne sont pas chargés ; NONE : ciel ouvert.
	[[nodiscard]] virtual Option<int> SkyCover(int /*x*/, int /*z*/) const { return NONE; }
};

/// Stockage persistant des chunks modifiés (cf. save.hpp).
class ChunkStorage {
public:
	virtual ~ChunkStorage() = default;
	/// Faux si la sauvegarde ne connaît pas ce chunk.
	virtual bool LoadChunk(Chunk &chunk) = 0;
	virtual void SaveChunk(const Chunk &chunk) = 0;
};

struct WorldSettings {
	String name = "Monde";
	uint32_t seed = 1337;
	String generator; ///< id d'un GeneratorDef (vide : le premier)
	NodePtr extra;    ///< paramètres libres (mods)
};

class World {
public:
	World(const ContentRegistry &content, WorldSettings settings = {});
	~World();

	[[nodiscard]] const ContentRegistry &Content() const noexcept { return m_content; }
	[[nodiscard]] const WorldSettings &Settings() const noexcept { return m_settings; }

	void SetGenerator(std::shared_ptr<ChunkGenerator> generator) { m_generator = std::move(generator); }
	[[nodiscard]] ChunkGenerator *Generator() const noexcept { return m_generator.get(); }
	void SetStorage(ChunkStorage *storage) { m_storage = storage; }

	// ── Blocs ───────────────────────────────────────────────────────────────

	/// AIR hors des chunks chargés (cf. IsLoaded).
	[[nodiscard]] BlockId GetBlock(const BlockPos &p) const;
	[[nodiscard]] uint8_t GetParam(const BlockPos &p) const;
	[[nodiscard]] const BlockDef &BlockAt(const BlockPos &p) const { return m_content.Block(GetBlock(p)); }
	[[nodiscard]] bool IsLoaded(const BlockPos &p) const { return m_chunks.contains(ChunkOf(p)); }
	/// Pose un bloc (chunk chargé ou généré à la demande) ; faux si rien n'a changé.
	bool SetBlock(const BlockPos &p, BlockId id, uint8_t param = 0, ChangeCause cause = ChangeCause::SCRIPT);
	bool SetBlock(const BlockPos &p, const String &id, uint8_t param = 0, ChangeCause cause = ChangeCause::SCRIPT);
	/// Écriture de génération : différée si le chunk n'existe pas encore,
	/// n'écrase un bloc solide que si `replaceSolid`.
	void GenerationWrite(const BlockPos &p, BlockId id, bool replaceSolid = true);
	/// Pose une structure, ancre en `origin` ; rotation 0..3 (quarts de tour autour de Y).
	void PlaceStructure(const StructureDef &structure, const BlockPos &origin, int rotation = 0, bool generation = true);
	/// Plus haut bloc solide de la colonne parmi les chunks chargés (NONE : aucun).
	[[nodiscard]] Option<int> HighestSolid(int x, int z, int fromY, int toY) const;

	// ── Chunks ──────────────────────────────────────────────────────────────

	[[nodiscard]] Chunk *GetChunk(const ChunkPos &p);
	[[nodiscard]] const Chunk *GetChunk(const ChunkPos &p) const;
	/// Charge ou génère le chunk sur place.
	Chunk &EnsureChunk(const ChunkPos &p);
	/// Zone de jeu autour des ancres : rayon horizontal/vertical en chunks.
	/// Rend le nombre de chunks chargés pendant cet appel.
	size_t UpdateLoadedArea(const std::vector<BlockPos> &anchors, int radius, int verticalRadius, size_t budget = 8);
	/// Décharge un chunk (enregistré s'il a été modifié).
	void UnloadChunk(const ChunkPos &p);
	[[nodiscard]] size_t LoadedChunkCount() const noexcept { return m_chunks.size(); }
	[[nodiscard]] std::vector<ChunkPos> LoadedChunks() const;
	/// Enregistre tous les chunks modifiés (sauvegarde) ; rend leur nombre.
	size_t SaveModifiedChunks();

	// ── Simulation ──────────────────────────────────────────────────────────

	/// Positions de blocs instables (tombants, sans support) apparues depuis
	/// le dernier appel.
	[[nodiscard]] std::vector<BlockPos> TakeUnstableBlocks();

	// ── Événements ──────────────────────────────────────────────────────────

	events::Signal<const BlockChange &> onBlockChanged;
	events::Signal<const ChunkPos &> onChunkLoaded;
	events::Signal<const ChunkPos &> onChunkUnloaded;

	[[nodiscard]] uint64_t GeneratedChunkCount() const noexcept { return m_generatedCount; }

private:
	void CheckStability(const BlockPos &p);
	void TouchNeighbors(const BlockPos &p);
	/// Chunk arrivé : ses six voisins (faces au bord, occlusion) et, s'il
	/// contient des blocs, les chunks en dessous (lumière du ciel).
	void TouchAround(const ChunkPos &p, bool below);
	/// Les chunks chargés sous `p` (colonnes 3 × 3) : lumière du ciel à revoir.
	void TouchBelow(const ChunkPos &p);

	const ContentRegistry &m_content;
	WorldSettings m_settings;
	std::shared_ptr<ChunkGenerator> m_generator;
	ChunkStorage *m_storage = nullptr;
	std::unordered_map<ChunkPos, std::unique_ptr<Chunk>> m_chunks;
	/// Écritures de génération en attente, par chunk : (indice local, bloc, remplace le solide).
	struct PendingWrite {
		int index;
		BlockId id;
		bool replaceSolid;
	};
	std::unordered_map<ChunkPos, std::vector<PendingWrite>> m_pending;
	std::deque<BlockPos> m_unstable;
	std::unordered_set<BlockPos> m_unstableSet;
	std::vector<ChunkPos> m_generatingStack; ///< chunks en cours de génération
	uint64_t m_generatedCount = 0;
};

} // namespace voxel
