// Définitions de physics/narrowphase.hpp — fichier généré par splitter.py : le code
// vient tel quel de l'en-tête (seules les signatures sont réécrites).

#include "physics/narrowphase.hpp"

namespace physics {

// ── Manifold ─────────────────────────────────────────────────────────────────

void Manifold::AddPoint(const math::FVector3 &worldPoint, float penetration) noexcept {
	if (pointCount < MAX_MANIFOLD_POINTS)
		points[pointCount++] = {worldPoint, penetration};
}

math::FVector3 ClosestPointOnSegment(const math::FVector3 &p, const math::FVector3 &a, const math::FVector3 &b) noexcept {
	math::FVector3 ab = b - a;
	float lenSq = ab.LengthSq();
	if (lenSq < 1e-12f)
		return a;
	float t = sdl3::Clamp((p - a).Dot(ab) / lenSq, 0.f, 1.f);
	return a + ab * t;
}

void ClosestPointsSegmentSegment(const math::FVector3 &p1, const math::FVector3 &q1, const math::FVector3 &p2,
		const math::FVector3 &q2, math::FVector3 &c1,
		math::FVector3 &c2) noexcept {
	constexpr float EPS = 1e-8f;
	math::FVector3 d1 = q1 - p1, d2 = q2 - p2, r = p1 - p2;
	float a = d1.Dot(d1), e = d2.Dot(d2), f = d2.Dot(r);
	float s, t;

	if (a <= EPS && e <= EPS) {
		s = 0.f;
		t = 0.f;
	} else if (a <= EPS) {
		s = 0.f;
		t = sdl3::Clamp(f / e, 0.f, 1.f);
	} else {
		float c = d1.Dot(r);
		if (e <= EPS) {
			t = 0.f;
			s = sdl3::Clamp(-c / a, 0.f, 1.f);
		} else {
			float b = d1.Dot(d2);
			float denom = a * e - b * b;
			s = denom > EPS ? sdl3::Clamp((b * f - c * e) / denom, 0.f, 1.f) : 0.f;
			t = (b * s + f) / e;
			if (t < 0.f) {
				t = 0.f;
				s = sdl3::Clamp(-c / a, 0.f, 1.f);
			} else if (t > 1.f) {
				t = 1.f;
				s = sdl3::Clamp((b - c) / a, 0.f, 1.f);
			}
		}
	}
	c1 = p1 + d1 * s;
	c2 = p2 + d2 * t;
}

bool CollideSphereSphere(const Sphere &a, const Sphere &b, Manifold &out) noexcept {
	math::FVector3 delta = b.center - a.center;
	float distSq = delta.LengthSq();
	float radiusSum = a.radius + b.radius;
	if (distSq >= radiusSum * radiusSum)
		return false;

	float dist = delta.Length();
	math::FVector3 normal = dist > 1e-6f ? delta / dist : math::FVector3{0.f, 1.f, 0.f};
	float penetration = radiusSum - dist;

	out.normal = normal;
	out.pointCount = 0;
	math::FVector3 onA = a.center + normal * a.radius;
	math::FVector3 onB = b.center - normal * b.radius;
	out.AddPoint((onA + onB) * 0.5f, penetration);
	return true;
}

math::FVector3 ClosestPointOnBox(const math::FVector3 &point, const Box &box) noexcept {
	math::FVector3 d = point - box.center;
	math::FVector3 ax = box.AxisX(), ay = box.AxisY(), az = box.AxisZ();
	float lx = sdl3::Clamp(d.Dot(ax), -box.halfExtents.x, box.halfExtents.x);
	float ly = sdl3::Clamp(d.Dot(ay), -box.halfExtents.y, box.halfExtents.y);
	float lz = sdl3::Clamp(d.Dot(az), -box.halfExtents.z, box.halfExtents.z);
	return box.center + ax * lx + ay * ly + az * lz;
}

bool CollideSphereBox(const Sphere &sph, const Box &box, Manifold &out) noexcept {
	math::FVector3 d = sph.center - box.center;
	math::FVector3 ax = box.AxisX(), ay = box.AxisY(), az = box.AxisZ();
	float lx = d.Dot(ax), ly = d.Dot(ay), lz = d.Dot(az);
	float cx = sdl3::Clamp(lx, -box.halfExtents.x, box.halfExtents.x);
	float cy = sdl3::Clamp(ly, -box.halfExtents.y, box.halfExtents.y);
	float cz = sdl3::Clamp(lz, -box.halfExtents.z, box.halfExtents.z);
	bool inside = (cx == lx && cy == ly && cz == lz);

	math::FVector3 normal;
	float penetration;

	if (!inside) {
		math::FVector3 closest = box.center + ax * cx + ay * cy + az * cz;
		math::FVector3 delta = sph.center - closest; // points (roughly) box -> sphere
		float distSq = delta.LengthSq();
		if (distSq >= sph.radius * sph.radius)
			return false;
		float dist = sdl3::Sqrt(distSq);
		normal = dist > 1e-6f ? -(delta / dist) : math::FVector3{0.f, 1.f, 0.f}; // sphere -> box
		penetration = sph.radius - dist;
	} else {
		// Sphere centre is inside the box (deep-penetration edge case): push out
		// through whichever face is nearest. The normal must still point
		// sphere -> box, i.e. AGAINST that face's outward direction (it used to
		// point along it, which made the solver push the sphere deeper in).
		float px = box.halfExtents.x - sdl3::Abs(lx);
		float py = box.halfExtents.y - sdl3::Abs(ly);
		float pz = box.halfExtents.z - sdl3::Abs(lz);
		if (px <= py && px <= pz) {
			normal = (lx >= 0.f ? -ax : ax);
			penetration = px + sph.radius;
		} else if (py <= pz) {
			normal = (ly >= 0.f ? -ay : ay);
			penetration = py + sph.radius;
		} else {
			normal = (lz >= 0.f ? -az : az);
			penetration = pz + sph.radius;
		}
	}

	out.normal = normal;
	out.pointCount = 0;
	// Deepest point of the sphere INTO the box — the side facing the box, as
	// in CollideSphereSphere (it used to be the opposite side, which put the
	// solver's friction and torque lever on the wrong side of the sphere).
	out.AddPoint(sph.center + normal * sph.radius, penetration);
	return true;
}

bool CollideBoxBox(const Box &boxA, const Box &boxB, Manifold &out) noexcept {
	math::FVector3 axesA[3] = {boxA.AxisX(), boxA.AxisY(), boxA.AxisZ()};
	math::FVector3 axesB[3] = {boxB.AxisX(), boxB.AxisY(), boxB.AxisZ()};
	const math::FVector3 &ha = boxA.halfExtents;
	const math::FVector3 &hb = boxB.halfExtents;
	math::FVector3 t = boxB.center - boxA.center;

	constexpr float FACE_BIAS = 0.95f;
	float bestBiased = 1e30f;
	float bestOverlap = 1e30f;
	math::FVector3 bestAxis{0.f, 1.f, 0.f}; // oriented A -> B
	int bestCase = -1;                      // 0..2: face of A; 3..5: face of B; 6..14: edge i x edge j

	auto testAxis = [&](math::FVector3 axis, int caseId) -> bool {
		float len = axis.Length();
		if (len < 1e-6f)
			return true; // degenerate (near-parallel edges) — not a valid separating-axis candidate

		axis = axis / len;
		float ra = ha.x * sdl3::Abs(axesA[0].Dot(axis)) + ha.y * sdl3::Abs(axesA[1].Dot(axis)) +
				   ha.z * sdl3::Abs(axesA[2].Dot(axis));
		float rb = hb.x * sdl3::Abs(axesB[0].Dot(axis)) + hb.y * sdl3::Abs(axesB[1].Dot(axis)) +
				   hb.z * sdl3::Abs(axesB[2].Dot(axis));
		float dist = t.Dot(axis);
		float overlap = ra + rb - sdl3::Abs(dist);
		if (overlap < 0.f)
			return false; // separating axis found: boxes do not overlap at all

		float biased = caseId < 6 ? overlap : overlap / FACE_BIAS;
		if (biased < bestBiased) {
			bestBiased = biased;
			bestOverlap = overlap;
			bestAxis = dist >= 0.f ? axis : -axis; // orient A -> B
			bestCase = caseId;
		}
		return true;
	};

	for (int i = 0; i < 3; ++i)
		if (!testAxis(axesA[i], i))
			return false;
	for (int j = 0; j < 3; ++j)
		if (!testAxis(axesB[j], 3 + j))
			return false;
	for (int i = 0; i < 3; ++i)
		for (int j = 0; j < 3; ++j)
			if (!testAxis(axesA[i].Cross(axesB[j]), 6 + i * 3 + j))
				return false;

	out.normal = bestAxis;
	out.pointCount = 0;

	if (bestCase < 6) {
		// Face contact: reference box is A (case < 3) or B (case in [3,6)).
		bool refIsA = bestCase < 3;
		const Box &refBox = refIsA ? boxA : boxB;
		const Box &incBox = refIsA ? boxB : boxA;
		const math::FVector3 *refAxes = refIsA ? axesA : axesB;
		const math::FVector3 *incAxes = refIsA ? axesB : axesA;
		const math::FVector3 &refHalf = refIsA ? ha : hb;
		int refAxisIdx = bestCase % 3;

		// bestAxis points A -> B; the reference face's OUTWARD normal (pointing
		// away from the reference box, into the incident box) is bestAxis when
		// the reference is A, and -bestAxis when the reference is B.
		math::FVector3 refNormalOutward = refIsA ? bestAxis : -bestAxis;

		// Incident face: whichever of incBox's 6 face normals is most anti-parallel
		// to refNormalOutward (i.e. faces the reference box most directly).
		int incK = 0;
		float incSign = 1.f;
		float bestDot = 1e30f;
		for (int k = 0; k < 3; ++k) {
			for (float sign : {1.f, -1.f}) {
				float dp = (incAxes[k] * sign).Dot(refNormalOutward);
				if (dp < bestDot) {
					bestDot = dp;
					incK = k;
					incSign = sign;
				}
			}
		}

		int otherA = (incK + 1) % 3, otherB = (incK + 2) % 3;
		const math::FVector3 &incHalf = refIsA ? hb : ha;
		float heK = (&incHalf.x)[incK] * incSign;
		float heA = (&incHalf.x)[otherA];
		float heB = (&incHalf.x)[otherB];
		math::FVector3 faceCenter = incBox.center + incAxes[incK] * heK;
		math::FVector3 vA = incAxes[otherA] * heA;
		math::FVector3 vB = incAxes[otherB] * heB;

		math::FVector3 poly[MAX_MANIFOLD_POINTS] = {faceCenter + vA + vB, faceCenter + vA - vB,
													 faceCenter - vA + vB, faceCenter - vA - vB};
		int polyCount = 4;

		// Clip against the 4 side planes of the reference face (Sutherland-Hodgman).
		int refOtherA = (refAxisIdx + 1) % 3, refOtherB = (refAxisIdx + 2) % 3;
		float refHeA = (&refHalf.x)[refOtherA];
		float refHeB = (&refHalf.x)[refOtherB];

		auto clipPlane = [&](const math::FVector3 &normal, float offset) {
			math::FVector3 tmp[MAX_MANIFOLD_POINTS];
			int tmpCount = 0;
			for (int i = 0; i < polyCount; ++i) {
				const math::FVector3 &curr = poly[i];
				const math::FVector3 &next = poly[(i + 1) % polyCount];
				float dCurr = curr.Dot(normal) - offset;
				float dNext = next.Dot(normal) - offset;
				bool currIn = dCurr <= 0.f;
				bool nextIn = dNext <= 0.f;
				if (currIn && tmpCount < MAX_MANIFOLD_POINTS)
					tmp[tmpCount++] = curr;
				if (currIn != nextIn && tmpCount < MAX_MANIFOLD_POINTS) {
					float denom = dCurr - dNext;
					float frac = sdl3::Abs(denom) > 1e-8f ? dCurr / denom : 0.f;
					tmp[tmpCount++] = curr.Lerp(next, frac);
				}
			}
			polyCount = tmpCount;
			for (int i = 0; i < polyCount; ++i)
				poly[i] = tmp[i];
		};

		math::FVector3 refCenterVec = refBox.center;
		clipPlane(refAxes[refOtherA], refCenterVec.Dot(refAxes[refOtherA]) + refHeA);
		clipPlane(-refAxes[refOtherA], refHeA - refCenterVec.Dot(refAxes[refOtherA]));
		clipPlane(refAxes[refOtherB], refCenterVec.Dot(refAxes[refOtherB]) + refHeB);
		clipPlane(-refAxes[refOtherB], refHeB - refCenterVec.Dot(refAxes[refOtherB]));

		float refFaceOffset = refCenterVec.Dot(refNormalOutward) + (&refHalf.x)[refAxisIdx];
		for (int i = 0; i < polyCount; ++i) {
			float penetration = refFaceOffset - poly[i].Dot(refNormalOutward);
			if (penetration > -1e-4f)
				out.AddPoint(poly[i], sdl3::Max(penetration, 0.f));
		}

		if (out.pointCount == 0) // defensive fallback — shouldn't normally trigger given SAT confirmed overlap
			out.AddPoint((boxA.center + boxB.center) * 0.5f, bestOverlap);
	} else {
		// Edge-edge contact: approximate edge selection (pick, on each box, the
		// one of the 4 parallel edges along the winning axis that's nearest the
		// other box) then closest-point-between-segments for a single-point manifold.
		int i = (bestCase - 6) / 3;
		int j = (bestCase - 6) % 3;

		int otherIA = (i + 1) % 3, otherIB = (i + 2) % 3;
		math::FVector3 toB = boxB.center - boxA.center;
		float signA1 = axesA[otherIA].Dot(toB) >= 0.f ? 1.f : -1.f;
		float signA2 = axesA[otherIB].Dot(toB) >= 0.f ? 1.f : -1.f;
		math::FVector3 edgeACenter = boxA.center + axesA[otherIA] * (signA1 * (&ha.x)[otherIA]) +
									 axesA[otherIB] * (signA2 * (&ha.x)[otherIB]);
		math::FVector3 p1 = edgeACenter - axesA[i] * (&ha.x)[i];
		math::FVector3 q1 = edgeACenter + axesA[i] * (&ha.x)[i];

		int otherJA = (j + 1) % 3, otherJB = (j + 2) % 3;
		math::FVector3 toA = boxA.center - boxB.center;
		float signB1 = axesB[otherJA].Dot(toA) >= 0.f ? 1.f : -1.f;
		float signB2 = axesB[otherJB].Dot(toA) >= 0.f ? 1.f : -1.f;
		math::FVector3 edgeBCenter = boxB.center + axesB[otherJA] * (signB1 * (&hb.x)[otherJA]) +
									 axesB[otherJB] * (signB2 * (&hb.x)[otherJB]);
		math::FVector3 p2 = edgeBCenter - axesB[j] * (&hb.x)[j];
		math::FVector3 q2 = edgeBCenter + axesB[j] * (&hb.x)[j];

		math::FVector3 c1, c2;
		ClosestPointsSegmentSegment(p1, q1, p2, q2, c1, c2);
		out.AddPoint((c1 + c2) * 0.5f, bestOverlap);
	}

	return true;
}

bool CollideCapsuleSphere(const Capsule &cap, const Sphere &sph, Manifold &out) noexcept {
	math::FVector3 closest = ClosestPointOnSegment(sph.center, cap.PointA(), cap.PointB());
	Sphere effective{closest, cap.radius};
	return CollideSphereSphere(effective, sph, out);
}

bool CollideCapsuleCapsule(const Capsule &a, const Capsule &b, Manifold &out) noexcept {
	math::FVector3 c1, c2;
	ClosestPointsSegmentSegment(a.PointA(), a.PointB(), b.PointA(), b.PointB(), c1, c2);
	Sphere sa{c1, a.radius};
	Sphere sb{c2, b.radius};
	return CollideSphereSphere(sa, sb, out);
}

float BoxSignedDistance(const Box &box, const math::FVector3 &worldPoint) noexcept {
	const math::FVector3 d = worldPoint - box.center;
	const math::FVector3 q{sdl3::Abs(d.Dot(box.AxisX())) - box.halfExtents.x,
						   sdl3::Abs(d.Dot(box.AxisY())) - box.halfExtents.y,
						   sdl3::Abs(d.Dot(box.AxisZ())) - box.halfExtents.z};
	const math::FVector3 outside{sdl3::Max(q.x, 0.f), sdl3::Max(q.y, 0.f), sdl3::Max(q.z, 0.f)};
	const float inside = sdl3::Min(sdl3::Max(q.x, sdl3::Max(q.y, q.z)), 0.f);
	return outside.Length() + inside;
}

bool CollideCapsuleBox(const Capsule &cap, const Box &box, Manifold &out) noexcept {
	const math::FVector3 a = cap.PointA();
	const math::FVector3 b = cap.PointB();
	float lo = 0.f, hi = 1.f;
	for (int iteration = 0; iteration < 40; ++iteration) {
		const float m1 = lo + (hi - lo) / 3.f;
		const float m2 = hi - (hi - lo) / 3.f;
		if (BoxSignedDistance(box, a.Lerp(b, m1)) <= BoxSignedDistance(box, a.Lerp(b, m2)))
			hi = m2;
		else
			lo = m1;
	}
	const float best = (lo + hi) * 0.5f;
	if (!CollideSphereBox(Sphere{a.Lerp(b, best), cap.radius}, box, out))
		return false;

	for (float end : {0.f, 1.f}) {
		if (sdl3::Abs(end - best) < 1e-3f)
			continue;
		Manifold extra;
		if (CollideSphereBox(Sphere{end == 0.f ? a : b, cap.radius}, box, extra) &&
			extra.normal.Dot(out.normal) > 0.95f)
			out.AddPoint(extra.points[0].worldPoint, extra.points[0].penetration);
	}
	return true;
}

bool GenerateManifold(ecs::Entity entA, const Shape &shapeA, ecs::Entity entB, const Shape &shapeB, Manifold &out) noexcept {
	out.a = entA;
	out.b = entB;
	out.pointCount = 0;

	return std::visit(
		[&](const auto &sa) {
			return std::visit(
				[&](const auto &sb) -> bool {
					using TA = std::decay_t<decltype(sa)>;
					using TB = std::decay_t<decltype(sb)>;

					if constexpr (std::is_same_v<TA, Sphere> && std::is_same_v<TB, Sphere>) {
						return CollideSphereSphere(sa, sb, out);
					} else if constexpr (std::is_same_v<TA, Sphere> && std::is_same_v<TB, Box>) {
						return CollideSphereBox(sa, sb, out);
					} else if constexpr (std::is_same_v<TA, Box> && std::is_same_v<TB, Sphere>) {
						bool hit = CollideSphereBox(sb, sa, out); // computed sphere(B) -> box(A)
						if (hit)
							out.normal = -out.normal; // want A(box) -> B(sphere)
						return hit;
					} else if constexpr (std::is_same_v<TA, Box> && std::is_same_v<TB, Box>) {
						return CollideBoxBox(sa, sb, out);
					} else if constexpr (std::is_same_v<TA, Sphere> && std::is_same_v<TB, Capsule>) {
						bool hit = CollideCapsuleSphere(sb, sa, out); // computed capsule(B) -> sphere(A)
						if (hit)
							out.normal = -out.normal; // want A(sphere) -> B(capsule)
						return hit;
					} else if constexpr (std::is_same_v<TA, Capsule> && std::is_same_v<TB, Sphere>) {
						return CollideCapsuleSphere(sa, sb, out);
					} else if constexpr (std::is_same_v<TA, Capsule> && std::is_same_v<TB, Capsule>) {
						return CollideCapsuleCapsule(sa, sb, out);
					} else if constexpr (std::is_same_v<TA, Capsule> && std::is_same_v<TB, Box>) {
						return CollideCapsuleBox(sa, sb, out);
					} else if constexpr (std::is_same_v<TA, Box> && std::is_same_v<TB, Capsule>) {
						bool hit = CollideCapsuleBox(sb, sa, out); // computed capsule(B) -> box(A)
						if (hit)
							out.normal = -out.normal; // want A(box) -> B(capsule)
						return hit;
					} else {
						return false; // unreachable — every combination of the 3 shape kinds is handled above
					}
				},
				shapeB);
		},
		shapeA);
}

} // namespace physics
