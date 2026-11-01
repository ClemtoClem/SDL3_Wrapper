#pragma once
/**
 * ReflectionVolume — la grille des rayons réfléchis autour de la caméra
 * (render3d::VoxelReflections, voxel/raygrid.hpp).
 *
 *  - 8 × 4 × 8 chunks (128 × 64 × 128 cellules), TORIQUE : la cellule (x, y,
 *    z) du monde vit au texel (x, y, z) & (taille − 1). Quand la caméra
 *    change de chunk, seuls les chunks qui entrent sont envoyés, à la place
 *    de ceux qui sortent.
 *  - Les chunks à envoyer (entrés, modifiés : révision changée, ou chargés
 *    depuis) partent quelques-uns par image, les plus proches d'abord, en
 *    une seule passe de copie.
 *  - La table des blocs (faces, teintes) et la carte de réflectivité des
 *    textures viennent de l'atlas et du mailleur : refaites à chaque
 *    rechargement du contenu.
 * Aucune texture ne réfléchit (TextureDef::reflectivity) : rien n'est créé
 * ni envoyé, le Canvas n'a pas de grille.
 */
#include <vector>

#include <render3d/canvas.hpp>
#include <voxel/game.hpp>
#include <voxel/meshing.hpp>

namespace voxelia {

class ReflectionVolume {
public:
	static constexpr int CHUNKS_X = 8, CHUNKS_Y = 4, CHUNKS_Z = 8;

	explicit ReflectionVolume(render3d::Canvas &canvas) : m_canvas(canvas) {}

	/// Table des blocs et carte de réflectivité (après l'atlas) ; grille
	/// vidée. Sans texture qui réfléchit : inactif.
	Result<bool, String> Initialize(const voxel::TextureAtlas &atlas, const voxel::ChunkMesher &mesher);
	/// Autre monde (dimension) : tout est à renvoyer.
	void Reset();
	/// Une image : chunks à envoyer (`budget` au plus), grille donnée au
	/// Canvas — ou retirée si `enabled` est faux. `skyColor` : linéaire ;
	/// `biome` : celui de la caméra (teintes de l'herbe, du feuillage, de l'eau
	/// dans les reflets — la table des blocs est refaite quand il change).
	void Update(const voxel::World &world, bool sky, const math::FVector3 &camera, bool enabled, int maxBounces,
				const math::FVector3 &skyColor, const voxel::BiomeDef *biome, size_t budget);

	/// Une texture au moins réfléchit (la grille existe).
	[[nodiscard]] bool Available() const noexcept { return m_grid.IsSome(); }
	/// Chunks de la grille pas encore à jour.
	[[nodiscard]] size_t PendingChunks() const noexcept { return m_pending; }
	/// Chunks envoyés depuis le début (rapport, tests).
	[[nodiscard]] size_t Uploads() const noexcept { return m_uploads; }

private:
	struct Slot {
		voxel::ChunkPos chunk{0, 0, 0};
		uint64_t revision = 0;
		bool filled = false, loaded = false;
	};
	[[nodiscard]] Slot &SlotOf(const voxel::ChunkPos &p);
	/// Table des blocs aux teintes de `biome`, envoyée dans m_blocks (créée au besoin).
	Result<bool, String> UploadBlockTable(const voxel::BiomeDef *biome);

	render3d::Canvas &m_canvas;
	Option<sdl3::GpuTexture> m_grid = NONE, m_blocks = NONE, m_reflectivity = NONE;
	int m_blocksPerRow = 256;
	const voxel::ChunkMesher *m_mesher = nullptr;
	const voxel::BiomeDef *m_biome = nullptr; ///< teintes de la table envoyée
	std::vector<Slot> m_slots;
	size_t m_pending = 0, m_uploads = 0;
};

} // namespace voxelia
