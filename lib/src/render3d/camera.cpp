// Définitions de render3d/camera.hpp
#include "render3d/camera.hpp"

namespace render3d {

namespace detail {

math::FMatrix4 ClipObliqueRow(const math::FMatrix4 &view, const math::FMatrix4 &proj,
		const math::FVector3 &planePos,
		const math::FVector3 &planeNormal) noexcept {
	math::FVector3 cPos = view.TransformPoint(planePos);
	math::FVector3 cNormal = view.TransformDir(planeNormal);
	math::FVector4 cPlane{cNormal.x, cNormal.y, cNormal.z, -cPos.Dot(cNormal)};

	math::FMatrix4 invProj = proj.Inverse();
	math::FVector4 cornerClip{(cPlane.x < 0.f ? 1.f : -1.f), (cPlane.y < 0.f ? 1.f : -1.f), 1.f, 1.f};
	math::FVector4 q = invProj * cornerClip;

	float wq = -q.z;
	float dot = cPlane.Dot(q);
	float k = sdl3::Abs(dot) > 1e-8f ? (wq / dot) : 0.f;
	math::FVector4 newRow2 = cPlane * k;

	math::FMatrix4 result = proj;
	result.At(2, 0) = newRow2.x;
	result.At(2, 1) = newRow2.y;
	result.At(2, 2) = newRow2.z;
	result.At(2, 3) = newRow2.w;
	return result;
}

} // namespace detail

// ── Camera ───────────────────────────────────────────────────────────────────

math::FMatrix4 Camera::ProjectionMatrix() const noexcept {
	return math::FMatrix4::Perspective(fovYRadians, aspect, nearPlane, farPlane);
}

math::FMatrix4 Camera::ClipObliqueNearPlane(const math::FVector3 &planePos, const math::FVector3 &planeNormal) const noexcept {
	return detail::ClipObliqueRow(ViewMatrix(), ProjectionMatrix(), planePos, planeNormal);
}

math::FRay Camera::ScreenPointToRay(float x, float y, float width, float height) const noexcept {
	const float ndcX = width > 0.f ? (2.f * x / width) - 1.f : 0.f;
	const float ndcY = height > 0.f ? 1.f - (2.f * y / height) : 0.f;
	const math::FMatrix4 inverse = ViewProjectionMatrix().Inverse();
	// `nearPoint`/`farPoint`, not `near`/`far`: those are macros in the
	// Windows SDK headers.
	const math::FVector3 nearPoint = inverse.TransformPoint({ndcX, ndcY, 0.f});
	const math::FVector3 farPoint = inverse.TransformPoint({ndcX, ndcY, 1.f});
	return math::FRay{nearPoint, farPoint - nearPoint}; // FRay normalises its direction
}

Option<math::FVector2> Camera::WorldToScreen(const math::FVector3 &point, float width, float height) const noexcept {
	const math::FVector4 clip = ViewProjectionMatrix() * math::FVector4{point, 1.f};
	if (clip.w <= 1e-6f)
		return NONE;
	const math::FVector3 ndc = clip.perspDiv();
	return Some(math::FVector2{(ndc.x * 0.5f + 0.5f) * width, (0.5f - ndc.y * 0.5f) * height});
}

} // namespace render3d
