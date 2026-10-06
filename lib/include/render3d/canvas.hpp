#pragma once
#include <array>
#include <cstddef>
#include <cstring>
#include <span>
#include <unordered_map>
#include <vector>

#include "../core/core.hpp"
#include "../math/math.hpp"
#include "../sdl3/gpu.hpp"
#include "camera.hpp"
#include "light.hpp"
#include "material.hpp"
#include "mesh.hpp"
#include "cubemap.hpp"
#include "environment.hpp"
#include "object3d.hpp"
#include "shader_builder.hpp"
#include "shader_program.hpp"
#include "shadow.hpp"
#include "skinned_geometry.hpp"

namespace render3d {

// Déclaration avancée seulement : RenderObjectOffscreen ci-dessous n'a besoin
// que d'une référence dans sa signature, pas du type complet — la définition
// complète et le corps de la méthode vivent dans offscreen.hpp (M21), qui
// inclut ce fichier ; l'inverse (canvas.hpp incluant offscreen.hpp) créerait
// un cycle.
class OffscreenTarget;

// Compositeur 3D construit directement sur sdl3::Gpu* : un GpuDevice, un
// GpuCommandBuffer par frame, un cache de GpuGraphicsPipeline par
// combinaison de shader/états réellement rencontrée (PipelineKey). Le dessin
// 2D (shape2d*) et l'intégration comme widget UI sont hors périmètre de
// cette phase — voir le plan. `Device()` est exposé volontairement : la
// phase suivante (compositing GPU natif dans RenderSystem) en aura besoin.
class Canvas {
	sdl3::GpuDevice m_device;
	/// NONE quand ce Canvas est construit via Create(sdl3::Renderer&, ...) —
	/// usage offscreen exclusivement (RenderObjectToTexture/M21, jamais
	/// Begin()/End() sur la swapchain), voir ce constructeur pour le
	/// raisonnement complet : aucune Window réelle n'est nécessaire dans ce
	/// cas (SDL_ClaimWindowForGPUDevice n'est jamais appelé), et il n'existe
	/// aucun moyen sûr de reconstruire une Ref<sdl3::Window> à partir du seul
	/// SDL_Window* que SDL_GetRenderWindow() renvoie (Ref<T> exige un objet
	/// C++ existant, pas juste un pointeur C). Begin() échoue proprement
	/// (return false) si ce champ est NONE.
	Option<Ref<sdl3::Window>> m_window;
	/// Faux quand le SDL_GPUDevice sous-jacent est EMPRUNTÉ à un
	/// sdl3::Renderer existant (Create(sdl3::Renderer&, ...), via
	/// Renderer::GetGPUDevice() — voir sa doc dans render.hpp) plutôt que créé
	/// et possédé par ce Canvas — le destructeur relâche alors m_device SANS
	/// le détruire (cf. ~Canvas() ci-dessous), pour ne jamais détruire un
	/// device que le Renderer croit encore posséder.
	bool m_ownsDevice = true;
	int m_width;
	int m_height;

	sdl3::GpuTextureFormat m_colorFormat;
	sdl3::GpuTextureFormat m_depthFormat;
	Option<sdl3::GpuTexture> m_depthTexture = NONE;

	sdl3::GpuTexture m_whiteTexture;   // liée quand un Material n'a pas d'albedo (le shader Phong exige un sampler).
	sdl3::GpuSampler m_defaultSampler;

	ShaderProgram m_basicShader;
	ShaderProgram m_phongShader;

	std::unordered_map<PipelineKey, sdl3::GpuGraphicsPipeline> m_pipelineCache;

	sdl3::Color m_backgroundColor = sdl3::Color::BLACK();
	Camera m_camera;
	math::FFrustum m_frustum{};
	bool m_frustumValid = false;
	bool m_frustumCulling = true;
	uint32_t m_culledDraws = 0;
	uint32_t m_submittedDraws = 0;
	DirectionalLight m_directionalLight;
	AmbientLight m_ambientLight;
	std::vector<PointLight> m_pointLights;
	std::vector<SpotLight> m_spotLights;

