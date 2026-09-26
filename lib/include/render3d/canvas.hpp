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
	bool m_frameActive = false;

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
	~Canvas() {
		if (!m_ownsDevice)
			(void)m_device.Release();
	}

private:
	[[nodiscard]] Result<bool, StringView> RecreateDepthTexture() {
		sdl3::GpuTextureCreateInfo info{};
		info.type = sdl3::gpu_texture_type::TEXTURE2_D;
		info.format = m_depthFormat;
		info.usage = sdl3::gpu_texture_usage::DEPTH_STENCIL_TARGET;
		info.width = uint32_t(m_width);
		info.height = uint32_t(m_height);
		info.layer_count_or_depth = 1;
		info.num_levels = 1;
		info.sample_count = sdl3::gpu_sample_count::SAMPLE1;

		auto result = m_device.CreateTexture(info);
		if (!result)
			return Err(result.Error());
		m_depthTexture = Some(std::move(result.Value()));
		return Ok(true);
	}

	/// Crée les ressources de shadow mapping (depth textures directionnelle +
	/// 2 spots, sampler de comparaison, pipeline "depth-only") au premier
	/// appel, puis les réutilise — best-effort : un échec laisse
	/// m_shadowCasterPipeline à NONE, End() dégrade alors en scène non ombrée
	/// plutôt que de faire échouer toute la frame.
	[[nodiscard]] bool EnsureShadowResources() {
		if (m_shadowCasterPipeline.IsSome())
			return true;

		m_shadowDepthFormat = ResolveShadowDepthFormat(m_device);

		auto dir = CreateShadowDepthTexture(m_device, SHADOW_MAP_RESOLUTION, m_shadowDepthFormat);
		if (!dir)
			return false;
		m_dirShadowMap = Some(std::move(dir.Value()));

		for (auto &slot : m_spotShadowMaps) {
			auto spot = CreateShadowDepthTexture(m_device, SHADOW_MAP_RESOLUTION, m_shadowDepthFormat);
			if (!spot)
				return false;
			slot = Some(std::move(spot.Value()));
		}

		auto sampler = CreateShadowComparisonSampler(m_device);
		if (!sampler)
			return false;
		m_shadowSampler = Some(std::move(sampler.Value()));

		auto program = BuildShadowCasterShaderProgram(m_device);
		if (!program)
			return false;
		auto pipeline = CreateShadowCasterPipeline(m_device, program.Value(), m_shadowDepthFormat);
		if (!pipeline)
			return false;
		m_shadowCasterPipeline = Some(std::move(pipeline.Value()));

		auto pointCube = CreatePointShadowCubeTexture(m_device, POINT_SHADOW_MAP_RESOLUTION);
		if (!pointCube)
			return false;
		m_pointShadowMap = Some(std::move(pointCube.Value()));

		auto pointFaceDepth = CreateShadowDepthTexture(m_device, POINT_SHADOW_MAP_RESOLUTION, m_shadowDepthFormat);
		if (!pointFaceDepth)
			return false;
		m_pointShadowFaceDepth = Some(std::move(pointFaceDepth.Value()));

		auto pointSampler = CreatePointShadowSampler(m_device);
		if (!pointSampler)
			return false;
		m_pointShadowSampler = Some(std::move(pointSampler.Value()));

		auto pointProgram = BuildPointShadowCasterShaderProgram(m_device);
		if (!pointProgram)
			return false;
		auto pointPipeline = CreatePointShadowCasterPipeline(m_device, pointProgram.Value(), m_shadowDepthFormat);
		if (!pointPipeline)
			return false;
		m_pointShadowCasterPipeline = Some(std::move(pointPipeline.Value()));
		return true;
	}

	/// Crée le pipeline/sampler/vertex buffer du skybox au premier appel où
	/// un environnement est défini — best-effort comme EnsureShadowResources :
	/// un échec laisse m_skyboxPipeline à NONE, End() saute alors le skybox
	/// plutôt que de faire échouer la frame.
	[[nodiscard]] bool EnsureSkyboxResources() {
		if (m_skyboxPipeline.IsSome())
			return true;

		auto program = BuildSkyboxShaderProgram(m_device);
		if (!program)
			return false;
		auto pipeline = CreateSkyboxPipeline(m_device, program.Value(), m_colorFormat, m_depthFormat);
		if (!pipeline)
			return false;
		m_skyboxPipeline = Some(std::move(pipeline.Value()));

		std::vector<Vertex3D> cubeVerts = CubeVertices();
		m_skyboxVertexCount = uint32_t(cubeVerts.size());
		uint32_t cubeBytes = uint32_t(cubeVerts.size() * sizeof(Vertex3D));
		auto vbResult = m_device.CreateBuffer(sdl3::gpu_buffer_usage::VERTEX, cubeBytes);
		if (!vbResult)
			return false;
		sdl3::GpuBuffer vertexBuffer = std::move(vbResult.Value());
		{
			auto transferResult = m_device.CreateTransferBuffer(sdl3::gpu_transfer_buffer_usage::UPLOAD, cubeBytes);
			if (!transferResult)
				return false;
			sdl3::GpuTransferBuffer transfer = std::move(transferResult.Value());
			{
				auto mapped = transfer.Map(false);
				if (!mapped)
					return false;
				std::memcpy(mapped.GetData(), cubeVerts.data(), cubeBytes);
			}
			auto uploadCmd = m_device.AcquireCommandBuffer();
			{
				sdl3::GpuCopyPass copyPass = uploadCmd.BeginCopyPass();
				sdl3::GpuTransferBufferLocation src{transfer.Get(), 0};
				sdl3::GpuBufferRegion dst{vertexBuffer.Get(), 0, cubeBytes};
				copyPass.UploadToBuffer(src, dst, false);
			}
			if (!uploadCmd.Submit())
				return false;
		}
		m_skyboxVertexBuffer = Some(std::move(vertexBuffer));

		sdl3::GpuSamplerCreateInfo samplerInfo{};
		samplerInfo.min_filter = sdl3::gpu_filter::LINEAR;
		samplerInfo.mag_filter = sdl3::gpu_filter::LINEAR;
		samplerInfo.mipmap_mode = sdl3::gpu_sampler_mipmap_mode::LINEAR;
		samplerInfo.address_mode_u = sdl3::gpu_sampler_address_mode::CLAMP_TO_EDGE;
		samplerInfo.address_mode_v = sdl3::gpu_sampler_address_mode::CLAMP_TO_EDGE;
		samplerInfo.address_mode_w = sdl3::gpu_sampler_address_mode::CLAMP_TO_EDGE;
		auto samplerResult = m_device.CreateSampler(samplerInfo);
		if (!samplerResult)
			return false;
		m_skyboxSampler = Some(std::move(samplerResult.Value()));
		return true;
	}

	/// Samplers IBL (M14) — un pour les 2 cubemaps (irradiance/préfiltré,
	/// LINEAR+mipmaps pour le préfiltré), un pour la LUT 2D (pas besoin de
	/// mipmaps ni de wrap : coordonnées toujours dans [0,1]).
	[[nodiscard]] bool EnsureIblSamplers() {
		if (m_iblCubeSampler.IsSome())
			return true;

		sdl3::GpuSamplerCreateInfo cubeSamplerInfo{};
		cubeSamplerInfo.min_filter = sdl3::gpu_filter::LINEAR;
		cubeSamplerInfo.mag_filter = sdl3::gpu_filter::LINEAR;
		cubeSamplerInfo.mipmap_mode = sdl3::gpu_sampler_mipmap_mode::LINEAR;
		cubeSamplerInfo.address_mode_u = sdl3::gpu_sampler_address_mode::CLAMP_TO_EDGE;
		cubeSamplerInfo.address_mode_v = sdl3::gpu_sampler_address_mode::CLAMP_TO_EDGE;
		cubeSamplerInfo.address_mode_w = sdl3::gpu_sampler_address_mode::CLAMP_TO_EDGE;
		auto cubeSamplerResult = m_device.CreateSampler(cubeSamplerInfo);
		if (!cubeSamplerResult)
			return false;
		m_iblCubeSampler = Some(std::move(cubeSamplerResult.Value()));

		sdl3::GpuSamplerCreateInfo lutSamplerInfo{};
		lutSamplerInfo.min_filter = sdl3::gpu_filter::LINEAR;
		lutSamplerInfo.mag_filter = sdl3::gpu_filter::LINEAR;
		lutSamplerInfo.mipmap_mode = sdl3::gpu_sampler_mipmap_mode::NEAREST;
		lutSamplerInfo.address_mode_u = sdl3::gpu_sampler_address_mode::CLAMP_TO_EDGE;
		lutSamplerInfo.address_mode_v = sdl3::gpu_sampler_address_mode::CLAMP_TO_EDGE;
		lutSamplerInfo.address_mode_w = sdl3::gpu_sampler_address_mode::CLAMP_TO_EDGE;
		auto lutSamplerResult = m_device.CreateSampler(lutSamplerInfo);
		if (!lutSamplerResult)
			return false;
		m_iblLutSampler = Some(std::move(lutSamplerResult.Value()));
		return true;
	}

	[[nodiscard]] Option<Ref<sdl3::GpuGraphicsPipeline>> GetOrCreatePipeline(const PipelineKey &key) {
		auto it = m_pipelineCache.find(key);
		if (it != m_pipelineCache.end())
			return Some(MakeRef(it->second));

		// Avec au moins une lumière ponctuelle/spot active, le Phong précompilé
		// de Canvas (directionnelle+ambiante seulement) ne suffit plus ; PBR
		// n'a de toute façon pas de shader précompilé (jamais construit par
		// Canvas::Create()) : dans les deux cas, un shader est généré à la
		// volée via ShaderBuilder pour cette combinaison exacte (mis en cache
		// par PipelineKey comme le reste — pas de recompilation par frame). Le
		// ShaderProgram généré n'a besoin de survivre qu'à la création du
		// pipeline (SDL_GPU copie ce dont il a besoin en interne).
		bool needsBuiltShader = key.shader == ShaderKind::PBR || key.pointLightCount > 0 || key.spotLightCount > 0 ||
								key.hasShadows || key.hasEnvironment || key.instanced || key.skinned;
		ShaderProgram builtProgram;
		if (needsBuiltShader) {
			LightingModel model = key.shader == ShaderKind::PBR       ? LightingModel::PBR
								  : key.shader == ShaderKind::PHONG ? LightingModel::PHONG
																	: LightingModel::UNLIT;
			auto built = ShaderBuilder()
							.Lit(model)
							.PointLights(key.pointLightCount)
							.SpotLights(key.spotLightCount)
							.Shadow(key.hasShadows ? 1 : 0)
							.Environment(key.hasEnvironment)
							.Instanced(key.instanced)
							.Skinned(key.skinned)
							.Build(m_device);
			if (!built)
				return NONE;
			builtProgram = std::move(built.Value());
		}
		ShaderProgram &program =
			needsBuiltShader ? builtProgram : (key.shader == ShaderKind::PHONG ? m_phongShader : m_basicShader);

		sdl3::GpuVertexBufferDescription vertexBufferDescs[2] = {
			{0, uint32_t(sizeof(Vertex3D)), sdl3::gpu_vertex_input_rate::VERTEX, 0},
			{1, uint32_t(sizeof(math::FMatrix4)), sdl3::gpu_vertex_input_rate::INSTANCE, 0},
		};
		sdl3::GpuVertexAttribute vertexAttributes[8] = {
			{0, 0, sdl3::gpu_vertex_element_format::FLOAT3, uint32_t(offsetof(Vertex3D, position))},
			{1, 0, sdl3::gpu_vertex_element_format::FLOAT3, uint32_t(offsetof(Vertex3D, normal))},
			{2, 0, sdl3::gpu_vertex_element_format::FLOAT2, uint32_t(offsetof(Vertex3D, uv))},
			{3, 0, sdl3::gpu_vertex_element_format::UBYTE4_NORM, uint32_t(offsetof(Vertex3D, color))},
			// Colonnes de la matrice d'instance (mat4 = 4 vec4 consécutifs en
			// GLSL, voir shader_chunks::INSTANCE_ATTRIBUTES) — buffer_slot=1,
			// seulement utilisées si key.instanced (voir num_vertex_attributes
			// ci-dessous, qui exclut ces 4 entrées sinon).
			{4, 1, sdl3::gpu_vertex_element_format::FLOAT4, 0},
			{5, 1, sdl3::gpu_vertex_element_format::FLOAT4, 16},
			{6, 1, sdl3::gpu_vertex_element_format::FLOAT4, 32},
			{7, 1, sdl3::gpu_vertex_element_format::FLOAT4, 48},
		};

		// SkinnedVertex3D (M17) — layout de sommet totalement différent
		// (jamais combiné à key.instanced, voir le plan), un seul buffer.
		sdl3::GpuVertexBufferDescription skinnedVertexBufferDesc{0, uint32_t(sizeof(SkinnedVertex3D)),
																 sdl3::gpu_vertex_input_rate::VERTEX, 0};
		sdl3::GpuVertexAttribute skinnedVertexAttributes[6] = {
			{0, 0, sdl3::gpu_vertex_element_format::FLOAT3, uint32_t(offsetof(SkinnedVertex3D, position))},
			{1, 0, sdl3::gpu_vertex_element_format::FLOAT3, uint32_t(offsetof(SkinnedVertex3D, normal))},
			{2, 0, sdl3::gpu_vertex_element_format::FLOAT2, uint32_t(offsetof(SkinnedVertex3D, uv))},
			{3, 0, sdl3::gpu_vertex_element_format::UBYTE4_NORM, uint32_t(offsetof(SkinnedVertex3D, color))},
			{4, 0, sdl3::gpu_vertex_element_format::FLOAT4, uint32_t(offsetof(SkinnedVertex3D, boneIndices))},
			{5, 0, sdl3::gpu_vertex_element_format::FLOAT4, uint32_t(offsetof(SkinnedVertex3D, boneWeights))},
		};

		sdl3::GpuGraphicsPipelineCreateInfo info{};
		info.vertex_shader = program.Vertex().Get();
		info.fragment_shader = program.Fragment().Get();
		if (key.skinned) {
			info.vertex_input_state.vertex_buffer_descriptions = &skinnedVertexBufferDesc;
			info.vertex_input_state.num_vertex_buffers = 1;
			info.vertex_input_state.vertex_attributes = skinnedVertexAttributes;
			info.vertex_input_state.num_vertex_attributes = 6;
		} else {
			info.vertex_input_state.vertex_buffer_descriptions = vertexBufferDescs;
			info.vertex_input_state.num_vertex_buffers = key.instanced ? 2 : 1;
			info.vertex_input_state.vertex_attributes = vertexAttributes;
			info.vertex_input_state.num_vertex_attributes = key.instanced ? 8 : 4;
		}
		info.primitive_type = key.topology == PrimitiveTopology::LINES    ? sdl3::gpu_primitive_type::LINE_LIST
							  : key.topology == PrimitiveTopology::POINTS ? sdl3::gpu_primitive_type::POINT_LIST
																		  : sdl3::gpu_primitive_type::TRIANGLE_LIST;

		info.rasterizer_state.fill_mode = key.wireframe ? sdl3::gpu_fill_mode::LINE : sdl3::gpu_fill_mode::FILL;
		info.rasterizer_state.cull_mode = key.doubleSided ? sdl3::gpu_cull_mode::NONE : sdl3::gpu_cull_mode::BACK;
		// COUNTER_CLOCKWISE : c'est la convention dans laquelle les maillages de
		// ce module sont écrits (`Mesh::Cube()`/`Sphere()`/`Plane()` — sens
		// anti-horaire vu de l'EXTÉRIEUR).
		//
		// Ce réglage était à CLOCKWISE, et son commentaire disait pourquoi :
		// `FMatrix4::Perspective()` retournait Y, ce qui inverse le sens de
		// rotation apparent des triangles à l'écran. Ce retournement était
		// lui-même un doublon de celui que SDL_GPU applique déjà (cf.
		// math.hpp) ; en le supprimant, le sens d'enroulement redevient
		// direct, et laisser CLOCKWISE revenait alors à éliminer les faces
		// AVANT en gardant les faces arrière — on voyait l'intérieur des
		// objets (« la face du dessous au lieu de celle du dessus »). Les deux
		// réglages doivent changer ENSEMBLE : ils décrivent le même sens.
		info.rasterizer_state.front_face = sdl3::gpu_front_face::COUNTER_CLOCKWISE;
		info.rasterizer_state.enable_depth_clip = true;

		info.multisample_state.sample_count = sdl3::gpu_sample_count::SAMPLE1;

		// `depthTest` faux : dessin en surimpression (manipulateur d'éditeur,
		// axes de débogage) — ni test ni écriture, sinon les parties arrière
		// de l'objet posé par-dessus masqueraient ses parties avant.
		info.depth_stencil_state.enable_depth_test = key.depthTest;
		info.depth_stencil_state.enable_depth_write = key.depthTest;
		info.depth_stencil_state.compare_op = sdl3::gpu_compare_op::LESS_OR_EQUAL;

		sdl3::GpuColorTargetDescription colorTargetDesc{};
		colorTargetDesc.format = key.colorFormat;
		info.target_info.color_target_descriptions = &colorTargetDesc;
		info.target_info.num_color_targets = 1;
		info.target_info.has_depth_stencil_target = true;
		info.target_info.depth_stencil_format = key.depthFormat;

		auto pipelineResult = m_device.CreateGraphicsPipeline(info);
		if (!pipelineResult)
			return NONE;

		auto [inserted, ok] = m_pipelineCache.emplace(key, std::move(pipelineResult.Value()));
		(void)ok;
		return Some(MakeRef(inserted->second));
	}

