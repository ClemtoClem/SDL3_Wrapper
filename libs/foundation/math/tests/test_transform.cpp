#include "test.hpp"
#include <math/transform.hpp>

TEST(Transform_ThreeAndTwoDimensions) {
	math::Transform3D t;
	t.position = {1.f, 2.f, 3.f};
	t.SetEulerDegrees({10.f, 45.f, 0.f});
	t.scale = {2.f, 2.f, 2.f};
	const math::Transform3D back = math::Transform3D::FromMatrix(t.Matrix());
	CHECK(math::Abs(back.position.y - 2.f) < 1e-4f);
	CHECK(math::Abs(back.EulerDegrees().y - 45.f) < 1e-2f);
	CHECK(math::Abs(back.scale.x - 2.f) < 1e-4f);
	// scene::Transform reste compatible (il dérive de Transform3D).
	math::Transform2D t2;
	t2.position = {10.f, 0.f};
	t2.rotationDegrees = 90.f;
	const math::FVector2 p = t2.Apply({1.f, 0.f});
	CHECK(math::Abs(p.x - 10.f) < 1e-4f && math::Abs(p.y - 1.f) < 1e-4f);
	const math::FVector3 viaMatrix = t2.Matrix().TransformPoint({1.f, 0.f, 0.f});
	CHECK(math::Abs(viaMatrix.x - 10.f) < 1e-4f && math::Abs(viaMatrix.y - 1.f) < 1e-4f);
}

