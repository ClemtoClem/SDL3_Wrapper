// Définitions de render3d/portal.hpp — fichier généré par splitter.py : le code
// vient tel quel de l'en-tête (seules les signatures sont réécrites).

#include "render3d/portal.hpp"

namespace render3d {

// ── Portal ───────────────────────────────────────────────────────────────────

void Portal::UpdateDelta() noexcept {
	if (!node || !linkedPortal || !linkedPortal->node)
		return;
	math::FMatrix4 thisWorld = node->WorldMatrix();
	math::FMatrix4 linkedWorld = linkedPortal->node->WorldMatrix();
	delta = linkedWorld * thisWorld.Inverse();
	deltaInv = thisWorld * linkedWorld.Inverse();
}

math::FVector3 Portal::WorldPlanePos() const noexcept {
	const math::FVector3 vec = {0.f, 0.f, 0.f};
	return node ? node->WorldMatrix().TransformPoint(vec) : vec;
}

math::FVector3 Portal::WorldPlaneNormal() const noexcept {
	const math::FVector3 vec = {0.f, 0.f, 1.f};
	return node ? node->WorldMatrix().TransformDir(vec).Normalize() : vec;
}

namespace detail {

math::FAABB PortalNodeWorldBounds(Object3D &node) noexcept {
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

void RenderPortalRecursive(Canvas &canvas, Object3D &scene, Camera baseCam, Portal &portal, int depth,
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
