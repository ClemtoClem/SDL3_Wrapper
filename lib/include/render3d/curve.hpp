#pragma once
#include <vector>

#include "../math/math.hpp"
#include "../sdl3/stdinc.hpp"

namespace render3d {

/// Courbe 3D paramétrique (three.js Curve) — `GetPoint(t)` pour t dans
/// [0, 1]. Base pour Mesh::Tube()/Mesh::TorusKnot() (via ComputeFrenetFrames).
class Curve3 {
public:
	virtual ~Curve3() = default;
	[[nodiscard]] virtual math::FVector3 GetPoint(float t) const noexcept = 0;

	/// Tangente par différence finie centrée — suffisant pour un repère de
	/// Frenet approché (three.js fait la même chose par défaut).
	[[nodiscard]] math::FVector3 GetTangent(float t) const noexcept;

	struct FrenetFrame {
		math::FVector3 tangent, normal, binormal;
	};

	/// Repères de Frenet le long de la courbe, un par point d'échantillon
	/// (`segments + 1` au total) — méthode du transport parallèle (three.js
	/// computeFrenetFrames) : évite les à-coups de la formule "normale =
	/// dérivée de la tangente" quand la courbure s'annule (segment droit).
	[[nodiscard]] std::vector<FrenetFrame> ComputeFrenetFrames(int segments, bool closed) const noexcept;
};

/// Segment de droite — la courbe la plus simple, utile en test et comme
/// chemin trivial pour Mesh::Tube().
class LineCurve3 : public Curve3 {
	math::FVector3 m_start, m_end;

public:
	LineCurve3(math::FVector3 start, math::FVector3 end) noexcept : m_start(start), m_end(end) {}
	[[nodiscard]] math::FVector3 GetPoint(float t) const noexcept override { return m_start.Lerp(m_end, t); }
};

/// Spline de Catmull-Rom passant par tous les points de contrôle donnés
/// (contrairement à une courbe de Bézier) — la courbe "chemin libre" typique
/// pour Mesh::Tube()/Extrude-le-long-d'un-chemin.
class CatmullRomCurve3 : public Curve3 {
	std::vector<math::FVector3> m_points;
	bool m_closed;

public:
	explicit CatmullRomCurve3(std::vector<math::FVector3> points, bool closed = false) noexcept
		: m_points(std::move(points)), m_closed(closed) {}

	[[nodiscard]] math::FVector3 GetPoint(float t) const noexcept override;
};

} // namespace render3d
