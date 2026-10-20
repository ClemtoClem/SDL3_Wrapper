#pragma once
/**
 * Moteur d'inférence — ce qui rend la modélisation de SketchUp « simple » :
 * sous le curseur, le point proposé s'accroche tout seul à ce qui a du
 * sens (extrémité, milieu, arête, face, axe rouge/vert/bleu issu du point
 * précédent, origine, sol), avec une étiquette et une couleur pour le dire.
 *
 * Indépendant de toute caméra : l'appelant fournit le rayon de la souris
 * et une fonction qui projette un point du modèle en pixels écran. Tout est
 * exprimé dans le repère LOCAL du modèle (l'éditeur compose la transformation
 * de l'objet avec la caméra).
 */
#include <functional>

#include "model.hpp"

namespace modeling {

enum class InferenceKind : uint8_t {
	NONE,
	GROUND,     ///< plan du sol (y = groundY)
	ON_FACE,
	ON_EDGE,
	MIDPOINT,
	ENDPOINT,
	ORIGIN,
	ON_AXIS_X,  ///< axe rouge passant par le point d'ancrage
	ON_AXIS_Y,  ///< axe vert (vertical)
	ON_AXIS_Z,  ///< axe bleu
	LOCKED_AXIS, ///< axe imposé (Maj, flèches)
	ON_PLANE     ///< plan de travail d'une esquisse (InferenceSettings::workPlane)
};

struct Inference {
	Vec3 point;
	InferenceKind kind = InferenceKind::NONE;
	FaceId face = INVALID_ID;     ///< face sous le curseur (ou portant le point)
	EdgeId edge = INVALID_ID;     ///< arête accrochée (ON_EDGE, MIDPOINT)
	VertexId vertex = INVALID_ID; ///< sommet accroché (ENDPOINT)
	/// Normale du plan de travail : celle de la face touchée, sinon +Y.
	Vec3 planeNormal{0.f, 1.f, 0.f};

	[[nodiscard]] bool IsValid() const noexcept { return kind != InferenceKind::NONE; }
	[[nodiscard]] bool IsAxis() const noexcept {
		return kind == InferenceKind::ON_AXIS_X || kind == InferenceKind::ON_AXIS_Y || kind == InferenceKind::ON_AXIS_Z ||
			   kind == InferenceKind::LOCKED_AXIS;
	}
};

/// Étiquette affichée près du curseur (« Extrémité », « Sur l'axe rouge »…).
[[nodiscard]] const char *InferenceLabel(InferenceKind kind) noexcept;
/// Couleur conventionnelle de l'inférence (axes rouge/vert/bleu, etc.).
[[nodiscard]] Color InferenceColor(InferenceKind kind) noexcept;
/// Direction de l'axe d'une inférence d'axe (rouge = X, vert = Y, bleu = Z).
[[nodiscard]] Vec3 AxisDirection(InferenceKind kind) noexcept;

/// Projection d'un point du modèle en pixels ; NONE s'il est derrière la caméra.
using Projector = std::function<Option<math::FVector2>(const Vec3 &)>;

struct InferenceQuery {
	math::FRay ray;          ///< rayon de la souris, repère du modèle
	math::FVector2 cursor;   ///< position de la souris en pixels
	Projector project;       ///< modèle → pixels
	Option<Vec3> anchor = NONE;      ///< point précédent (inférences d'axes)
	Option<Vec3> lockedAxis = NONE;  ///< direction imposée depuis l'ancrage
};

struct InferenceSettings {
	float pixelTolerance = 12.f;
	float groundY = 0.f;
	/// Pas de la grille d'accroche sur le sol (0 = aucune).
	float gridStep = 0.f;
	bool inferAxes = true;
	bool inferOrigin = true;
	/// Plan d'ESQUISSE (point, normale) : tout point inféré y appartient —
	/// les accroches hors du plan sont ignorées, et le plan remplace le sol
	/// et les faces qui ne lui sont pas coplanaires.
	struct WorkPlane {
		Vec3 origin;
		Vec3 normal{0.f, 1.f, 0.f};
	};
	Option<WorkPlane> workPlane = NONE;
};

[[nodiscard]] Inference Infer(const Model &model, const InferenceQuery &query, const InferenceSettings &settings = {});

/// Point de la droite (origine, direction) le plus proche du rayon.
[[nodiscard]] Vec3 ClosestPointOnLine(const Vec3 &origin, const Vec3 &direction, const math::FRay &ray) noexcept;
/// Point du segment [a, b] le plus proche du rayon.
[[nodiscard]] Vec3 ClosestPointOnSegment(const Vec3 &a, const Vec3 &b, const math::FRay &ray) noexcept;
/// Intersection du rayon avec un plan (NONE si parallèle ou derrière).
[[nodiscard]] Option<Vec3> IntersectPlane(const math::FRay &ray, const Vec3 &point, const Vec3 &normal) noexcept;

} // namespace modeling
