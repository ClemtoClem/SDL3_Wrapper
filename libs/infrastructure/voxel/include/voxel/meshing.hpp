#pragma once
/**
 * Maillage d'affichage — CPU seulement (testable) ; l'application envoie les
 * tampons au GPU.
 *
 *  - TextureAtlas : toutes les textures du contenu dans une seule image
 *    (tuiles carrées, mises à l'échelle au plus proche) ; une texture
 *    manquante reçoit le damier magenta.
 *  - ChunkMesher : un chunk → deux tampons, OPAQUE (y compris CUTOUT :
 *    pixels transparents écartés par le shader) et TRANSLUCIDE (eau, verre,
 *    glace : mélangés, dessinés après). Faces cachées par un bloc opaque
 *    retirées, faces entre deux blocs translucides identiques retirées,
 *    occlusion ambiante par sommet (coins assombris), lumière du CIEL par
 *    sommet (assombrie par les blocs opaques au-dessus, sur toute la hauteur
 *    chargée — estimée par le générateur au-delà), nodebox orientées
 *    (param), plantes en croix, modèles .obj/.b3d/json, eau abaissée sous
 *    l'air.
 *  - Formes isolées (entités, objets au sol, blocs qui tombent) : même code.
 *
 * Coordonnées : relatives à l'origine du chunk (précision des flottants).
 */
#include <array>
#include <unordered_map>
#include <vector>

#include "assets.hpp"
#include "world.hpp"

namespace voxel {

struct MeshVertex {
	math::FVector3 position{0.f, 0.f, 0.f};
	math::FVector3 normal{0.f, 1.f, 0.f};
	math::FVector2 uv{0.f, 0.f};
	std::array<uint8_t, 4> color{255, 255, 255, 255};
};

struct MeshBuffer {
	std::vector<MeshVertex> vertices;
	std::vector<uint32_t> indices;
	[[nodiscard]] bool IsEmpty() const noexcept { return indices.empty(); }
	void Clear() {
		vertices.clear();
		indices.clear();
	}
};

struct ChunkMesh {
	MeshBuffer opaque, translucent;
	uint64_t revision = 0;
	/// Blocs lumineux du chunk (centre, niveau 1..15) : lumières dynamiques.
	std::vector<std::pair<math::FVector3, int>> lights;
};

class TextureAtlas {
public:
	struct Rect {
		float u0 = 0.f, v0 = 0.f, u1 = 1.f, v1 = 1.f;
	};
	static constexpr const char *MISSING = "voxel:missing";

	/// Rassemble les textures référencées par le contenu (blocs, objets,
	/// entités, modèles) et toutes celles déclarées.
	void Build(AssetManager &assets, const ContentRegistry &content, int tileSize = 16);
	[[nodiscard]] Rect Lookup(const String &id) const;
	[[nodiscard]] bool Contains(const String &id) const;
	[[nodiscard]] const Image &Pixels() const noexcept { return m_image; }
	[[nodiscard]] int TileSize() const noexcept { return m_tile; }
	[[nodiscard]] size_t Count() const noexcept { return m_rects.size(); }

private:
	Image m_image;
	int m_tile = 16;
	std::unordered_map<std::string, Rect> m_rects;
};

class ChunkMesher {
public:
	ChunkMesher(const ContentRegistry &content, const TextureAtlas &atlas, AssetManager *assets = nullptr);

	[[nodiscard]] ChunkMesh Build(const World &world, const ChunkPos &chunk) const;

	/// Options de rendu (réglages graphiques).
	struct Options {
		bool ambientOcclusion = true;
		bool skyLight = true;     ///< grottes et surplombs assombris (cf. DimensionDef::skyLight)
		float crossDensity = 1.f; ///< part des plantes en croix affichées (0..1)
	};
	Options options;

	/// Forme isolée, centrée en x/z, base à y = 0 (entités, objets) ; `size`
	/// remplace celle de la forme pour un cube (collision des entités).
	[[nodiscard]] MeshBuffer BuildShape(const ShapeDesc &shape, const Appearance &look, uint8_t param = 0,
										const String &mod = String()) const;
	/// Un bloc en miniature (objet au sol, bloc qui tombe), centré, base à 0.
	[[nodiscard]] MeshBuffer BuildBlock(BlockId block) const;

private:
	struct BlockRender {
		bool visible = false, opaqueCube = false, translucent = false, fluid = false;
		ShapeKind shape = ShapeKind::NONE;
		std::array<TextureAtlas::Rect, FACE_COUNT> faces{};
		std::array<uint8_t, 4> tint{255, 255, 255, 255};
		std::array<uint8_t, 4> tinted{255, 255, 255, 255}; ///< teinte des faces `tintFaces` (biome)
		uint8_t biomeTint = 0; ///< 0 aucune, 1 herbe, 2 feuillage, 3 eau
		uint8_t tintFaces = 0;
		BlockId id = AIR;
	};
	/// Teintes d'un biome (herbe, feuillage, eau).
	[[nodiscard]] std::array<std::array<uint8_t, 3>, 3> BiomeColors(const BiomeDef *biome) const;

	void EmitBox(MeshBuffer &out, const Box &box, const BlockRender &render, const math::FVector3 &offset,
				 const std::function<bool(Face)> &cull, const std::function<std::array<float, 4>(Face, const math::FVector3 *)> &ao) const;
	void EmitCross(MeshBuffer &out, const BlockRender &render, const math::FVector3 &offset, float light = 1.f) const;
	void EmitModel(MeshBuffer &out, const String &model, const BlockRender &render, const math::FVector3 &offset, uint8_t param,
				   const String &mod, bool normalize, float light = 1.f) const;
	/// Couverture de chaque cellule du chunk bordé : nombre de blocs opaques
	/// au-dessus (plafonné à 255), sur les chunks chargés puis estimé.
	void SkyCover(const World &world, const ChunkPos &cp, std::vector<uint8_t> &cover) const;

	const ContentRegistry &m_content;
	const TextureAtlas &m_atlas;
	AssetManager *m_assets;
	std::vector<BlockRender> m_blocks;
	Image m_colormaps[3];
};

} // namespace voxel
