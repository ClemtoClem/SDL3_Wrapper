// Définitions de math/polygon.hpp
#include <math/polygon.hpp>

#include <algorithm>
#include <cmath>
#include <cstddef>

namespace math {

float SignedArea(const std::vector<FVector2> &points) noexcept {
	float area = 0.f;
	const size_t count = points.size();
	for (size_t i = 0; i < count; ++i) {
		const FVector2 &a = points[i];
		const FVector2 &b = points[(i + 1) % count];
		area += a.x * b.y - b.x * a.y;
	}
	return area * 0.5f;
}

bool OnPolygonBorder(const std::vector<FVector2> &poly, const FVector2 &p, float tolerance) noexcept {
	const size_t n = poly.size();
	for (size_t i = 0; i < n; ++i) {
		const FVector2 &a = poly[i];
		const FVector2 ab = poly[(i + 1) % n] - a;
		const float len2 = ab.LengthSq();
		const float t = len2 > 0.f ? Clamp((p - a).Dot(ab) / len2, 0.f, 1.f) : 0.f;
		if ((a + ab * t).DistanceSq(p) <= tolerance * tolerance)
			return true;
	}
	return false;
}

bool PointInPolygon(const std::vector<FVector2> &poly, const FVector2 &p) noexcept {
	bool inside = false;
	const size_t n = poly.size();
	for (size_t i = 0, j = n - 1; i < n; j = i++) {
		const FVector2 &a = poly[i];
		const FVector2 &b = poly[j];
		if ((a.y > p.y) != (b.y > p.y) && p.x < (b.x - a.x) * (p.y - a.y) / (b.y - a.y) + a.x)
			inside = !inside;
	}
	return inside;
}

namespace {

using V2 = FVector2;

float Cross(const V2 &o, const V2 &a, const V2 &b) { return (a.x - o.x) * (b.y - o.y) - (a.y - o.y) * (b.x - o.x); }

bool InTriangle(const V2 &p, const V2 &a, const V2 &b, const V2 &c) {
	const float d1 = Cross(a, b, p), d2 = Cross(b, c, p), d3 = Cross(c, a, p);
	const bool neg = d1 < -1e-9f || d2 < -1e-9f || d3 < -1e-9f;
	const bool pos = d1 > 1e-9f || d2 > 1e-9f || d3 > 1e-9f;
	return !(neg && pos);
}

/// Raccorde un trou (sens horaire) au polygone (sens trigonométrique) par un
/// pont M→P (Eberly, « Triangulation by Ear Clipping », §3).
void BridgeHole(std::vector<uint32_t> &poly, const std::vector<uint32_t> &hole, const std::vector<V2> &pts) {
	size_t mIndex = 0;
	for (size_t i = 1; i < hole.size(); ++i)
		if (pts[hole[i]].x > pts[hole[mIndex]].x)
			mIndex = i;
	const V2 m = pts[hole[mIndex]];

	// Rayon horizontal vers +x : arête la plus proche.
	float bestX = 1e30f;
	size_t bestEdge = SIZE_MAX;
	V2 hit{0.f, 0.f};
	for (size_t i = 0; i < poly.size(); ++i) {
		const V2 a = pts[poly[i]], b = pts[poly[(i + 1) % poly.size()]];
		if ((a.y > m.y) == (b.y > m.y) && a.y != m.y && b.y != m.y)
			continue;
		if (std::fabs(b.y - a.y) < 1e-12f) {
			if (std::fabs(a.y - m.y) < 1e-9f) {
				const float x = Min(a.x, b.x);
				if (x >= m.x && x < bestX) {
					bestX = x;
					bestEdge = i;
					hit = {x, m.y};
				}
			}
			continue;
		}
		const float t = (m.y - a.y) / (b.y - a.y);
		if (t < 0.f || t > 1.f)
			continue;
		const float x = a.x + t * (b.x - a.x);
		if (x >= m.x && x < bestX) {
			bestX = x;
			bestEdge = i;
			hit = {x, m.y};
		}
	}
	size_t pIndex = 0;
	if (bestEdge == SIZE_MAX) {
		// Repli : sommet du polygone le plus proche.
		float best = 1e30f;
		for (size_t i = 0; i < poly.size(); ++i)
			if (float d = pts[poly[i]].DistanceSq(m); d < best) {
				best = d;
				pIndex = i;
			}
	} else {
		const size_t i0 = bestEdge, i1 = (bestEdge + 1) % poly.size();
		pIndex = pts[poly[i0]].x > pts[poly[i1]].x ? i0 : i1;
		const V2 p = pts[poly[pIndex]];
		// Sommets réflexes dans le triangle (M, I, P) : on prend celui qui
		// fait le plus petit angle avec l'axe x.
		float bestCos = -2.f, bestDist = 1e30f;
		size_t candidate = SIZE_MAX;
		for (size_t i = 0; i < poly.size(); ++i) {
			if (i == pIndex)
				continue;
			const V2 r = pts[poly[i]];
			const V2 prev = pts[poly[(i + poly.size() - 1) % poly.size()]];
			const V2 next = pts[poly[(i + 1) % poly.size()]];
			if (Cross(prev, r, next) > 0.f)
				continue; // convexe
			if (!InTriangle(r, m, hit, p))
				continue;
			const V2 d = r - m;
			const float len = d.Length();
			if (len < 1e-12f)
				continue;
			const float c = d.x / len;
			if (c > bestCos + 1e-6f || (std::fabs(c - bestCos) <= 1e-6f && len < bestDist)) {
				bestCos = c;
				bestDist = len;
				candidate = i;
			}
		}
		if (candidate != SIZE_MAX)
			pIndex = candidate;
	}
	// Un sommet qui a déjà servi de pont figure plusieurs fois dans le
	// polygone : le pont doit partir de la copie dont le secteur intérieur
	// contient la direction M − P, sans quoi il croiserait le pont précédent.
	{
		const uint32_t target = poly[pIndex];
		const V2 p = pts[target];
		auto angle = [](const V2 &v) {
			float a = std::atan2(v.y, v.x);
			return a < 0.f ? a + 2.f * PI : a;
		};
		for (size_t k = 0; k < poly.size(); ++k) {
			if (poly[k] != target)
				continue;
			const V2 toNext = pts[poly[(k + 1) % poly.size()]] - p;
			const V2 toPrev = pts[poly[(k + poly.size() - 1) % poly.size()]] - p;
			// Secteur intérieur (contour trigonométrique) : de toNext à toPrev
			// en tournant dans le sens trigonométrique.
			const float start = angle(toNext);
			float span = angle(toPrev) - start, dir = angle(m - p) - start;
			if (span <= 0.f)
				span += 2.f * PI;
			if (dir < 0.f)
				dir += 2.f * PI;
			if (dir <= span) {
				pIndex = k;
				break;
			}
		}
	}
	std::vector<uint32_t> merged;
	merged.insert(merged.end(), poly.begin(), poly.begin() + std::ptrdiff_t(pIndex) + 1);
	for (size_t k = 0; k <= hole.size(); ++k)
		merged.push_back(hole[(mIndex + k) % hole.size()]);
	merged.push_back(poly[pIndex]);
	merged.insert(merged.end(), poly.begin() + std::ptrdiff_t(pIndex) + 1, poly.end());
	poly = std::move(merged);
}

} // namespace

bool TriangulatePolygon(const std::vector<V2> &outer, const std::vector<std::vector<V2>> &holes,
						std::vector<uint32_t> &outIndices) {
	if (outer.size() < 3)
		return false;
	std::vector<V2> pts = outer;
	std::vector<uint32_t> poly(outer.size());
	for (size_t i = 0; i < outer.size(); ++i)
		poly[i] = uint32_t(i);
	if (SignedArea(outer) < 0.f)
		std::reverse(poly.begin(), poly.end());

	// Trous : du plus à droite au plus à gauche, chacun dans le sens horaire.
	std::vector<std::vector<uint32_t>> holeLoops;
	for (const auto &h : holes) {
		if (h.size() < 3)
			continue;
		std::vector<uint32_t> loop;
		for (const V2 &p : h) {
			loop.push_back(uint32_t(pts.size()));
			pts.push_back(p);
		}
		if (SignedArea(h) > 0.f)
			std::reverse(loop.begin(), loop.end());
		holeLoops.push_back(std::move(loop));
	}
	std::sort(holeLoops.begin(), holeLoops.end(), [&](const auto &a, const auto &b) {
		float ma = -1e30f, mb = -1e30f;
		for (uint32_t i : a)
			ma = Max(ma, pts[i].x);
		for (uint32_t i : b)
			mb = Max(mb, pts[i].x);
		return ma > mb;
	});
	for (const auto &loop : holeLoops)
		BridgeHole(poly, loop, pts);

	// Découpe des oreilles.
	const size_t start = outIndices.size();
	size_t guard = poly.size() * poly.size() + 16;
	while (poly.size() > 3 && guard-- > 0) {
		bool clipped = false;
		const size_t n = poly.size();
		for (size_t i = 0; i < n; ++i) {
			const uint32_t ia = poly[(i + n - 1) % n], ib = poly[i], ic = poly[(i + 1) % n];
			const V2 a = pts[ia], b = pts[ib], c = pts[ic];
			const float cross = Cross(a, b, c);
			if (std::fabs(cross) < 1e-12f) {
				// Sommet aligné (ou doublon de pont) : retiré sans triangle.
				poly.erase(poly.begin() + std::ptrdiff_t(i));
				clipped = true;
				break;
			}
			if (cross < 0.f)
				continue; // réflexe
			// Seul un sommet RÉFLEXE peut se trouver dans une oreille d'un
			// polygone simple (Eberly) : tester aussi les sommets convexes
			// bloquerait à tort les copies de sommets créées par les ponts.
			bool ear = true;
			for (size_t k = 0; k < n && ear; ++k) {
				const uint32_t ik = poly[k];
				if (ik == ia || ik == ib || ik == ic)
					continue;
				if (Cross(pts[poly[(k + n - 1) % n]], pts[ik], pts[poly[(k + 1) % n]]) > 0.f)
					continue; // convexe
				const V2 p = pts[ik];
				if ((p.x == a.x && p.y == a.y) || (p.x == b.x && p.y == b.y) || (p.x == c.x && p.y == c.y))
					continue; // sommet dupliqué par un pont
				if (InTriangle(p, a, b, c))
					ear = false;
			}
			if (!ear)
				continue;
			outIndices.insert(outIndices.end(), {ia, ib, ic});
			poly.erase(poly.begin() + std::ptrdiff_t(i));
			clipped = true;
			break;
		}
		if (!clipped) {
			// Polygone auto-intersectant ou dégénéré : éventail de secours.
			for (size_t i = 1; i + 1 < poly.size(); ++i)
				outIndices.insert(outIndices.end(), {poly[0], poly[i], poly[i + 1]});
			poly.clear();
		}
	}
	if (poly.size() == 3 && std::fabs(Cross(pts[poly[0]], pts[poly[1]], pts[poly[2]])) > 1e-12f)
		outIndices.insert(outIndices.end(), {poly[0], poly[1], poly[2]});
	return outIndices.size() > start;
}


} // namespace math
