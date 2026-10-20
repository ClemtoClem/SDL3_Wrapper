#pragma once
/**
 * Primitives paramétriques — redéveloppement des générateurs d'ezGeometry
 * (ezEngine, Core/Graphics/Geometry.h : AddPyramid, AddHalfSphere,
 * AddCapsule, AddStairs, AddArch, AddTexturedRamp) qui manquent aux
 * fabriques de render3d::Mesh. Les formes déjà fournies par render3d
 * (boîte, sphère, cylindre, cône, tore, géosphère…) ne sont PAS
 * dupliquées : on les convertit avec modeling::FromTriangles.
 *
 * Contrairement à ezGeometry (qui produit des triangles), chaque primitive
 * est construite comme un modèle éditable avec les outils eux-mêmes
 * (profil + Pousser/Tirer, révolution) : ses faces restent planes et ses
 * surfaces courbes adoucies, prêtes à être retravaillées.
 *
 * Repère local : Y vers le haut, la primitive posée sur le plan y = 0 et
 * centrée sur l'axe Y (sauf mention contraire), puis placée par
 * `options.transform`.
 */
#include <vector>

#include "model.hpp"

namespace modeling {

struct PrimitiveOptions {
	math::FMatrix4 transform = math::FMatrix4::Identity();
	uint32_t material = INVALID_ID;
};

/// Pyramide à base carrée (ez AddPyramid).
Result<std::vector<FaceId>, String> AddPyramid(Model &model, float baseSize, float height, bool cap = true,
											   const PrimitiveOptions &options = {});

/// Demi-sphère posée sur sa base (ez AddHalfSphere).
Result<std::vector<FaceId>, String> AddHalfSphere(Model &model, float radius, int segments = 24, int stacks = 8,
												  bool cap = true, const PrimitiveOptions &options = {});

/// Capsule : cylindre de hauteur `height` coiffé de deux demi-sphères
/// (ez AddCapsule) ; hauteur totale height + 2 × radius.
Result<std::vector<FaceId>, String> AddCapsule(Model &model, float radius, float height, int segments = 24,
											   int stacks = 6, const PrimitiveOptions &options = {});

/// Escalier droit (ez AddStairs) : `size` = largeur (x), hauteur (y),
/// profondeur (z) ; les marches montent vers +z.
Result<std::vector<FaceId>, String> AddStairs(Model &model, const Vec3 &size, int steps,
											  const PrimitiveOptions &options = {});

/// Escalier hélicoïdal autour de l'axe Y (ez AddStairs avec courbure).
Result<std::vector<FaceId>, String> AddSpiralStairs(Model &model, float innerRadius, float outerRadius, float height,
													int steps, float totalAngle = 2.f * math::PI,
													const PrimitiveOptions &options = {});

/// Arche (ez AddArch) : demi-anneau de rayon extérieur width/2, épaisseur
/// `thickness`, profondeur `depth` (z), balayant `angle` radians.
Result<std::vector<FaceId>, String> AddArch(Model &model, float width, float thickness, float depth,
											int segments = 16, float angle = math::PI, const PrimitiveOptions &options = {});

/// Rampe (ez AddTexturedRamp) : coin de `size` montant vers +z.
Result<std::vector<FaceId>, String> AddRamp(Model &model, const Vec3 &size, const PrimitiveOptions &options = {});

} // namespace modeling
