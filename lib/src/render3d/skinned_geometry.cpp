// Définitions de render3d/skinned_geometry.hpp — fichier généré par splitter.py : le code
// vient tel quel de l'en-tête (seules les signatures sont réécrites).

#include "render3d/skinned_geometry.hpp"

namespace render3d {

// ── SkinnedGeometry ──────────────────────────────────────────────────────────

Result<bool, StringView> SkinnedGeometry::EnsureGpuBuffers(sdl3::GpuDevice &device, sdl3::GpuCommandBuffer &cmd) {
	if (!m_dirty && HasGpuBuffers())
		return Ok(true);
	if (m_vertices.empty() || m_indices.empty())
		return Err(StringView("SkinnedGeometry::EnsureGpuBuffers: empty mesh"));

	auto vertexBytes = uint32_t(m_vertices.size() * sizeof(SkinnedVertex3D));
	auto indexBytes = uint32_t(m_indices.size() * sizeof(uint32_t));

	auto vertexBufferResult = device.CreateBuffer(sdl3::gpu_buffer_usage::VERTEX, vertexBytes);
	if (!vertexBufferResult)
		return Err(vertexBufferResult.Error());

	auto indexBufferResult = device.CreateBuffer(sdl3::gpu_buffer_usage::INDEX, indexBytes);
	if (!indexBufferResult)
		return Err(indexBufferResult.Error());

	auto transferResult =
		device.CreateTransferBuffer(sdl3::gpu_transfer_buffer_usage::UPLOAD, vertexBytes + indexBytes);
	if (!transferResult)
		return Err(transferResult.Error());
	sdl3::GpuTransferBuffer transfer = std::move(transferResult.Value());

	{
		auto mapped = transfer.Map(false);
		if (!mapped)
			return Err(StringView("SkinnedGeometry::EnsureGpuBuffers: GpuTransferBuffer::Map failed"));
		std::memcpy(mapped.GetData(), m_vertices.data(), vertexBytes);
		std::memcpy(static_cast<uint8_t *>(mapped.GetData()) + vertexBytes, m_indices.data(), indexBytes);
	}

	m_vertexBuffer = Some(std::move(vertexBufferResult.Value()));
	m_indexBuffer = Some(std::move(indexBufferResult.Value()));

	{
		sdl3::GpuCopyPass copyPass = cmd.BeginCopyPass();
		sdl3::GpuTransferBufferLocation srcVertex{transfer.Get(), 0};
		sdl3::GpuBufferRegion dstVertex{m_vertexBuffer.Value().Get(), 0, vertexBytes};
		copyPass.UploadToBuffer(srcVertex, dstVertex, false);

		sdl3::GpuTransferBufferLocation srcIndex{transfer.Get(), vertexBytes};
		sdl3::GpuBufferRegion dstIndex{m_indexBuffer.Value().Get(), 0, indexBytes};
		copyPass.UploadToBuffer(srcIndex, dstIndex, false);
	}

	m_dirty = false;
	return Ok(true);
}

} // namespace render3d
