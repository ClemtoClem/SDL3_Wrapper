// Définitions de physics/portal_teleport.hpp — fichier généré par splitter.py : le code
// vient tel quel de l'en-tête (seules les signatures sont réécrites).

#include "physics/portal_teleport.hpp"

namespace physics {

namespace portal_detail {

math::FQuaternion QuaternionFromRotationMatrix(const math::FMatrix4 &m) noexcept {
	float m00 = m.At(0, 0), m01 = m.At(0, 1), m02 = m.At(0, 2);
	float m10 = m.At(1, 0), m11 = m.At(1, 1), m12 = m.At(1, 2);
	float m20 = m.At(2, 0), m21 = m.At(2, 1), m22 = m.At(2, 2);
	float trace = m00 + m11 + m22;

	if (trace > 0.f) {
		float s = sdl3::Sqrt(trace + 1.f) * 2.f;
		return math::FQuaternion{(m21 - m12) / s, (m02 - m20) / s, (m10 - m01) / s, 0.25f * s}.Normalize();
	}
	if (m00 > m11 && m00 > m22) {
		float s = sdl3::Sqrt(1.f + m00 - m11 - m22) * 2.f;
		return math::FQuaternion{0.25f * s, (m01 + m10) / s, (m02 + m20) / s, (m21 - m12) / s}.Normalize();
	}
	if (m11 > m22) {
		float s = sdl3::Sqrt(1.f + m11 - m00 - m22) * 2.f;
		return math::FQuaternion{(m01 + m10) / s, 0.25f * s, (m12 + m21) / s, (m02 - m20) / s}.Normalize();
	}
	float s = sdl3::Sqrt(1.f + m22 - m00 - m11) * 2.f;
	return math::FQuaternion{(m02 + m20) / s, (m12 + m21) / s, 0.25f * s, (m10 - m01) / s}.Normalize();
}

} // namespace portal_detail

void TeleportBodiesThroughPortals(RigidBody &rb, const math::FVector3 &prevPosition,
		const std::vector<PortalPlane> &planes) noexcept {
	if (planes.empty() || rb.invMass <= 0.f)
		return;

	math::FVector3 newPosition = rb.position;
	math::FVector3 segment = newPosition - prevPosition;
	float segmentLength = segment.Length();
	if (segmentLength < 1e-8f)
		return; // body didn't actually move this step — nothing to cross

	for (const PortalPlane &plane : planes) {
		math::FPlane fplane(plane.normal, plane.position);
		float distPrev = fplane.Distance(prevPosition);
		float distNew = fplane.Distance(newPosition);
		// "Entering" crossing only: front (positive side, along `normal`)
		// -> back (negative/on-plane side). A body that starts on the back
		// side (already teleported, or spawned there) must not re-trigger
		// every subsequent step just because it stays on that side.
		if (!(distPrev > 0.f && distNew <= 0.f))
			continue;

		math::FRay ray(prevPosition, segment);
		float t = 0.f;
		if (!ray.Intersects(fplane, t))
			continue;
		if (t < 0.f || t > segmentLength)
			continue; // crossing point falls outside THIS step's actual motion segment

		rb.position = plane.delta.TransformPoint(rb.position);
		rb.orientation = (portal_detail::QuaternionFromRotationMatrix(plane.delta) * rb.orientation).Normalize();
		rb.linearVelocity = plane.delta.TransformDir(rb.linearVelocity);
		rb.angularVelocity = plane.delta.TransformDir(rb.angularVelocity);
		return;
	}
}

} // namespace physics
