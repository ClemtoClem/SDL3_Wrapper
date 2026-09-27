#pragma once
/**
 * game_editor — le moteur 2D, partie PURE (ni fenêtre, ni GPU) : transforms
 * plans, liste de dessin d'une scène, projection vers l'écran, sélection au
 * pointeur. Le dessin lui-même vit dans canvas2d_render.hpp ; ce qui est ici
 * se teste sans écran (tests/game_editor_2d_smoke_test.cpp).
 *
 * ── Trois repères ────────────────────────────────────────────────────────
 *  - MONDE 2D : celui des nœuds (x vers la droite, y vers le BAS, en pixels
 *    de la résolution de référence). La caméra 2D courante y choisit ce qui
 *    est visible ; sans caméra, on voit le rectangle (0,0)–(largeur,hauteur).
 *  - ÉCRAN DE RÉFÉRENCE : l'écran virtuel de `Canvas2DDesc` (1280×720 par
 *    défaut). Les descendants d'un `CanvasLayer` y vivent directement : ils
 *    ignorent la caméra (HUD, menus).
 *  - PIXELS de la vue réelle : l'écran de référence y est mis à l'échelle
 *    en gardant ses proportions (bandes sur les côtés si besoin).
 *
 * `View2D` résume ces passages en deux transforms affines — l'un pour le
 * monde, l'autre pour l'écran de référence — calculés pour le JEU (caméra 2D)
 * ou pour l'ÉDITEUR (panoramique et zoom libres, le cadre de l'écran de
 * référence posé à l'origine du monde, comme dans Godot).
 *
 * ── Ordre de dessin ──────────────────────────────────────────────────────
 * Par `z` croissant, puis dans l'ordre de l'arbre (parcours préfixe : un
 * enfant passe devant son parent), les dessins immédiats des scripts
 * (`draw2d.*`) après les nœuds à `z` égal.
 */
#include <algorithm>
#include <array>
#include <cmath>
#include <functional>
#include <vector>

#include "core/core.hpp"
#include "math/math.hpp"
#include "sdl3/structs.hpp"

#include "../document/project.hpp"

namespace game_editor {

// ============================================================================
// Transform affine plan
// ============================================================================

/// x' = a·x + c·y + tx ; y' = b·x + d·y + ty.
struct Transform2D {
	float a = 1.f, b = 0.f, c = 0.f, d = 1.f, tx = 0.f, ty = 0.f;

	static constexpr float DEG2RAD = 3.14159265358979323846f / 180.f;

	[[nodiscard]] static Transform2D Identity() noexcept { return {}; }
	[[nodiscard]] static Transform2D Translate(float x, float y) noexcept { return {1.f, 0.f, 0.f, 1.f, x, y}; }
	[[nodiscard]] static Transform2D Translate(math::FVector2 v) noexcept { return Translate(v.x, v.y); }
	[[nodiscard]] static Transform2D Scale(float sx, float sy) noexcept { return {sx, 0.f, 0.f, sy, 0.f, 0.f}; }
	[[nodiscard]] static Transform2D Scale(float s) noexcept { return Scale(s, s); }
	/// Rotation en degrés ; avec Y vers le bas, un angle positif tourne dans
	/// le sens HORAIRE à l'écran.
	[[nodiscard]] static Transform2D Rotate(float degrees) noexcept;

	/// Composition : `(A * B).Apply(p) == A.Apply(B.Apply(p))`.
	[[nodiscard]] Transform2D operator*(const Transform2D &o) const noexcept {
		return {a * o.a + c * o.b,	   b * o.a + d * o.b,	  a * o.c + c * o.d,
				b * o.c + d * o.d,	   a * o.tx + c * o.ty + tx, b * o.tx + d * o.ty + ty};
	}

	[[nodiscard]] math::FVector2 Apply(math::FVector2 p) const noexcept;
	[[nodiscard]] math::FVector2 ApplyVector(math::FVector2 v) const noexcept { return {a * v.x + c * v.y, b * v.x + d * v.y}; }

	[[nodiscard]] float Determinant() const noexcept { return a * d - b * c; }

	[[nodiscard]] Option<Transform2D> Inverse() const noexcept;

	/// Facteur d'échelle moyen (pour une épaisseur de trait, une taille de
	/// police à l'écran) : racine du déterminant.
	[[nodiscard]] float MeanScale() const noexcept { return std::sqrt(std::fabs(Determinant())); }
	/// Angle de l'axe X transformé, en degrés.
	[[nodiscard]] float RotationDegrees() const noexcept { return std::atan2(b, a) / DEG2RAD; }
	[[nodiscard]] math::FVector2 Origin() const noexcept { return {tx, ty}; }
};

/// Transform local 2D d'un nœud : translation(x, y) · rotation(z) · échelle(x, y).
[[nodiscard]] Transform2D Local2D(const scene::Transform &t) noexcept;

// ============================================================================
// Liste de dessin
// ============================================================================

/// Repère d'un élément : le monde (suit la caméra 2D) ou l'écran de
/// référence (sous un `CanvasLayer`, ou dessin de script `space: "screen"`).
enum class Space2D : uint8_t { WORLD, SCREEN };

/// Mesure d'un texte (largeur, hauteur) à une taille de police — fournie par
/// le moteur de rendu quand il existe, estimée sinon (cf. EstimateText).
using TextMeasure = std::function<math::FVector2(const String &text, float fontSize)>;

/// Estimation sans police : ~0,56 em par caractère, 1,25 em de haut.
[[nodiscard]] math::FVector2 EstimateText(const String &text, float fontSize);

struct DrawItem2D {
	scene::NodeId id; ///< vide pour un dessin de script
	CanvasItemDesc item;
	Transform2D transform; ///< local → repère de `space`
	Space2D space = Space2D::WORLD;
	size_t order = 0; ///< rang de création (départage à `z` égal)
};

/// Caméra 2D résolue : le point du monde visé (au centre) et son zoom.
struct Camera2DState {
	scene::NodeId id;
	math::FVector2 position;
	float zoom = 1.f;
};

/// Ce que montre une scène 2D à un instant donné.
struct Canvas2DFrame {
	std::vector<DrawItem2D> items; ///< triés dans l'ordre de dessin
	Option<Camera2DState> camera;  ///< caméra 2D courante (mode Jeu)
	std::vector<Camera2DState> cameras; ///< toutes (repères de l'éditeur)

