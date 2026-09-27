#pragma once
/**
 * @file portal.hpp
 * @brief render3d::Portal / RenderPortalRecursive — portal rendering (M30,
 * Phase 9's first sub-milestone of the engine-expansion plan
 * `/home/clement/.claude/plans/optimized-humming-toast.md`). Builds directly
 * on `Camera::ClipObliqueNearPlane` (camera.hpp, already implemented and
 * verified — treated here as a locked building block) and the explicit-
 * view-projection `Canvas::RenderObjectOffscreen` overload (offscreen.hpp,
 * M30 plumbing addition).
 *
 * ── The `delta`/`deltaInv` convention ───────────────────────────────────
 *
 * For a linked pair of portals A/B, `A.delta = worldB * worldA^-1` maps a
 * WORLD point expressed "relative to A" (i.e. as if A were the origin) to
 * the equivalent WORLD point "relative to B" — the standard portal-pair
 * teleport reinterpretation. `A.deltaInv = worldA * worldB^-1` is the exact
 * matrix inverse of `A.delta` (verified in tests/portal_render_smoke_test.cpp:
 * `delta * deltaInv ~= Identity`), used by the physics-teleport code
 * (physics/world.hpp) to know which direction a crossing body should be
 * moved.
 *
 * ── Virtual camera composition — a correctness fix versus the reference ──
 *
 * The reference NonEuclidean project's shipped implementation composes the
 * virtual camera's view matrix as `worldView *= portal.delta` (i.e.
 * `realView * delta`). Ported literally against THIS codebase's `Portal`
 * definition above, that is backwards: it does not even send the virtual
 * camera's own world position to the view-space origin (checked by hand
 * with a simple translated-only portal pair — see the task's own
 * verification notes). The correct composition, re-derived and empirically
 * verified here, is:
 *
 *     virtualView = baseCam.ViewMatrix() * portal.deltaInv
 *
 * Reasoning: if the virtual camera's own WORLD (camera-to-world) matrix is
 * `Wv = portal.delta * Wr` (`Wr` the real camera's world matrix — this is
 * exactly what makes `virtualPos = portal.delta.TransformPoint(baseCam.
 * position)` correct, see below), then the virtual VIEW matrix, being
 * `Wv^-1`, is `Wr^-1 * delta^-1 = baseCam.ViewMatrix() * portal.deltaInv`.
 * A concrete numeric check (portal A at the origin, portal B translated by
 * (10,0,0), both unrotated, real camera at (0,0,-5) looking at the origin)
 * confirms `virtualView * virtualPos == view-space origin` ONLY for the
 * `deltaInv` composition, not `delta`. This is exactly the kind of "reads
 * plausible, compiles fine, subtly backwards" bug the wider task called out
 * — the reference project's own `delta` is presumably defined with the
 * opposite multiplication order from this codebase's `Portal::delta`
 * (mandated verbatim by the plan for part 2), so the FORMULA differs even
 * though the underlying technique doesn't.
 *
 * Recursive round-trip sanity check (also confirms `deltaInv` is right, not
 * just consistent with itself): entering portal A and then, within that
 * view, conceptually entering portal B again (depth 1) composes to
 * `virtualView1 = realView * A.deltaInv * B.deltaInv = realView * A.deltaInv
 * * A.delta = realView` (since `B.deltaInv == A.delta`, see UpdateDelta's
 * doc) — i.e. going out A and back in B returns you to your start, exactly
 * the physically-sensible behaviour of a linked portal pair.
 */
#include "../math/math.hpp"
#include "camera.hpp"
#include "canvas.hpp"
#include "material.hpp"
#include "object3d.hpp"
#include "offscreen.hpp"
#include "shape.hpp"

namespace render3d {

/// One end of a linked portal pair. `node`/`linkedPortal` are both
/// NON-OWNING (mirrors `physics::World`'s own non-owning-registry pattern,
/// and Viewport3D's non-owning scene root): the caller keeps ownership of
/// both the portal's own surface geometry (`node` — whatever Object3D the
/// app places the portal on, typically a flat `Shape` quad) and of the
/// `Portal` objects themselves.
struct Portal {
	Object3D *node = nullptr;
	Portal *linkedPortal = nullptr;