	// Shadow mapping (M7-M9, voir shadow.hpp) : ressources créées paresseusement
	// à la première frame où une lumière a castShadow=true, puis réutilisées.
	sdl3::GpuTextureFormat m_shadowDepthFormat{};
	Option<sdl3::GpuTexture> m_dirShadowMap = NONE;
	std::array<Option<sdl3::GpuTexture>, 2> m_spotShadowMaps{};
	Option<sdl3::GpuSampler> m_shadowSampler = NONE;
	Option<sdl3::GpuGraphicsPipeline> m_shadowCasterPipeline = NONE;
	// Point light (cubemap de distance, M9) — un seul point light peut
	// projeter une ombre (voir le plan), pipeline/texture séparés du
	// shadow caster 2D directionnel/spot ci-dessus (cible couleur, pas
	// depth-only, voir shadow.hpp).
	Option<sdl3::GpuTexture> m_pointShadowMap = NONE;
	Option<sdl3::GpuTexture> m_pointShadowFaceDepth = NONE;
	Option<sdl3::GpuSampler> m_pointShadowSampler = NONE;
	Option<sdl3::GpuGraphicsPipeline> m_pointShadowCasterPipeline = NONE;

	// Environnement / skybox / IBL (M11-M14, voir cubemap.hpp/environment.hpp)
	// — Canvas ne possède pas l'Environment (chargé une fois via
	// LoadEnvironmentFromHdr, possédé par l'appelant, comme Material::albedo),
	// seulement des références + les ressources de dessin, créées paresseusement.
	Option<Ref<sdl3::GpuTexture>> m_environmentCubemap = NONE;
	Option<Ref<sdl3::GpuTexture>> m_environmentIrradiance = NONE;
	Option<Ref<sdl3::GpuTexture>> m_environmentPrefiltered = NONE;
	Option<Ref<sdl3::GpuTexture>> m_environmentBrdfLut = NONE;
	Option<sdl3::GpuGraphicsPipeline> m_skyboxPipeline = NONE;
	Option<sdl3::GpuBuffer> m_skyboxVertexBuffer = NONE;
	Option<sdl3::GpuSampler> m_skyboxSampler = NONE;
	uint32_t m_skyboxVertexCount = 0;
	Option<sdl3::GpuSampler> m_iblCubeSampler = NONE;
	Option<sdl3::GpuSampler> m_iblLutSampler = NONE;

	struct DrawCall {
		RefMut<Mesh> mesh;
		math::FMatrix4 transform;
		Material material;
		uint32_t firstIndex = 0;
		uint32_t indexCount = 0; // 0 = maillage entier (résolu dans End())
		PrimitiveTopology topology = PrimitiveTopology::TRIANGLES;
	};
	std::vector<DrawCall> m_drawList;

	/// File séparée pour InstancedMesh (M16) — `instances` copiées à l'appel
	/// (même philosophie que Material déjà copié par valeur dans DrawCall),
	/// uploadées dans un vertex buffer transient recréé chaque frame par
	/// End() (voir m_instancedFrameBuffers) plutôt qu'un buffer persistant :
	/// les transforms d'instance changent typiquement chaque frame.
	struct InstancedDrawCall {
		RefMut<Mesh> mesh;
		Material material;
		std::vector<math::FMatrix4> instances;
	};
	std::vector<InstancedDrawCall> m_instancedDrawList;

	/// File séparée pour SkinnedMesh (M17) — `skinMatrices` copiées à l'appel
	/// (calculées une fois par frame dans SkinnedMesh::OnDraw, voir
	/// skinned_mesh.hpp), uploadées dans un storage buffer transient recréé
	/// chaque frame par End() (même politique que les instance buffers M16).
	struct SkinnedDrawCall {
		RefMut<SkinnedGeometry> geometry;
		Material material;
		std::vector<math::FMatrix4> skinMatrices;
	};
	std::vector<SkinnedDrawCall> m_skinnedDrawList;

	Option<sdl3::GpuCommandBuffer> m_commandBuffer = NONE;
	Borrowed<SDL_GPUTexture> m_swapchainTexture;
	uint32_t m_swapchainWidth = 0;
	uint32_t m_swapchainHeight = 0;
	bool m_frameActive = false;

	/// Calque 2D de la frame en cours (cf. SetOverlay) : une copie compacte
	/// des pixels de chaque région, envoyée puis copiée par End().
	struct OverlayRegion {
		sdl3::Rect rect;
		std::vector<uint8_t> pixels; ///< RGBA8, rect.w * rect.h * 4 octets
	};
	std::vector<OverlayRegion> m_overlayRegions;
	Option<sdl3::GpuTexture> m_overlayTexture = NONE; ///< taille de la swapchain, recréée si elle change
	uint32_t m_overlayWidth = 0;
	uint32_t m_overlayHeight = 0;
	void UploadAndBlitOverlay(sdl3::GpuCommandBuffer &cmd);

