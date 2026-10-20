// Définitions de math/transform.hpp
#include "math/transform.hpp"

namespace math {

Transform3D Transform3D::FromMatrix(const FMatrix4 &m) noexcept {
	const TRS trs = DecomposeTRS(m);
	Transform3D t;
	t.position = trs.translation;
	t.rotation = trs.rotation;
	t.scale = trs.scale;
	return t;
}

FVector3 Transform3D::EulerDegrees() const noexcept {
	const FVector3 e = rotation.ToEuler();
	return {e.x * RAD2DEG, e.y * RAD2DEG, e.z * RAD2DEG};
}

void Transform3D::SetEulerDegrees(const FVector3 &degrees) noexcept {
	rotation = FQuaternion::FromEuler(degrees.x * DEG2RAD, degrees.y * DEG2RAD, degrees.z * DEG2RAD);
}

Transform3D Transform2D::To3D() const noexcept {
	Transform3D t;
	t.position = {position.x, position.y, 0.f};
	t.rotation = FQuaternion::FromEuler(0.f, 0.f, rotationDegrees * Transform3D::DEG2RAD);
	t.scale = {scale.x, scale.y, 1.f};
	return t;
}

FMatrix4 Transform2D::Matrix() const noexcept {
	return To3D().Matrix();
}

FVector2 Transform2D::Apply(const FVector2 &local) const noexcept {
	const float a = rotationDegrees * Transform3D::DEG2RAD;
	const float c = Cos(a), s = Sin(a);
	const float x = local.x * scale.x, y = local.y * scale.y;
	return {position.x + c * x - s * y, position.y + s * x + c * y};
}

} // namespace math
