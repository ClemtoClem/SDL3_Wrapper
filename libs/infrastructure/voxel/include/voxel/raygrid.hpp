#pragma once
/**
 * Grille des rayons réfléchis : ce que le rendu (render3d::VoxelReflections)
 * envoie au GPU pour que les surfaces qui réfléchissent (TextureDef::
 * reflectivity) lancent leurs rayons dans le monde.
 *
 * Chaque cellule est un texel de 4 entiers de 16 bits :
 *   x : le bloc (id numérique) s'il arrête les rayons, 0 sinon ;
 *   y : part de l'ambiante 0..255 — celle du ciel (ambiante et soleil),
 *       ou 255 dans une dimension sans ciel (la lumière ambiante y est la
 *       lumière de base, partout, comme pour les sommets des chunks) ;
 *   z : lumière des sources, rouge | vert << 8 ; w : bleu — 0..255 pour un
 *       éclairement de 0..4 (lighting.hpp, linéaire).
 * Le shader éclaire une face touchée avec la cellule DEVANT elle (celle d'où
 * vient le rayon), comme les sommets des chunks.
 *
 * Simplifications : seuls les cubes pleins arrêtent les rayons (le feuillage
 * découpé aussi, le shader testant l'alpha de sa texture) ; liquides, verre
 * et formes (escaliers, plantes, torches…) sont traversés. La lumière des
 * sources n'est pas occultée (portée seulement), la part de ciel est celle
 * de la colonne (transmissions des cellules au-dessus).
 */
#include <cstdint>
#include <span>

#include "world.hpp"

namespace voxel::raygrid {

inline constexpr int CHANNELS = 4;
/// Texels d'un chunk (CHUNK_SIZE³ × CHANNELS).
inline constexpr size_t CHUNK_TEXELS = size_t(CHUNK_SIZE) * size_t(CHUNK_SIZE) * size_t(CHUNK_SIZE) * size_t(CHANNELS);
/// Hauteur de colonne examinée au-dessus d'un chunk pour la part de ciel.
inline constexpr int SKY_SCAN = 64;

/// Le bloc arrête-t-il les rayons ? Cube plein, visible, ni liquide ni
/// translucide (le feuillage découpé aussi).
[[nodiscard]] bool StopsRays(const BlockDef &block) noexcept;

/// Remplit `out` (CHUNK_TEXELS entiers, x puis y puis z) pour le chunk
/// `chunk` ; `sky` faux : dimension sans ciel (part de l'ambiante 255
/// partout). Faux si le chunk n'est pas chargé (texels vides, ciel ouvert).
bool FillChunk(const World &world, const ChunkPos &chunk, bool sky, std::span<uint16_t> out);

} // namespace voxel::raygrid