	/// Recomputed FRESH every call from both portals' CURRENT
	/// `Object3D::WorldMatrix()` — never cached, matching this codebase's
	/// established "correctness by recomputation" convention (see e.g.
	/// `Object3D::WorldMatrix()`'s own doc comment, `physics::RigidBody::
	/// WorldInverseInertia()`'s).
	math::FMatrix4 delta = math::FMatrix4::Identity();
	math::FMatrix4 deltaInv = math::FMatrix4::Identity();

	/// Recomputes `delta`/`deltaInv` from the two linked portals' current
	/// world matrices — call once per frame (by whatever code owns a
	/// scene's portal list) before rendering/physics use them that frame.
	/// No-op (leaves the previous values in place) if either half of the
	/// pair isn't wired up yet.
	void UpdateDelta() noexcept;

	/// World-space position of THIS portal's own surface plane — the
	/// translation column of `node->WorldMatrix()`.
	[[nodiscard]] math::FVector3 WorldPlanePos() const noexcept;

	/// World-space normal of THIS portal's own surface plane — local +Z
	/// transformed by `node->WorldMatrix()`'s rotation, matching this
	/// codebase's established "front face is +Z" convention (Mesh::Cube()'s
	/// own +Z face group, Mesh::Extrude()'s front cap normal — see
	/// mesh.hpp), NOT Mesh::Plane()'s +Y (that primitive is a "floor",
	/// authored flat in the XZ plane — the wrong convention for a portal
	/// surface the camera looks INTO).
	[[nodiscard]] math::FVector3 WorldPlaneNormal() const noexcept;
};

namespace detail {

/// Conservative world-space bounds for a portal's own surface node — used
/// only for the "is the linked portal visible in the just-computed virtual
/// frustum" cheap cull (RenderPortalRecursive below), never for anything
/// safety-critical. If `node` is a `Shape` (the expected case — a portal's
/// surface is normally a flat quad), uses its actual local-space vertex
/// extents transformed by `WorldMatrix()`; otherwise falls back to a tiny
/// point-like box at the node's own world position so the check still has
/// something non-empty to test against rather than silently never
/// recursing.
[[nodiscard]] math::FAABB PortalNodeWorldBounds(Object3D &node) noexcept;

} // namespace detail

/// Renders `scene` as seen "through" `portal`, into `target`, recursing up
/// to `maxDepth` times when the linked portal is itself visible within the
/// virtual view (a simple bounds/frustum check — no general portal-
/// visibility graph, see the plan's explicit scope cut).
///
/// Postcondition (this is the whole compositing mechanism — see the file
/// header and Viewport3D's own "render to FBO, draw textured quad" pattern,
/// which this reuses architecturally): if `portal.node` is a `Shape`, EVERY
/// one of its materials has `albedo` set to `target`'s just-rendered color
/// texture by the time this call returns — ready to be sampled completely
/// normally the next time `scene` (which structurally contains `portal.
/// node`, being part of the same scene graph) is drawn/rendered, with zero
/// additional compositing code: the existing DrawMeshGroup/textured-quad
/// draw path already does the rest. This is exactly how the recursive step
/// composites a deeper level's render onto the linked portal's quad BEFORE
/// finishing the current level's render (see the recursion below), and it's
/// also exactly what a depth-0 top-level caller needs: call this once per
/// portal visible from the main camera, then draw `scene` normally with the
/// main camera — each portal's quad already shows the right thing.
///
/// A portal whose linked half isn't wired up (`portal.linkedPortal` or its
/// `node` null) is a no-op — nothing to render, nothing to composite.
void RenderPortalRecursive(Canvas &canvas, Object3D &scene, Camera baseCam, Portal &portal, int depth,
								   int maxDepth, OffscreenTarget &target);

} // namespace render3d
