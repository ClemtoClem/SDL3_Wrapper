#pragma once
/**
 * Polygones 2D : aire signée, appartenance d'un point, triangulation avec
 * trous. Partagé par render3d (Shape2D, Mesh::Extrude) et modeling (faces
 * des maillages éditables) — une seule implémentation dans la couche
 * Foundation.
 */
#include <cstdint>
#include <vector>

#include "math.hpp"

namespace math {

/// Aire signée (positive pour un contour dans le sens trigonométrique).
[[nodiscard]] float SignedArea(const std::vector<FVector2> &polygon) noexcept;

/// Le point est-il à moins de `tolerance` d'un côté du polygone ?
[[nodiscard]] bool OnPolygonBorder(const std::vector<FVector2> &polygon, const FVector2 &p, float tolerance) noexcept;

/// Le point est-il strictement à l'intérieur (règle pair-impair) ?
[[nodiscard]] bool PointInPolygon(const std::vector<FVector2> &polygon, const FVector2 &p) noexcept;

/// Triangule un polygone simple avec trous par découpe d'oreilles ; chaque
/// trou est raccordé au contour par un pont « visible » (Eberly,
/// Triangulation by Ear Clipping) — robuste aux trous proches du contour
/// et aux contours concaves. Les indices ajoutés à `outIndices` se
/// rapportent à la concaténation [outer, holes[0], holes[1]…] ; les
/// triangles sont dans le sens trigonométrique. Le sens des contours
/// fournis est indifférent. Faux si rien n'a pu être triangulé.
bool TriangulatePolygon(const std::vector<FVector2> &outer, const std::vector<std::vector<FVector2>> &holes,
						std::vector<uint32_t> &outIndices);

} // namespace math
