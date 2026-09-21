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
	[[nodiscard]] math::FVector3 GetTangent(float t) const noexcept {
		constexpr float DELTA = 1e-4f;
		float t0 = sdl3::Max(0.f, t - DELTA);
		float t1 = sdl3::Min(1.f, t + DELTA);
		return (GetPoint(t1) - GetPoint(t0)).Normalize();
	}

	struct FrenetFrame {
		math::FVector3 tangent, normal, binormal;
	};

	/// Repères de Frenet le long de la courbe, un par point d'échantillon
	/// (`segments + 1` au total) — méthode du transport parallèle (three.js
	/// computeFrenetFrames) : évite les à-coups de la formule "normale =
	/// dérivée de la tangente" quand la courbure s'annule (segment droit).
	[[nodiscard]] std::vector<FrenetFrame> ComputeFrenetFrames(int segments, bool closed) const noexcept {
		std::vector<FrenetFrame> frames(size_t(segments) + 1);

		math::FVector3 tangent0 = GetTangent(0.f);
		// Premier vecteur "normal" arbitraire, seulement contraint à ne pas
		// être colinéaire à la tangente.
		math::FVector3 seed{1.f, 0.f, 0.f};
		if (std::abs(tangent0.Dot(seed)) > 0.9f)
			seed = {0.f, 1.f, 0.f};
		math::FVector3 normal0 = (seed - tangent0 * tangent0.Dot(seed)).Normalize();

		frames[0].tangent = tangent0;
		frames[0].normal = normal0;
		frames[0].binormal = tangent0.Cross(normal0);

		for (int i = 1; i <= segments; ++i) {
			float t = float(i) / float(segments);
			math::FVector3 tangent = GetTangent(t);
			math::FVector3 normal = frames[size_t(i) - 1].normal;
			// Projette la normale précédente sur le plan perpendiculaire à la
			// nouvelle tangente (transport parallèle), puis renormalise.
			normal = (normal - tangent * tangent.Dot(normal)).Normalize();
			frames[size_t(i)].tangent = tangent;
			frames[size_t(i)].normal = normal;
			frames[size_t(i)].binormal = tangent.Cross(normal);
		}

		if (closed) {
			// Referme la torsion accumulée en répartissant l'écart entre le
			// premier et le dernier repère sur tous les segments.
			float angleError = sdl3::Acos(sdl3::Clamp(frames[0].normal.Dot(frames.back().normal), -1.f, 1.f));
			angleError /= float(segments);
			if (frames[0].tangent.Cross(frames.back().normal).Dot(frames[0].normal) < 0.f)
				angleError = -angleError;
			for (int i = 1; i <= segments; ++i) {
				float angle = angleError * float(i);
				math::FQuaternion q = math::FQuaternion::FromAxisAngle(frames[size_t(i)].tangent, angle);
				frames[size_t(i)].normal = q.Rotate(frames[size_t(i)].normal);
				frames[size_t(i)].binormal = frames[size_t(i)].tangent.Cross(frames[size_t(i)].normal);
			}
		}

		return frames;
	}
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

	[[nodiscard]] math::FVector3 GetPoint(float t) const noexcept override {
		if (m_points.size() < 2)
			return m_points.empty() ? math::FVector3{} : m_points[0];

		size_t segmentCount = m_closed ? m_points.size() : m_points.size() - 1;
		float scaled = sdl3::Clamp(t, 0.f, 1.f) * float(segmentCount);
		size_t segment = size_t(scaled);
		if (segment >= segmentCount)
			segment = segmentCount - 1;
		float localT = scaled - float(segment);

		auto at = [&](long index) -> const math::FVector3 & {
			long n = long(m_points.size());
			if (m_closed)
				index = ((index % n) + n) % n;
			else
				index = sdl3::Clamp(index, 0L, n - 1);
			return m_points[size_t(index)];
		};

		long i = long(segment);
		const math::FVector3 &p0 = at(i - 1);
		const math::FVector3 &p1 = at(i);
		const math::FVector3 &p2 = at(i + 1);
		const math::FVector3 &p3 = at(i + 2);

		float t2 = localT * localT, t3 = t2 * localT;
		return (p1 * 2.f + (p2 - p0) * localT + (p0 * 2.f - p1 * 5.f + p2 * 4.f - p3) * t2 +
			   (p1 * 3.f - p0 - p2 * 3.f + p3) * t3) *
			   0.5f;
	}
};

} // namespace render3d
