#pragma once
/**
 * ui::Viewport3D — widget affichant une scène render3d (Vulkan via SDL_GPU).
 *
 * La scène est rendue HORS ÉCRAN par le `render3d::Canvas` enregistré dans
 * l'interface (`Ui::Initialize(canvas)`), à la taille calculée du widget,
 * puis affichée comme une image : elle se compose donc avec le reste de
 * l'arbre (clips, popups au-dessus, défilement). Sans Canvas enregistré, le
 * widget reste vide (aucune erreur).
 *
 * @code{.cpp}
 * render3d::Object3D scene;            // doit vivre plus longtemps que le widget
 * gui.Initialize(canvas);
 * gui.Mount(ui::Viewport3D(scene, camera).Grow().Ref(view));
 * view.Get<ui::Viewport3DState>().Unwrap()->camera = orbit.Camera();
 * @endcode
 */
#include <vector>

#include <render3d/camera.hpp>
#include <render3d/canvas.hpp>
#include <render3d/light.hpp>
#include <render3d/object3d.hpp>
#include <render3d/offscreen.hpp>

#include "builder.hpp"

namespace ui {

inline constexpr sdl3::GpuTextureFormat VIEWPORT3D_COLOR_FORMAT = SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM;
inline constexpr sdl3::GpuTextureFormat VIEWPORT3D_DEPTH_FORMAT = SDL_GPU_TEXTUREFORMAT_D32_FLOAT;

/// Éclairage propre à une vue (sinon celui du Canvas).
struct ViewportLighting {
	render3d::DirectionalLight sun;
	render3d::AmbientLight ambient;
	sdl3::Color background{0, 0, 0, 255};
};

/// Brique : vue 3D (état GPU compris, non copiable).
struct Viewport3DState {
	render3d::Object3D *root = nullptr; ///< non possédé
	render3d::Camera camera{};
	render3d::OffscreenTarget target{};
	Option<sdl3::Texture> displayTexture = NONE;
	uint32_t textureWidth = 0, textureHeight = 0;
	std::vector<uint8_t> pixels;
	/// Vrai : rendue à chaque image ; faux : seulement si `needsRender` ou
	/// si la taille change (scène statique).
	bool continuous = true;
	bool needsRender = true;
	Option<ViewportLighting> lighting = NONE;
};

/// Widget `viewport3d` (sans taille par défaut : à dimensionner).
[[nodiscard]] WidgetBuilder Viewport3D(render3d::Object3D &root, render3d::Camera camera = {}, bool continuous = true,
									   Option<ViewportLighting> lighting = NONE);

/// Demande un nouveau rendu d'une vue non continue.
void RequestViewportRender(Widget view);

/// Rendu hors écran et affichage des vues (enregistrée par ui::Ui).
void RegisterViewport3DExtension(Context &ctx);

} // namespace ui
