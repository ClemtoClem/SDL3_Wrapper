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

/// Passage de l'image rendue (texture SDL_GPU) au renderer 2D de l'interface.
enum class ViewportTransfer : uint8_t {
	/// Rendu, attente du GPU, téléchargement des pixels, attente, copie dans
	/// une texture du renderer : deux attentes bloquantes et un aller-retour
	/// GPU → CPU → GPU par image. Marche avec tout renderer.
	READBACK,
	/// Même aller-retour, sans attente : chaque image est rendue et téléchargée
	/// dans une des trois cibles d'un anneau ; on affiche la plus récente dont
	/// le GPU a fini (une image de retard en général).
	PIPELINED,
	/// Aucune copie : le renderer 2D (« gpu », même device que le Canvas)
	/// affiche directement la texture rendue ; la file GPU ordonne le rendu
	/// 3D avant l'affichage, le CPU n'attend rien. Repli sur READBACK si le
	/// renderer ne le permet pas.
	DIRECT,
};

/// Brique : vue 3D (état GPU compris, non copiable).
struct Viewport3DState {
	render3d::Object3D *root = nullptr; ///< non possédé
	render3d::Camera camera{};
	render3d::OffscreenTarget target{};
	Option<sdl3::Texture> displayTexture = NONE;
	uint32_t textureWidth = 0, textureHeight = 0;
	std::vector<uint8_t> pixels;
	ViewportTransfer transfer = ViewportTransfer::READBACK;
	/// PIPELINED : cibles en vol (rendu + téléchargement soumis, clôture).
	struct InFlight {
		render3d::OffscreenTarget target{};
		Option<sdl3::GpuFence> fence = NONE;
		uint64_t frame = 0;
	};
	std::vector<InFlight> ring;
	uint64_t ringFrame = 0, shownFrame = 0;
	bool directFailed = false; ///< DIRECT refusé par le renderer : READBACK
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
