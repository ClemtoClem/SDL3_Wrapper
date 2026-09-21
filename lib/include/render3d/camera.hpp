#pragma once
#include "../math/math.hpp"

namespace render3d {

// Shared free-function core of Camera::ClipObliqueNearPlane (M30, Phase 9 —
// see the plan) — factored out so portal rendering (portal.hpp) can apply
// the exact same, already-verified row-replacement math against an
// EXPLICITLY supplied view matrix (the composed virtual-camera view for a
// recursive portal render) instead of `Camera::ViewMatrix()` computed from
// `this`'s own position/target/up, which is all `ClipObliqueNearPlane` alone
// could ever use. Matches this repo's habit of factoring shared logic into a
// `detail::` free function (see physics::solver_detail, sql::detail).
namespace detail {

/// Returns `proj` with its near-clip row replaced by the oblique plane
/// (world-space `planePos`/`planeNormal`), evaluated against the given
/// `view` — see Camera::ClipObliqueNearPlane's doc comment (below) for the
/// full derivation; this free function is exactly that derivation's body,
/// generalised to take `view`/`proj` as parameters instead of computing them
/// from `this`.
[[nodiscard]] inline math::FMatrix4 ClipObliqueRow(const math::FMatrix4 &view, const math::FMatrix4 &proj,
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

struct Camera {
	math::FVector3 position{0.f, 0.f, -5.f};
	math::FVector3 target{0.f, 0.f, 0.f};
	math::FVector3 up{0.f, 1.f, 0.f};
	float fovYRadians = 60.f * (3.14159265f / 180.f);
	float aspect = 16.f / 9.f;
	float nearPlane = 0.1f;
	float farPlane = 100.f;

	[[nodiscard]] math::FMatrix4 ViewMatrix() const noexcept { return math::FMatrix4::LookAt(position, target, up); }
	[[nodiscard]] math::FMatrix4 ProjectionMatrix() const noexcept {
		return math::FMatrix4::Perspective(fovYRadians, aspect, nearPlane, farPlane);
	}
	[[nodiscard]] math::FMatrix4 ViewProjectionMatrix() const noexcept { return ProjectionMatrix() * ViewMatrix(); }

	/**
	 * Returns ProjectionMatrix() with its near clip plane replaced by an
	 * arbitrary oblique plane (world-space `planePos`/`planeNormal`) —
	 * Lengyel's oblique-near-plane-clipping technique, used by portal
	 * rendering to clip geometry between the camera and a portal surface
	 * without a separate stencil/scissor pass. Does not mutate `*this`
	 * (matches ProjectionMatrix()/ViewMatrix()'s own pure, recompute-don't-
	 * cache style).
	 *
	 * Ported from the reference NonEuclidean project's `Camera::ClipOblique`
	 * (Engine/Camera.cpp) — NOT a literal port: that project's matrix is
	 * OpenGL-convention (row-major storage, Z in [-1, 1]), whereas
	 * math::FMatrix4 is column-major (`m[col*4+row]`, matching GLSL) and
	 * targets Vulkan clip space (Z in [0, 1] — see FMatrix4's own doc
	 * comment). The reference computes `newRow2 = c - row3` (row3 always
	 * being [0,0,-1,0]); that subtraction specifically retargets the near
	 * plane to OpenGL's z_ndc = -1. For this codebase's Vulkan z_ndc = 0
	 * near value, the correct row is `newRow2 = k*cPlane` directly (NO "-
	 * row3" term) — verified numerically (not just re-derived on paper)
	 * against real FMatrix4/Camera instances: points on the given plane
	 * land at z_ndc ~= 0 to float precision, the far plane stays at
	 * z_ndc ~= 1 (unaffected), and this holds for both axis-aligned and
	 * oblique planes and for both a trivial identity-view camera and an
	 * arbitrarily translated+rotated one. See tests/portal_smoke_test.cpp.
	 *
	 * `k` is solved from the far-corner-preservation constraint: `q` is the
	 * camera-space point the ORIGINAL (unmodified) projection's inverse
	 * maps the far/near-agnostic clip-space corner `(sgnX, sgnY, 1, 1)`
	 * back to (the corner sign-selected via the plane's own x/y sign, per
	 * Lengyel — this picks the frustum corner most affected by the oblique
	 * cut). Since `proj * invProj == I`, z_ndc_before(q) is exactly 1
	 * (the far plane) by construction; requiring z_ndc_after(q) to stay 1
	 * (w_clip is untouched, since row3 is never modified) gives
	 * `k = w_clip(q) / cPlane.Dot(q)`, with `w_clip(q) = -q.z` (row3 =
	 * [0,0,-1,0]).
	 *
	 * Thin wrapper (M30, Phase 9) around `detail::ClipObliqueRow` — see that
	 * free function's doc comment above for the exact same body, factored
	 * out so portal rendering can apply it against an explicit (non-`this`)
	 * view matrix. tests/portal_smoke_test.cpp still exercises this exact
	 * wrapper end-to-end, unchanged, after the extraction.
	 */
	[[nodiscard]] math::FMatrix4 ClipObliqueNearPlane(const math::FVector3 &planePos,
	                                                    const math::FVector3 &planeNormal) const noexcept {
		return detail::ClipObliqueRow(ViewMatrix(), ProjectionMatrix(), planePos, planeNormal);
	}

	/**
	 * World-space ray through the pixel `(x, y)` of a `width` x `height`
	 * viewport — what a picker (render3d::PickMeshFace) needs to answer
	 * "what did the user click on?".
	 *
	 * `x`/`y` are in the viewport's own pixel space, ORIGIN TOP-LEFT (the
	 * convention of SDL mouse events and of ui::UiComputed::screen), and may
	 * be fractional. The returned ray starts on the NEAR plane and points
	 * into the scene, its direction normalised.
	 *
	 * Conversion to normalised device coordinates follows the convention the
	 * projection matrices of this repo target (see FMatrix4::Perspective):
	 * SDL_GPU normalises to **+Y up**, Z in [0, 1]. So `y` is flipped here
	 * (screen Y grows downwards, NDC Y upwards) and the near plane is z = 0.
	 *
	 * Works for any projection this Camera can produce, perspective or
	 * orthographic, because it un-projects TWO points (near and far) through
	 * the inverse view-projection rather than assuming rays converge on
	 * `position` — for an orthographic camera they do not.
	 */
	[[nodiscard]] math::FRay ScreenPointToRay(float x, float y, float width, float height) const noexcept {
		const float ndcX = width > 0.f ? (2.f * x / width) - 1.f : 0.f;
		const float ndcY = height > 0.f ? 1.f - (2.f * y / height) : 0.f;
		const math::FMatrix4 inverse = ViewProjectionMatrix().Inverse();
		// `nearPoint`/`farPoint`, not `near`/`far`: those are macros in the
		// Windows SDK headers.
		const math::FVector3 nearPoint = inverse.TransformPoint({ndcX, ndcY, 0.f});
		const math::FVector3 farPoint = inverse.TransformPoint({ndcX, ndcY, 1.f});
		return math::FRay{nearPoint, farPoint - nearPoint}; // FRay normalises its direction
	}

	/**
	 * Inverse of `ScreenPointToRay`: pixel a world-space point projects to,
	 * same top-left pixel convention. NONE when the point is BEHIND the
	 * camera (`w <= 0`), where the perspective divide would mirror it to a
	 * plausible-looking but wrong pixel — the caller must be able to tell
	 * "off-screen behind me" from "off-screen to the left".
	 */
	[[nodiscard]] Option<math::FVector2> WorldToScreen(const math::FVector3 &point, float width,
	                                                     float height) const noexcept {
		const math::FVector4 clip = ViewProjectionMatrix() * math::FVector4{point, 1.f};
		if (clip.w <= 1e-6f)
			return NONE;
		const math::FVector3 ndc = clip.perspDiv();
		return Some(math::FVector2{(ndc.x * 0.5f + 0.5f) * width, (0.5f - ndc.y * 0.5f) * height});
	}
};

} // namespace render3d
