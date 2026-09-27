#pragma once
#include <cstdint>
#include <cstring>
#include <vector>

#include "../core/core.hpp"
#include "../sdl3/gpu.hpp"
#include "camera.hpp"
#include "canvas.hpp"
#include "object3d.hpp"

// M21 (Phase 2 du plan) : primitive de compositing GPU<->CPU partagée par
// Viewport3D (M22) et les effets shader de widget (M24) — SDL_Renderer (2D,
// ui::) et Canvas/GpuDevice (SDL_GPU) sont deux sous-systèmes SDL séparés
// sans interop documentée dans cet environnement ; le round-trip CPU via
// GpuTransferBuffer est le seul mécanisme disponible (déjà celui du loader
// Radiance HDR et du render-to-texture shadow/IBL de ce dépôt, voir le plan).
//
// Scope cut délibéré (voir le plan) : RenderObjectToTexture ne supporte NI
// les ombres NI l'IBL — même précédent que InstancedMesh/SkinnedMesh (M16/
// M17). Seul le chemin Phong/PBR "plat" (hasShadows=false, hasEnvironment=
// false) est rendu ; voir Canvas::RenderObjectOffscreen (canvas.hpp, corps
// ci-dessous) pour le détail.
namespace render3d {

/// Paire de GpuTexture couleur (COLOR_TARGET|SAMPLER) + profondeur
/// (DEPTH_STENCIL_TARGET), (re)créée paresseusement quand la taille ou le
/// format demandés changent — même schéma que Canvas::RecreateDepthTexture
/// (canvas.hpp:133-149), dupliqué pour la texture couleur.
class OffscreenTarget {
	Option<sdl3::GpuTexture> m_colorTexture = NONE;
	Option<sdl3::GpuTexture> m_depthTexture = NONE;
	/// Tampon de transfert GPU->CPU, CONSERVÉ d'une image à l'autre : il ne
	/// dépend que de la taille et du format de la cible, donc le recréer à
	/// chaque téléchargement était une allocation de mémoire GPU par image et
	/// par viewport, pour un contenu de taille rigoureusement identique.
	/// Recréé uniquement par EnsureSize, comme les textures.
	Option<sdl3::GpuTransferBuffer> m_downloadBuffer = NONE;
	uint32_t m_downloadByteSize = 0;
	uint32_t m_width = 0;
	uint32_t m_height = 0;
	sdl3::GpuTextureFormat m_colorFormat{};
	sdl3::GpuTextureFormat m_depthFormat{};

public:
	OffscreenTarget() = default;

	/// No-op si la taille/le format demandés correspondent déjà aux
	/// ressources existantes (comme Canvas::Begin() pour sa propre depth
	/// texture) — recrée sinon la paire couleur+profondeur en entier.
	[[nodiscard]] Result<bool, StringView> EnsureSize(sdl3::GpuDevice &device, uint32_t width, uint32_t height,
													   sdl3::GpuTextureFormat colorFormat,
													   sdl3::GpuTextureFormat depthFormat);

	[[nodiscard]] uint32_t Width() const noexcept { return m_width; }
	[[nodiscard]] uint32_t Height() const noexcept { return m_height; }
	[[nodiscard]] sdl3::GpuTextureFormat ColorFormat() const noexcept { return m_colorFormat; }
	[[nodiscard]] sdl3::GpuTextureFormat DepthFormat() const noexcept { return m_depthFormat; }
	[[nodiscard]] bool IsReady() const noexcept { return m_colorTexture.IsSome() && m_depthTexture.IsSome(); }

	[[nodiscard]] sdl3::GpuTexture &ColorTexture() noexcept { return m_colorTexture.Value(); }
	[[nodiscard]] const sdl3::GpuTexture &ColorTexture() const noexcept { return m_colorTexture.Value(); }
	[[nodiscard]] sdl3::GpuTexture &DepthTexture() noexcept { return m_depthTexture.Value(); }
	[[nodiscard]] const sdl3::GpuTexture &DepthTexture() const noexcept { return m_depthTexture.Value(); }

