// Définitions de navigation.hpp
#include <editor/navigation.hpp>

#include <algorithm>
#include <cmath>

namespace editor {

namespace {

constexpr math::FVector3 WORLD_UP{0.f, 1.f, 0.f};

struct PresetName {
	ViewPreset preset;
	const char *id;
};
constexpr PresetName PRESETS[] = {{ViewPreset::Front, "front"}, {ViewPreset::Back, "back"},	  {ViewPreset::Left, "left"},
								  {ViewPreset::Right, "right"}, {ViewPreset::Top, "top"},	  {ViewPreset::Bottom, "bottom"},
								  {ViewPreset::Iso, "iso"}};

} // namespace

const char *ViewPresetId(ViewPreset preset) noexcept {
	for (const PresetName &p : PRESETS)
		if (p.preset == preset)
			return p.id;
	return "iso";
}

Option<ViewPreset> ParseViewPreset(StringView id) noexcept {
	for (const PresetName &p : PRESETS)
		if (id == StringView(p.id))
			return Some(p.preset);
	return NONE;
}

math::FVector3 OrbitNavigator::Forward() const noexcept {
	const float cp = std::cos(pitch);
	return {cp * std::sin(yaw), std::sin(pitch), cp * std::cos(yaw)};
}

math::FVector3 OrbitNavigator::Right() const noexcept {
	// Indépendant du tangage : jamais dégénéré.
	return {-std::cos(yaw), 0.f, std::sin(yaw)};
}

math::FVector3 OrbitNavigator::Up() const noexcept { return Right().Cross(Forward()); }

render3d::Camera OrbitNavigator::ToCamera(float aspect, float nearPlane, float farPlane) const {
	render3d::Camera camera;
	camera.position = Eye();
	camera.target = pivot;
	camera.up = Up();
	camera.fovYRadians = fovY;
	camera.aspect = aspect > 0.f ? aspect : 1.f;
	camera.nearPlane = nearPlane;
	camera.farPlane = farPlane;
	return camera;
}

float OrbitNavigator::WorldPerPixel(float viewportHeight) const noexcept {
	return 2.f * distance * std::tan(fovY * 0.5f) / std::max(1.f, viewportHeight);
}

void OrbitNavigator::Orbit(float dx, float dy, float sensitivity) noexcept {
	yaw -= dx * sensitivity;
	pitch = std::clamp(pitch - dy * sensitivity, -PITCH_LIMIT, PITCH_LIMIT);
}

void OrbitNavigator::OrbitAbout(const math::FVector3 &point, float dx, float dy, float sensitivity) noexcept {
	// L'œil exprimé dans le repère de la caméra autour de `point` garde ses
	// coordonnées : la rotation du repère emporte œil et pivot.
	const math::FVector3 eye = Eye();
	const math::FVector3 offset = eye - point;
	const float r = offset.Dot(Right()), u = offset.Dot(Up()), f = offset.Dot(Forward());
	Orbit(dx, dy, sensitivity);
	const math::FVector3 newEye = point + Right() * r + Up() * u + Forward() * f;
	pivot = newEye + Forward() * distance;
}

void OrbitNavigator::Pan(float dx, float dy, float viewportHeight) noexcept {
	const float k = WorldPerPixel(viewportHeight);
	pivot = pivot - Right() * (dx * k) + Up() * (dy * k);
}

void OrbitNavigator::Dolly(float steps, Option<math::FVector3> toward) noexcept {
	const float wanted = std::clamp(distance * std::pow(0.85f, steps), minDistance, maxDistance);
	const float factor = wanted / distance;
	if (toward.IsNone()) {
		distance = wanted;
		return;
	}
	// Vers le point visé : l'œil s'en rapproche de la même fraction, la
	// direction de vue ne change pas.
	const math::FVector3 eye = Eye();
	const math::FVector3 newEye = eye + (toward.Value() - eye) * (1.f - factor);
	distance = wanted;
	pivot = newEye + Forward() * distance;
}

void OrbitNavigator::Frame(const math::FVector3 &min, const math::FVector3 &max, float aspect) noexcept {
	const math::FVector3 center = (min + max) * 0.5f;
	const float radius = std::max(0.25f, (max - min).Length() * 0.5f);
	const float fovX = 2.f * std::atan(std::tan(fovY * 0.5f) * std::max(0.1f, aspect));
	const float half = std::min(fovY, fovX) * 0.5f;
	pivot = center;
	distance = std::clamp(radius / std::sin(half) * 1.05f, minDistance, maxDistance);
}

void OrbitNavigator::SetView(ViewPreset preset) noexcept {
	constexpr float PI = 3.14159265f;
	switch (preset) {
	case ViewPreset::Front: yaw = PI, pitch = 0.f; break;		   // regarde vers -Z
	case ViewPreset::Back: yaw = 0.f, pitch = 0.f; break;		   // vers +Z
	case ViewPreset::Left: yaw = PI * 0.5f, pitch = 0.f; break;	   // vers +X
	case ViewPreset::Right: yaw = -PI * 0.5f, pitch = 0.f; break;  // vers -X
	case ViewPreset::Top: yaw = PI, pitch = -PITCH_LIMIT; break;   // vers le bas
	case ViewPreset::Bottom: yaw = PI, pitch = PITCH_LIMIT; break; // vers le haut
	case ViewPreset::Iso: yaw = -PI * 0.75f, pitch = -0.6155f; break;
	}
}

void OrbitNavigator::Fly(float forward, float right, float up) noexcept {
	pivot = pivot + Forward() * forward + Right() * right + WORLD_UP * up;
}

void OrbitNavigator::LookFrom(const math::FVector3 &eye, const math::FVector3 &target) noexcept {
	const math::FVector3 d = target - eye;
	const float length = d.Length();
	if (length < 1e-5f)
		return;
	const math::FVector3 f = d * (1.f / length);
	pivot = target;
	distance = std::clamp(length, minDistance, maxDistance);
	pitch = std::clamp(std::asin(std::clamp(f.y, -1.f, 1.f)), -PITCH_LIMIT, PITCH_LIMIT);
	yaw = std::atan2(f.x, f.z);
}

} // namespace editor
