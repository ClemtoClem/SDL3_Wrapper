#pragma once
/**
 * @file picking.hpp
 * @brief render3d::PickMeshFace — raycast-vs-mesh-face picker (M30, Phase 9
 * of the engine-expansion plan). A small standalone utility, not yet wired
 * into any interactive/editor code (the editor UI is M31, later work) — see
 * tests/picking_smoke_test.cpp for CPU-only verification against
 * hand-computed expected values.
 */
#include "../core/core.hpp"
#include "../math/math.hpp"
#include "mesh.hpp"
#include "shape.hpp"

namespace render3d {

/// Result of a successful `PickMeshFace` hit.
struct PickResult {
	math::FVector3 point;       ///< World-space hit point (`ray.At(t)` in world space).
	float t = 0.f;               ///< Distance along the WORLD-space `ray` to `point`.
	uint32_t triangleIndex = 0;  ///< 0-based index of the hit triangle (`indices[triangleIndex*3 + 0..2]`).
	uint32_t groupIndex = 0;     ///< Index into `Mesh::Groups()` the hit triangle belongs to (0 if the mesh has no explicit groups).
	uint32_t materialIndex = 0;  ///< `Mesh::Group::materialIndex` of the hit group (0 if the mesh has no explicit groups).
	float u = 0.f, v = 0.f;      ///< Barycentric coordinates of `point` within its triangle (Möller-Trumbore convention — see `FRay::Intersects`).
};

/// Raycasts a WORLD-space `ray` against `mesh` (placed in the world by
/// `worldMatrix`), respecting `Mesh::Group` boundaries so the result can
/// report which group/material was hit, not just "the mesh". Walks every
/// triangle (Möller-Trumbore, `math::FRay::Intersects(v0,v1,v2,t,u,v)`,
/// already exists in math/math.hpp) and keeps the closest (smallest `t`).
///
/// The RAY is transformed into the mesh's LOCAL space (via
/// `worldMatrix.Inverse()`) rather than transforming every vertex into
/// world space — cheaper for a mesh with many triangles, and exactly as
/// correct: picking is a pure ray-vs-geometry test. `t`/`point` in the
/// returned `PickResult` are still reported in WORLD space, recomputed from
/// the local hit via `worldMatrix.TransformPoint()` and a fresh distance
/// from `ray.origin` — this is necessary (not just cosmetic) whenever
/// `worldMatrix` scales the mesh, since `FRay::Transformed()` re-normalises
/// its direction, so a raw local-space `t` would be in the WRONG units to
/// mean anything against the caller's original world-space `ray`.
///
/// Returns NONE if no triangle is hit.
[[nodiscard]] inline Option<PickResult> PickMeshFace(const math::FRay &ray, const Mesh &mesh,
													  const math::FMatrix4 &worldMatrix) noexcept {
	math::FMatrix4 invWorld = worldMatrix.Inverse();
	math::FRay localRay = ray.Transformed(invWorld);

	const auto &vertices = mesh.Vertices();
	const auto &indices = mesh.Indices();
	const auto &groups = mesh.Groups();

	auto groupForFirstIndex = [&](uint32_t firstIndex) -> uint32_t {
		for (uint32_t g = 0; g < uint32_t(groups.size()); ++g)
			if (firstIndex >= groups[g].start && firstIndex < groups[g].start + groups[g].count)
				return g;
		return 0;
	};

	bool found = false;
	float bestLocalT = 0.f;
	uint32_t bestTriangleIndex = 0;
	uint32_t bestGroupIndex = 0;
	float bestU = 0.f, bestV = 0.f;

	for (size_t i = 0; i + 2 < indices.size(); i += 3) {
		const math::FVector3 &v0 = vertices[indices[i + 0]].position;
		const math::FVector3 &v1 = vertices[indices[i + 1]].position;
		const math::FVector3 &v2 = vertices[indices[i + 2]].position;

		float t = 0.f, u = 0.f, v = 0.f;
		if (!localRay.Intersects(v0, v1, v2, t, u, v))
			continue;
		if (found && t >= bestLocalT)
			continue;

		found = true;
		bestLocalT = t;
		bestTriangleIndex = uint32_t(i / 3);
		bestGroupIndex = groupForFirstIndex(uint32_t(i));
		bestU = u;
		bestV = v;
	}

	if (!found)
		return NONE;

	PickResult result{};
	math::FVector3 localPoint = localRay.At(bestLocalT);
	result.point = worldMatrix.TransformPoint(localPoint);
	result.t = (result.point - ray.origin).Length();
	result.triangleIndex = bestTriangleIndex;
	result.groupIndex = bestGroupIndex;
	result.materialIndex = bestGroupIndex < uint32_t(groups.size()) ? groups[bestGroupIndex].materialIndex : 0;
	result.u = bestU;
	result.v = bestV;
	return Some(result);
}

/// Convenience overload: picks against `shape`'s own `Geometry()`/`WorldMatrix()`.
[[nodiscard]] inline Option<PickResult> PickMeshFace(const math::FRay &ray, Shape &shape) noexcept {
	return PickMeshFace(ray, shape.Geometry(), shape.WorldMatrix());
}

} // namespace render3d
