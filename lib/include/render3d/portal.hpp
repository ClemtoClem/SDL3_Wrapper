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
	void UpdateDelta() noexcept {
		if (!node || !linkedPortal || !linkedPortal->node)
			return;
		math::FMatrix4 thisWorld = node->WorldMatrix();
		math::FMatrix4 linkedWorld = linkedPortal->node->WorldMatrix();
		delta = linkedWorld * thisWorld.Inverse();
		deltaInv = thisWorld * linkedWorld.Inverse();
	}

	/// World-space position of THIS portal's own surface plane — the
	/// translation column of `node->WorldMatrix()`.
	[[nodiscard]] math::FVector3 WorldPlanePos() const noexcept {
		const math::FVector3 vec = {0.f, 0.f, 0.f};
		return node ? node->WorldMatrix().TransformPoint(vec) : vec;
	}

	/// World-space normal of THIS portal's own surface plane — local +Z
	/// transformed by `node->WorldMatrix()`'s rotation, matching this
	/// codebase's established "front face is +Z" convention (Mesh::Cube()'s
	/// own +Z face group, Mesh::Extrude()'s front cap normal — see
	/// mesh.hpp), NOT Mesh::Plane()'s +Y (that primitive is a "floor",
	/// authored flat in the XZ plane — the wrong convention for a portal
	/// surface the camera looks INTO).
	[[nodiscard]] math::FVector3 WorldPlaneNormal() const noexcept {
		const math::FVector3 vec = {0.f, 0.f, 1.f};
		return node ? node->WorldMatrix().TransformDir(vec).Normalize() : vec;
	}
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
[[nodiscard]] inline math::FAABB PortalNodeWorldBounds(Object3D &node) noexcept {
	if (auto *shape = dynamic_cast<Shape *>(&node)) {
		// `Mesh::LocalBounds()` est calculée une fois puis mémorisée : ce
		// parcours des sommets se faisait auparavant à chaque appel, donc à
		// chaque image et pour chaque portail.
		const math::FAABB &local = shape->Geometry().LocalBounds();
		if (local.IsValid())
			return local.Transformed(node.WorldMatrix());
	}
	math::FVector3 p = node.WorldMatrix().TransformPoint({0.f, 0.f, 0.f});
	return math::FAABB{p - math::FVector3{0.01f}, p + math::FVector3{0.01f}};
}

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
inline void RenderPortalRecursive(Canvas &canvas, Object3D &scene, Camera baseCam, Portal &portal, int depth,
								   int maxDepth, OffscreenTarget &target) {
	if (!portal.node || !portal.linkedPortal || !portal.linkedPortal->node)
		return;
	Portal &linked = *portal.linkedPortal;

	// Un portail HORS CHAMP ne coûte rien. Le niveau récursif faisait déjà ce
	// test avant de descendre d'un cran (plus bas), mais l'appel de plus haut
	// niveau, lui, rendait la scène entière dans la cible du portail même
	// quand celui-ci était derrière la caméra — soit une passe de rendu
	// complète par portail et par image, purement gâchée. Le matériau du
	// portail n'est PAS touché dans ce cas : il garde la texture de l'image
	// précédente, ce qui est sans conséquence puisque sa surface n'est pas à
	// l'écran, et qui évite un scintillement au moment où il y revient.
	{
		math::FFrustum cameraFrustum = math::FFrustum::FromViewProj(baseCam.ViewProjectionMatrix());
		if (!cameraFrustum.Intersects(detail::PortalNodeWorldBounds(*portal.node)))
			return;
	}

	// Clear any stale texture from a previous frame FIRST (correctness by
	// recomputation, same convention as delta/deltaInv above) — overwritten
	// below by the recursive call's own postcondition if we do recurse, but
	// this keeps a depth cutoff (or a failed recursive render) from leaking
	// a stale/deep texture onto the linked portal's quad.
	if (auto *linkedShape = dynamic_cast<Shape *>(linked.node))
		for (Material &mat : linkedShape->Materials())
			mat.albedo = NONE;

	// See the file header for the full derivation of why this is
	// `deltaInv`, not `delta` (the reference project's own literal formula)
	// — verified both by a concrete numeric check and by the recursive
	// round-trip identity (entering A then B returns you to your start).
	math::FMatrix4 virtualView = baseCam.ViewMatrix() * portal.deltaInv;

	math::FVector3 virtualPos = portal.delta.TransformPoint(baseCam.position);
	math::FVector3 realForward = (baseCam.target - baseCam.position).Normalize();
	math::FVector3 virtualForward = portal.delta.TransformDir(realForward).Normalize();
	math::FVector3 virtualUp = portal.delta.TransformDir(baseCam.up).Normalize();

	// Reconstructed LookAt-style Camera (position/target/up) — used (a) as
	// the Camera argument to RenderObjectOffscreen's explicit-vp overload,
	// where only `.position` matters (lighting UBO `cameraPos`), and (b) as
	// `baseCam` for the recursive call below, where ALL of position/target/
	// up matter (that call derives its own `virtualView` from `.ViewMatrix()`
	// on it). Since `portal.delta`'s rotational part is rigid (composed from
	// two ordinary Object3D WorldMatrix() rotations), this LookAt
	// reconstruction matches the raw `virtualView` matrix above to float
	// precision — it is NOT used for this level's own render (which uses
	// the exact `virtualView` computed above directly).
	Camera virtualCam = baseCam;
	virtualCam.position = virtualPos;
	virtualCam.target = virtualPos + virtualForward;
	virtualCam.up = virtualUp;

	// Clip against the LINKED portal's own world-space surface plane: this
	// removes geometry between the virtual camera and the linked portal's
	// surface, so the recursive render doesn't show geometry "behind" the
	// portal from the virtual vantage point (Lengyel's technique, see
	// Camera::ClipObliqueNearPlane's doc comment, camera.hpp).
	math::FVector3 linkedPlanePos = linked.WorldPlanePos();
	math::FVector3 linkedPlaneNormal = linked.WorldPlaneNormal();
	math::FMatrix4 clippedProj = detail::ClipObliqueRow(virtualView, baseCam.ProjectionMatrix(), linkedPlanePos,
														 linkedPlaneNormal);
	math::FMatrix4 virtualViewProjection = clippedProj * virtualView;

	// `deeperTarget` MUST outlive this level's own RenderObjectOffscreen
	// call below (which samples `linked.node`'s material, set by the
	// recursive call to point at `deeperTarget`'s color texture) — declared
	// at function scope, not nested inside the `if` below, specifically so
	// its lifetime spans that far. A dangling Ref<GpuTexture> here would be
	// a genuine use-after-free.
	OffscreenTarget deeperTarget;
	if (depth < maxDepth) {
		math::FFrustum frustum = math::FFrustum::FromViewProj(virtualViewProjection);
		math::FAABB linkedBounds = detail::PortalNodeWorldBounds(*linked.node);
		if (frustum.Intersects(linkedBounds)) {
			auto sized = deeperTarget.EnsureSize(canvas.Device(), target.Width(), target.Height(),
												 target.ColorFormat(), target.DepthFormat());
			if (sized)
				RenderPortalRecursive(canvas, scene, virtualCam, linked, depth + 1, maxDepth, deeperTarget);
		}
	}

	// Best-effort (matches RenderObjectOffscreen's own honesty about GPU
	// failures elsewhere in this codebase — e.g. Viewport3DSystem::Update):
	// a failed render here simply leaves `portal.node`'s material untouched
	// below (still whatever it was before this call, or NONE from a
	// previous frame's own defensive clear).
	auto rendered = canvas.RenderObjectOffscreen(scene, virtualCam, virtualViewProjection, target);
	if (!rendered)
		return;

	if (auto *portalShape = dynamic_cast<Shape *>(portal.node))
		for (Material &mat : portalShape->Materials())
			mat.albedo = Some(MakeRef(target.ColorTexture()));
}

} // namespace render3d
