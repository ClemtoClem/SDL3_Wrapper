// Définitions de sdl3/gpu.hpp — fichier généré par splitter.py : le code
// vient tel quel de l'en-tête (seules les signatures sont réécrites).

#include "sdl3/gpu.hpp"

namespace sdl3 {

// ── GpuBuffer ────────────────────────────────────────────────────────────────

void GpuBuffer::SetName(const char *name) {
    if (m_handle && m_device)
        SDL_SetGPUBufferName(m_device, m_handle, name);
}

// ── GpuMappedBuffer ──────────────────────────────────────────────────────────

GpuMappedBuffer::~GpuMappedBuffer() {
    if (ptr && m_device && buf)
        SDL_UnmapGPUTransferBuffer(m_device, buf);
}

// ── GpuTransferBuffer ────────────────────────────────────────────────────────

GpuMappedBuffer GpuTransferBuffer::Map(bool cycle) noexcept {
    return GpuMappedBuffer(m_device, m_handle, cycle);
}

// ── GpuTexture ───────────────────────────────────────────────────────────────

void GpuTexture::SetName(const char *name) {
    if (m_handle && m_device)
        SDL_SetGPUTextureName(m_device, m_handle, name);
}

// ── GpuComputePipeline ───────────────────────────────────────────────────────

GpuComputePipeline::~GpuComputePipeline() {
    if (m_handle && m_device)
        SDL_ReleaseGPUComputePipeline(m_device, m_handle);
}

// ── GpuRenderPass ────────────────────────────────────────────────────────────

void GpuRenderPass::End() noexcept {
    if (m_handle) {
        SDL_EndGPURenderPass(m_handle);
        m_handle = nullptr;
    }
}

void GpuRenderPass::BindPipeline(const GpuGraphicsPipeline &p) noexcept {
    if (m_handle)
        SDL_BindGPUGraphicsPipeline(m_handle, p.Get());
}

void GpuRenderPass::SetViewport(const GpuViewport &vp) noexcept {
    if (m_handle)
        SDL_SetGPUViewport(m_handle, &vp);
}

void GpuRenderPass::SetScissor(const SDL_Rect &r) noexcept {
    if (m_handle)
        SDL_SetGPUScissor(m_handle, &r);
}

void GpuRenderPass::SetBlendConstants(SDL_FColor color) noexcept {
    if (m_handle)
        SDL_SetGPUBlendConstants(m_handle, color);
}

void GpuRenderPass::SetStencilReference(uint8_t ref) noexcept {
    if (m_handle)
        SDL_SetGPUStencilReference(m_handle, ref);
}

void GpuRenderPass::BindVertexBuffers(uint32_t firstSlot, std::span<const GpuBufferBinding> bindings) noexcept {
    if (m_handle)
        SDL_BindGPUVertexBuffers(m_handle, firstSlot, bindings.data(), uint32_t(bindings.size()));
}

void GpuRenderPass::BindVertexBuffer(uint32_t slot, const GpuBufferBinding &binding) noexcept {
    BindVertexBuffers(slot, {&binding, 1});
}

void GpuRenderPass::BindIndexBuffer(const GpuBufferBinding &binding, GpuIndexElementSize elemSize) noexcept {
    if (m_handle)
        SDL_BindGPUIndexBuffer(m_handle, &binding, elemSize);
}

void GpuRenderPass::BindVertexSamplers(uint32_t firstSlot, std::span<const GpuTextureSamplerBinding> bindings) noexcept {
    if (m_handle)
        SDL_BindGPUVertexSamplers(m_handle, firstSlot, bindings.data(), uint32_t(bindings.size()));
}

void GpuRenderPass::BindVertexStorageTextures(uint32_t firstSlot, std::span<const Ref<GpuTexture>> textures) {
    if (!m_handle)
        return;
    auto raw = ::detail::ToRawHandles(textures);
    SDL_BindGPUVertexStorageTextures(m_handle, firstSlot, raw.data(), uint32_t(raw.size()));
}

void GpuRenderPass::BindVertexStorageBuffers(uint32_t firstSlot, std::span<const Ref<GpuBuffer>> buffers) {
    if (!m_handle)
        return;
    auto raw = ::detail::ToRawHandles(buffers);
    SDL_BindGPUVertexStorageBuffers(m_handle, firstSlot, raw.data(), uint32_t(raw.size()));
}

void GpuRenderPass::BindFragmentSamplers(uint32_t firstSlot, std::span<const GpuTextureSamplerBinding> bindings) noexcept {
    if (m_handle)
        SDL_BindGPUFragmentSamplers(m_handle, firstSlot, bindings.data(), uint32_t(bindings.size()));
}

void GpuRenderPass::BindFragmentStorageTextures(uint32_t firstSlot, std::span<const Ref<GpuTexture>> textures) {
    if (!m_handle)
        return;
    auto raw = ::detail::ToRawHandles(textures);
    SDL_BindGPUFragmentStorageTextures(m_handle, firstSlot, raw.data(), uint32_t(raw.size()));
}

void GpuRenderPass::BindFragmentStorageBuffers(uint32_t firstSlot, std::span<const Ref<GpuBuffer>> buffers) {
    if (!m_handle)
        return;
    auto raw = ::detail::ToRawHandles(buffers);
    SDL_BindGPUFragmentStorageBuffers(m_handle, firstSlot, raw.data(), uint32_t(raw.size()));
}

void GpuRenderPass::DrawPrimitives(uint32_t numVertices, uint32_t numInstances, uint32_t firstVertex,
		uint32_t firstInstance) noexcept {
    if (m_handle)
        SDL_DrawGPUPrimitives(m_handle, numVertices, numInstances, firstVertex, firstInstance);
}

void GpuRenderPass::DrawIndexedPrimitives(uint32_t numIndices, uint32_t numInstances, uint32_t firstIndex,
		int32_t vertexOffset, uint32_t firstInstance) noexcept {
    if (m_handle)
        SDL_DrawGPUIndexedPrimitives(m_handle, numIndices, numInstances, firstIndex, vertexOffset, firstInstance);
}

void GpuRenderPass::DrawPrimitivesIndirect(const GpuBuffer &buf, uint32_t offset, uint32_t drawCount) noexcept {
    if (m_handle)
        SDL_DrawGPUPrimitivesIndirect(m_handle, buf.Get(), offset, drawCount);
}

void GpuRenderPass::DrawIndexedPrimitivesIndirect(const GpuBuffer &buf, uint32_t offset, uint32_t drawCount) noexcept {
    if (m_handle)
        SDL_DrawGPUIndexedPrimitivesIndirect(m_handle, buf.Get(), offset, drawCount);
}

// ── GpuComputePass ───────────────────────────────────────────────────────────

void GpuComputePass::End() noexcept {
    if (m_handle) {
        SDL_EndGPUComputePass(m_handle);
        m_handle = nullptr;
    }
}

void GpuComputePass::BindPipeline(const GpuComputePipeline &p) noexcept {
    if (m_handle)
        SDL_BindGPUComputePipeline(m_handle, p.Get());
}

void GpuComputePass::BindSamplers(uint32_t firstSlot, std::span<const GpuTextureSamplerBinding> bindings) noexcept {
    if (m_handle)
        SDL_BindGPUComputeSamplers(m_handle, firstSlot, bindings.data(), uint32_t(bindings.size()));
}

void GpuComputePass::BindStorageTextures(uint32_t firstSlot, std::span<const Ref<GpuTexture>> textures) {
    if (!m_handle)
        return;
    auto raw = ::detail::ToRawHandles(textures);
    SDL_BindGPUComputeStorageTextures(m_handle, firstSlot, raw.data(), uint32_t(raw.size()));
}

void GpuComputePass::BindStorageBuffers(uint32_t firstSlot, std::span<const Ref<GpuBuffer>> buffers) {
    if (!m_handle)
        return;
    auto raw = ::detail::ToRawHandles(buffers);
    SDL_BindGPUComputeStorageBuffers(m_handle, firstSlot, raw.data(), uint32_t(raw.size()));
}

void GpuComputePass::Dispatch(uint32_t groupX, uint32_t groupY, uint32_t groupZ) noexcept {
    if (m_handle)
        SDL_DispatchGPUCompute(m_handle, groupX, groupY, groupZ);
}

void GpuComputePass::DispatchIndirect(const GpuBuffer &buf, uint32_t offset) noexcept {
    if (m_handle)
        SDL_DispatchGPUComputeIndirect(m_handle, buf.Get(), offset);
}

// ── GpuCopyPass ──────────────────────────────────────────────────────────────

void GpuCopyPass::End() noexcept {
    if (m_handle) {
        SDL_EndGPUCopyPass(m_handle);
        m_handle = nullptr;
    }
}

void GpuCopyPass::UploadToTexture(const GpuTextureTransferInfo &src, const GpuTextureRegion &dst, bool cycle) noexcept {
    if (m_handle)
        SDL_UploadToGPUTexture(m_handle, &src, &dst, cycle);
}

void GpuCopyPass::UploadToBuffer(const GpuTransferBufferLocation &src, const GpuBufferRegion &dst, bool cycle) noexcept {
    if (m_handle)
        SDL_UploadToGPUBuffer(m_handle, &src, &dst, cycle);
}

void GpuCopyPass::CopyTextureToTexture(const GpuTextureLocation &src, const GpuTextureLocation &dst, uint32_t w, uint32_t h,
		uint32_t d, bool cycle) noexcept {
    if (m_handle)
        SDL_CopyGPUTextureToTexture(m_handle, &src, &dst, w, h, d, cycle);
}

void GpuCopyPass::CopyBufferToBuffer(const GpuBufferLocation &src, const GpuBufferLocation &dst, uint32_t size,
		bool cycle) noexcept {
    if (m_handle)
        SDL_CopyGPUBufferToBuffer(m_handle, &src, &dst, size, cycle);
}

void GpuCopyPass::DownloadFromTexture(const GpuTextureRegion &src, const GpuTextureTransferInfo &dst) noexcept {
    if (m_handle)
        SDL_DownloadFromGPUTexture(m_handle, &src, &dst);
}

void GpuCopyPass::DownloadFromBuffer(const GpuBufferRegion &src, const GpuTransferBufferLocation &dst) noexcept {
    if (m_handle)
        SDL_DownloadFromGPUBuffer(m_handle, &src, &dst);
}

void CancelGPUCommandBuffer(SDL_GPUDevice *d, SDL_GPUCommandBuffer* b) {
    (void)d;
    SDL_CancelGPUCommandBuffer(b);
}

// ── GpuCommandBuffer ─────────────────────────────────────────────────────────

void GpuCommandBuffer::InsertDebugLabel(const char *label) noexcept {
    if (m_handle)
        SDL_InsertGPUDebugLabel(m_handle, label);
}

void GpuCommandBuffer::PushDebugGroup(const char *name) noexcept {
    if (m_handle)
        SDL_PushGPUDebugGroup(m_handle, name);
}

void GpuCommandBuffer::PopDebugGroup() noexcept {
    if (m_handle)
        SDL_PopGPUDebugGroup(m_handle);
}

GpuRenderPass GpuCommandBuffer::BeginRenderPass(std::span<const GpuColorTargetInfo> colorTargets,
		const GpuDepthStencilTargetInfo *depthStencil) noexcept {
    if (!m_handle)
        return {};
    return GpuRenderPass(
        SDL_BeginGPURenderPass(m_handle, colorTargets.data(), uint32_t(colorTargets.size()), depthStencil));
}

GpuRenderPass GpuCommandBuffer::BeginRenderPass(const GpuColorTargetInfo &colorTarget,
		const GpuDepthStencilTargetInfo *depthStencil) noexcept {
    return BeginRenderPass({&colorTarget, 1}, depthStencil);
}

GpuComputePass GpuCommandBuffer::BeginComputePass(std::span<const GpuStorageTextureReadWriteBinding> storageTextures,
		std::span<const GpuStorageBufferReadWriteBinding> storageBuffers) noexcept {
    if (!m_handle)
        return {};
    return GpuComputePass(SDL_BeginGPUComputePass(m_handle, storageTextures.data(),
                                                  uint32_t(storageTextures.size()), storageBuffers.data(),
                                                  uint32_t(storageBuffers.size())));
}

GpuCopyPass GpuCommandBuffer::BeginCopyPass() noexcept {
    if (!m_handle)
        return {};
    return GpuCopyPass(SDL_BeginGPUCopyPass(m_handle));
}

void GpuCommandBuffer::GenerateMipmaps(const GpuTexture &tex) noexcept {
    if (m_handle)
        SDL_GenerateMipmapsForGPUTexture(m_handle, tex.Get());
}

void GpuCommandBuffer::BlitTexture(const GpuBlitInfo &info) noexcept {
    if (m_handle)
        SDL_BlitGPUTexture(m_handle, &info);
}

Option<Borrowed<SDL_GPUTexture>> GpuCommandBuffer::AcquireSwapchainTexture(Ref<Window> win, uint32_t *outW,
		uint32_t *outH) noexcept {
    if (!m_handle)
        return NONE;
    SDL_GPUTexture *tex = nullptr;
    SDL_AcquireGPUSwapchainTexture(m_handle, win->Get(), &tex, outW, outH);
    if (!tex)
        return NONE;
    return Some(Borrowed<SDL_GPUTexture>(tex));
}

Option<Borrowed<SDL_GPUTexture>> GpuCommandBuffer::WaitAndAcquireSwapchainTexture(Ref<Window> win, uint32_t *outW, uint32_t *outH) noexcept {
    if (!m_handle)
        return NONE;
    SDL_GPUTexture *tex = nullptr;
    SDL_WaitAndAcquireGPUSwapchainTexture(m_handle, win->Get(), &tex, outW, outH);
    if (!tex)
        return NONE;
    return Some(Borrowed<SDL_GPUTexture>(tex));
}

bool GpuCommandBuffer::Submit() noexcept {
    if (!m_handle)
        return false;
    bool ok = SDL_SubmitGPUCommandBuffer(m_handle);
    m_handle = nullptr;
    return ok;
}

Option<GpuFence> GpuCommandBuffer::SubmitAndAcquireFence() noexcept {
    if (!m_handle)
        return NONE;
    SDL_GPUFence *fence = SDL_SubmitGPUCommandBufferAndAcquireFence(m_handle);
    m_handle = nullptr;
    if (!fence)
        return NONE;
    return Some(GpuFence(m_device, fence));
}

bool GpuCommandBuffer::Cancel() noexcept {
    if (!m_handle)
        return false;
    bool ok = SDL_CancelGPUCommandBuffer(m_handle);
    m_handle = nullptr;
    return ok;
}

// ── GpuDevice ────────────────────────────────────────────────────────────────

Result<GpuDevice, Error> GpuDevice::Create(SDL_GPUShaderFormat formats, bool debugMode, const char *driverName) {
    auto *d = SDL_CreateGPUDevice(formats, debugMode, driverName);
    if (!d)
        return Err(GetError());
    return Ok(GpuDevice(d));
}

Result<GpuDevice, StringView> GpuDevice::CreateWithProperties(SDL_PropertiesID props) {
    auto *d = SDL_CreateGPUDeviceWithProperties(props);
    if (!d)
        return Err(GetError());
    return Ok(GpuDevice(d));
}

SDL_GPUShaderFormat GpuDevice::ShaderFormats() const noexcept {
    return m_handle ? SDL_GetGPUShaderFormats(m_handle) : SDL_GPUShaderFormat(SDL_GPU_SHADERFORMAT_INVALID);
}

SDL_PropertiesID GpuDevice::Properties() const noexcept {
    return m_handle ? SDL_GetGPUDeviceProperties(m_handle) : 0;
}

bool GpuDevice::TextureSupportsFormat(SDL_GPUTextureFormat format, SDL_GPUTextureType type,
		SDL_GPUTextureUsageFlags usage) const noexcept {
    return m_handle && SDL_GPUTextureSupportsFormat(m_handle, format, type, usage);
}

bool GpuDevice::TextureSupportsSampleCount(SDL_GPUTextureFormat format, SDL_GPUSampleCount count) const noexcept {
    return m_handle && SDL_GPUTextureSupportsSampleCount(m_handle, format, count);
}

bool GpuDevice::ClaimWindow(Ref<Window> win) noexcept {
    return m_handle && SDL_ClaimWindowForGPUDevice(m_handle, win->Get());
}

void GpuDevice::ReleaseWindow(Ref<Window> win) noexcept {
    if (m_handle)
        SDL_ReleaseWindowFromGPUDevice(m_handle, win->Get());
}

bool GpuDevice::SetSwapchainParameters(Ref<Window> win, SDL_GPUSwapchainComposition composition,
		SDL_GPUPresentMode presentMode) noexcept {
    return m_handle && SDL_SetGPUSwapchainParameters(m_handle, win->Get(), composition, presentMode);
}

bool GpuDevice::SetAllowedFramesInFlight(uint32_t n) noexcept {
    return m_handle && SDL_SetGPUAllowedFramesInFlight(m_handle, n);
}

SDL_GPUTextureFormat GpuDevice::SwapchainTextureFormat(Ref<Window> win) const noexcept {
    return m_handle ? SDL_GetGPUSwapchainTextureFormat(m_handle, win->Get()) : SDL_GPU_TEXTUREFORMAT_INVALID;
}

bool GpuDevice::WindowSupportsComposition(Ref<Window> win, SDL_GPUSwapchainComposition c) const noexcept {
    return m_handle && SDL_WindowSupportsGPUSwapchainComposition(m_handle, win->Get(), c);
}

bool GpuDevice::WindowSupportsPresentMode(Ref<Window> win, SDL_GPUPresentMode mode) const noexcept {
    return m_handle && SDL_WindowSupportsGPUPresentMode(m_handle, win->Get(), mode);
}

bool GpuDevice::WaitForSwapchain(Ref<Window> win) noexcept {
    return m_handle && SDL_WaitForGPUSwapchain(m_handle, win->Get());
}

GpuCommandBuffer GpuDevice::AcquireCommandBuffer() noexcept {
    if (!m_handle)
        return {};
    return GpuCommandBuffer(m_handle, SDL_AcquireGPUCommandBuffer(m_handle));
}

bool GpuDevice::WaitForFences(bool waitAll, std::span<const Ref<GpuFence>> fences) {
    if (!m_handle)
        return false;
    auto raw = ::detail::ToRawHandles(fences);
    return SDL_WaitForGPUFences(m_handle, waitAll, raw.data(), uint32_t(raw.size()));
}

Result<GpuBuffer, StringView> GpuDevice::CreateBuffer(const GpuBufferCreateInfo &info) {
    auto *b = SDL_CreateGPUBuffer(m_handle, &info);
    if (!b)
        return Err(GetError());
    return Ok(GpuBuffer(m_handle, b));
}

Result<GpuBuffer, StringView> GpuDevice::CreateBuffer(SDL_GPUBufferUsageFlags usage, uint32_t size) {
    GpuBufferCreateInfo info{};
    info.usage = usage;
    info.size = size;
    return CreateBuffer(info);
}

Result<GpuTransferBuffer, StringView> GpuDevice::CreateTransferBuffer(const GpuTransferBufferCreateInfo &info) {
    auto *b = SDL_CreateGPUTransferBuffer(m_handle, &info);
    if (!b)
        return Err(GetError());
    return Ok(GpuTransferBuffer(m_handle, b));
}

Result<GpuTransferBuffer, StringView> GpuDevice::CreateTransferBuffer(SDL_GPUTransferBufferUsage usage, uint32_t size) {
    GpuTransferBufferCreateInfo info{};
    info.usage = usage;
    info.size = size;
    return CreateTransferBuffer(info);
}

Result<GpuTexture, StringView> GpuDevice::CreateTexture(const GpuTextureCreateInfo &info) {
    auto *t = SDL_CreateGPUTexture(m_handle, &info);
    if (!t)
        return Err(GetError());
    return Ok(GpuTexture(m_handle, t));
}

Result<GpuSampler, StringView> GpuDevice::CreateSampler(const GpuSamplerCreateInfo &info) {
    auto *s = SDL_CreateGPUSampler(m_handle, &info);
    if (!s)
        return Err(GetError());
    return Ok(GpuSampler(m_handle, s));
}

Result<GpuShader, StringView> GpuDevice::CreateShader(const GpuShaderCreateInfo &info) {
    auto *s = SDL_CreateGPUShader(m_handle, &info);
    if (!s)
        return Err(GetError());
    return Ok(GpuShader(m_handle, s));
}

Result<GpuGraphicsPipeline, StringView> GpuDevice::CreateGraphicsPipeline(const GpuGraphicsPipelineCreateInfo &info) {
    auto *p = SDL_CreateGPUGraphicsPipeline(m_handle, &info);
    if (!p)
        return Err(GetError());
    return Ok(GpuGraphicsPipeline(m_handle, p));
}

Result<GpuComputePipeline, StringView> GpuDevice::CreateComputePipeline(const GpuComputePipelineCreateInfo &info) {
    auto *p = SDL_CreateGPUComputePipeline(m_handle, &info);
    if (!p)
        return Err(GetError());
    return Ok(GpuComputePipeline(m_handle, p));
}

Result<GpuDevice, StringView> Renderer::GetGPUDevice() const {
    SDL_GPUDevice *gpu = SDL_GetGPURendererDevice(m_handle);
    if (!gpu)
        return Err(GetError());
    return Ok(GpuDevice(gpu));
}

bool GpuSupportsShaderFormats(SDL_GPUShaderFormat formats, const char *name) noexcept {
    return SDL_GPUSupportsShaderFormats(formats, name);
}

bool GpuSupportsProperties(SDL_PropertiesID props) noexcept {
    return SDL_GPUSupportsProperties(props);
}

uint32_t GpuTexelBlockSize(SDL_GPUTextureFormat fmt) noexcept {
    return SDL_GPUTextureFormatTexelBlockSize(fmt);
}

uint32_t GpuCalculateTextureSize(SDL_GPUTextureFormat fmt, uint32_t w, uint32_t h, uint32_t depthOrLayers) noexcept {
    return SDL_CalculateGPUTextureFormatSize(fmt, w, h, depthOrLayers);
}

SDL_PixelFormat GpuTextureFormatToPixel(SDL_GPUTextureFormat fmt) noexcept {
    return SDL_GetPixelFormatFromGPUTextureFormat(fmt);
}

SDL_GPUTextureFormat GpuTextureFormatFromPixel(SDL_PixelFormat fmt) noexcept {
    return SDL_GetGPUTextureFormatFromPixelFormat(fmt);
}

} // namespace sdl3