	/// Remet les éléments dans l'ordre de dessin (z, puis rang).
	void Sort();
};

/// Parcourt l'arbre de la scène : chaque nœud visible portant un
/// `CanvasItem` devient un élément de dessin. Seuls les ancêtres 2D
/// composent le transform (un nœud 2D rangé sous un groupe 3D repart de
/// l'origine) ; un `CanvasLayer` fait passer sa descendance dans le repère
/// de l'écran ; un nœud masqué masque sa descendance.
[[nodiscard]] Canvas2DFrame BuildCanvasFrame(const SceneDesc &scene);

/// Transform d'un nœud (local → repère de `space`), selon les mêmes règles
/// que BuildCanvasFrame ; NONE pour un nœud qui n'est pas 2D.
struct NodePlacement2D {
	Transform2D transform; ///< le nœud lui-même
	Transform2D parent;    ///< son parent (ce qui déplace sa position locale)
	Space2D space = Space2D::WORLD;
};

[[nodiscard]] Option<NodePlacement2D> PlaceNode2D(const SceneDesc &scene, scene::NodeId id);

// ============================================================================
// Géométrie locale et sélection
// ============================================================================

/// Rectangle aligné (min, max).
struct Bounds2D {
	math::FVector2 min, max;

	[[nodiscard]] bool Contains(math::FVector2 p) const noexcept;
	[[nodiscard]] math::FVector2 Size() const noexcept { return {max.x - min.x, max.y - min.y}; }
	[[nodiscard]] math::FVector2 Center() const noexcept { return {(min.x + max.x) * 0.5f, (min.y + max.y) * 0.5f}; }
	void Grow(float margin) noexcept;
};

/// Boîte d'un élément dans son repère local (avant son transform).
[[nodiscard]] Bounds2D LocalBounds(const CanvasItemDesc &item, const TextMeasure &measure = {});

/// Distance d'un point à un segment.
[[nodiscard]] float DistanceToSegment(math::FVector2 p, math::FVector2 a, math::FVector2 b) noexcept;

/// Point dans un polygone (règle pair-impair, concave accepté).
[[nodiscard]] bool PointInPolygon(math::FVector2 p, const std::vector<math::FVector2> &poly) noexcept;

/// Le point `local` (repère de l'élément) touche-t-il l'élément ? `slack` :
/// marge en unités locales (un trait fin reste attrapable).
[[nodiscard]] bool HitsLocal(const CanvasItemDesc &item, math::FVector2 local, float slack,
									const TextMeasure &measure = {});

// ============================================================================
// Projection vers l'écran
// ============================================================================

/// Passage du 2D aux pixels d'une vue.
struct View2D {
	Transform2D world;  ///< monde 2D → pixels
	Transform2D screen; ///< écran de référence → pixels
	sdl3::FRect frame{}; ///< l'écran de référence, en pixels

	[[nodiscard]] const Transform2D &For(Space2D space) const noexcept;

	/// Vue du JEU : l'écran de référence tient dans `viewport` sans
	/// déformation (centré) ; la caméra 2D, s'il y en a une, amène le point
	/// visé au centre et zoome autour de lui.
	[[nodiscard]] static View2D Game(sdl3::FRect viewport, math::FVector2 reference,
									 const Option<Camera2DState> &camera);

	/// Vue de l'ÉDITEUR : `center` (monde) au centre de la vue, `zoom`
	/// pixels par unité. L'écran de référence est le cadre (0,0)–référence
	/// du monde : le HUD s'édite donc là où il apparaîtra.
	[[nodiscard]] static View2D Editor(sdl3::FRect viewport, math::FVector2 reference, math::FVector2 center,
									   float zoom);

	/// Zoom qui fait tenir l'écran de référence dans la vue, avec une marge.
	[[nodiscard]] static float FitZoom(sdl3::FRect viewport, math::FVector2 reference, float margin = 0.88f);
};

/// Élément le plus en avant sous le point `pixel` (repère de la vue), en
/// partant du DERNIER dessiné. `slackPixels` : tolérance à l'écran.
[[nodiscard]] Option<size_t> PickItem(const std::vector<DrawItem2D> &items, const View2D &view,
											 math::FVector2 pixel, float slackPixels = 3.f,
											 const TextMeasure &measure = {});

/// Coins d'un élément à l'écran (pour son cadre de sélection).
[[nodiscard]] std::array<math::FVector2, 4> ScreenCorners(const DrawItem2D &item, const View2D &view,
																 const TextMeasure &measure = {});

/// Deux éléments se recouvrent-ils ? Test des boîtes ORIENTÉES (axes
/// séparateurs) dans le repère du monde — la collision des jeux 2D simples.
[[nodiscard]] bool Overlaps(const DrawItem2D &x, const DrawItem2D &y, const TextMeasure &measure = {});

} // namespace game_editor
