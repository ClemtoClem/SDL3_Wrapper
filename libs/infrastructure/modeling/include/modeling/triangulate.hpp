#pragma once
/**
 * Triangulation d'un Model pour l'affichage, et lancer de rayon sur ses
 * faces.
 *
 * BuildMesh produit des tableaux prêts pour un tampon de sommets : une
 * entrée par coin de face (les normales ne sont donc partagées qu'entre
 * faces reliées par une arête LISSE, comme dans SketchUp), des triangles
 * groupés par matériau (un groupe = un appel de dessin), et pour chaque
 * triangle la face d'origine (sélection à la souris).
 */
#include <cstdint>
#include <vector>

#include <math/polygon.hpp>

#include "model.hpp"

namespace modeling {

struct MeshGroup {
	uint32_t start = 0; ///< premier indice
	uint32_t count = 0; ///< nombre d'indices
	uint32_t material = INVALID_ID;
};

struct MeshData {
	std::vector<Vec3> positions;
	std::vector<Vec3> normals;
	std::vector<math::FVector2> uvs;
	std::vector<Color> colors;
	std::vector<uint32_t> indices;
	std::vector<MeshGroup> groups;
	std::vector<FaceId> triangleFace; ///< face d'origine de chaque triangle

	[[nodiscard]] size_t TriangleCount() const noexcept { return indices.size() / 3; }
};

struct BuildOptions {
	/// Faces des calques masqués incluses quand même.
	bool includeHiddenLayers = false;
	/// Faces dessinées des deux côtés (face arrière dupliquée, normale inversée).
	bool doubleSided = false;
	/// Couleur des faces sans matériau.
	Color defaultColor{0.92f, 0.92f, 0.9f, 1.f};
};

[[nodiscard]] MeshData BuildMesh(const Model &model, const BuildOptions &options = {});

/// Arêtes à dessiner en contour (ni adoucies, ni sur un calque masqué).
[[nodiscard]] std::vector<EdgeId> VisibleEdges(const Model &model);

struct RayHit {
	FaceId face = INVALID_ID;
	float distance = 0.f;
	Vec3 point;
	Vec3 normal;
};

/// Face la plus proche touchée par le rayon (faces vues des deux côtés).
[[nodiscard]] Option<RayHit> Raycast(const Model &model, const math::FRay &ray, bool includeHiddenLayers = false);

} // namespace modeling
