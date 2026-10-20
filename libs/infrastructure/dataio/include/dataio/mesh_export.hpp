#pragma once
/**
 * Export de maillages triangulés vers les formats d'échange 3D courants :
 * Wavefront OBJ (+ bibliothèque de matériaux .mtl), STL (binaire ou
 * ASCII) et PLY (ASCII). Comme le reste de dataio::, la sortie est
 * produite en mémoire (EncodeXxx) ou écrite dans un fichier (SaveMesh,
 * format choisi d'après l'extension).
 *
 * Le maillage est décrit par des tableaux plats (x, y, z…), ce qui permet
 * à n'importe quel module de l'exporter sans conversion : terrain de
 * generators::, maillage modélisé de modeling::, etc.
 */
#include <cstdint>
#include <span>
#include <vector>

#include <core/core.hpp>

namespace dataio {

/// Plage de triangles partageant un matériau (une ligne `usemtl` en OBJ).
struct MeshMaterialGroup {
	String name;
	float r = 0.8f, g = 0.8f, b = 0.8f, a = 1.f;
	uint32_t firstIndex = 0; ///< premier indice (multiple de 3)
	uint32_t indexCount = 0;
};

struct TriangleMeshView {
	std::span<const float> positions; ///< x, y, z par sommet (obligatoire)
	std::span<const float> normals;   ///< x, y, z par sommet (facultatif)
	std::span<const float> uvs;       ///< u, v par sommet (facultatif)
	std::span<const uint8_t> colors;  ///< r, g, b par sommet (facultatif)
	std::span<const uint32_t> indices;
	/// Matériaux (facultatif) : vide = un seul groupe sans matériau.
	std::span<const MeshMaterialGroup> materials;

	[[nodiscard]] size_t VertexCount() const noexcept { return positions.size() / 3; }
	[[nodiscard]] size_t TriangleCount() const noexcept { return indices.size() / 3; }
};

struct ObjOptions {
	String comment;     ///< ligne(s) d'en-tête « # … »
	String mtlFileName; ///< nom du .mtl référencé (`mtllib`) ; vide = pas de matériaux
};

/// OBJ ; les couleurs de sommets utilisent l'extension « v x y z r g b »
/// (Blender, MeshLab).
[[nodiscard]] String EncodeObj(const TriangleMeshView &mesh, const ObjOptions &options = {});
/// Bibliothèque de matériaux associée (couleur diffuse et opacité).
[[nodiscard]] String EncodeMtl(std::span<const MeshMaterialGroup> materials);
[[nodiscard]] String EncodeStlAscii(const TriangleMeshView &mesh, const String &solidName = String("mesh"));
/// STL binaire (80 octets d'en-tête, normales recalculées par triangle).
[[nodiscard]] std::vector<uint8_t> EncodeStlBinary(const TriangleMeshView &mesh);
[[nodiscard]] String EncodePly(const TriangleMeshView &mesh);

/// Écrit `mesh` dans `path` au format donné par l'extension (.obj, .stl,
/// .ply) ; un .obj avec matériaux écrit aussi son .mtl à côté. NONE en cas
/// de succès, sinon le message d'erreur.
[[nodiscard]] Option<String> SaveMesh(const String &path, const TriangleMeshView &mesh, const String &comment = String());

} // namespace dataio