	Canvas(sdl3::GpuDevice device, Option<Ref<sdl3::Window>> window, bool ownsDevice, int w, int h,
		   sdl3::GpuTextureFormat colorFormat, sdl3::GpuTextureFormat depthFormat, sdl3::GpuTexture whiteTexture,
		   sdl3::GpuSampler defaultSampler, ShaderProgram basicShader, ShaderProgram phongShader) noexcept
		: m_device(std::move(device)), m_window(window), m_ownsDevice(ownsDevice), m_width(w), m_height(h),
		  m_colorFormat(colorFormat), m_depthFormat(depthFormat), m_whiteTexture(std::move(whiteTexture)),
		  m_defaultSampler(std::move(defaultSampler)), m_basicShader(std::move(basicShader)),
		  m_phongShader(std::move(phongShader)) {}

public:
	/// Mouvement explicite (nécessaire dès qu'un destructeur personnalisé est
	/// déclaré, cf. ~Canvas() ci-dessous — sinon le déplacement implicite
	/// serait supprimé, cassant `return Ok(std::move(canvas));` dans Create()
	/// et tout code appelant qui déplace un Canvas construit).
	Canvas(Canvas &&) noexcept = default;
	Canvas &operator=(Canvas &&) noexcept = default;

	/// Neutralise la destruction de m_device quand ce Canvas ne le possède
	/// pas (cf. m_ownsDevice) — Release() vide le handle AVANT que le
	/// destructeur normal de m_device (membre) ne s'exécute juste après le
	/// corps de ~Canvas(), donc SDL_DestroyGPUDevice n'est alors JAMAIS
	/// appelé sur un device qu'un sdl3::Renderer croit encore posséder.
	~Canvas();

private:
	[[nodiscard]] Result<bool, StringView> RecreateDepthTexture();

	/// Crée les ressources de shadow mapping (depth textures directionnelle +
	/// 2 spots, sampler de comparaison, pipeline "depth-only") au premier
	/// appel, puis les réutilise — best-effort : un échec laisse
	/// m_shadowCasterPipeline à NONE, End() dégrade alors en scène non ombrée
	/// plutôt que de faire échouer toute la frame.
	[[nodiscard]] bool EnsureShadowResources();

	/// Crée le pipeline/sampler/vertex buffer du skybox au premier appel où
	/// un environnement est défini — best-effort comme EnsureShadowResources :
	/// un échec laisse m_skyboxPipeline à NONE, End() saute alors le skybox
	/// plutôt que de faire échouer la frame.
	[[nodiscard]] bool EnsureSkyboxResources();

	/// Samplers IBL (M14) — un pour les 2 cubemaps (irradiance/préfiltré,
	/// LINEAR+mipmaps pour le préfiltré), un pour la LUT 2D (pas besoin de
	/// mipmaps ni de wrap : coordonnées toujours dans [0,1]).
	[[nodiscard]] bool EnsureIblSamplers();

	[[nodiscard]] Option<Ref<sdl3::GpuGraphicsPipeline>> GetOrCreatePipeline(const PipelineKey &key);

public:
	[[nodiscard]] static Result<Canvas, Error> Create(sdl3::Window &window, int w = 0, int h = 0);

	/// Variante offscreen : partage le SDL_GPUDevice d'un sdl3::Renderer déjà
	/// GPU-backed (Renderer::GetGPUDevice(), render.hpp — Err si `renderer`
	/// n'a pas été créé avec le backend "gpu" de SDL3) au lieu d'en créer un
	/// second, indépendant, comme Create(Window&, ...). N'appelle JAMAIS
	/// SDL_ClaimWindowForGPUDevice (aucune swapchain propre) : le Canvas
	/// résultant sert exclusivement au rendu offscreen (RenderObjectToTexture,
	/// M21 — voir ui::Viewport3D, M22) — Begin()/End() échoueront toujours
	/// dessus (pas de Window associée, cf. m_window). `w`/`h` n'ont ici
	/// d'effet que sur la depth texture PROPRE à ce Canvas (jamais utilisée
	/// par RenderObjectToTexture, qui rend dans l'OffscreenTarget fourni par
	/// l'appelant) — 0 vaut simplement "pas de dimension particulière".
	[[nodiscard]] static Result<Canvas, Error> Create(sdl3::Renderer &renderer, int w = 0, int h = 0);

	// ── Culling de frustum ───────────────────────────────────────────────────

	/// Active/désactive l'élimination des objets hors du champ de vision.
	///
	/// Actif par défaut : c'est la première optimisation qu'attend n'importe
	/// quelle scène un peu grande, et elle n'a AUCUN effet visible (un objet
	/// écarté est, par construction, hors de l'image). Désactivable pour
	/// comparer, ou si un shader déplace les sommets au point que la boîte
	/// englobante du maillage ne les contienne plus.
	void SetFrustumCullingEnabled(bool enabled) noexcept { m_frustumCulling = enabled; }
	[[nodiscard]] bool IsFrustumCullingEnabled() const noexcept { return m_frustumCulling; }

