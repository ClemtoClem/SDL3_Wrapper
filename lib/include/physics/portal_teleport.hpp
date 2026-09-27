#pragma once
/**
 * @file portal_teleport.hpp
 * @brief physics::PortalPlane / TeleportBodiesThroughPortals — optional
 * portal-teleport support for physics::World::Step (M30, Phase 9 of the
 * engine-expansion plan `/home/clement/.claude/plans/optimized-humming-
 * toast.md`). Deliberately depends on NOTHING from render3d:: (see
 * memory/project_physics_module.md: physics:: has zero dependency on
 * render3d::/ui::, only math::/ecs:: — preserved here) — the caller
 * populates a lightweight `PortalPlane` value type each frame from its own
 * `render3d::Portal` data (position/normal/delta/deltaInv, see
 * render3d/portal.hpp's `Portal::WorldPlanePos()`/`WorldPlaneNormal()`/
 * `delta`/`deltaInv`), rather than physics:: including any render3d::
 * header.
 */
#include "../math/math.hpp"
#include "rigidbody.hpp"
#include <vector>

namespace physics {

/// Lightweight, render3d-independent mirror of one portal's world-space
/// surface plane + teleport matrices — populated by the caller (typically
/// once per frame, in lockstep with `render3d::Portal::UpdateDelta()`)
/// rather than owned or computed here. `normal` is the "front" direction: a
/// body crossing from the `normal`-facing side to the far side teleports
/// (see TeleportBodiesThroughPortals below); a body starting on, or
/// crossing from, the far side does not.
struct PortalPlane {
	math::FVector3 position;
	math::FVector3 normal{0.f, 0.f, 1.f};
	math::FMatrix4 delta = math::FMatrix4::Identity();
	math::FMatrix4 deltaInv = math::FMatrix4::Identity();
};

namespace portal_detail {

/// Rotation-matrix -> quaternion conversion (Shepperd's method — picks the
/// numerically largest of the four candidate denominators rather than
/// always using the trace, which is unstable near a 180-degree rotation).
/// Derived and cross-checked term-by-term against THIS codebase's own
/// `FQuaternion::ToMat4()` element layout (math/math.hpp) rather than
/// ported from a generic reference — `math::FQuaternion`/`math::FMatrix4`
/// do not already provide this direction of conversion (`ToMat4()` only
/// goes quaternion -> matrix). Assumes `m`'s upper-left 3x3 is a proper
/// rotation (no scale/shear) — true for `Portal::delta`/`deltaInv`, which
/// are products of ordinary rigid `Object3D::WorldMatrix()` transforms.
/// Verified by a round-trip test in tests/physics_smoke_test.cpp.
[[nodiscard]] math::FQuaternion QuaternionFromRotationMatrix(const math::FMatrix4 &m) noexcept;

} // namespace portal_detail

/// Teleports `rb` if its motion this step (`prevPosition` -> `rb.position`,
/// i.e. called AFTER `World::Step`'s own semi-implicit-Euler integration
/// already advanced `rb.position`) crosses one of `planes` in the
/// "entering" direction (front-to-back relative to the plane's own
/// `normal`) within the actual segment length — not merely a sign change
/// anywhere along the infinite line. Static bodies (`invMass <= 0`) and an
/// empty `planes` list are both no-ops: genuinely optional/inert when the
/// caller hasn't registered any portals, zero behaviour change for every
/// existing physics_smoke_test call site (none of which register any).
///
/// Applies the SAME `delta`/`deltaInv` matrices that drive rendering (see
/// render3d/portal.hpp's file header) to `position` (`TransformPoint`),
/// `orientation` (via `portal_detail::QuaternionFromRotationMatrix`), and
/// `linearVelocity`/`angularVelocity` (`TransformDir`, direction-only) —
/// one unified source of truth for the teleport transform, matching the
/// reference project's own render/physics unification (see the plan).
/// Stops at the first plane crossed this step (a body that also happens to
/// re-cross a second, badly-placed portal using its now-teleported position
/// should not immediately re-teleport within the same step).
void TeleportBodiesThroughPortals(RigidBody &rb, const math::FVector3 &prevPosition,
										  const std::vector<PortalPlane> &planes) noexcept;

} // namespace physics
