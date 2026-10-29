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

class Schematic;

struct MeshVertex {
	math::FVector3 position{0.f, 0.f, 0.f};
	math::FVector3 normal{0.f, 1.f, 0.f};
	math::FVector2 uv{0.f, 0.f};
	std::array<uint8_t, 4> color{255, 255, 255, 255}; ///< teinte (sRGB) seulement : pas d'éclairage
	/// Lumière reçue (cf. lighting.hpp), en LINÉAIRE : rgb = éclairement
	/// direct des sources (occulté, Lambert), a = part du ciel × occlusion
	/// ambiante (multiplie ciel, soleil et ambiante dans le shader).
	std::array<float, 4> light{0.f, 0.f, 0.f, 1.f};
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
	/// Blocs lumineux (« light » > 0, sauf translucides) : à dessiner SANS
	/// éclairage, pleine luminosité de jour comme de nuit, textures « lueur ».
	MeshBuffer emissive;
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
	///
	/// Chaque texture garde sa résolution (puissance de deux, entre `tileSize`
	/// et `maxTileSize`) : une texture de 2048 px est réduite par moyenne de
	/// zones, une de 8 px agrandie au plus proche. Les tuiles, carrées et
	/// alignées sur leur taille, sont rangées en ordre de Morton : les niveaux
	/// de mipmap (Mipmaps) ne mélangent jamais deux tuiles voisines.
	void Build(AssetManager &assets, const ContentRegistry &content, int tileSize = 16, int maxTileSize = 0);
	[[nodiscard]] Rect Lookup(const String &id) const;
	[[nodiscard]] bool Contains(const String &id) const;
	/// Variantes d'une texture (« nom.1.png », « nom.2.png »… : une tirée au
	/// hasard pour chaque bloc posé) ; vide si elle n'en a pas.
	[[nodiscard]] std::vector<Rect> LookupVariants(const String &id, bool glow = false) const;
	/// Variante « lueur » d'une texture (blocs lumineux) : contraste étiré,
	/// couleurs ravivées et éclaircies ; la texture elle-même si elle n'existe pas.
	[[nodiscard]] Rect LookupGlow(const String &id) const;
	[[nodiscard]] static String GlowId(const String &id) { return String("glow|") + id; }
	/// Traitement « lueur » (exposé pour les tests).
	[[nodiscard]] static Image Glow(const Image &source);
	/// Réduction d'une image (première image d'une bande animée) en carré
	/// `size` × `size` : moyenne pondérée par l'alpha, ou plus proche voisin
	/// pour l'agrandir.
	[[nodiscard]] static Image Resample(const Image &source, int size);
	[[nodiscard]] const Image &Pixels() const noexcept { return m_image; }
	/// Niveaux de mipmap 1, 2… (moitié, quart… de Pixels), jusqu'à une tuile
	/// de base réduite à un pixel.
	[[nodiscard]] const std::vector<Image> &Mipmaps() const noexcept { return m_mipmaps; }
	/// Plus petite taille de tuile (les textures plus fines sont agrandies).
	[[nodiscard]] int TileSize() const noexcept { return m_tile; }
	[[nodiscard]] size_t Count() const noexcept { return m_rects.size(); }

private:
	Image m_image;
	std::vector<Image> m_mipmaps;
	int m_tile = 16;
	std::unordered_map<std::string, Rect> m_rects;
	std::unordered_map<std::string, std::vector<std::string>> m_variants; ///< texture → ids de ses variantes
};

class ChunkMesher {
public:
	ChunkMesher(const ContentRegistry &content, const TextureAtlas &atlas, AssetManager *assets = nullptr);

	[[nodiscard]] ChunkMesh Build(const World &world, const ChunkPos &chunk) const;

	/// Options de rendu (réglages graphiques).
	struct Options {
		bool ambientOcclusion = true;
		bool skyLight = true;     ///< part de ciel par sommet (sinon 1 partout, cf. DimensionDef::skyLight)
		bool blockLight = true;   ///< éclairement direct des sources (lighting.hpp)
		float crossDensity = 1.f; ///< part des plantes en croix affichées (0..1)
	};
	Options options;

	/// Forme isolée, centrée en x/z, base à y = 0 (entités, objets) ; `size`
	/// remplace celle de la forme pour un cube (collision des entités).
	[[nodiscard]] MeshBuffer BuildShape(const ShapeDesc &shape, const Appearance &look, uint8_t param = 0,
										const String &mod = String()) const;
	/// Assemblage de blocs (≤ 14 de côté) maillé comme un petit monde à part :
	/// faces, occlusion et lumière de ses propres sources (lampe de cabine),
	/// sommets relatifs à son coin minimal.
	[[nodiscard]] ChunkMesh BuildStructure(const Schematic &structure) const;
	/// Un bloc en miniature (objet au sol, bloc qui tombe), centré, base à 0.
	[[nodiscard]] MeshBuffer BuildBlock(BlockId block) const;
	/// Image d'une texture (icône d'outil, d'objet) en plaque mince : faces
	/// avant et arrière à l'image (pixels transparents découpés par le
	/// shader), et sur la tranche une facette par bord de pixel opaque, de sa
	/// couleur. Carré d'un bloc de côté dans le plan XY, centré en x, base à
	/// y = 0, épaisseur `thickness` en z.
	[[nodiscard]] MeshBuffer BuildSprite(const String &texture, float thickness = 1.f / 16.f) const;

private:
	struct BlockRender {
		bool visible = false, opaqueCube = false, translucent = false, fluid = false;
		ShapeKind shape = ShapeKind::NONE;
		std::array<TextureAtlas::Rect, FACE_COUNT> faces{};
		/// Variantes par face (tirées d'après la position du bloc) ; vides sinon.
		std::array<std::vector<TextureAtlas::Rect>, FACE_COUNT> variants{};
		bool varied = false;
		std::array<uint8_t, 4> tint{255, 255, 255, 255};
		std::array<uint8_t, 4> tinted{255, 255, 255, 255}; ///< teinte des faces `tintFaces` (biome)
		uint8_t biomeTint = 0; ///< 0 aucune, 1 herbe, 2 feuillage, 3 eau
		uint8_t tintFaces = 0;
		bool emissive = false; ///< bloc lumineux : pleine luminosité, textures « lueur »
		BlockId id = AIR;
	};
	/// Teintes d'un biome (herbe, feuillage, eau).
	[[nodiscard]] std::array<std::array<uint8_t, 3>, 3> BiomeColors(const BiomeDef *biome) const;

	/// `rotation` : quarts de tour du bloc (param & 3) — chaque face prend la
	/// texture et les UV de la face d'origine (façade d'un coffre, dessus d'un lit).
	void EmitBox(MeshBuffer &out, const Box &box, const BlockRender &render, const math::FVector3 &offset,
				 const std::function<bool(Face)> &cull, const std::function<std::array<float, 4>(Face, const math::FVector3 *)> &ao,
				 uint8_t rotation = 0) const;
	void EmitCross(MeshBuffer &out, const BlockRender &render, const math::FVector3 &offset, float light = 1.f) const;
	void EmitModel(MeshBuffer &out, const String &model, const BlockRender &render, const math::FVector3 &offset, uint8_t param,
				   const String &mod, bool normalize, float light = 1.f) const;

	const ContentRegistry &m_content;
	const TextureAtlas &m_atlas;
	AssetManager *m_assets;
	std::vector<BlockRender> m_blocks;
	Image m_colormaps[3];
};

} // namespace voxel
