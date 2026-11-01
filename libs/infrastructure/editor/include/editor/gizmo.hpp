#pragma once
/**
 * editor::TransformGizmo — gizmo de déplacement / rotation / échelle d'un
 * éditeur 3D, de taille CONSTANTE à l'écran.
 *
 *  - Pick : la poignée sous le curseur (axe, plan, centre, anneau), en
 *    pixels, à partir de la caméra de la vue ;
 *  - Begin / Drag / End : un glissé rend la transformation TOTALE depuis le
 *    début du geste (`GizmoDelta`), jamais un incrément — l'appelant applique
 *    `delta` à l'état qu'il avait au Begin (une seule étape d'annulation) ;
 *  - accroches : pas de grille (déplacement), pas d'angle (rotation), pas
 *    d'échelle ;
 *  - Strokes : ce qu'il faut dessiner, en pixels de la vue (tracé 2D par
 *    dessus le rendu 3D : ui::Painter, sdl3::Renderer…).
 *
 * Pur calcul, sans fenêtre : les tests pilotent le gizmo en pixels.
 */
#include <vector>

#include <core/core.hpp>
#include <render3d/camera.hpp>

namespace editor {

enum class GizmoMode : uint8_t { Translate, Rotate, Scale };
/// Poignée : axe, plan (deux axes), centre (`All` : écran en déplacement,
/// uniforme en échelle).
enum class GizmoHandle : uint8_t { None, X, Y, Z, XY, YZ, ZX, All };

[[nodiscard]] const char *GizmoModeId(GizmoMode mode) noexcept; ///< "translate", "rotate", "scale"
[[nodiscard]] Option<GizmoMode> ParseGizmoMode(StringView id) noexcept;

/// Transformation totale depuis le début du glissé.
struct GizmoDelta {
	math::FVector3 translation{0.f, 0.f, 0.f};
	math::FVector3 rotationAxis{0.f, 1.f, 0.f};
	float angle = 0.f; ///< radians, autour de `rotationAxis` passant par le centre
	math::FVector3 scale{1.f, 1.f, 1.f};
};

/// Segment à dessiner (pixels de la vue).
struct GizmoStroke {
	math::FVector2 a, b;
	sdl3::FColor color;
	float width = 2.f;
};

/// La vue : caméra et taille en pixels.
struct GizmoView {
	render3d::Camera camera;
	float width = 1.f, height = 1.f;
};

class TransformGizmo {
public:
	GizmoMode mode = GizmoMode::Translate;
	math::FVector3 position{0.f, 0.f, 0.f};
	float sizePixels = 90.f;   ///< longueur des axes à l'écran
	float pickRadius = 8.f;	   ///< tolérance de saisie (pixels)
	float translateSnap = 0.f; ///< 0 : libre
	float rotateSnap = 0.f;	   ///< radians, 0 : libre
	float scaleSnap = 0.f;	   ///< 0 : libre
	bool visible = true;

	/// Longueur monde des axes pour `sizePixels` à l'écran.
	[[nodiscard]] float WorldSize(const GizmoView &view) const noexcept;
	[[nodiscard]] GizmoHandle Pick(const GizmoView &view, float x, float y) const;
	/// Survol (mise en évidence) ; rend la poignée.
	GizmoHandle Hover(const GizmoView &view, float x, float y);

	/// Commence un glissé sur la poignée sous le curseur ; false s'il n'y en a pas.
	bool Begin(const GizmoView &view, float x, float y);
	/// Transformation totale depuis Begin (accroches appliquées).
	[[nodiscard]] GizmoDelta Drag(const GizmoView &view, float x, float y) const;
	void End() noexcept { m_active = GizmoHandle::None; }
	[[nodiscard]] bool Active() const noexcept { return m_active != GizmoHandle::None; }
	[[nodiscard]] GizmoHandle ActiveHandle() const noexcept { return m_active; }
	[[nodiscard]] GizmoHandle Hovered() const noexcept { return m_hovered; }

	[[nodiscard]] std::vector<GizmoStroke> Strokes(const GizmoView &view) const;

private:
	[[nodiscard]] Option<math::FVector3> PlaneHit(const math::FRay &ray, const math::FVector3 &normal) const;
	[[nodiscard]] float AxisParameter(const math::FRay &ray, const math::FVector3 &axis) const;

	GizmoHandle m_active = GizmoHandle::None;
	GizmoHandle m_hovered = GizmoHandle::None;
	// État du début du glissé.
	math::FVector3 m_startPosition{0.f, 0.f, 0.f};
	math::FVector3 m_startHit{0.f, 0.f, 0.f};
	math::FVector3 m_planeNormal{0.f, 1.f, 0.f};
	float m_startParameter = 0.f;
	float m_startScreenDistance = 1.f;
	math::FVector2 m_startScreenCenter{0.f, 0.f};
};

} // namespace editor
