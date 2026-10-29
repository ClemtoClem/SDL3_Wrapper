// Définitions de overlay_ribbons.hpp
#include <sandbox/engine/overlay_ribbons.hpp>

namespace sandbox {

void OverlayRibbons::Add(const math::FVector3 &a0, const math::FVector3 &b0, sdl3::Color color, float widthPx) {
	const math::FVector3 a = a0 + (m_eye - a0) * 0.002f, b = b0 + (m_eye - b0) * 0.002f;
	const math::FVector3 mid = (a + b) * 0.5f;
	math::FVector3 side = (b - a).Cross(m_eye - mid);
	const float length = side.Length();
	if (length < 1e-9f)
		return;
	side = side * (1.f / length);
	const float halfA = (m_eye - a).Length() * m_pixel * widthPx * 0.5f;
	const float halfB = (m_eye - b).Length() * m_pixel * widthPx * 0.5f;
	const math::FVector3 normal = (m_eye - mid).Normalize();
	const uint32_t base = uint32_t(m_vertices.size());
	for (const math::FVector3 &p : {a - side * halfA, a + side * halfA, b + side * halfB, b - side * halfB}) {
		render3d::Vertex3D v;
		v.position = p;
		v.normal = normal;
		v.color = color;
		m_vertices.push_back(v);
	}
	for (uint32_t i : {0u, 1u, 2u, 0u, 2u, 3u})
		m_indices.push_back(base + i);
}

void OverlayRibbons::AddDashed(const math::FVector3 &a, const math::FVector3 &b, sdl3::Color color, float widthPx,
							   float dashPx) {
	const float length = (b - a).Length();
	if (length < 1e-6f)
		return;
	// Longueur d'un tiret mesurée à la distance du milieu du segment.
	const float dash = math::Max((m_eye - (a + b) * 0.5f).Length() * m_pixel * dashPx, length / 200.f);
	const math::FVector3 dir = (b - a) * (1.f / length);
	for (float t = 0.f; t < length; t += 2.f * dash)
		Add(a + dir * t, a + dir * math::Min(t + dash, length), color, widthPx);
}

void OverlayRibbons::AddBox(const math::FAABB &box, sdl3::Color color, float widthPx) {
	const math::FVector3 lo = box.min, hi = box.max;
	const math::FVector3 c[8] = {{lo.x, lo.y, lo.z}, {hi.x, lo.y, lo.z}, {hi.x, lo.y, hi.z}, {lo.x, lo.y, hi.z},
								 {lo.x, hi.y, lo.z}, {hi.x, hi.y, lo.z}, {hi.x, hi.y, hi.z}, {lo.x, hi.y, hi.z}};
	for (int i = 0; i < 4; ++i) {
		Add(c[i], c[(i + 1) % 4], color, widthPx);
		Add(c[4 + i], c[4 + (i + 1) % 4], color, widthPx);
		Add(c[i], c[4 + i], color, widthPx);
	}
}

} // namespace sandbox
