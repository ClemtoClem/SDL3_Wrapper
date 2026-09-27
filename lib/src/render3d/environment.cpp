// Définitions de render3d/environment.hpp — fichier généré par splitter.py : le code
// vient tel quel de l'en-tête (seules les signatures sont réécrites).

#include "render3d/environment.hpp"

namespace render3d {

namespace detail {

Result<bool, String> RenderCubeFaces(sdl3::GpuDevice &device, sdl3::GpuGraphicsPipeline &pipeline, sdl3::GpuBuffer &vertexBuffer,
		uint32_t vertexCount, sdl3::GpuTextureSamplerBinding sourceSampler, sdl3::GpuTexture &target,
		uint32_t targetSize) {
	for (int face = 0; face < 6; ++face) {
		auto cmd = device.AcquireCommandBuffer();
		sdl3::GpuColorTargetInfo colorTarget{};
		colorTarget.texture = target.Get();
		colorTarget.layer_or_depth_plane = uint32_t(face);
		colorTarget.load_op = sdl3::gpu_load_op::DONT_CARE;
		colorTarget.store_op = sdl3::gpu_store_op::STORE;
		{
			sdl3::GpuRenderPass pass = cmd.BeginRenderPass(colorTarget, nullptr);
			pass.SetViewport(sdl3::GpuViewport{0.f, 0.f, float(targetSize), float(targetSize), 0.f, 1.f});
			pass.BindPipeline(pipeline);
			sdl3::GpuBufferBinding binding{vertexBuffer.Get(), 0};
			pass.BindVertexBuffer(0, binding);
			cmd.PushVertexUniformData(0, CubeFaceViewProjection(face));
			pass.BindFragmentSamplers(0, {&sourceSampler, 1});
			pass.DrawPrimitives(vertexCount);
		}
		if (!cmd.Submit())
			return Err(String(sdl3::GetError()));
	}
	return Ok(true);
}

Result<bool, String> RenderPrefilteredMips(sdl3::GpuDevice &device, sdl3::GpuGraphicsPipeline &pipeline, sdl3::GpuBuffer &vertexBuffer,
		uint32_t vertexCount, sdl3::GpuTextureSamplerBinding sourceSampler, sdl3::GpuTexture &target,
		uint32_t baseSize) {
	struct PrefilterUBO {
		float roughness;
	};
	for (uint32_t mip = 0; mip < PREFILTER_MIP_LEVELS; ++mip) {
		uint32_t mipSize = std::max(1u, baseSize >> mip);
		float roughness = float(mip) / float(PREFILTER_MIP_LEVELS - 1);
		for (int face = 0; face < 6; ++face) {
			auto cmd = device.AcquireCommandBuffer();
			sdl3::GpuColorTargetInfo colorTarget{};
			colorTarget.texture = target.Get();
			colorTarget.mip_level = mip;
			colorTarget.layer_or_depth_plane = uint32_t(face);
			colorTarget.load_op = sdl3::gpu_load_op::DONT_CARE;
			colorTarget.store_op = sdl3::gpu_store_op::STORE;
			{
				sdl3::GpuRenderPass pass = cmd.BeginRenderPass(colorTarget, nullptr);
				pass.SetViewport(sdl3::GpuViewport{0.f, 0.f, float(mipSize), float(mipSize), 0.f, 1.f});
				pass.BindPipeline(pipeline);
				sdl3::GpuBufferBinding binding{vertexBuffer.Get(), 0};
				pass.BindVertexBuffer(0, binding);
				cmd.PushVertexUniformData(0, CubeFaceViewProjection(face));
				cmd.PushFragmentUniformData(0, PrefilterUBO{roughness});
				pass.BindFragmentSamplers(0, {&sourceSampler, 1});
				pass.DrawPrimitives(vertexCount);
			}
			if (!cmd.Submit())
				return Err(String(sdl3::GetError()));
		}
	}
	return Ok(true);
}

} // namespace detail

Result<Environment, String> LoadEnvironmentFromHdr(sdl3::GpuDevice &device, const String &path, uint32_t cubemapSize,
		uint32_t irradianceSize, uint32_t prefilterBaseSize, uint32_t brdfLutSize) {
	auto hdrResult = LoadHdrEquirectangular(path);
	if (!hdrResult)
		return Err(std::move(hdrResult.Error()));

	auto equirectResult = UploadEquirectTexture(device, hdrResult.Value());
	if (!equirectResult)
		return Err(std::move(equirectResult.Error()));
	sdl3::GpuTexture equirectTexture = std::move(equirectResult.Value());

	sdl3::GpuSamplerCreateInfo equirectSamplerInfo{};
	equirectSamplerInfo.min_filter = sdl3::gpu_filter::LINEAR;
	equirectSamplerInfo.mag_filter = sdl3::gpu_filter::LINEAR;
	equirectSamplerInfo.mipmap_mode = sdl3::gpu_sampler_mipmap_mode::LINEAR;
	equirectSamplerInfo.address_mode_u = sdl3::gpu_sampler_address_mode::REPEAT; // longitude, boucle
	equirectSamplerInfo.address_mode_v = sdl3::gpu_sampler_address_mode::CLAMP_TO_EDGE; // latitude, pôles
	equirectSamplerInfo.address_mode_w = sdl3::gpu_sampler_address_mode::CLAMP_TO_EDGE;
	auto equirectSamplerResult = device.CreateSampler(equirectSamplerInfo);
	if (!equirectSamplerResult)
		return Err(String(equirectSamplerResult.Error()));
	sdl3::GpuSampler equirectSampler = std::move(equirectSamplerResult.Value());

	sdl3::GpuTextureFormat cubemapFormat = ResolveHdrColorFormat(device);
	auto cubemapResult = CreateEnvironmentCubemap(device, cubemapSize, cubemapFormat);
	if (!cubemapResult)
		return Err(String(cubemapResult.Error()));
	sdl3::GpuTexture cubemap = std::move(cubemapResult.Value());

	auto equirectProgramResult = BuildEquirectToCubemapShaderProgram(device);
	if (!equirectProgramResult)
		return Err(std::move(equirectProgramResult.Error()));
	ShaderProgram equirectProgram = std::move(equirectProgramResult.Value());
	auto equirectPipelineResult = CreateCubeCapturePipeline(device, equirectProgram, cubemapFormat);
	if (!equirectPipelineResult)
		return Err(String(equirectPipelineResult.Error()));
	sdl3::GpuGraphicsPipeline equirectPipeline = std::move(equirectPipelineResult.Value());

	std::vector<Vertex3D> cubeVerts = CubeVertices();
	uint32_t cubeBytes = uint32_t(cubeVerts.size() * sizeof(Vertex3D));
	auto vbResult = device.CreateBuffer(sdl3::gpu_buffer_usage::VERTEX, cubeBytes);
	if (!vbResult)
		return Err(String(vbResult.Error()));
	sdl3::GpuBuffer vertexBuffer = std::move(vbResult.Value());
	{
		auto transferResult = device.CreateTransferBuffer(sdl3::gpu_transfer_buffer_usage::UPLOAD, cubeBytes);
		if (!transferResult)
			return Err(String(transferResult.Error()));
		sdl3::GpuTransferBuffer transfer = std::move(transferResult.Value());
		{
			auto mapped = transfer.Map(false);
			if (!mapped)
				return Err(String("LoadEnvironmentFromHdr: échec du mapping du transfer buffer (sommets)"));
			std::memcpy(mapped.GetData(), cubeVerts.data(), cubeBytes);
		}
		auto uploadCmd = device.AcquireCommandBuffer();
		{
			sdl3::GpuCopyPass copyPass = uploadCmd.BeginCopyPass();
			sdl3::GpuTransferBufferLocation src{transfer.Get(), 0};
			sdl3::GpuBufferRegion dst{vertexBuffer.Get(), 0, cubeBytes};
			copyPass.UploadToBuffer(src, dst, false);
		}
		if (!uploadCmd.Submit())
			return Err(String(sdl3::GetError()));
	}
	uint32_t vertexCount = uint32_t(cubeVerts.size());

	sdl3::GpuTextureSamplerBinding equirectBinding{equirectTexture.Get(), equirectSampler.Get()};
	auto equirectRender =
		detail::RenderCubeFaces(device, equirectPipeline, vertexBuffer, vertexCount, equirectBinding, cubemap, cubemapSize);
	if (!equirectRender)
		return Err(std::move(equirectRender.Error()));
	if (!device.WaitIdle())
		return Err(String(sdl3::GetError()));

	// Sampler pour échantillonner `cubemap` en tant que SOURCE de la
	// convolution d'irradiance (distinct du sampler de sortie du skybox,
	// même si les paramètres sont identiques — vies indépendantes).
	sdl3::GpuSamplerCreateInfo cubemapSourceSamplerInfo{};
	cubemapSourceSamplerInfo.min_filter = sdl3::gpu_filter::LINEAR;
	cubemapSourceSamplerInfo.mag_filter = sdl3::gpu_filter::LINEAR;
	cubemapSourceSamplerInfo.mipmap_mode = sdl3::gpu_sampler_mipmap_mode::LINEAR;
	cubemapSourceSamplerInfo.address_mode_u = sdl3::gpu_sampler_address_mode::CLAMP_TO_EDGE;
	cubemapSourceSamplerInfo.address_mode_v = sdl3::gpu_sampler_address_mode::CLAMP_TO_EDGE;
	cubemapSourceSamplerInfo.address_mode_w = sdl3::gpu_sampler_address_mode::CLAMP_TO_EDGE;
	auto cubemapSourceSamplerResult = device.CreateSampler(cubemapSourceSamplerInfo);
	if (!cubemapSourceSamplerResult)
		return Err(String(cubemapSourceSamplerResult.Error()));
	sdl3::GpuSampler cubemapSourceSampler = std::move(cubemapSourceSamplerResult.Value());

	auto irradianceResult = CreateEnvironmentCubemap(device, irradianceSize, cubemapFormat);
	if (!irradianceResult)
		return Err(String(irradianceResult.Error()));
	sdl3::GpuTexture irradianceCubemap = std::move(irradianceResult.Value());

	auto irradianceProgramResult = BuildIrradianceConvolutionShaderProgram(device);
	if (!irradianceProgramResult)
		return Err(std::move(irradianceProgramResult.Error()));
	ShaderProgram irradianceProgram = std::move(irradianceProgramResult.Value());
	auto irradiancePipelineResult = CreateCubeCapturePipeline(device, irradianceProgram, cubemapFormat);
	if (!irradiancePipelineResult)
		return Err(String(irradiancePipelineResult.Error()));
	sdl3::GpuGraphicsPipeline irradiancePipeline = std::move(irradiancePipelineResult.Value());

	sdl3::GpuTextureSamplerBinding cubemapBinding{cubemap.Get(), cubemapSourceSampler.Get()};
	auto irradianceRender = detail::RenderCubeFaces(device, irradiancePipeline, vertexBuffer, vertexCount,
													 cubemapBinding, irradianceCubemap, irradianceSize);
	if (!irradianceRender)
		return Err(std::move(irradianceRender.Error()));
	if (!device.WaitIdle())
		return Err(String(sdl3::GetError()));

	// ── M13 : préfiltrage spéculaire (mips par rugosité) ────────────────────
	auto prefilteredResult =
		CreateEnvironmentCubemap(device, prefilterBaseSize, cubemapFormat, PREFILTER_MIP_LEVELS);
	if (!prefilteredResult)
		return Err(String(prefilteredResult.Error()));
	sdl3::GpuTexture prefilteredCubemap = std::move(prefilteredResult.Value());

	auto prefilterProgramResult = BuildPrefilterShaderProgram(device);
	if (!prefilterProgramResult)
		return Err(std::move(prefilterProgramResult.Error()));
	ShaderProgram prefilterProgram = std::move(prefilterProgramResult.Value());
	auto prefilterPipelineResult = CreateCubeCapturePipeline(device, prefilterProgram, cubemapFormat);
	if (!prefilterPipelineResult)
		return Err(String(prefilterPipelineResult.Error()));
	sdl3::GpuGraphicsPipeline prefilterPipeline = std::move(prefilterPipelineResult.Value());

	auto prefilterRender = detail::RenderPrefilteredMips(device, prefilterPipeline, vertexBuffer, vertexCount,
														  cubemapBinding, prefilteredCubemap, prefilterBaseSize);
	if (!prefilterRender)
		return Err(std::move(prefilterRender.Error()));
	if (!device.WaitIdle())
		return Err(String(sdl3::GetError()));

	// ── M13 : BRDF LUT (quad plein cadre, pas de cube) ──────────────────────
	sdl3::GpuTextureFormat brdfLutFormat = ResolveBrdfLutFormat(device);
	auto brdfLutTextureResult = CreateBrdfLutTexture(device, brdfLutSize, brdfLutFormat);
	if (!brdfLutTextureResult)
		return Err(String(brdfLutTextureResult.Error()));
	sdl3::GpuTexture brdfLut = std::move(brdfLutTextureResult.Value());

	auto brdfLutProgramResult = BuildBrdfLutShaderProgram(device);
	if (!brdfLutProgramResult)
		return Err(std::move(brdfLutProgramResult.Error()));
	ShaderProgram brdfLutProgram = std::move(brdfLutProgramResult.Value());
	auto brdfLutPipelineResult = CreateBrdfLutPipeline(device, brdfLutProgram, brdfLutFormat);
	if (!brdfLutPipelineResult)
		return Err(String(brdfLutPipelineResult.Error()));
	sdl3::GpuGraphicsPipeline brdfLutPipeline = std::move(brdfLutPipelineResult.Value());

	std::vector<Vertex3D> lutQuad = BrdfLutQuadVertices();
	uint32_t lutQuadBytes = uint32_t(lutQuad.size() * sizeof(Vertex3D));
	auto lutVbResult = device.CreateBuffer(sdl3::gpu_buffer_usage::VERTEX, lutQuadBytes);
	if (!lutVbResult)
		return Err(String(lutVbResult.Error()));
	sdl3::GpuBuffer lutVertexBuffer = std::move(lutVbResult.Value());
	{
		auto transferResult = device.CreateTransferBuffer(sdl3::gpu_transfer_buffer_usage::UPLOAD, lutQuadBytes);
		if (!transferResult)
			return Err(String(transferResult.Error()));
		sdl3::GpuTransferBuffer transfer = std::move(transferResult.Value());
		{
			auto mapped = transfer.Map(false);
			if (!mapped)
				return Err(String("LoadEnvironmentFromHdr: échec du mapping du transfer buffer (quad BRDF LUT)"));
			std::memcpy(mapped.GetData(), lutQuad.data(), lutQuadBytes);
		}
		auto uploadCmd = device.AcquireCommandBuffer();
		{
			sdl3::GpuCopyPass copyPass = uploadCmd.BeginCopyPass();
			sdl3::GpuTransferBufferLocation src{transfer.Get(), 0};
			sdl3::GpuBufferRegion dst{lutVertexBuffer.Get(), 0, lutQuadBytes};
			copyPass.UploadToBuffer(src, dst, false);
		}
		if (!uploadCmd.Submit())
			return Err(String(sdl3::GetError()));
	}

	{
		auto cmd = device.AcquireCommandBuffer();
		sdl3::GpuColorTargetInfo colorTarget{};
		colorTarget.texture = brdfLut.Get();
		colorTarget.load_op = sdl3::gpu_load_op::DONT_CARE;
		colorTarget.store_op = sdl3::gpu_store_op::STORE;
		{
			sdl3::GpuRenderPass pass = cmd.BeginRenderPass(colorTarget, nullptr);
			pass.SetViewport(sdl3::GpuViewport{0.f, 0.f, float(brdfLutSize), float(brdfLutSize), 0.f, 1.f});
			pass.BindPipeline(brdfLutPipeline);
			sdl3::GpuBufferBinding binding{lutVertexBuffer.Get(), 0};
			pass.BindVertexBuffer(0, binding);
			pass.DrawPrimitives(uint32_t(lutQuad.size()));
		}
		if (!cmd.Submit())
			return Err(String(sdl3::GetError()));
	}
	if (!device.WaitIdle())
		return Err(String(sdl3::GetError()));

	return Ok(Environment{std::move(cubemap), std::move(irradianceCubemap), std::move(prefilteredCubemap),
						  std::move(brdfLut)});
}

} // namespace render3d