	/// Tampon de transfert réutilisable pour le téléchargement des pixels
	/// (cf. m_downloadBuffer) — `NONE` s'il n'a pas pu être créé.
	[[nodiscard]] bool HasDownloadBuffer() const noexcept { return m_downloadBuffer.IsSome(); }
	[[nodiscard]] sdl3::GpuTransferBuffer &DownloadBuffer() noexcept { return m_downloadBuffer.Value(); }
	[[nodiscard]] uint32_t DownloadByteSize() const noexcept { return m_downloadByteSize; }
};

// ----------------------------------------------------------------------------
// Canvas::RenderObjectOffscreen — corps (déclaré dans canvas.hpp, voir la
// note sur l'include circulaire là-bas). A besoin du type complet
// d'OffscreenTarget, donc ne peut pas vivre dans canvas.hpp lui-même.
// ----------------------------------------------------------------------------

/// Choix d'implémentation (voir le plan, option (a)) : plutôt que de faire
/// marcher Object3D::Traverse+OnDraw() sur une file de dessin séparée (ce qui
/// exigerait de dupliquer le point d'extension OnDraw()/DrawMesh* pour
/// chaque futur Object3D — Points/LineSegments/Sprite/LOD...), on réutilise
/// le VRAI dispatch OnDraw() de Canvas (celui que DrawObject() utilise) en
/// sauvegardant/vidant/restaurant m_drawList et m_frameActive autour de
/// l'appel : DrawMesh()/DrawMeshGroup() n'empilent que si m_frameActive est
/// vrai, donc on le force temporairement même hors Begin()/End(). Le rendu
/// lui-même utilise un command buffer LOCAL, distinct de m_commandBuffer —
/// aucun risque de perturber une frame de fenêtre déjà ouverte.


/// Forwarder d'une ligne (M30) — voir la déclaration dans canvas.hpp pour le
/// raisonnement complet : `camera.ViewProjectionMatrix()` EST le
/// view-projection que ce chemin utilisait déjà, donc comportement
/// strictement identique pour tout appelant existant.


namespace detail {

/// Téléchargement partagé (M30) par les deux overloads de RenderObjectToTexture
/// ci-dessous — factorisé pour que l'overload à view-projection explicite ne
/// duplique pas cette manœuvre GPU copy-pass/map (même raisonnement DRY que
/// detail::ClipObliqueRow, camera.hpp).
[[nodiscard]] Result<bool, StringView> DownloadColorTexture(Canvas &canvas, OffscreenTarget &target,
																	std::vector<uint8_t> &outPixels);

} // namespace detail

/// Rend `root` (sous-arbre Shape visible, voir Canvas::RenderObjectOffscreen
/// ci-dessus) dans `target` avec `camera`, dont `aspect` est ÉCRASÉ à partir
/// des dimensions réelles de `target` avant de dessiner — c'est ce mécanisme
/// qui permettra plus tard à un widget Viewport3D de n'importe quelle forme
/// de ne jamais déformer la projection 3D (voir M22 du plan). Vérification
/// défensive : l'appelant est censé avoir déjà réglé `camera.aspect`
/// lui-même (voir Camera::aspect, camera.hpp:11). Télécharge ensuite la
/// texture couleur rendue dans `outPixels` (tightly packed, row-major) —
/// best-effort, ne fait jamais planter l'appelant en cas d'échec GPU.
[[nodiscard]] Result<bool, StringView> RenderObjectToTexture(Canvas &canvas, Object3D &root, Camera camera,
																	OffscreenTarget &target,
																	std::vector<uint8_t> &outPixels);

/// Overload (M30) à view-projection explicite — voir la déclaration de
/// Canvas::RenderObjectOffscreen (4 arguments) pour le raisonnement complet.
/// À la différence de l'overload ci-dessus, `camera.aspect` N'EST PAS écrasé
/// depuis les dimensions de `target` : l'appelant a déjà construit
/// `viewProjection` avec le bon aspect (typiquement via une projection à
/// plan proche oblique composée sur une vue caméra déjà correcte — voir
/// portal.hpp), donc l'écraser ici referait un travail déjà fait et pourrait
/// même le rendre incohérent avec la projection réellement utilisée.
[[nodiscard]] Result<bool, StringView> RenderObjectToTexture(Canvas &canvas, Object3D &root, Camera camera,
																	const math::FMatrix4 &viewProjection,
																	OffscreenTarget &target,
																	std::vector<uint8_t> &outPixels);

/// Direction inverse (CPU -> GPU) — upload d'un buffer de pixels
/// tightly-packed (p.ex. issu d'un sdl3::Surface converti en RGBA32, ou tout
/// buffer déjà dans le format demandé) vers une nouvelle GpuTexture
/// (usage SAMPLER). Même enchaînement que LoadTextureFromFile
/// (texture_loader.hpp) mais depuis une mémoire déjà décodée plutôt qu'un
/// fichier — pas encore consommé par ce jalon (M21), sert M23/M24 (widget
/// shader effects, voir le plan) : testé indépendamment ici.
[[nodiscard]] Result<sdl3::GpuTexture, StringView>
UploadSurfaceToGpuTexture(sdl3::GpuDevice &device, const uint8_t *pixels, uint32_t width, uint32_t height,
						  sdl3::GpuTextureFormat format);

} // namespace render3d