	/// Nombre de dessins écartés et soumis lors de la DERNIÈRE PASSE rendue —
	/// une image de fenêtre, ou un rendu hors écran (widget Viewport3D,
	/// portail), le dernier en date l'emportant. De quoi afficher
	/// « 68 / 92 objets écartés » dans un profileur.
	[[nodiscard]] uint32_t CulledDrawCount() const noexcept { return m_culledDraws; }
	[[nodiscard]] uint32_t SubmittedDrawCount() const noexcept { return m_submittedDraws; }

	void SetBackgroundColor(sdl3::Color color) noexcept { m_backgroundColor = color; }
	[[nodiscard]] sdl3::Color BackgroundColor() const noexcept { return m_backgroundColor; }
	[[nodiscard]] const DirectionalLight &Directional() const noexcept { return m_directionalLight; }
	[[nodiscard]] const AmbientLight &Ambient() const noexcept { return m_ambientLight; }
	[[nodiscard]] const std::vector<PointLight> &ActivePointLights() const noexcept { return m_pointLights; }
	[[nodiscard]] const std::vector<SpotLight> &ActiveSpotLights() const noexcept { return m_spotLights; }
	void SetCamera(const Camera &camera) noexcept { m_camera = camera; }
	[[nodiscard]] const Camera &GetCamera() const noexcept { return m_camera; }

	/// Fixe le frustum de culling depuis une matrice vue-projection et remet
	/// les compteurs de dessins à zéro. Appelé au début de chaque image
	/// (`Begin`) ET de chaque rendu hors écran, qui a sa propre projection —
	/// celle d'un portail, notamment, n'est pas celle de la caméra.
	void SetFrustumFromViewProjection(const math::FMatrix4 &viewProjection) noexcept;
	void SetLighting(const DirectionalLight &directional, const AmbientLight &ambient) noexcept;

	/// Lumières ponctuelles/spot actives pour les prochaines frames — scène
	/// entière (comme SetLighting), pas par Material. Passer des spans vides
	/// revient au Phong précompilé (directionnelle+ambiante seulement).
	void SetLights(std::span<const PointLight> pointLights, std::span<const SpotLight> spotLights);

	/// Environnement (skybox, voir environment.hpp) — Canvas garde seulement
	/// une référence, `env` doit rester en vie tant que l'environnement est
	/// actif (même contrat que Material::albedo pour les textures).
	void SetEnvironment(const Environment &env) noexcept;
	void ClearEnvironment() noexcept;

	[[nodiscard]] ShaderProgram &PhongShader() noexcept { return m_phongShader; }
	[[nodiscard]] ShaderProgram &BasicShader() noexcept { return m_basicShader; }
	[[nodiscard]] sdl3::GpuDevice &Device() noexcept { return m_device; }

	/// Acquiert la swapchain et ouvre une nouvelle frame. Retourne false (rien
	/// à dessiner) si la fenêtre est minimisée — cas normal, pas une erreur ;
	/// GetError() reste disponible pour distinguer une vraie erreur GPU.
	[[nodiscard]] bool Begin();

	/// Vrai si ce maillage, placé par `transform`, peut être visible.
	///
	/// Conservateur à dessein : un maillage vide (boîte invalide) ou un
	/// frustum non initialisé laisse TOUT passer. Écarter à tort est un bug
	/// visible (objet manquant), garder à tort ne coûte qu'un dessin.
	/// `DrawMeshGroup` teste la boîte du maillage ENTIER alors qu'il n'en
	/// dessine qu'une plage : plus large que nécessaire, donc toujours du bon
	/// côté.
	[[nodiscard]] bool IsPotentiallyVisible(const Mesh &mesh, const math::FMatrix4 &transform) const noexcept;

	void DrawMesh(Mesh &mesh, const math::FMatrix4 &transform, const Material &material,
				 PrimitiveTopology topology = PrimitiveTopology::TRIANGLES);

	void DrawMesh(RefMut<Mesh> mesh, const math::FMatrix4 &transform, const Material &material,
				 PrimitiveTopology topology = PrimitiveTopology::TRIANGLES);

	/// Dessine seulement la plage d'indices [firstIndex, firstIndex+indexCount)
	/// du maillage — utilisé pour les Mesh::Group (matériau par face), voir
	/// Shape::OnDraw().
	void DrawMeshGroup(Mesh &mesh, uint32_t firstIndex, uint32_t indexCount, const math::FMatrix4 &transform,
					   const Material &material, PrimitiveTopology topology = PrimitiveTopology::TRIANGLES);

