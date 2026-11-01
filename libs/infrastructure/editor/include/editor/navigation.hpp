#pragma once
/**
 * editor::OrbitNavigator — caméra d'éditeur autour d'un PIVOT (Blender,
 * SketchUp, IngeTrazo) : orbite, orbite autour du point visé, panoramique
 * qui garde le point sous le curseur, zoom vers le curseur, cadrage d'une
 * boîte (« zoom étendu »), vues normalisées (face, dessus, iso…) et vol libre
 * (ZQSD) qui déplace pivot et œil ensemble.
 *
 * Pur calcul (Y vers le haut, angles en radians) : aucune dépendance à la
 * souris ni à la fenêtre ; les tests pilotent la caméra en pixels.
 */
#include <core/core.hpp>
#include <render3d/camera.hpp>

namespace editor {

enum class ViewPreset : uint8_t { Front, Back, Left, Right, Top, Bottom, Iso };

[[nodiscard]] const char *ViewPresetId(ViewPreset preset) noexcept; ///< "front", "top", "iso"…
[[nodiscard]] Option<ViewPreset> ParseViewPreset(StringView id) noexcept;

class OrbitNavigator {
public:
	/// Tangage borné (±89°) : au-delà, le vecteur « haut » dégénère.
	static constexpr float PITCH_LIMIT = 1.55f;

	math::FVector3 pivot{0.f, 0.f, 0.f};
	float distance = 10.f;
	float yaw = -2.35f;	 ///< 0 : regarde vers +Z
	float pitch = -0.5f; ///< négatif : regarde vers le bas
	float fovY = 60.f * (3.14159265f / 180.f);
	float minDistance = 0.05f, maxDistance = 1.0e5f;

	[[nodiscard]] math::FVector3 Forward() const noexcept;
	[[nodiscard]] math::FVector3 Right() const noexcept;
	[[nodiscard]] math::FVector3 Up() const noexcept;
	[[nodiscard]] math::FVector3 Eye() const noexcept { return pivot - Forward() * distance; }
	[[nodiscard]] render3d::Camera ToCamera(float aspect, float nearPlane = 0.05f, float farPlane = 1000.f) const;
	/// Monde par pixel à la distance du pivot (vue de `viewportHeight` px).
	[[nodiscard]] float WorldPerPixel(float viewportHeight) const noexcept;

	/// Orbite autour du pivot (glissé de `dx`, `dy` pixels).
	void Orbit(float dx, float dy, float sensitivity = 0.008f) noexcept;
	/// Orbite autour d'un point (celui sous le curseur) : ce point reste fixe
	/// à l'écran, pivot et œil tournent autour.
	void OrbitAbout(const math::FVector3 &point, float dx, float dy, float sensitivity = 0.008f) noexcept;
	/// Panoramique : le monde suit le curseur.
	void Pan(float dx, float dy, float viewportHeight) noexcept;
	/// Zoom de `steps` crans (positif : avance) ; vers `toward` (le point sous
	/// le curseur) s'il est donné, sinon vers le pivot.
	void Dolly(float steps, Option<math::FVector3> toward = NONE) noexcept;
	/// Cadre la boîte (zoom étendu), même direction de vue.
	void Frame(const math::FVector3 &min, const math::FVector3 &max, float aspect) noexcept;
	void SetView(ViewPreset preset) noexcept;
	/// Vol libre : avance / droite / monte (unités monde), pivot et œil ensemble.
	void Fly(float forward, float right, float up) noexcept;
	/// Œil en `eye`, regardant `target` (pivot = cible).
	void LookFrom(const math::FVector3 &eye, const math::FVector3 &target) noexcept;
};

} // namespace editor
