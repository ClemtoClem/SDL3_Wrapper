// Définitions de render3d/picking.hpp
#include "render3d/picking.hpp"

namespace render3d {

Option<PickResult> PickMeshFace(const math::FRay &ray, const Mesh &mesh, const math::FMatrix4 &worldMatrix) noexcept {
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

Option<PickResult> PickMeshFace(const math::FRay &ray, Shape &shape) noexcept {
	return PickMeshFace(ray, shape.Geometry(), shape.WorldMatrix());
}

} // namespace render3d