	/// Dessine `mesh` une fois par transform dans `instances` (rendu instancié
	/// GPU réel — un seul draw call, voir shader_chunks::INSTANCE_ATTRIBUTES) —
	/// InstancedMesh (object3d.hpp) l'appelle depuis OnDraw(). `instances` est
	/// copié (voir InstancedDrawCall) : pas de contrainte de durée de vie sur
	/// le span passé.
	void DrawInstancedMesh(Mesh &mesh, std::span<const math::FMatrix4> instances, const Material &material);

	/// Dessine une géométrie skinnée avec ses matrices de skinning courantes
	/// (bone.WorldMatrix() * inverseBindMatrix par os, voir SkinnedMesh::OnDraw,
	/// skinned_mesh.hpp) — GPU skinning réel (storage buffer, voir
	/// shader_chunks::BONE_MATRIX_STORAGE_BUFFER), pas d'ombres/IBL cette
	/// phase (même scope cut que DrawInstancedMesh, voir le plan).
	void DrawSkinnedMesh(SkinnedGeometry &geometry, std::span<const math::FMatrix4> skinMatrices,
						const Material &material);

	/// Parcourt la hiérarchie d'un Object3D (sous-arbres invisibles sautés,
	/// voir Object3D::Traverse) et appelle OnDraw() sur chaque nœud — Object3D/
	/// Group purs ne dessinent rien (OnDraw() par défaut est un no-op), Shape
	/// émet un DrawMesh/DrawMeshGroup par groupe de matériau.
	void DrawObject(Object3D &root);

	/// Rend `root` (M21, voir offscreen.hpp) dans la paire couleur+profondeur
	/// de `target` au lieu de la swapchain, via le MÊME dispatch OnDraw() par
	/// nœud que DrawObject() — commande GPU indépendante de la frame de
	/// fenêtre en cours (n'utilise ni m_commandBuffer ni la swapchain), donc
	/// utilisable que Begin()/End() soit actif ou non. Pas d'ombres/IBL
	/// (scope cut délibéré M21, même précédent que InstancedMesh/SkinnedMesh
	/// — voir le plan) : implémenté dans offscreen.hpp, qui a besoin du type
	/// complet d'OffscreenTarget (voir la déclaration avancée ci-dessus).
	///
	/// Forwarder d'une ligne (M30, Phase 9 — portal rendering, voir le plan)
	/// vers l'overload 4-arguments ci-dessous : `camera.ViewProjectionMatrix()`
	/// EST le view-projection utilisé, donc un comportement strictement
	/// identique pour tout appelant existant (aucun ne passe encore par le
	/// nouvel overload).
	[[nodiscard]] Result<bool, StringView> RenderObjectOffscreen(Object3D &root, const Camera &camera,
																  OffscreenTarget &target);

	/// Overload (M30) qui rend avec un view-projection déjà calculé plutôt
	/// que `camera.ViewProjectionMatrix()` — le rendu de portail (portal.hpp)
	/// en a besoin pour injecter une projection à plan proche oblique
	/// (Camera::ClipObliqueNearPlane) composée avec une vue "virtuelle"
	/// (caméra transformée à travers un portail) que `Camera` seul n'a aucun
	/// moyen de représenter. `camera.position` sert encore à l'UBO
	/// d'éclairage (`cameraPos`), indépendant de la projection — voir
	/// offscreen.hpp pour les deux corps (même patron extract-and-forward
	/// que sql::Database::CreateTable/InsertRow).
	[[nodiscard]] Result<bool, StringView> RenderObjectOffscreen(Object3D &root, const Camera &camera,
																  const math::FMatrix4 &viewProjection,
																  OffscreenTarget &target);

	/// Calque 2D copié TEL QUEL dans la swapchain à End(), par-dessus la
	/// scène — typiquement la barre de titre et la barre d'état d'une
	/// interface ui:: rendue en logiciel (cf. ui::CanvasWindowFrame). Seules
	/// les `regions` (en pixels de fenêtre) sont copiées, sans mélange alpha :
	/// elles doivent être opaques. `pixels` : RGBA8 (octets R, G, B, A),
	/// `width` × `height` pixels, `pitch` octets par ligne ; copiés
	/// immédiatement. Vaut pour la frame en cours : à appeler entre Begin()
	/// et End().
	void SetOverlay(const uint8_t *pixels, uint32_t width, uint32_t height, uint32_t pitch,
					std::span<const sdl3::Rect> regions);

	void End();
};

} // namespace render3d
