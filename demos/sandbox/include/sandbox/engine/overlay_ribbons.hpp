#pragma once
/**
 * OverlayRibbons — traits d'éditeur (arêtes du modèle édité, boîte de la
 * sélection, mètre) dessinés DANS la scène 3D comme de fins rubans de deux
 * triangles tournés vers la caméra.
 *
 * Pourquoi pas render3d::LineSegments : la topologie LINES n'apparaît pas
 * dans le rendu hors écran du widget Viewport3D ; les triangles passent par
 * le même chemin que tout le reste. La largeur est donnée en PIXELS (elle
 * dépend de la distance à la caméra), et chaque ruban est avancé d'un rien
 * vers la caméra pour ne pas se battre avec la face sur laquelle il est posé.
 */
#include <cstdint>
#include <vector>

#include <math/math.hpp>
#include <render3d/camera.hpp>
#include <render3d/vertex.hpp>

namespace sandbox {

class OverlayRibbons {
public:
	/// `viewportHeight` en pixels : sert à convertir une largeur en pixels.
	OverlayRibbons(const render3d::Camera &camera, float viewportHeight) noexcept
		: m_eye(camera.position),
		  m_pixel(2.f * math::Tan(camera.fovYRadians * 0.5f) / math::Max(viewportHeight, 1.f)) {}

	/// Segment monde [a, b], `widthPx` pixels de large.
	void Add(const math::FVector3 &a, const math::FVector3 &b, sdl3::Color color, float widthPx = 1.6f);

	/// Segment en pointillés (tirets de `dashPx` pixels environ).
	void AddDashed(const math::FVector3 &a, const math::FVector3 &b, sdl3::Color color, float widthPx = 1.6f,
				   float dashPx = 8.f);

	/// Les 12 arêtes d'une boîte englobante.
	void AddBox(const math::FAABB &box, sdl3::Color color, float widthPx = 2.f);

	[[nodiscard]] bool IsEmpty() const noexcept { return m_indices.empty(); }
	std::vector<render3d::Vertex3D> &Vertices() noexcept { return m_vertices; }
	std::vector<uint32_t> &Indices() noexcept { return m_indices; }

private:
	math::FVector3 m_eye;
	float m_pixel;
	std::vector<render3d::Vertex3D> m_vertices;
	std::vector<uint32_t> m_indices;
};

} // namespace sandbox
