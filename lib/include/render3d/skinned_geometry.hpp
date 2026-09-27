#pragma once
#include <cstring>
#include <vector>

#include "../core/core.hpp"
#include "../sdl3/gpu.hpp"
#include "vertex.hpp"

namespace render3d {

// Maillage CPU skinné (SkinnedVertex3D, voir vertex.hpp) + upload GPU
// paresseux — même structure que Mesh (mesh.hpp) mais pour SkinnedVertex3D,
// dupliquée plutôt que généralisée (templater Mesh<VertexT> pour un seul
// autre usage n'apporterait rien ici, voir le principe du projet contre
// l'abstraction prématurée). Pas de Mesh::Group : un SkinnedMesh (M17,
// skinned_mesh.hpp) dessine toujours l'intégralité de sa géométrie.
class SkinnedGeometry {
	std::vector<SkinnedVertex3D> m_vertices;
	std::vector<uint32_t> m_indices;
	bool m_dirty = true;

	Option<sdl3::GpuBuffer> m_vertexBuffer = NONE;
	Option<sdl3::GpuBuffer> m_indexBuffer = NONE;

public:
	SkinnedGeometry() = default;
	SkinnedGeometry(std::vector<SkinnedVertex3D> vertices, std::vector<uint32_t> indices) noexcept
		: m_vertices(std::move(vertices)), m_indices(std::move(indices)) {}

	[[nodiscard]] const std::vector<SkinnedVertex3D> &Vertices() const noexcept { return m_vertices; }
	[[nodiscard]] const std::vector<uint32_t> &Indices() const noexcept { return m_indices; }
	[[nodiscard]] uint32_t IndexCount() const noexcept { return uint32_t(m_indices.size()); }

	void MarkDirty() noexcept { m_dirty = true; }
	[[nodiscard]] bool HasGpuBuffers() const noexcept { return m_vertexBuffer.IsSome() && m_indexBuffer.IsSome(); }

	[[nodiscard]] Result<bool, StringView> EnsureGpuBuffers(sdl3::GpuDevice &device, sdl3::GpuCommandBuffer &cmd);

	[[nodiscard]] Ref<sdl3::GpuBuffer> VertexBuffer() const noexcept { return MakeRef(m_vertexBuffer.Value()); }
	[[nodiscard]] Ref<sdl3::GpuBuffer> IndexBuffer() const noexcept { return MakeRef(m_indexBuffer.Value()); }
};

} // namespace render3d
