// Définitions de render3d/offscreen.hpp — fichier généré par splitter.py : le code
// vient tel quel de l'en-tête (seules les signatures sont réécrites).

#include "render3d/offscreen.hpp"

namespace render3d {

// ── OffscreenTarget ──────────────────────────────────────────────────────────

Result<bool, StringView> OffscreenTarget::EnsureSize(sdl3::GpuDevice &device, uint32_t width, uint32_t height,
		sdl3::GpuTextureFormat colorFormat,
		sdl3::GpuTextureFormat depthFormat) {
	if (width == 0 || height == 0)
		return Err(StringView("OffscreenTarget::EnsureSize: zero size"));
	if (m_colorTexture.IsSome() && m_depthTexture.IsSome() && m_width == width && m_height == height &&
		m_colorFormat == colorFormat && m_depthFormat == depthFormat)
		return Ok(true);

	sdl3::GpuTextureCreateInfo colorInfo{};
	colorInfo.type = sdl3::gpu_texture_type::TEXTURE2_D;
	colorInfo.format = colorFormat;
	colorInfo.usage = sdl3::gpu_texture_usage::COLOR_TARGET | sdl3::gpu_texture_usage::SAMPLER;
	colorInfo.width = width;
	colorInfo.height = height;
	colorInfo.layer_count_or_depth = 1;
	colorInfo.num_levels = 1;
	colorInfo.sample_count = sdl3::gpu_sample_count::SAMPLE1;
	auto colorResult = device.CreateTexture(colorInfo);
	if (!colorResult)
		return Err(colorResult.Error());

	sdl3::GpuTextureCreateInfo depthInfo{};
	depthInfo.type = sdl3::gpu_texture_type::TEXTURE2_D;
	depthInfo.format = depthFormat;
	depthInfo.usage = sdl3::gpu_texture_usage::DEPTH_STENCIL_TARGET;
	depthInfo.width = width;
	depthInfo.height = height;
	depthInfo.layer_count_or_depth = 1;
	depthInfo.num_levels = 1;
	depthInfo.sample_count = sdl3::gpu_sample_count::SAMPLE1;
	auto depthResult = device.CreateTexture(depthInfo);
	if (!depthResult)
		return Err(depthResult.Error());

	// Tampon de téléchargement dimensionné une fois pour toutes ici (cf.
	// m_downloadBuffer). Son échec n'est PAS fatal : seule la lecture des
	// pixels vers le CPU en dépend, pas le rendu lui-même — une cible
	// qu'on ne fait que sampler côté GPU reste parfaitement utilisable.
	uint32_t byteSize = width * height * sdl3::GpuTexelBlockSize(colorFormat);
	auto downloadResult = device.CreateTransferBuffer(sdl3::gpu_transfer_buffer_usage::DOWNLOAD, byteSize);
	if (downloadResult) {
		m_downloadBuffer = Some(std::move(downloadResult.Value()));
		m_downloadByteSize = byteSize;
	} else {
		m_downloadBuffer = NONE;
		m_downloadByteSize = 0;
	}

	m_colorTexture = Some(std::move(colorResult.Value()));
	m_depthTexture = Some(std::move(depthResult.Value()));
	m_width = width;
	m_height = height;
	m_colorFormat = colorFormat;
	m_depthFormat = depthFormat;
	return Ok(true);
}

Result<bool, StringView> Canvas::RenderObjectOffscreen(Object3D &root, const Camera &camera,
		const math::FMatrix4 &viewProjection,
		OffscreenTarget &target) {
	if (!target.IsReady())
		return Err(StringView("Canvas::RenderObjectOffscreen: target not sized (call OffscreenTarget::EnsureSize "
							  "first)"));

	// Ce rendu est IMBRIQUÉ : il peut survenir au milieu d'une image de
	// fenêtre déjà commencée. Tout l'état de collecte (liste de dessins,
	// drapeau d'image active, frustum de culling) est donc sauvegardé puis
	// restauré — le frustum en particulier, car celui-ci n'est PAS celui de
	// la caméra de la fenêtre (un portail rend avec une projection à plan
	// proche oblique).
	std::vector<DrawCall> savedDrawList = std::move(m_drawList);
	bool savedFrameActive = m_frameActive;
	math::FFrustum savedFrustum = m_frustum;
	bool savedFrustumValid = m_frustumValid;

	m_drawList.clear();
	m_frameActive = true;
	SetFrustumFromViewProjection(viewProjection);
	root.Traverse([this](Object3D &node) { node.OnDraw(*this); });
	std::vector<DrawCall> offscreenDrawList = std::move(m_drawList);

	m_drawList = std::move(savedDrawList);
	m_frameActive = savedFrameActive;
	m_frustum = savedFrustum;
	m_frustumValid = savedFrustumValid;
	// Les COMPTEURS de dessins ne sont volontairement pas restaurés : ils
	// décrivent la DERNIÈRE passe rendue, offscreen comprise (cf. leur
	// documentation dans canvas.hpp). Les restaurer rendrait le culling d'un
	// rendu hors écran — donc d'un widget Viewport3D, c'est-à-dire du cas le
	// plus courant — totalement invisible au profilage.

	auto cmd = m_device.AcquireCommandBuffer();

	// Tous les uploads AVANT le render pass — même règle que End() (canvas.hpp
	// lignes 643-656) : un GpuCopyPass et un GpuRenderPass sont mutuellement
	// exclusifs sur un même command buffer.
	for (auto &draw : offscreenDrawList) {
		auto uploaded = draw.mesh->EnsureGpuBuffers(m_device, cmd);
		(void)uploaded; // best-effort : un maillage non uploadé est simplement sauté ci-dessous
	}

	sdl3::GpuColorTargetInfo colorTarget{};
	colorTarget.texture = target.ColorTexture().Get();
	colorTarget.clear_color = m_backgroundColor.ToFloat();
	colorTarget.load_op = sdl3::gpu_load_op::CLEAR;
	colorTarget.store_op = sdl3::gpu_store_op::STORE;

	sdl3::GpuDepthStencilTargetInfo depthTarget{};
	depthTarget.texture = target.DepthTexture().Get();
	depthTarget.clear_depth = 1.f;
	depthTarget.load_op = sdl3::gpu_load_op::CLEAR;
	depthTarget.store_op = sdl3::gpu_store_op::DONT_CARE;
	depthTarget.stencil_load_op = sdl3::gpu_load_op::DONT_CARE;
	depthTarget.stencil_store_op = sdl3::gpu_store_op::DONT_CARE;

	{
		sdl3::GpuRenderPass pass = cmd.BeginRenderPass(colorTarget, &depthTarget);
		sdl3::GpuViewport viewport{0.f, 0.f, float(target.Width()), float(target.Height()), 0.f, 1.f};
		pass.SetViewport(viewport);

		bool multiLight = !m_pointLights.empty() || !m_spotLights.empty();

		// Boucle de dessin "plate" — copie du 2e passage sur m_drawList dans
		// End() (canvas.hpp, ~lignes 906-1149) pour le cas hasShadows=false &&
		// hasEnvironment=false, SANS la branche m_instancedDrawList/
		// m_skinnedDrawList (scope cut M21, voir le plan : offscreen ne
		// marche que sur les Shape simples collectées via OnDraw() ci-dessus).
		for (auto &draw : offscreenDrawList) {
			if (!draw.mesh->HasGpuBuffers())
				continue;

			PipelineKey key{};
			key.shader = !draw.material.lit  ? ShaderKind::BASIC
						: draw.material.pbr ? ShaderKind::PBR
											 : ShaderKind::PHONG;
			key.doubleSided = draw.material.doubleSided;
			key.wireframe = draw.material.wireframe;
			key.hasTexture = draw.material.albedo.IsSome();
			if (draw.material.lit && multiLight) {
				key.pointLightCount = int(m_pointLights.size());
				key.spotLightCount = int(m_spotLights.size());
			}
			key.hasShadows = false;     // scope cut M21 (voir le plan) : pas d'ombres offscreen
			key.hasEnvironment = false; // idem, pas d'IBL offscreen
			key.topology = draw.topology;
			key.colorFormat = target.ColorFormat();
			key.depthFormat = target.DepthFormat();

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
				lightUbo.cameraPos[0] = camera.position.x;
				lightUbo.cameraPos[1] = camera.position.y;
				lightUbo.cameraPos[2] = camera.position.z;

				// Même condition que GetOrCreatePipeline::needsBuiltShader
				// (avec hasShadows/hasEnvironment/instanced/skinned toujours
				// faux ici) — détermine si le shader est le Phong précompilé
				// (MaterialUBO à 4 floats) ou un shader ShaderBuilder
				// (MaterialUBO avec `emissive`/`useIBL`, voir shader_chunks).
				bool needsBuiltShader = key.shader == ShaderKind::PBR || key.pointLightCount > 0 ||
										key.spotLightCount > 0;
				if (needsBuiltShader) {
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
							gpuLights.push_back(
								{{light.position.x, light.position.y, light.position.z},
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
							gpuLights.push_back(
								{{light.position.x, light.position.y, light.position.z},
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

			uint32_t indexCount = draw.indexCount == 0 ? draw.mesh->IndexCount() : draw.indexCount;
			pass.DrawIndexedPrimitives(indexCount, 1, draw.firstIndex);
		}
	}

	// Synchronisation ici (plutôt que de la laisser deviner à l'appelant) : le
	// téléchargement de pixels qui suit typiquement cet appel (voir
	// RenderObjectToTexture ci-dessous) utilise un command buffer SÉPARÉ —
	// sans synchronisation explicite, SDL_GPU ne garantit pas que le rendu
	// soit visible à ce 2e command buffer.
	//
	// Attente sur la CLÔTURE de cette soumission, et non `WaitIdle()` : la
	// garantie recherchée (ce rendu-ci est terminé) est exactement celle que
	// donne la clôture, alors que `WaitIdle` attendait en plus tout le
	// travail sans rapport en vol sur le device — y compris le rendu 2D de
	// `SDL_Renderer`, qui partage ce device. Il y avait ainsi DEUX barrières
	// globales par viewport et par image (celle-ci et celle du
	// téléchargement).
	Option<sdl3::GpuFence> fence = cmd.SubmitAndAcquireFence();
	if (fence.IsNone())
		return Err(sdl3::GetError());
	Ref<sdl3::GpuFence> fenceRef = MakeRef(fence.Value());
	if (!m_device.WaitForFences(true, std::span<const Ref<sdl3::GpuFence>>(&fenceRef, 1)))
		return Err(StringView("Canvas::RenderObjectOffscreen: attente de la clôture GPU échouée"));
	return Ok(true);
}

Result<bool, StringView> Canvas::RenderObjectOffscreen(Object3D &root, const Camera &camera, OffscreenTarget &target) {
	return RenderObjectOffscreen(root, camera, camera.ViewProjectionMatrix(), target);
}

namespace detail {

Result<bool, StringView> DownloadColorTexture(Canvas &canvas, OffscreenTarget &target, std::vector<uint8_t> &outPixels) {
	sdl3::GpuDevice &device = canvas.Device();
	uint32_t width = target.Width();
	uint32_t height = target.Height();

	// Le tampon de transfert appartient désormais à la CIBLE et vit aussi
	// longtemps qu'elle (cf. OffscreenTarget) : plus d'allocation de mémoire
	// GPU par image et par viewport, pour une taille qui ne change qu'au
	// redimensionnement.
	if (!target.HasDownloadBuffer())
		return Err(StringView("DownloadColorTexture: la cible n'a pas de tampon de transfert"));
	sdl3::GpuTransferBuffer &downloadBuffer = target.DownloadBuffer();
	uint32_t byteSize = target.DownloadByteSize();

	auto downloadCmd = device.AcquireCommandBuffer();
	{
		sdl3::GpuCopyPass copyPass = downloadCmd.BeginCopyPass();
		sdl3::GpuTextureRegion src{target.ColorTexture().Get(), 0, 0, 0, 0, 0, width, height, 1};
		sdl3::GpuTextureTransferInfo dst{downloadBuffer.Get(), 0, width, height};
		copyPass.DownloadFromTexture(src, dst);
	}

	// Attente sur la CLÔTURE de CETTE soumission, et non `WaitIdle()` qui
	// attendait que le device ENTIER se vide — donc aussi tout travail sans
	// rapport déjà en vol (les autres viewports, les portails, le rendu 2D de
	// SDL_Renderer qui partage ce device). C'était une barrière globale posée
	// une à plusieurs fois par image.
	Option<sdl3::GpuFence> fence = downloadCmd.SubmitAndAcquireFence();
	if (fence.IsNone())
		return Err(sdl3::GetError());
	Ref<sdl3::GpuFence> fenceRef = MakeRef(fence.Value());
	if (!device.WaitForFences(true, std::span<const Ref<sdl3::GpuFence>>(&fenceRef, 1)))
		return Err(StringView("DownloadColorTexture: attente de la clôture GPU échouée"));

	auto mapped = downloadBuffer.Map(false);
	if (!mapped)
		return Err(StringView("DownloadColorTexture: mappage du tampon de transfert impossible"));
	outPixels.resize(byteSize);
	std::memcpy(outPixels.data(), mapped.GetData(), byteSize);
	return Ok(true);
}

} // namespace detail

Result<bool, StringView> RenderObjectToTexture(Canvas &canvas, Object3D &root, Camera camera,
		OffscreenTarget &target,
		std::vector<uint8_t> &outPixels) {
	if (!target.IsReady())
		return Err(StringView("RenderObjectToTexture: target not sized (call OffscreenTarget::EnsureSize first)"));

	camera.aspect = float(target.Width()) / float(target.Height());

	auto rendered = canvas.RenderObjectOffscreen(root, camera, target);
	if (!rendered)
		return Err(rendered.Error());

	return detail::DownloadColorTexture(canvas, target, outPixels);
}

Result<bool, StringView> RenderObjectToTexture(Canvas &canvas, Object3D &root, Camera camera,
		const math::FMatrix4 &viewProjection,
		OffscreenTarget &target,
		std::vector<uint8_t> &outPixels) {
	if (!target.IsReady())
		return Err(StringView("RenderObjectToTexture: target not sized (call OffscreenTarget::EnsureSize first)"));

	auto rendered = canvas.RenderObjectOffscreen(root, camera, viewProjection, target);
	if (!rendered)
		return Err(rendered.Error());

	return detail::DownloadColorTexture(canvas, target, outPixels);
}

Result<sdl3::GpuTexture, StringView> UploadSurfaceToGpuTexture(sdl3::GpuDevice &device, const uint8_t *pixels, uint32_t width, uint32_t height,
		sdl3::GpuTextureFormat format) {
	if (!pixels || width == 0 || height == 0)
		return Err(StringView("UploadSurfaceToGpuTexture: invalid input"));

	sdl3::GpuTextureCreateInfo textureInfo{};
	textureInfo.type = sdl3::gpu_texture_type::TEXTURE2_D;
	textureInfo.format = format;
	textureInfo.usage = sdl3::gpu_texture_usage::SAMPLER;
	textureInfo.width = width;
	textureInfo.height = height;
	textureInfo.layer_count_or_depth = 1;
	textureInfo.num_levels = 1;
	textureInfo.sample_count = sdl3::gpu_sample_count::SAMPLE1;
	auto textureResult = device.CreateTexture(textureInfo);
	if (!textureResult)
		return Err(textureResult.Error());

	uint32_t bytesPerTexel = sdl3::GpuTexelBlockSize(format);
	uint32_t byteSize = width * height * bytesPerTexel;
	auto transferResult = device.CreateTransferBuffer(sdl3::gpu_transfer_buffer_usage::UPLOAD, byteSize);
	if (!transferResult)
		return Err(transferResult.Error());
	sdl3::GpuTransferBuffer transfer = std::move(transferResult.Value());
	{
		auto mapped = transfer.Map(false);
		if (!mapped)
			return Err(StringView("UploadSurfaceToGpuTexture: failed to map transfer buffer"));
		std::memcpy(mapped.GetData(), pixels, byteSize);
	}

	auto cmd = device.AcquireCommandBuffer();
	{
		sdl3::GpuCopyPass copyPass = cmd.BeginCopyPass();
		sdl3::GpuTextureTransferInfo src{transfer.Get(), 0, width, height};
		sdl3::GpuTextureRegion dst{textureResult.Value().Get(), 0, 0, 0, 0, 0, width, height, 1};
		copyPass.UploadToTexture(src, dst, false);
	}
	if (!cmd.Submit())
		return Err(sdl3::GetError());
	if (!device.WaitIdle())
		return Err(StringView("UploadSurfaceToGpuTexture: WaitIdle failed"));

	return Ok(std::move(textureResult.Value()));
}

} // namespace render3d