public:
	[[nodiscard]] static Result<Canvas, Error> Create(sdl3::Window &window, int w = 0, int h = 0) {
		auto deviceResult = sdl3::GpuDevice::Create(sdl3::gpu_shader_format::SPIR_V | sdl3::gpu_shader_format::MSL |
													 sdl3::gpu_shader_format::DXIL);
		if (!deviceResult)
			return Err(deviceResult.Error());
		sdl3::GpuDevice device = std::move(deviceResult.Value());

		if (!device.ClaimWindow(MakeRef(window)))
			return Err(sdl3::GetError());

		sdl3::GpuTextureFormat colorFormat = device.SwapchainTextureFormat(MakeRef(window));

		sdl3::GpuTextureFormat depthFormat = SDL_GPU_TEXTUREFORMAT_D32_FLOAT;
		if (!device.TextureSupportsFormat(depthFormat, sdl3::gpu_texture_type::TEXTURE2_D,
										  sdl3::gpu_texture_usage::DEPTH_STENCIL_TARGET)) {
			depthFormat = SDL_GPU_TEXTUREFORMAT_D24_UNORM;
			if (!device.TextureSupportsFormat(depthFormat, sdl3::gpu_texture_type::TEXTURE2_D,
											  sdl3::gpu_texture_usage::DEPTH_STENCIL_TARGET))
				depthFormat = SDL_GPU_TEXTUREFORMAT_D16_UNORM;
		}

		// Texture blanche 1x1 : liée par défaut sur le sampler du shader Phong
		// quand un Material n'a pas d'albedo (le sampler doit toujours être lié).
		sdl3::GpuTextureCreateInfo whiteInfo{};
		whiteInfo.type = sdl3::gpu_texture_type::TEXTURE2_D;
		whiteInfo.format = SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM;
		whiteInfo.usage = sdl3::gpu_texture_usage::SAMPLER;
		whiteInfo.width = 1;
		whiteInfo.height = 1;
		whiteInfo.layer_count_or_depth = 1;
		whiteInfo.num_levels = 1;
		whiteInfo.sample_count = sdl3::gpu_sample_count::SAMPLE1;
		auto whiteTextureResult = device.CreateTexture(whiteInfo);
		if (!whiteTextureResult)
			return Err(whiteTextureResult.Error());

		uint8_t whitePixel[4] = {255, 255, 255, 255};
		auto transferResult = device.CreateTransferBuffer(sdl3::gpu_transfer_buffer_usage::UPLOAD, sizeof(whitePixel));
		if (!transferResult)
			return Err(transferResult.Error());
		sdl3::GpuTransferBuffer transfer = std::move(transferResult.Value());
		{
			auto mapped = transfer.Map(false);
			if (!mapped)
				return Err(StringView("Canvas::Create: failed to map the default texture's transfer buffer"));
			std::memcpy(mapped.GetData(), whitePixel, sizeof(whitePixel));
		}

		auto cmd = device.AcquireCommandBuffer();
		{
			sdl3::GpuCopyPass copyPass = cmd.BeginCopyPass();
			sdl3::GpuTextureTransferInfo src{transfer.Get(), 0, 1, 1};
			sdl3::GpuTextureRegion dst{whiteTextureResult.Value().Get(), 0, 0, 0, 0, 0, 1, 1, 1};
			copyPass.UploadToTexture(src, dst, false);
		}
		if (!cmd.Submit())
			return Err(sdl3::GetError());

		sdl3::GpuSamplerCreateInfo samplerInfo{};
		samplerInfo.min_filter = sdl3::gpu_filter::LINEAR;
		samplerInfo.mag_filter = sdl3::gpu_filter::LINEAR;
		samplerInfo.mipmap_mode = sdl3::gpu_sampler_mipmap_mode::LINEAR;
		samplerInfo.address_mode_u = sdl3::gpu_sampler_address_mode::REPEAT;
		samplerInfo.address_mode_v = sdl3::gpu_sampler_address_mode::REPEAT;
		samplerInfo.address_mode_w = sdl3::gpu_sampler_address_mode::REPEAT;
		auto samplerResult = device.CreateSampler(samplerInfo);
		if (!samplerResult)
			return Err(samplerResult.Error());

		auto basicResult = ShaderProgram::Load(device, String("assets/shaders/bin/mesh_basic"),
											   ShaderProgram::StageInfo{0, 2}, ShaderProgram::StageInfo{0, 1});
		if (!basicResult)
			return Err(basicResult.Error());

		auto phongResult = ShaderProgram::Load(device, String("assets/shaders/bin/mesh_phong"),
											   ShaderProgram::StageInfo{0, 2}, ShaderProgram::StageInfo{1, 2});
		if (!phongResult)
			return Err(phongResult.Error());

		if (w == 0) w = window.GetWidth();
		if (h == 0) h = window.GetHeight();
		Canvas canvas(std::move(device), Some(MakeRef(window)), /*ownsDevice=*/true, w, h, colorFormat, depthFormat,
					  std::move(whiteTextureResult.Value()), std::move(samplerResult.Value()),
					  std::move(basicResult.Value()), std::move(phongResult.Value()));

		auto depthResult = canvas.RecreateDepthTexture();
		if (!depthResult)
			return Err(depthResult.Error());

		return Ok(std::move(canvas));
	}

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
	[[nodiscard]] static Result<Canvas, Error> Create(sdl3::Renderer &renderer, int w = 0, int h = 0) {
		auto deviceResult = renderer.GetGPUDevice();
		if (!deviceResult)
			return Err(deviceResult.Error());
		sdl3::GpuDevice device = std::move(deviceResult.Value());

		// Pas de swapchain à sonder (pas de Window ici) : format couleur par
		// défaut cohérent avec celui déjà utilisé par les OffscreenTarget de
		// ui::Viewport3D (voir ui::VIEWPORT3D_COLOR_FORMAT, viewport3d.hpp) —
		// cette valeur n'est de toute façon jamais lue par
		// RenderObjectToTexture, seulement stockée pour cohérence de l'API.
		sdl3::GpuTextureFormat colorFormat = SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM;

		sdl3::GpuTextureFormat depthFormat = SDL_GPU_TEXTUREFORMAT_D32_FLOAT;
		if (!device.TextureSupportsFormat(depthFormat, sdl3::gpu_texture_type::TEXTURE2_D,
										  sdl3::gpu_texture_usage::DEPTH_STENCIL_TARGET)) {
			depthFormat = SDL_GPU_TEXTUREFORMAT_D24_UNORM;
			if (!device.TextureSupportsFormat(depthFormat, sdl3::gpu_texture_type::TEXTURE2_D,
											  sdl3::gpu_texture_usage::DEPTH_STENCIL_TARGET))
				depthFormat = SDL_GPU_TEXTUREFORMAT_D16_UNORM;
		}

		// Texture blanche 1x1 + sampler par défaut — identique à Create(Window&, ...).
		sdl3::GpuTextureCreateInfo whiteInfo{};
		whiteInfo.type = sdl3::gpu_texture_type::TEXTURE2_D;
		whiteInfo.format = SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM;
		whiteInfo.usage = sdl3::gpu_texture_usage::SAMPLER;
		whiteInfo.width = 1;
		whiteInfo.height = 1;
		whiteInfo.layer_count_or_depth = 1;
		whiteInfo.num_levels = 1;
		whiteInfo.sample_count = sdl3::gpu_sample_count::SAMPLE1;
		auto whiteTextureResult = device.CreateTexture(whiteInfo);
		if (!whiteTextureResult)
			return Err(whiteTextureResult.Error());

		uint8_t whitePixel[4] = {255, 255, 255, 255};
		auto transferResult = device.CreateTransferBuffer(sdl3::gpu_transfer_buffer_usage::UPLOAD, sizeof(whitePixel));
		if (!transferResult)
			return Err(transferResult.Error());
		sdl3::GpuTransferBuffer transfer = std::move(transferResult.Value());
		{
			auto mapped = transfer.Map(false);
			if (!mapped)
				return Err(StringView("Canvas::Create: failed to map the default texture's transfer buffer"));
			std::memcpy(mapped.GetData(), whitePixel, sizeof(whitePixel));
		}

		auto cmd = device.AcquireCommandBuffer();
		{
			sdl3::GpuCopyPass copyPass = cmd.BeginCopyPass();
			sdl3::GpuTextureTransferInfo src{transfer.Get(), 0, 1, 1};
			sdl3::GpuTextureRegion dst{whiteTextureResult.Value().Get(), 0, 0, 0, 0, 0, 1, 1, 1};
			copyPass.UploadToTexture(src, dst, false);
		}
		if (!cmd.Submit())
			return Err(sdl3::GetError());

		sdl3::GpuSamplerCreateInfo samplerInfo{};
		samplerInfo.min_filter = sdl3::gpu_filter::LINEAR;
		samplerInfo.mag_filter = sdl3::gpu_filter::LINEAR;
		samplerInfo.mipmap_mode = sdl3::gpu_sampler_mipmap_mode::LINEAR;
		samplerInfo.address_mode_u = sdl3::gpu_sampler_address_mode::REPEAT;
		samplerInfo.address_mode_v = sdl3::gpu_sampler_address_mode::REPEAT;
		samplerInfo.address_mode_w = sdl3::gpu_sampler_address_mode::REPEAT;
		auto samplerResult = device.CreateSampler(samplerInfo);
		if (!samplerResult)
			return Err(samplerResult.Error());

		auto basicResult = ShaderProgram::Load(device, String("assets/shaders/bin/mesh_basic"),
											   ShaderProgram::StageInfo{0, 2}, ShaderProgram::StageInfo{0, 1});
		if (!basicResult)
			return Err(basicResult.Error());

		auto phongResult = ShaderProgram::Load(device, String("assets/shaders/bin/mesh_phong"),
											   ShaderProgram::StageInfo{0, 2}, ShaderProgram::StageInfo{1, 2});
		if (!phongResult)
			return Err(phongResult.Error());

		if (w == 0) w = renderer.GetOutputWidth();
		if (h == 0) h = renderer.GetOutputHeight();
		// NONE (pas de Window), ownsDevice=false (le Renderer reste
		// propriétaire du device, cf. ~Canvas()) — pas de RecreateDepthTexture
		// ici : cette depth texture n'est jamais utilisée par le chemin
		// offscreen (cf. doc de cette méthode ci-dessus).
		Canvas canvas(std::move(device), NONE, /*ownsDevice=*/false, w, h, colorFormat, depthFormat,
					  std::move(whiteTextureResult.Value()), std::move(samplerResult.Value()),
					  std::move(basicResult.Value()), std::move(phongResult.Value()));
		return Ok(std::move(canvas));
	}

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
	void SetFrustumFromViewProjection(const math::FMatrix4 &viewProjection) noexcept {
		m_frustum = math::FFrustum::FromViewProj(viewProjection);
		m_frustumValid = true;
		m_culledDraws = 0;
		m_submittedDraws = 0;
	}
	void SetLighting(const DirectionalLight &directional, const AmbientLight &ambient) noexcept {
		m_directionalLight = directional;
		m_ambientLight = ambient;
	}

	/// Lumières ponctuelles/spot actives pour les prochaines frames — scène
	/// entière (comme SetLighting), pas par Material. Passer des spans vides
	/// revient au Phong précompilé (directionnelle+ambiante seulement).
	void SetLights(std::span<const PointLight> pointLights, std::span<const SpotLight> spotLights) {
		m_pointLights.assign(pointLights.begin(), pointLights.end());
		m_spotLights.assign(spotLights.begin(), spotLights.end());
	}

	/// Environnement (skybox, voir environment.hpp) — Canvas garde seulement
	/// une référence, `env` doit rester en vie tant que l'environnement est
	/// actif (même contrat que Material::albedo pour les textures).
	void SetEnvironment(const Environment &env) noexcept {
		m_environmentCubemap = Some(MakeRef(env.cubemap));
		m_environmentIrradiance = Some(MakeRef(env.irradianceCubemap));
		m_environmentPrefiltered = Some(MakeRef(env.prefilteredCubemap));
		m_environmentBrdfLut = Some(MakeRef(env.brdfLut));
	}
	void ClearEnvironment() noexcept {
		m_environmentCubemap = NONE;
		m_environmentIrradiance = NONE;
		m_environmentPrefiltered = NONE;
		m_environmentBrdfLut = NONE;
	}

	[[nodiscard]] ShaderProgram &PhongShader() noexcept { return m_phongShader; }
	[[nodiscard]] ShaderProgram &BasicShader() noexcept { return m_basicShader; }
	[[nodiscard]] sdl3::GpuDevice &Device() noexcept { return m_device; }

	/// Acquiert la swapchain et ouvre une nouvelle frame. Retourne false (rien
	/// à dessiner) si la fenêtre est minimisée — cas normal, pas une erreur ;
	/// GetError() reste disponible pour distinguer une vraie erreur GPU.
	[[nodiscard]] bool Begin() {
		if (m_frameActive)
			return false;
		// Canvas construit via Create(sdl3::Renderer&, ...) : offscreen
		// exclusivement, aucune Window associée (cf. m_window) — jamais de
		// frame de swapchain possible sur ce Canvas.
		if (m_window.IsNone())
			return false;

		sdl3::Point size = m_window.Value()->GetSize();
		if (size.x != m_width || size.y != m_height) {
			m_width = size.x;
			m_height = size.y;
			auto resized = RecreateDepthTexture();
			(void)resized; // best-effort : en cas d'échec on garde l'ancienne depth texture
		}
		if (m_width <= 0 || m_height <= 0)
			return false;

		auto cmd = m_device.AcquireCommandBuffer();
		auto swapchain = cmd.WaitAndAcquireSwapchainTexture(m_window.Value());
		if (!swapchain)
			return false; // cmd annulé automatiquement (RAII) à la sortie de portée

		m_swapchainTexture = swapchain.Value();
		m_commandBuffer = Some(std::move(cmd));
		// Frustum de CETTE image : `m_camera` peut avoir changé depuis la
		// précédente, et le culling doit tester contre le point de vue
		// réellement utilisé pour dessiner (cf. viewProjection dans End()).
		SetFrustumFromViewProjection(m_camera.ViewProjectionMatrix());
		m_drawList.clear();
		m_instancedDrawList.clear();
		m_skinnedDrawList.clear();
		m_frameActive = true;
		return true;
	}

	/// Vrai si ce maillage, placé par `transform`, peut être visible.
	///
	/// Conservateur à dessein : un maillage vide (boîte invalide) ou un
	/// frustum non initialisé laisse TOUT passer. Écarter à tort est un bug
	/// visible (objet manquant), garder à tort ne coûte qu'un dessin.
	/// `DrawMeshGroup` teste la boîte du maillage ENTIER alors qu'il n'en
	/// dessine qu'une plage : plus large que nécessaire, donc toujours du bon
	/// côté.
	[[nodiscard]] bool IsPotentiallyVisible(const Mesh &mesh, const math::FMatrix4 &transform) const noexcept {
		if (!m_frustumCulling || !m_frustumValid)
			return true;
		const math::FAABB &local = mesh.LocalBounds();
		if (!local.IsValid())
			return true;
		return m_frustum.Intersects(local.Transformed(transform));
	}

	void DrawMesh(Mesh &mesh, const math::FMatrix4 &transform, const Material &material,
				 PrimitiveTopology topology = PrimitiveTopology::TRIANGLES) {
		if (!m_frameActive)
			return;
		if (!IsPotentiallyVisible(mesh, transform)) {
			++m_culledDraws;
			return;
		}
		++m_submittedDraws;
		m_drawList.push_back({MakeRefMut(mesh), transform, material, 0, 0, topology});
	}

	void DrawMesh(RefMut<Mesh> mesh, const math::FMatrix4 &transform, const Material &material,
				 PrimitiveTopology topology = PrimitiveTopology::TRIANGLES) {
		if (!m_frameActive)
			return;
		if (!IsPotentiallyVisible(*mesh, transform)) {
			++m_culledDraws;
			return;
		}
		++m_submittedDraws;
		m_drawList.push_back({mesh, transform, material, 0, 0, topology});
	}

	/// Dessine seulement la plage d'indices [firstIndex, firstIndex+indexCount)
	/// du maillage — utilisé pour les Mesh::Group (matériau par face), voir
	/// Shape::OnDraw().
	void DrawMeshGroup(Mesh &mesh, uint32_t firstIndex, uint32_t indexCount, const math::FMatrix4 &transform,
					   const Material &material, PrimitiveTopology topology = PrimitiveTopology::TRIANGLES) {
		if (!m_frameActive)
			return;
		if (!IsPotentiallyVisible(mesh, transform)) {
			++m_culledDraws;
			return;
		}
		++m_submittedDraws;
		m_drawList.push_back({MakeRefMut(mesh), transform, material, firstIndex, indexCount, topology});
	}

	/// Dessine `mesh` une fois par transform dans `instances` (rendu instancié
	/// GPU réel — un seul draw call, voir shader_chunks::INSTANCE_ATTRIBUTES) —
	/// InstancedMesh (object3d.hpp) l'appelle depuis OnDraw(). `instances` est
	/// copié (voir InstancedDrawCall) : pas de contrainte de durée de vie sur
	/// le span passé.
	void DrawInstancedMesh(Mesh &mesh, std::span<const math::FMatrix4> instances, const Material &material) {
		if (!m_frameActive || instances.empty())
			return;
		m_instancedDrawList.push_back({MakeRefMut(mesh), material, std::vector(instances.begin(), instances.end())});
	}

	/// Dessine une géométrie skinnée avec ses matrices de skinning courantes
	/// (bone.WorldMatrix() * inverseBindMatrix par os, voir SkinnedMesh::OnDraw,
	/// skinned_mesh.hpp) — GPU skinning réel (storage buffer, voir
	/// shader_chunks::BONE_MATRIX_STORAGE_BUFFER), pas d'ombres/IBL cette
	/// phase (même scope cut que DrawInstancedMesh, voir le plan).
	void DrawSkinnedMesh(SkinnedGeometry &geometry, std::span<const math::FMatrix4> skinMatrices,
						const Material &material) {
		if (!m_frameActive || skinMatrices.empty())
			return;
		m_skinnedDrawList.push_back(
			{MakeRefMut(geometry), material, std::vector(skinMatrices.begin(), skinMatrices.end())});
	}

	/// Parcourt la hiérarchie d'un Object3D (sous-arbres invisibles sautés,
	/// voir Object3D::Traverse) et appelle OnDraw() sur chaque nœud — Object3D/
	/// Group purs ne dessinent rien (OnDraw() par défaut est un no-op), Shape
	/// émet un DrawMesh/DrawMeshGroup par groupe de matériau.
	void DrawObject(Object3D &root) {
		root.Traverse([this](Object3D &node) { node.OnDraw(*this); });
	}

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

	void End() {
		if (!m_frameActive)
			return;

		sdl3::GpuCommandBuffer &cmd = m_commandBuffer.Value();

		// Tous les uploads doivent se faire AVANT le render pass : un GpuCopyPass
		// et un GpuRenderPass sont mutuellement exclusifs sur un même command buffer.
		for (auto &draw : m_drawList) {
			auto uploaded = draw.mesh->EnsureGpuBuffers(m_device, cmd);
			(void)uploaded; // best-effort : un maillage non uploadé est simplement sauté ci-dessous
		}
		for (auto &draw : m_instancedDrawList) {
			auto uploaded = draw.mesh->EnsureGpuBuffers(m_device, cmd);
			(void)uploaded;
		}
		for (auto &draw : m_skinnedDrawList) {
			auto uploaded = draw.geometry->EnsureGpuBuffers(m_device, cmd);
			(void)uploaded;
		}

		// Buffers de transforms d'instance — transients (recréés chaque frame,
		// voir InstancedDrawCall), doivent rester en vie jusqu'à cmd.Submit()
		// (le render pass les référence par pointeur GPU brut) : gardés ici,
		// dans la portée de End(), pas dans Canvas lui-même.
		std::vector<Option<sdl3::GpuBuffer>> instanceBuffers;
		instanceBuffers.reserve(m_instancedDrawList.size());
		for (auto &draw : m_instancedDrawList) {
			uint32_t bytes = uint32_t(draw.instances.size() * sizeof(math::FMatrix4));
			auto bufferResult = m_device.CreateBuffer(sdl3::gpu_buffer_usage::VERTEX, bytes);
			if (!bufferResult) {
				instanceBuffers.push_back(NONE); // sautée ci-dessous (best-effort)
				continue;
			}
			sdl3::GpuBuffer buffer = std::move(bufferResult.Value());
			auto transferResult = m_device.CreateTransferBuffer(sdl3::gpu_transfer_buffer_usage::UPLOAD, bytes);
			if (!transferResult) {
				instanceBuffers.push_back(NONE);
				continue;
			}
			sdl3::GpuTransferBuffer transfer = std::move(transferResult.Value());
			{
				auto mapped = transfer.Map(false);
				if (!mapped) {
					instanceBuffers.push_back(NONE);
					continue;
				}
				std::memcpy(mapped.GetData(), draw.instances.data(), bytes);
			}
			{
				sdl3::GpuCopyPass copyPass = cmd.BeginCopyPass();
				sdl3::GpuTransferBufferLocation src{transfer.Get(), 0};
				sdl3::GpuBufferRegion dst{buffer.Get(), 0, bytes};
				copyPass.UploadToBuffer(src, dst, false);
			}
			instanceBuffers.push_back(Some(std::move(buffer)));
		}

		// Storage buffers de matrices de skinning — même politique transient
		// que instanceBuffers ci-dessus, usage GRAPHICS_STORAGE_READ (pas
		// VERTEX : lu depuis le vertex shader via un SSBO, voir
		// shader_chunks::BONE_MATRIX_STORAGE_BUFFER, pas un vertex buffer).
		std::vector<Option<sdl3::GpuBuffer>> boneBuffers;
		boneBuffers.reserve(m_skinnedDrawList.size());
		for (auto &draw : m_skinnedDrawList) {
			uint32_t bytes = uint32_t(draw.skinMatrices.size() * sizeof(math::FMatrix4));
			auto bufferResult = m_device.CreateBuffer(sdl3::gpu_buffer_usage::GRAPHICS_STORAGE_READ, bytes);
			if (!bufferResult) {
				boneBuffers.push_back(NONE);
				continue;
			}
			sdl3::GpuBuffer buffer = std::move(bufferResult.Value());
			auto transferResult = m_device.CreateTransferBuffer(sdl3::gpu_transfer_buffer_usage::UPLOAD, bytes);
			if (!transferResult) {
				boneBuffers.push_back(NONE);
				continue;
			}
			sdl3::GpuTransferBuffer transfer = std::move(transferResult.Value());
			{
				auto mapped = transfer.Map(false);
				if (!mapped) {
					boneBuffers.push_back(NONE);
					continue;
				}
				std::memcpy(mapped.GetData(), draw.skinMatrices.data(), bytes);
			}
			{
				sdl3::GpuCopyPass copyPass = cmd.BeginCopyPass();
				sdl3::GpuTransferBufferLocation src{transfer.Get(), 0};
				sdl3::GpuBufferRegion dst{buffer.Get(), 0, bytes};
				copyPass.UploadToBuffer(src, dst, false);
			}
			boneBuffers.push_back(Some(std::move(buffer)));
		}

		// Lumières spot projetant une ombre, dans l'ordre de m_spotLights,
		// plafonné à 2 (voir shader_chunks::SHADOW_MATRIX_UBO/MAX_SPOT_SHADOWS
		// implicite côté shader) — les spots au-delà du 2e restent sans ombre.
		std::vector<size_t> shadowSpotIndices;
		for (size_t i = 0; i < m_spotLights.size() && shadowSpotIndices.size() < 2; ++i)
			if (m_spotLights[i].castShadow)
				shadowSpotIndices.push_back(i);
		// Un seul point light peut projeter une ombre (voir le plan) — le
		// premier rencontré avec castShadow=true, les autres restent sans ombre.
		int pointShadowLightIndex = -1;
		for (size_t i = 0; i < m_pointLights.size(); ++i)
			if (m_pointLights[i].castShadow) {
				pointShadowLightIndex = int(i);
				break;
			}

		bool anyShadowCaster = m_directionalLight.castShadow || !shadowSpotIndices.empty() || pointShadowLightIndex >= 0;
		if (anyShadowCaster && !EnsureShadowResources())
			anyShadowCaster = false; // dégradation best-effort : scène non ombrée plutôt qu'une frame perdue

		// Même dégradation best-effort que les ombres ci-dessus : un échec de
		// création des samplers IBL désactive l'IBL pour cette frame plutôt
		// que de la faire échouer (le pipeline PBR généré n'attendrait alors
		// aucun sampler IBL, cohérent avec anyEnvironment=false ci-dessous).
		bool anyEnvironment = m_environmentCubemap.IsSome();
		if (anyEnvironment && !EnsureIblSamplers())
			anyEnvironment = false;

		if (anyShadowCaster) {
			auto renderShadowPass = [&](sdl3::GpuTexture &target, const math::FMatrix4 &lightViewProjection) {
				sdl3::GpuDepthStencilTargetInfo shadowDepthTarget{};
				shadowDepthTarget.texture = target.Get();
				shadowDepthTarget.clear_depth = 1.f;
				shadowDepthTarget.load_op = sdl3::gpu_load_op::CLEAR;
				shadowDepthTarget.store_op = sdl3::gpu_store_op::STORE;
				shadowDepthTarget.stencil_load_op = sdl3::gpu_load_op::DONT_CARE;
				shadowDepthTarget.stencil_store_op = sdl3::gpu_store_op::DONT_CARE;

				sdl3::GpuRenderPass shadowPass =
					cmd.BeginRenderPass(std::span<const sdl3::GpuColorTargetInfo>{}, &shadowDepthTarget);
				sdl3::GpuViewport shadowViewport{0.f, 0.f, float(SHADOW_MAP_RESOLUTION), float(SHADOW_MAP_RESOLUTION),
												 0.f, 1.f};
				shadowPass.SetViewport(shadowViewport);
				shadowPass.BindPipeline(m_shadowCasterPipeline.Value());

				for (auto &draw : m_drawList) {
					if (!draw.mesh->HasGpuBuffers())
						continue;
					sdl3::GpuBufferBinding vertexBinding{draw.mesh->VertexBuffer()->Get(), 0};
					shadowPass.BindVertexBuffer(0, vertexBinding);
					sdl3::GpuBufferBinding indexBinding{draw.mesh->IndexBuffer()->Get(), 0};
					shadowPass.BindIndexBuffer(indexBinding, sdl3::gpu_index_element_size::BITS32);

					cmd.PushVertexUniformData(0, lightViewProjection);
					struct PerObjectUBO {
						math::FMatrix4 model;
						math::FMatrix4 normalMatrix;
					};
					// normalMatrix inutilisé par le shader depth-only (position seule)
					// mais PerObjectUBO doit garder la même disposition que le pipeline
					// principal (même binding=1, set=1, voir shader_chunks::PER_OBJECT_UBO).
					PerObjectUBO perObject{draw.transform, draw.transform};
					cmd.PushVertexUniformData(1, perObject);

					uint32_t indexCount = draw.indexCount == 0 ? draw.mesh->IndexCount() : draw.indexCount;
					shadowPass.DrawIndexedPrimitives(indexCount, 1, draw.firstIndex);
				}
			};

			if (m_directionalLight.castShadow)
				renderShadowPass(m_dirShadowMap.Value(), m_directionalLight.ShadowViewProjection());
			for (size_t slot = 0; slot < shadowSpotIndices.size(); ++slot)
				renderShadowPass(m_spotShadowMaps[slot].Value(), m_spotLights[shadowSpotIndices[slot]].ShadowViewProjection());

			if (pointShadowLightIndex >= 0) {
				const PointLight &shadowPoint = m_pointLights[size_t(pointShadowLightIndex)];
				struct PointShadowCasterUBO {
					float lightPos[3];
					float padding0 = 0.f;
				};
				PointShadowCasterUBO pointUbo{
					{shadowPoint.position.x, shadowPoint.position.y, shadowPoint.position.z}, 0.f};

				for (int face = 0; face < 6; ++face) {
					sdl3::GpuColorTargetInfo faceColorTarget{};
					faceColorTarget.texture = m_pointShadowMap.Value().Get();
					faceColorTarget.layer_or_depth_plane = uint32_t(face);
					faceColorTarget.clear_color = SDL_FColor{1e6f, 1e6f, 1e6f, 1.f}; // "aucun caster" = distance énorme
					faceColorTarget.load_op = sdl3::gpu_load_op::CLEAR;
					faceColorTarget.store_op = sdl3::gpu_store_op::STORE;

					sdl3::GpuDepthStencilTargetInfo faceDepthTarget{};
					faceDepthTarget.texture = m_pointShadowFaceDepth.Value().Get();
					faceDepthTarget.clear_depth = 1.f;
					faceDepthTarget.load_op = sdl3::gpu_load_op::CLEAR;
					faceDepthTarget.store_op = sdl3::gpu_store_op::DONT_CARE;
					faceDepthTarget.stencil_load_op = sdl3::gpu_load_op::DONT_CARE;
					faceDepthTarget.stencil_store_op = sdl3::gpu_store_op::DONT_CARE;

					sdl3::GpuRenderPass facePass = cmd.BeginRenderPass(faceColorTarget, &faceDepthTarget);
					sdl3::GpuViewport faceViewport{0.f, 0.f, float(POINT_SHADOW_MAP_RESOLUTION),
												   float(POINT_SHADOW_MAP_RESOLUTION), 0.f, 1.f};
					facePass.SetViewport(faceViewport);
					facePass.BindPipeline(m_pointShadowCasterPipeline.Value());

					math::FMatrix4 faceViewProjection = shadowPoint.ShadowViewProjection(face);

					for (auto &draw : m_drawList) {
						if (!draw.mesh->HasGpuBuffers())
							continue;
						sdl3::GpuBufferBinding vertexBinding{draw.mesh->VertexBuffer()->Get(), 0};
						facePass.BindVertexBuffer(0, vertexBinding);
						sdl3::GpuBufferBinding indexBinding{draw.mesh->IndexBuffer()->Get(), 0};
						facePass.BindIndexBuffer(indexBinding, sdl3::gpu_index_element_size::BITS32);

						cmd.PushVertexUniformData(0, faceViewProjection);
						struct PerObjectUBO {
							math::FMatrix4 model;
							math::FMatrix4 normalMatrix;
						};
						PerObjectUBO perObject{draw.transform, draw.transform};
						cmd.PushVertexUniformData(1, perObject);
						cmd.PushFragmentUniformData(0, pointUbo);

						uint32_t indexCount = draw.indexCount == 0 ? draw.mesh->IndexCount() : draw.indexCount;
						facePass.DrawIndexedPrimitives(indexCount, 1, draw.firstIndex);
					}
				}
			}
		}

		sdl3::GpuColorTargetInfo colorTarget{};
		colorTarget.texture = m_swapchainTexture.Get();
		colorTarget.clear_color = m_backgroundColor.ToFloat();
		colorTarget.load_op = sdl3::gpu_load_op::CLEAR;
		colorTarget.store_op = sdl3::gpu_store_op::STORE;

		sdl3::GpuDepthStencilTargetInfo depthTarget{};
		depthTarget.texture = m_depthTexture.Value().Get();
		depthTarget.clear_depth = 1.f;
		depthTarget.load_op = sdl3::gpu_load_op::CLEAR;
		depthTarget.store_op = sdl3::gpu_store_op::DONT_CARE;
		depthTarget.stencil_load_op = sdl3::gpu_load_op::DONT_CARE;
		depthTarget.stencil_store_op = sdl3::gpu_store_op::DONT_CARE;

		{
			sdl3::GpuRenderPass pass = cmd.BeginRenderPass(colorTarget, &depthTarget);

			sdl3::GpuViewport viewport{0.f, 0.f, float(m_width), float(m_height), 0.f, 1.f};
			pass.SetViewport(viewport);

			// Skybox dessiné en premier (avant la géométrie normale, qui le
			// recouvre naturellement par ordre de dessin — voir CreateSkyboxPipeline,
			// profondeur désactivée exprès). Vue "rotation seule" (translation de
			// la caméra retirée, voir le plan M11) : même direction relative
			// target-position, œil forcé à l'origine.
			if (m_environmentCubemap.IsSome() && EnsureSkyboxResources()) {
				math::FMatrix4 skyboxView =
					math::FMatrix4::LookAt({0.f, 0.f, 0.f}, m_camera.target - m_camera.position, m_camera.up);
				math::FMatrix4 skyboxViewProjection = m_camera.ProjectionMatrix() * skyboxView;

				pass.BindPipeline(m_skyboxPipeline.Value());
				sdl3::GpuBufferBinding skyboxBinding{m_skyboxVertexBuffer.Value().Get(), 0};
				pass.BindVertexBuffer(0, skyboxBinding);
				cmd.PushVertexUniformData(0, skyboxViewProjection);
				sdl3::GpuTextureSamplerBinding skyboxSamplerBinding{m_environmentCubemap.Value()->Get(),
																	m_skyboxSampler.Value().Get()};
				pass.BindFragmentSamplers(0, {&skyboxSamplerBinding, 1});
				pass.DrawPrimitives(m_skyboxVertexCount);
			}

			math::FMatrix4 viewProjection = m_camera.ViewProjectionMatrix();
			bool multiLight = !m_pointLights.empty() || !m_spotLights.empty();

			for (auto &draw : m_drawList) {
				if (!draw.mesh->HasGpuBuffers())
					continue;

				PipelineKey key{};
				key.shader = !draw.material.lit  ? ShaderKind::BASIC
							: draw.material.pbr ? ShaderKind::PBR
												 : ShaderKind::PHONG;
				key.doubleSided = draw.material.doubleSided;
				key.wireframe = draw.material.wireframe;
				key.depthTest = draw.material.depthTest;
				key.hasTexture = draw.material.albedo.IsSome();
				if (draw.material.lit && multiLight) {
					key.pointLightCount = int(m_pointLights.size());
					key.spotLightCount = int(m_spotLights.size());
				}
				key.hasShadows = draw.material.lit && anyShadowCaster;
				key.hasEnvironment = draw.material.pbr && draw.material.lit && anyEnvironment;
				key.topology = draw.topology;
				key.colorFormat = m_colorFormat;
				key.depthFormat = m_depthFormat;

				auto pipeline = GetOrCreatePipeline(key);
				if (!pipeline)
					continue;
				pass.BindPipeline(pipeline.Value());

				sdl3::GpuBufferBinding vertexBinding{draw.mesh->VertexBuffer()->Get(), 0};
				pass.BindVertexBuffer(0, vertexBinding);
				sdl3::GpuBufferBinding indexBinding{draw.mesh->IndexBuffer()->Get(), 0};
				pass.BindIndexBuffer(indexBinding, sdl3::gpu_index_element_size::BITS32);

				cmd.PushVertexUniformData(0, viewProjection);

				struct PerObjectUBO {
					math::FMatrix4 model;
					math::FMatrix4 normalMatrix;
				};
				PerObjectUBO perObject{draw.transform, draw.transform.Inverse().Transpose()};
				cmd.PushVertexUniformData(1, perObject);

				if (draw.material.lit) {
					struct LightUBO {
						float lightDir[3];
						float padding1 = 0.f;
						float lightColor[4];
						float ambientColor[4];
						float cameraPos[3];
						float padding2 = 0.f;
					};
					sdl3::FColor lc(m_directionalLight.color);
					sdl3::FColor ac(m_ambientLight.color);
					LightUBO lightUbo{};
					lightUbo.lightDir[0] = m_directionalLight.direction.x;
					lightUbo.lightDir[1] = m_directionalLight.direction.y;
					lightUbo.lightDir[2] = m_directionalLight.direction.z;
					lightUbo.lightColor[0] = lc.r * m_directionalLight.intensity;
					lightUbo.lightColor[1] = lc.g * m_directionalLight.intensity;
					lightUbo.lightColor[2] = lc.b * m_directionalLight.intensity;
					lightUbo.lightColor[3] = lc.a;
					lightUbo.ambientColor[0] = ac.r * m_ambientLight.intensity;
					lightUbo.ambientColor[1] = ac.g * m_ambientLight.intensity;
					lightUbo.ambientColor[2] = ac.b * m_ambientLight.intensity;
					lightUbo.ambientColor[3] = ac.a;
					lightUbo.cameraPos[0] = m_camera.position.x;
					lightUbo.cameraPos[1] = m_camera.position.y;
					lightUbo.cameraPos[2] = m_camera.position.z;

					// PBR est TOUJOURS un shader généré par ShaderBuilder
					// (jamais de fichier précompilé, voir GetOrCreatePipeline)
					// — même condition que `needsBuiltShader` là-bas.
					if (key.shader == ShaderKind::PBR || key.pointLightCount > 0 || key.spotLightCount > 0 ||
						key.hasShadows || key.hasEnvironment) {
						// Shader généré par ShaderBuilder : MaterialUBO inclut
						// uEmissive (voir shader_chunks::MATERIAL_UBO) — forme
						// différente du Phong précompilé ci-dessous.
						struct MaterialUBO {
							float baseColor[4];
							float emissive[4];
							float roughness, metallic, useTexture, useIBL;
						};
						sdl3::FColor bc(draw.material.baseColor);
						MaterialUBO materialUbo{{bc.r, bc.g, bc.b, bc.a},
												{0.f, 0.f, 0.f, 0.f},
												draw.material.roughness,
												draw.material.metallic,
												draw.material.albedo.IsSome() ? 1.f : 0.f,
												key.hasEnvironment ? 1.f : 0.f};
						cmd.PushFragmentUniformData(0, materialUbo);

						if (key.hasShadows) {
							// Matrices d'ombre fusionnées dans LightUBO (même
							// binding=1) plutôt qu'un 5e binding séparé : SDL_GPU
							// limite à 4 uniform buffers fragment par étage, déjà
							// tous pris par Material/Light/Point/Spot — voir
							// shader_chunks::LIGHT_UBO_SHADOW.
							struct LightUBOWithShadow {
								LightUBO base;
								math::FMatrix4 dirShadowMatrix;
								math::FMatrix4 spotShadowMatrix0;
								math::FMatrix4 spotShadowMatrix1;
								float dirShadowEnabled;
								float numSpotShadows;
								float shadowBias;
								float padding3 = 0.f;
								float pointShadowEnabled;
								float pointShadowLightIndex;
								float pointShadowBias;
								float pointShadowPadding = 0.f;
							};
							LightUBOWithShadow shadowLightUbo{};
							shadowLightUbo.base = lightUbo;
							shadowLightUbo.dirShadowEnabled = m_directionalLight.castShadow ? 1.f : 0.f;
							shadowLightUbo.dirShadowMatrix = m_directionalLight.castShadow
																 ? m_directionalLight.ShadowViewProjection()
																 : math::FMatrix4::Identity();
							shadowLightUbo.numSpotShadows = float(shadowSpotIndices.size());
							shadowLightUbo.shadowBias = 0.0025f;
							if (!shadowSpotIndices.empty())
								shadowLightUbo.spotShadowMatrix0 =
									m_spotLights[shadowSpotIndices[0]].ShadowViewProjection();
							if (shadowSpotIndices.size() > 1)
								shadowLightUbo.spotShadowMatrix1 =
									m_spotLights[shadowSpotIndices[1]].ShadowViewProjection();
							shadowLightUbo.pointShadowEnabled = pointShadowLightIndex >= 0 ? 1.f : 0.f;
							shadowLightUbo.pointShadowLightIndex = float(pointShadowLightIndex);
							shadowLightUbo.pointShadowBias = 0.05f; // unités monde, pas NDC — voir shader_chunks::ComputePointShadowFactor
							cmd.PushFragmentUniformData(1, shadowLightUbo);
						} else {
							cmd.PushFragmentUniformData(1, lightUbo);
						}

						struct PointLightGpu {
							float position[3], distanceVal;
							float color[3], decay;
						};
						struct SpotLightGpu {
							float position[3], distanceVal;
							float direction[3], angleCos;
							float color[3], decay;
							float penumbra, pad0 = 0.f, pad1 = 0.f, pad2 = 0.f;
						};

						if (!m_pointLights.empty()) {
							std::vector<PointLightGpu> gpuLights;
							gpuLights.reserve(m_pointLights.size());
							for (const auto &light : m_pointLights) {
								sdl3::FColor pc(light.color);
								gpuLights.push_back({{light.position.x, light.position.y, light.position.z},
													 light.distance,
													 {pc.r * light.intensity, pc.g * light.intensity, pc.b * light.intensity},
													 light.decay});
							}
							SDL_PushGPUFragmentUniformData(cmd.Get(), 2, gpuLights.data(),
														   uint32_t(gpuLights.size() * sizeof(PointLightGpu)));
						}
						if (!m_spotLights.empty()) {
							std::vector<SpotLightGpu> gpuLights;
							gpuLights.reserve(m_spotLights.size());
							for (const auto &light : m_spotLights) {
								sdl3::FColor sc(light.color);
								gpuLights.push_back({{light.position.x, light.position.y, light.position.z},
													 light.distance,
													 {light.direction.x, light.direction.y, light.direction.z},
													 sdl3::Cos(light.angle),
													 {sc.r * light.intensity, sc.g * light.intensity, sc.b * light.intensity},
													 light.decay,
													 light.penumbra});
							}
							// Slot 3 fixe (shader_chunks::SPOT_LIGHT_ARRAY est toujours déclaré
							// au binding=3, même si PointLightUBO(binding=2) est absent —
							// les bindings SPIR-V n'ont pas besoin d'être contigus).
							SDL_PushGPUFragmentUniformData(cmd.Get(), 3, gpuLights.data(),
														   uint32_t(gpuLights.size() * sizeof(SpotLightGpu)));
						}
					} else {
						struct MaterialUBO {
							float baseColor[4];
							float roughness;
							float metallic;
							float useTexture;
							float padding0 = 0.f;
						};
						sdl3::FColor bc(draw.material.baseColor);
						MaterialUBO materialUbo{{bc.r, bc.g, bc.b, bc.a}, draw.material.roughness,
												draw.material.metallic, draw.material.albedo.IsSome() ? 1.f : 0.f, 0.f};
						cmd.PushFragmentUniformData(0, materialUbo);
						cmd.PushFragmentUniformData(1, lightUbo);
					}

					if (key.hasShadows || key.hasEnvironment) {
						// Bindings contigus à partir de 0 (albedo, puis ombres si
						// key.hasShadows, puis IBL si key.hasEnvironment — voir
						// shader_chunks::SAMPLER_IBL_WITH_SHADOWS/_NO_SHADOWS, l'ordre
						// ici doit correspondre exactement) : SDL_GPU exige un seul
						// appel couvrant toute la plage déclarée au pipeline.
						std::array<sdl3::GpuTextureSamplerBinding, 8> samplerBindings{};
						uint32_t count = 0;
						samplerBindings[count].texture =
							draw.material.albedo.IsSome() ? draw.material.albedo.Value()->Get() : m_whiteTexture.Get();
						samplerBindings[count].sampler = m_defaultSampler.Get();
						++count;
						if (key.hasShadows) {
							samplerBindings[count] = {m_dirShadowMap.Value().Get(), m_shadowSampler.Value().Get()};
							++count;
							samplerBindings[count] = {m_spotShadowMaps[0].Value().Get(), m_shadowSampler.Value().Get()};
							++count;
							samplerBindings[count] = {m_spotShadowMaps[1].Value().Get(), m_shadowSampler.Value().Get()};
							++count;
							samplerBindings[count] = {m_pointShadowMap.Value().Get(),
													  m_pointShadowSampler.Value().Get()};
							++count;
						}
						if (key.hasEnvironment) {
							samplerBindings[count] = {m_environmentIrradiance.Value()->Get(),
													  m_iblCubeSampler.Value().Get()};
							++count;
							samplerBindings[count] = {m_environmentPrefiltered.Value()->Get(),
													  m_iblCubeSampler.Value().Get()};
							++count;
							samplerBindings[count] = {m_environmentBrdfLut.Value()->Get(),
													  m_iblLutSampler.Value().Get()};
							++count;
						}
						pass.BindFragmentSamplers(0, std::span<const sdl3::GpuTextureSamplerBinding>(
														samplerBindings.data(), count));
					} else {
						sdl3::GpuTextureSamplerBinding samplerBinding{};
						samplerBinding.texture =
							draw.material.albedo.IsSome() ? draw.material.albedo.Value()->Get() : m_whiteTexture.Get();
						samplerBinding.sampler = m_defaultSampler.Get();
						pass.BindFragmentSamplers(0, {&samplerBinding, 1});
					}
				} else {
					struct BasicMaterialUBO {
						float baseColor[4];
					};
					sdl3::FColor bc(draw.material.baseColor);
					BasicMaterialUBO materialUbo{{bc.r, bc.g, bc.b, bc.a}};
					cmd.PushFragmentUniformData(0, materialUbo);
				}

				uint32_t indexCount = draw.indexCount == 0 ? draw.mesh->IndexCount() : draw.indexCount;
				pass.DrawIndexedPrimitives(indexCount, 1, draw.firstIndex);
			}

			// InstancedMesh (M16) — pas d'ombres/IBL pour cette phase (voir le
			// plan) : la matrice par instance remplace complètement uModel, et
			// ajouter les passes d'ombre par instance (6 faces x N instances
			// pour un point light, etc.) dépasse le périmètre de ce jalon.
			for (size_t i = 0; i < m_instancedDrawList.size(); ++i) {
				auto &draw = m_instancedDrawList[i];
				if (!draw.mesh->HasGpuBuffers() || instanceBuffers[i].IsNone())
					continue;

				PipelineKey key{};
				key.shader = !draw.material.lit  ? ShaderKind::BASIC
							: draw.material.pbr ? ShaderKind::PBR
												 : ShaderKind::PHONG;
				key.doubleSided = draw.material.doubleSided;
				key.wireframe = draw.material.wireframe;
				key.depthTest = draw.material.depthTest;
				key.hasTexture = draw.material.albedo.IsSome();
				if (draw.material.lit && multiLight) {
					key.pointLightCount = int(m_pointLights.size());
					key.spotLightCount = int(m_spotLights.size());
				}
				key.instanced = true;
				key.colorFormat = m_colorFormat;
				key.depthFormat = m_depthFormat;

				auto pipeline = GetOrCreatePipeline(key);
				if (!pipeline)
					continue;
				pass.BindPipeline(pipeline.Value());

				sdl3::GpuBufferBinding vertexBinding{draw.mesh->VertexBuffer()->Get(), 0};
				pass.BindVertexBuffer(0, vertexBinding);
				sdl3::GpuBufferBinding instanceBinding{instanceBuffers[i].Value().Get(), 0};
				pass.BindVertexBuffer(1, instanceBinding);
				sdl3::GpuBufferBinding indexBinding{draw.mesh->IndexBuffer()->Get(), 0};
				pass.BindIndexBuffer(indexBinding, sdl3::gpu_index_element_size::BITS32);

				cmd.PushVertexUniformData(0, viewProjection);
				// PerObjectUBO (slot 1) inutilisé côté shader instancié (voir
				// shader_chunks::VERTEX_MAIN_LIT/_UNLIT, #ifdef INSTANCED_ENABLED)
				// mais le pipeline le déclare quand même (2 UBO vertex fixes) —
				// une valeur bidon suffit, jamais lue.
				struct PerObjectUBO {
					math::FMatrix4 model;
					math::FMatrix4 normalMatrix;
				};
				cmd.PushVertexUniformData(1, PerObjectUBO{math::FMatrix4::Identity(), math::FMatrix4::Identity()});

				if (draw.material.lit) {
					struct LightUBO {
						float lightDir[3];
						float padding1 = 0.f;
						float lightColor[4];
						float ambientColor[4];
						float cameraPos[3];
						float padding2 = 0.f;
					};
					sdl3::FColor lc(m_directionalLight.color);
					sdl3::FColor ac(m_ambientLight.color);
					LightUBO lightUbo{};
					lightUbo.lightDir[0] = m_directionalLight.direction.x;
					lightUbo.lightDir[1] = m_directionalLight.direction.y;
					lightUbo.lightDir[2] = m_directionalLight.direction.z;
					lightUbo.lightColor[0] = lc.r * m_directionalLight.intensity;
					lightUbo.lightColor[1] = lc.g * m_directionalLight.intensity;
					lightUbo.lightColor[2] = lc.b * m_directionalLight.intensity;
					lightUbo.lightColor[3] = lc.a;
					lightUbo.ambientColor[0] = ac.r * m_ambientLight.intensity;
					lightUbo.ambientColor[1] = ac.g * m_ambientLight.intensity;
					lightUbo.ambientColor[2] = ac.b * m_ambientLight.intensity;
					lightUbo.ambientColor[3] = ac.a;
					lightUbo.cameraPos[0] = m_camera.position.x;
					lightUbo.cameraPos[1] = m_camera.position.y;
					lightUbo.cameraPos[2] = m_camera.position.z;

					struct MaterialUBO {
						float baseColor[4];
						float emissive[4];
						float roughness, metallic, useTexture, useIBL;
					};
					sdl3::FColor bc(draw.material.baseColor);
					MaterialUBO materialUbo{{bc.r, bc.g, bc.b, bc.a},
											{0.f, 0.f, 0.f, 0.f},
											draw.material.roughness,
											draw.material.metallic,
											draw.material.albedo.IsSome() ? 1.f : 0.f,
											0.f};
					cmd.PushFragmentUniformData(0, materialUbo);
					cmd.PushFragmentUniformData(1, lightUbo);

					struct PointLightGpu {
						float position[3], distanceVal;
						float color[3], decay;
					};
					struct SpotLightGpu {
						float position[3], distanceVal;
						float direction[3], angleCos;
						float color[3], decay;
						float penumbra, pad0 = 0.f, pad1 = 0.f, pad2 = 0.f;
					};
					if (!m_pointLights.empty()) {
						std::vector<PointLightGpu> gpuLights;
						gpuLights.reserve(m_pointLights.size());
						for (const auto &light : m_pointLights) {
							sdl3::FColor pc(light.color);
							gpuLights.push_back({{light.position.x, light.position.y, light.position.z},
												 light.distance,
												 {pc.r * light.intensity, pc.g * light.intensity, pc.b * light.intensity},
												 light.decay});
						}
						SDL_PushGPUFragmentUniformData(cmd.Get(), 2, gpuLights.data(),
													   uint32_t(gpuLights.size() * sizeof(PointLightGpu)));
					}
					if (!m_spotLights.empty()) {
						std::vector<SpotLightGpu> gpuLights;
						gpuLights.reserve(m_spotLights.size());
						for (const auto &light : m_spotLights) {
							sdl3::FColor sc(light.color);
							gpuLights.push_back({{light.position.x, light.position.y, light.position.z},
												 light.distance,
												 {light.direction.x, light.direction.y, light.direction.z},
												 sdl3::Cos(light.angle),
												 {sc.r * light.intensity, sc.g * light.intensity, sc.b * light.intensity},
												 light.decay,
												 light.penumbra});
						}
						SDL_PushGPUFragmentUniformData(cmd.Get(), 3, gpuLights.data(),
													   uint32_t(gpuLights.size() * sizeof(SpotLightGpu)));
					}

					sdl3::GpuTextureSamplerBinding samplerBinding{};
					samplerBinding.texture =
						draw.material.albedo.IsSome() ? draw.material.albedo.Value()->Get() : m_whiteTexture.Get();
					samplerBinding.sampler = m_defaultSampler.Get();
					pass.BindFragmentSamplers(0, {&samplerBinding, 1});
				} else {
					struct BasicMaterialUBO {
						float baseColor[4];
					};
					sdl3::FColor bc(draw.material.baseColor);
					BasicMaterialUBO materialUbo{{bc.r, bc.g, bc.b, bc.a}};
					cmd.PushFragmentUniformData(0, materialUbo);
				}

				pass.DrawIndexedPrimitives(draw.mesh->IndexCount(), uint32_t(draw.instances.size()), 0);
			}

			// SkinnedMesh (M17) — même scope cut que InstancedMesh ci-dessus
			// (pas d'ombres/IBL cette phase, voir le plan).
			for (size_t i = 0; i < m_skinnedDrawList.size(); ++i) {
				auto &draw = m_skinnedDrawList[i];
				if (!draw.geometry->HasGpuBuffers() || boneBuffers[i].IsNone())
					continue;

				PipelineKey key{};
				key.shader = !draw.material.lit  ? ShaderKind::BASIC
							: draw.material.pbr ? ShaderKind::PBR
												 : ShaderKind::PHONG;
				key.doubleSided = draw.material.doubleSided;
				key.wireframe = draw.material.wireframe;
				key.depthTest = draw.material.depthTest;
				key.hasTexture = draw.material.albedo.IsSome();
				if (draw.material.lit && multiLight) {
					key.pointLightCount = int(m_pointLights.size());
					key.spotLightCount = int(m_spotLights.size());
				}
				key.skinned = true;
				key.colorFormat = m_colorFormat;
				key.depthFormat = m_depthFormat;

				auto pipeline = GetOrCreatePipeline(key);
				if (!pipeline)
					continue;
				pass.BindPipeline(pipeline.Value());

				sdl3::GpuBufferBinding vertexBinding{draw.geometry->VertexBuffer()->Get(), 0};
				pass.BindVertexBuffer(0, vertexBinding);
				sdl3::GpuBufferBinding indexBinding{draw.geometry->IndexBuffer()->Get(), 0};
				pass.BindIndexBuffer(indexBinding, sdl3::gpu_index_element_size::BITS32);
				Ref<sdl3::GpuBuffer> boneBufferRef = MakeRef(boneBuffers[i].Value());
				pass.BindVertexStorageBuffers(0, {&boneBufferRef, 1});

				cmd.PushVertexUniformData(0, viewProjection);
				struct PerObjectUBO {
					math::FMatrix4 model;
					math::FMatrix4 normalMatrix;
				};
				cmd.PushVertexUniformData(1, PerObjectUBO{math::FMatrix4::Identity(), math::FMatrix4::Identity()});

				if (draw.material.lit) {
					struct LightUBO {
						float lightDir[3];
						float padding1 = 0.f;
						float lightColor[4];
						float ambientColor[4];
						float cameraPos[3];
						float padding2 = 0.f;
					};
					sdl3::FColor lc(m_directionalLight.color);
					sdl3::FColor ac(m_ambientLight.color);
					LightUBO lightUbo{};
					lightUbo.lightDir[0] = m_directionalLight.direction.x;
					lightUbo.lightDir[1] = m_directionalLight.direction.y;
					lightUbo.lightDir[2] = m_directionalLight.direction.z;
					lightUbo.lightColor[0] = lc.r * m_directionalLight.intensity;
					lightUbo.lightColor[1] = lc.g * m_directionalLight.intensity;
					lightUbo.lightColor[2] = lc.b * m_directionalLight.intensity;
					lightUbo.lightColor[3] = lc.a;
					lightUbo.ambientColor[0] = ac.r * m_ambientLight.intensity;
					lightUbo.ambientColor[1] = ac.g * m_ambientLight.intensity;
					lightUbo.ambientColor[2] = ac.b * m_ambientLight.intensity;
					lightUbo.ambientColor[3] = ac.a;
					lightUbo.cameraPos[0] = m_camera.position.x;
					lightUbo.cameraPos[1] = m_camera.position.y;
					lightUbo.cameraPos[2] = m_camera.position.z;

					struct MaterialUBO {
						float baseColor[4];
						float emissive[4];
						float roughness, metallic, useTexture, useIBL;
					};
					sdl3::FColor bc(draw.material.baseColor);
					MaterialUBO materialUbo{{bc.r, bc.g, bc.b, bc.a},
											{0.f, 0.f, 0.f, 0.f},
											draw.material.roughness,
											draw.material.metallic,
											draw.material.albedo.IsSome() ? 1.f : 0.f,
											0.f};
					cmd.PushFragmentUniformData(0, materialUbo);
					cmd.PushFragmentUniformData(1, lightUbo);

					struct PointLightGpu {
						float position[3], distanceVal;
						float color[3], decay;
					};
					struct SpotLightGpu {
						float position[3], distanceVal;
						float direction[3], angleCos;
						float color[3], decay;
						float penumbra, pad0 = 0.f, pad1 = 0.f, pad2 = 0.f;
					};
					if (!m_pointLights.empty()) {
						std::vector<PointLightGpu> gpuLights;
						gpuLights.reserve(m_pointLights.size());
						for (const auto &light : m_pointLights) {
							sdl3::FColor pc(light.color);
							gpuLights.push_back({{light.position.x, light.position.y, light.position.z},
												 light.distance,
												 {pc.r * light.intensity, pc.g * light.intensity, pc.b * light.intensity},
												 light.decay});
						}
						SDL_PushGPUFragmentUniformData(cmd.Get(), 2, gpuLights.data(),
													   uint32_t(gpuLights.size() * sizeof(PointLightGpu)));
					}
					if (!m_spotLights.empty()) {
						std::vector<SpotLightGpu> gpuLights;
						gpuLights.reserve(m_spotLights.size());
						for (const auto &light : m_spotLights) {
							sdl3::FColor sc(light.color);
							gpuLights.push_back({{light.position.x, light.position.y, light.position.z},
												 light.distance,
												 {light.direction.x, light.direction.y, light.direction.z},
												 sdl3::Cos(light.angle),
												 {sc.r * light.intensity, sc.g * light.intensity, sc.b * light.intensity},
												 light.decay,
												 light.penumbra});
						}
						SDL_PushGPUFragmentUniformData(cmd.Get(), 3, gpuLights.data(),
													   uint32_t(gpuLights.size() * sizeof(SpotLightGpu)));
					}

					sdl3::GpuTextureSamplerBinding samplerBinding{};
					samplerBinding.texture =
						draw.material.albedo.IsSome() ? draw.material.albedo.Value()->Get() : m_whiteTexture.Get();
					samplerBinding.sampler = m_defaultSampler.Get();
					pass.BindFragmentSamplers(0, {&samplerBinding, 1});
				} else {
					struct BasicMaterialUBO {
						float baseColor[4];
					};
					sdl3::FColor bc(draw.material.baseColor);
					BasicMaterialUBO materialUbo{{bc.r, bc.g, bc.b, bc.a}};
					cmd.PushFragmentUniformData(0, materialUbo);
				}

				pass.DrawIndexedPrimitives(draw.geometry->IndexCount(), 1, 0);
			}
		}

		bool submitted = cmd.Submit();
		(void)submitted; // best-effort : une frame perdue n'est pas fatale, GetError() reste disponible côté appelant
		m_commandBuffer = NONE;
		m_frameActive = false;
		m_drawList.clear();
		m_instancedDrawList.clear();
		m_skinnedDrawList.clear();
	}
};

} // namespace render3d
