#pragma once
#include <cmath>
#include <cstring>
#include <span>
#include <vector>

#include "../core/core.hpp"
#include "../sdl3/gpu.hpp"
#include "curve.hpp"
#include "shape2d.hpp"
#include "vertex.hpp"

namespace render3d {

// Maillage CPU (vertices + indices) avec upload GPU paresseux : les buffers ne
// sont (re)créés/(re)uploadés que lorsque le maillage est marqué dirty (à la
// construction, puis après tout appel à MarkDirty()).
class Mesh {
public:
	/// Sous-plage d'indices dessinable avec son propre matériau (three.js
	/// BufferGeometry.groups) — `materialIndex` réfère à l'ordre des
	/// matériaux d'un Shape (voir shape.hpp), pas à quoi que ce soit dans Mesh
	/// lui-même : Mesh ne connaît pas les matériaux, juste la topologie.
	struct Group {
		uint32_t start = 0;
		uint32_t count = 0;
		uint32_t materialIndex = 0;
	};

private:
	std::vector<Vertex3D> m_vertices;
	std::vector<uint32_t> m_indices;
	std::vector<Group> m_groups;
	bool m_dirty = true;
	/// Boîte englobante LOCALE, calculée paresseusement au premier appel de
	/// LocalBounds() puis conservée — la géométrie d'un Mesh ne change pas
	/// après construction dans ce moteur, et le culling de frustum interroge
	/// cette boîte à chaque objet et à chaque image.
	mutable math::FAABB m_localBounds;
	mutable bool m_boundsComputed = false;

	Option<sdl3::GpuBuffer> m_vertexBuffer = NONE;
	Option<sdl3::GpuBuffer> m_indexBuffer = NONE;

public:
	Mesh() = default;
	Mesh(std::vector<Vertex3D> vertices, std::vector<uint32_t> indices) noexcept
		: m_vertices(std::move(vertices)), m_indices(std::move(indices)) {}

	[[nodiscard]] const std::vector<Vertex3D> &Vertices() const noexcept { return m_vertices; }
	[[nodiscard]] const std::vector<uint32_t> &Indices() const noexcept { return m_indices; }
	[[nodiscard]] uint32_t IndexCount() const noexcept { return uint32_t(m_indices.size()); }

	void AddGroup(uint32_t start, uint32_t count, uint32_t materialIndex) {
		m_groups.push_back({start, count, materialIndex});
	}
	[[nodiscard]] const std::vector<Group> &Groups() const noexcept { return m_groups; }

	/// Boîte englobante des sommets, dans l'espace LOCAL du maillage.
	/// Invalide (min > max) pour un maillage vide — `FAABB::IsValid()` le dit,
	/// et le culling laisse alors passer le dessin plutôt que de l'écarter.
	[[nodiscard]] const math::FAABB &LocalBounds() const noexcept {
		if (!m_boundsComputed) {
			m_localBounds = math::FAABB{};
			for (const Vertex3D &vertex : m_vertices)
				m_localBounds.Expand(vertex.position);
			m_boundsComputed = true;
		}
		return m_localBounds;
	}

	void MarkDirty() noexcept {
		m_dirty = true;
		m_boundsComputed = false;
	}
	[[nodiscard]] bool IsDirty() const noexcept { return m_dirty; }
	[[nodiscard]] bool HasGpuBuffers() const noexcept { return m_vertexBuffer.IsSome() && m_indexBuffer.IsSome(); }

	/// Upload paresseux vers le GPU. `cmd` doit être un command buffer qui n'a
	/// PAS encore de render pass actif : cette méthode ouvre elle-même un
	/// GpuCopyPass, incompatible avec un GpuRenderPass déjà ouvert sur le même
	/// command buffer.
	[[nodiscard]] Result<bool, StringView> EnsureGpuBuffers(sdl3::GpuDevice &device, sdl3::GpuCommandBuffer &cmd) {
		if (!m_dirty && HasGpuBuffers())
			return Ok(true);
		if (m_vertices.empty() || m_indices.empty())
			return Err(StringView("Mesh::EnsureGpuBuffers: empty mesh"));

		auto vertexBytes = uint32_t(m_vertices.size() * sizeof(Vertex3D));
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
				return Err(StringView("Mesh::EnsureGpuBuffers: GpuTransferBuffer::Map failed"));
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

	[[nodiscard]] Ref<sdl3::GpuBuffer> VertexBuffer() const noexcept { return MakeRef(m_vertexBuffer.Value()); }
	[[nodiscard]] Ref<sdl3::GpuBuffer> IndexBuffer() const noexcept { return MakeRef(m_indexBuffer.Value()); }

	// ── Primitives procédurales ──────────────────────────────────────────────

	/// Cube d'arête `size`. Cas particulier de `Box()` ci-dessous.
	[[nodiscard]] static Mesh Cube(float size = 1.f) noexcept { return Box(size, size, size); }

	/// Pavé droit de dimensions arbitraires, centré sur l'origine
	/// (équivalent de `BoxGeometry(w, h, d)` de three.js, dont le reste de ce
	/// module est porté — seul `Cube()` uniforme existait, ce qui obligeait
	/// tout appelant voulant un mur, un sol ou une rampe à compenser par une
	/// échelle non uniforme sur le nœud, écrasant du même coup le champ
	/// `scale` que l'utilisateur édite dans un inspecteur).
	///
	/// Même découpage en 6 groupes (+X,-X,+Y,-Y,+Z,-Z) que `Cube()` : un
	/// `Shape` peut donc toujours affecter un `Material` différent par face.
	[[nodiscard]] static Mesh Box(float width, float height, float depth) noexcept {
		const math::FVector3 h{width * 0.5f, height * 0.5f, depth * 0.5f};

		struct Face {
			math::FVector3 normal, u, v; // u × v == normal (base directe)
		};
		// clang-format off
		static constexpr Face FACES[6] = {
			{{ 1.f,  0.f,  0.f}, { 0.f,  0.f, -1.f}, { 0.f,  1.f,  0.f}}, // +X
			{{-1.f,  0.f,  0.f}, { 0.f,  0.f,  1.f}, { 0.f,  1.f,  0.f}}, // -X
			{{ 0.f,  1.f,  0.f}, { 0.f,  0.f,  1.f}, { 1.f,  0.f,  0.f}}, // +Y
			{{ 0.f, -1.f,  0.f}, { 1.f,  0.f,  0.f}, { 0.f,  0.f,  1.f}}, // -Y
			{{ 0.f,  0.f,  1.f}, { 1.f,  0.f,  0.f}, { 0.f,  1.f,  0.f}}, // +Z
			{{ 0.f,  0.f, -1.f}, { 0.f,  1.f,  0.f}, { 1.f,  0.f,  0.f}}, // -Z
		};
		// clang-format on

		std::vector<Vertex3D> vertices;
		std::vector<uint32_t> indices;
		vertices.reserve(24);
		indices.reserve(36);

		// Un groupe par face (+X,-X,+Y,-Y,+Z,-Z, dans l'ordre de FACES) : un
		// Shape peut donc affecter un Material différent à chaque face sans
		// rien reconstruire — c'est le cas d'usage direct de Mesh::Group.
		for (uint32_t faceIndex = 0; faceIndex < 6; ++faceIndex) {
			const Face &face = FACES[faceIndex];
			// Chaque vecteur de base (normal/u/v) est un axe unitaire : le
			// multiplier composante par composante par `h` sélectionne la
			// demi-dimension du bon axe, ce qu'un scalaire unique ne pouvait
			// pas faire.
			math::FVector3 center = face.normal * h;
			math::FVector3 halfU = face.u * h;
			math::FVector3 halfV = face.v * h;
			math::FVector3 corners[4] = {
				center - halfU - halfV,
				center + halfU - halfV,
				center + halfU + halfV,
				center - halfU + halfV,
			};
			math::FVector2 uvs[4] = {{0.f, 1.f}, {1.f, 1.f}, {1.f, 0.f}, {0.f, 0.f}};

			uint32_t base = uint32_t(vertices.size());
			for (int i = 0; i < 4; ++i)
				vertices.push_back({corners[i], face.normal, uvs[i], sdl3::Color::WHITE()});

			indices.push_back(base + 0);
			indices.push_back(base + 1);
			indices.push_back(base + 2);
			indices.push_back(base + 0);
			indices.push_back(base + 2);
			indices.push_back(base + 3);
		}

		Mesh mesh(std::move(vertices), std::move(indices));
		for (uint32_t faceIndex = 0; faceIndex < 6; ++faceIndex)
			mesh.AddGroup(faceIndex * 6, 6, faceIndex);
		return mesh;
	}

	[[nodiscard]] static Mesh Sphere(float radius = 1.f, int segments = 24, int rings = 16) noexcept {
		std::vector<Vertex3D> vertices;
		std::vector<uint32_t> indices;
		vertices.reserve(size_t(segments + 1) * size_t(rings + 1));

		for (int ring = 0; ring <= rings; ++ring) {
			float theta = sdl3::PI_F * float(ring) / float(rings); // 0 (pôle nord) → π (pôle sud)
			float sinTheta = sdl3::Sin(theta), cosTheta = sdl3::Cos(theta);

			for (int seg = 0; seg <= segments; ++seg) {
				float phi = 2.f * sdl3::PI_F * float(seg) / float(segments);
				float sinPhi = sdl3::Sin(phi), cosPhi = sdl3::Cos(phi);

				math::FVector3 n{sinTheta * cosPhi, cosTheta, sinTheta * sinPhi};
				math::FVector2 uv{float(seg) / float(segments), float(ring) / float(rings)};
				vertices.push_back({n * radius, n, uv, sdl3::Color::WHITE()});
			}
		}

		uint32_t stride = uint32_t(segments) + 1;
		for (int ring = 0; ring < rings; ++ring) {
			for (int seg = 0; seg < segments; ++seg) {
				uint32_t a = uint32_t(ring) * stride + uint32_t(seg);
				uint32_t b = a + stride;

				// CCW vu de l'extérieur (radialement, depuis la normale de
				// sommet) : trouvé faux dans le sens (a,b,a+1)/(b,b+1,a+1) —
				// voir Render3dMesh::SphereInvariants (ExpectConsistentWinding).
				indices.push_back(a);
				indices.push_back(a + 1);
				indices.push_back(b);

				indices.push_back(b);
				indices.push_back(a + 1);
				indices.push_back(b + 1);
			}
		}

		return Mesh(std::move(vertices), std::move(indices));
	}

	[[nodiscard]] static Mesh Plane(float width = 1.f, float height = 1.f, int segmentsX = 1,
									int segmentsY = 1) noexcept {
		std::vector<Vertex3D> vertices;
		std::vector<uint32_t> indices;
		vertices.reserve(size_t(segmentsX + 1) * size_t(segmentsY + 1));

		for (int row = 0; row <= segmentsY; ++row) {
			float z = -height * 0.5f + height * float(row) / float(segmentsY);
			for (int col = 0; col <= segmentsX; ++col) {
				float x = -width * 0.5f + width * float(col) / float(segmentsX);
				math::FVector2 uv{float(col) / float(segmentsX), float(row) / float(segmentsY)};
				vertices.push_back({{x, 0.f, z}, {0.f, 1.f, 0.f}, uv, sdl3::Color::WHITE()});
			}
		}

		uint32_t stride = uint32_t(segmentsX) + 1;
		for (int row = 0; row < segmentsY; ++row) {
			for (int col = 0; col < segmentsX; ++col) {
				uint32_t a = uint32_t(row) * stride + uint32_t(col);
				uint32_t b = a + 1;
				uint32_t c = a + stride;
				uint32_t d = c + 1;

				indices.push_back(a);
				indices.push_back(c);
				indices.push_back(d);
				indices.push_back(a);
				indices.push_back(d);
				indices.push_back(b);
			}
		}

		return Mesh(std::move(vertices), std::move(indices));
	}

	/// Cylindre tronqué (radiusTop=0 -> cône, radiusBottom=0 -> cône inversé).
	/// `openEnded` désactive les capuchons haut/bas.
	[[nodiscard]] static Mesh Cylinder(float radiusTop = 1.f, float radiusBottom = 1.f, float height = 1.f,
									   int radialSegments = 32, int heightSegments = 1,
									   bool openEnded = false) noexcept {
		std::vector<Vertex3D> vertices;
		std::vector<uint32_t> indices;

		float halfHeight = height * 0.5f;
		// Pente du profil (pour incliner la normale du torse sur un tronc de
		// cône) — nulle pour un cylindre droit (radiusTop == radiusBottom).
		float slope = (radiusBottom - radiusTop) / height;

		for (int y = 0; y <= heightSegments; ++y) {
			float v = float(y) / float(heightSegments);
			float radius = radiusTop + v * (radiusBottom - radiusTop);
			float py = halfHeight - v * height;
			for (int x = 0; x <= radialSegments; ++x) {
				float u = float(x) / float(radialSegments);
				float theta = u * 2.f * sdl3::PI_F;
				float sinTheta = sdl3::Sin(theta), cosTheta = sdl3::Cos(theta);
				math::FVector3 pos{radius * sinTheta, py, radius * cosTheta};
				math::FVector3 normal = math::FVector3{sinTheta, slope, cosTheta}.Normalize();
				vertices.push_back({pos, normal, {u, 1.f - v}, sdl3::Color::WHITE()});
			}
		}

		uint32_t stride = uint32_t(radialSegments) + 1;
		for (int y = 0; y < heightSegments; ++y) {
			for (int x = 0; x < radialSegments; ++x) {
				uint32_t a = uint32_t(y) * stride + uint32_t(x);
				uint32_t b = a + stride;
				indices.push_back(a);
				indices.push_back(b);
				indices.push_back(a + 1);
				indices.push_back(b);
				indices.push_back(b + 1);
				indices.push_back(a + 1);
			}
		}

		if (!openEnded) {
			AddDisc(vertices, indices, radiusTop, halfHeight, radialSegments, true);
			AddDisc(vertices, indices, radiusBottom, -halfHeight, radialSegments, false);
		}

		return Mesh(std::move(vertices), std::move(indices));
	}

	[[nodiscard]] static Mesh Cone(float radius = 1.f, float height = 1.f, int radialSegments = 32,
								   int heightSegments = 1, bool openEnded = false) noexcept {
		return Cylinder(0.f, radius, height, radialSegments, heightSegments, openEnded);
	}

	[[nodiscard]] static Mesh Torus(float radius = 1.f, float tube = 0.4f, int radialSegments = 12,
									int tubularSegments = 48) noexcept {
		std::vector<Vertex3D> vertices;
		std::vector<uint32_t> indices;

		for (int j = 0; j <= radialSegments; ++j) {
			for (int i = 0; i <= tubularSegments; ++i) {
				float u = float(i) / float(tubularSegments) * 2.f * sdl3::PI_F;
				float v = float(j) / float(radialSegments) * 2.f * sdl3::PI_F;
				math::FVector3 center{radius * sdl3::Cos(u), radius * sdl3::Sin(u), 0.f};
				math::FVector3 pos{(radius + tube * sdl3::Cos(v)) * sdl3::Cos(u),
								   (radius + tube * sdl3::Cos(v)) * sdl3::Sin(u), tube * sdl3::Sin(v)};
				math::FVector3 normal = (pos - center).Normalize();
				math::FVector2 uv{float(i) / float(tubularSegments), float(j) / float(radialSegments)};
				vertices.push_back({pos, normal, uv, sdl3::Color::WHITE()});
			}
		}

		uint32_t stride = uint32_t(tubularSegments) + 1;
		for (int j = 1; j <= radialSegments; ++j) {
			for (int i = 1; i <= tubularSegments; ++i) {
				uint32_t a = stride * uint32_t(j) + uint32_t(i) - 1;
				uint32_t b = stride * (uint32_t(j) - 1) + uint32_t(i) - 1;
				uint32_t c = stride * (uint32_t(j) - 1) + uint32_t(i);
				uint32_t d = stride * uint32_t(j) + uint32_t(i);
				indices.push_back(a);
				indices.push_back(b);
				indices.push_back(d);
				indices.push_back(b);
				indices.push_back(c);
				indices.push_back(d);
			}
		}

		return Mesh(std::move(vertices), std::move(indices));
	}

	[[nodiscard]] static Mesh Circle(float radius = 1.f, int segments = 32) noexcept {
		std::vector<Vertex3D> vertices;
		std::vector<uint32_t> indices;
		AddDisc(vertices, indices, radius, 0.f, segments, true);
		return Mesh(std::move(vertices), std::move(indices));
	}

	[[nodiscard]] static Mesh Ring(float innerRadius = 0.5f, float outerRadius = 1.f, int thetaSegments = 32,
								   int phiSegments = 1) noexcept {
		std::vector<Vertex3D> vertices;
		std::vector<uint32_t> indices;

		for (int p = 0; p <= phiSegments; ++p) {
			float radius = innerRadius + (outerRadius - innerRadius) * float(p) / float(phiSegments);
			for (int t = 0; t <= thetaSegments; ++t) {
				float theta = float(t) / float(thetaSegments) * 2.f * sdl3::PI_F;
				float x = radius * sdl3::Cos(theta), z = radius * sdl3::Sin(theta);
				math::FVector2 uv{float(t) / float(thetaSegments), float(p) / float(phiSegments)};
				vertices.push_back({{x, 0.f, z}, {0.f, 1.f, 0.f}, uv, sdl3::Color::WHITE()});
			}
		}

		uint32_t stride = uint32_t(thetaSegments) + 1;
		for (int p = 0; p < phiSegments; ++p) {
			for (int t = 0; t < thetaSegments; ++t) {
				uint32_t a = uint32_t(p) * stride + uint32_t(t);
				uint32_t b = a + 1;
				uint32_t c = a + stride;
				uint32_t d = c + 1;
				// Grille polaire (t = angle, p = rayon), pas cartésienne comme
				// Plane() : le winding (a,c,d)/(a,d,b) de Plane ne s'y
				// transpose pas tel quel — sens inverse ici, voir
				// Render3dMesh::RingInvariants (ExpectConsistentWinding).
				indices.push_back(a);
				indices.push_back(d);
				indices.push_back(c);
				indices.push_back(a);
				indices.push_back(b);
				indices.push_back(d);
			}
		}

		return Mesh(std::move(vertices), std::move(indices));
	}

	/// Subdivise les triangles de `baseVertices`/`baseIndices` `detail` fois
	/// puis projette chaque sommet sur la sphère de rayon `radius` — sortie
	/// non indexée (comme three.js PolyhedronGeometry), une normale par
	/// sommet égale à sa position normalisée.
	[[nodiscard]] static Mesh Polyhedron(std::span<const math::FVector3> baseVertices,
										std::span<const uint32_t> baseIndices, float radius = 1.f,
										int detail = 0) noexcept {
		std::vector<math::FVector3> positions;
		for (size_t i = 0; i + 2 < baseIndices.size(); i += 3)
			SubdivideTriangle(baseVertices[baseIndices[i]], baseVertices[baseIndices[i + 1]],
							  baseVertices[baseIndices[i + 2]], detail, positions);

		std::vector<Vertex3D> vertices;
		std::vector<uint32_t> indices;
		vertices.reserve(positions.size());
		for (size_t i = 0; i < positions.size(); ++i) {
			math::FVector3 n = positions[i].Normalize();
			math::FVector3 p = n * radius;
			float u = 0.5f + sdl3::Atan2(n.z, n.x) / (2.f * sdl3::PI_F);
			float v = 0.5f - sdl3::Asin(sdl3::Clamp(n.y, -1.f, 1.f)) / sdl3::PI_F;
			vertices.push_back({p, n, {u, v}, sdl3::Color::WHITE()});
			indices.push_back(uint32_t(i));
		}
		return Mesh(std::move(vertices), std::move(indices));
	}

	[[nodiscard]] static Mesh Tetrahedron(float radius = 1.f, int detail = 0) noexcept {
		static constexpr math::FVector3 VERTS[4] = {
			{1.f, 1.f, 1.f}, {-1.f, -1.f, 1.f}, {-1.f, 1.f, -1.f}, {1.f, -1.f, -1.f}};
		static constexpr uint32_t IDX[12] = {2, 1, 0, 0, 3, 2, 1, 3, 0, 2, 3, 1};
		return Polyhedron(VERTS, IDX, radius, detail);
	}

	[[nodiscard]] static Mesh Octahedron(float radius = 1.f, int detail = 0) noexcept {
		static constexpr math::FVector3 VERTS[6] = {{1.f, 0.f, 0.f}, {-1.f, 0.f, 0.f}, {0.f, 1.f, 0.f},
													 {0.f, -1.f, 0.f}, {0.f, 0.f, 1.f}, {0.f, 0.f, -1.f}};
		static constexpr uint32_t IDX[24] = {0, 2, 4, 0, 4, 3, 0, 3, 5, 0, 5, 2,
											 1, 2, 5, 1, 5, 3, 1, 3, 4, 1, 4, 2};
		return Polyhedron(VERTS, IDX, radius, detail);
	}

	[[nodiscard]] static Mesh Icosahedron(float radius = 1.f, int detail = 0) noexcept {
		float t = (1.f + 2.2360679775f) * 0.5f; // nombre d'or
		// clang-format off
		static const math::FVector3 VERTS[12] = {
			{-1.f,  t, 0.f}, { 1.f,  t, 0.f}, {-1.f, -t, 0.f}, { 1.f, -t, 0.f},
			{ 0.f, -1.f,  t}, { 0.f, 1.f,  t}, { 0.f, -1.f, -t}, { 0.f, 1.f, -t},
			{ t, 0.f, -1.f}, { t, 0.f,  1.f}, {-t, 0.f, -1.f}, {-t, 0.f,  1.f},
		};
		static constexpr uint32_t IDX[60] = {
			0, 11, 5,  0, 5, 1,  0, 1, 7,  0, 7, 10,  0, 10, 11,
			1, 5, 9,   5, 11, 4, 11, 10, 2, 10, 7, 6, 7, 1, 8,
			3, 9, 4,   3, 4, 2,  3, 2, 6,  3, 6, 8,   3, 8, 9,
			4, 9, 5,   2, 4, 11, 6, 2, 10, 8, 6, 7,   9, 8, 1,
		};
		// clang-format on
		return Polyhedron(VERTS, IDX, radius, detail);
	}

	[[nodiscard]] static Mesh Dodecahedron(float radius = 1.f, int detail = 0) noexcept {
		float t = (1.f + 2.2360679775f) * 0.5f; // nombre d'or
		float r = 1.f / t;
		// clang-format off
		static const math::FVector3 VERTS[20] = {
			{-1.f, -1.f, -1.f}, {-1.f, -1.f, 1.f}, {-1.f, 1.f, -1.f}, {-1.f, 1.f, 1.f},
			{ 1.f, -1.f, -1.f}, { 1.f, -1.f, 1.f}, { 1.f, 1.f, -1.f}, { 1.f, 1.f, 1.f},
			{0.f, -r, -t}, {0.f, -r, t}, {0.f, r, -t}, {0.f, r, t},
			{-r, -t, 0.f}, {-r, t, 0.f}, {r, -t, 0.f}, {r, t, 0.f},
			{-t, 0.f, -r}, {t, 0.f, -r}, {-t, 0.f, r}, {t, 0.f, r},
		};
		static constexpr uint32_t IDX[108] = {
			3, 11, 7,   3, 7, 15,   3, 15, 13,
			7, 19, 17,  7, 17, 6,   7, 6, 15,
			17, 4, 8,   17, 8, 10,  17, 10, 6,
			8, 0, 16,   8, 16, 2,   8, 2, 10,
			0, 12, 1,   0, 1, 18,   0, 18, 16,
			6, 10, 2,   6, 2, 13,   6, 13, 15,
			2, 16, 18,  2, 18, 3,   2, 3, 13,
			18, 1, 9,   18, 9, 11,  18, 11, 3,
			4, 14, 12,  4, 12, 0,   4, 0, 8,
			11, 9, 5,   11, 5, 19,  11, 19, 7,
			19, 5, 14,  19, 14, 4,  19, 4, 17,
			1, 12, 14,  1, 14, 5,   1, 5, 9,
		};
		// clang-format on
		return Polyhedron(VERTS, IDX, radius, detail);
	}

	/// Balaie un cercle de rayon `radius` le long de `path` en utilisant ses
	/// repères de Frenet (Curve3::ComputeFrenetFrames) — three.js TubeGeometry.
	[[nodiscard]] static Mesh Tube(const Curve3 &path, int tubularSegments = 64, float radius = 1.f,
								   int radialSegments = 8, bool closed = false) noexcept {
		auto frames = path.ComputeFrenetFrames(tubularSegments, closed);

		std::vector<Vertex3D> vertices;
		std::vector<uint32_t> indices;
		for (int i = 0; i <= tubularSegments; ++i) {
			float u = float(i) / float(tubularSegments);
			math::FVector3 center = path.GetPoint(u);
			const auto &frame = frames[size_t(i)];
			for (int j = 0; j <= radialSegments; ++j) {
				float v = float(j) / float(radialSegments) * 2.f * sdl3::PI_F;
				math::FVector3 normal = (frame.normal * sdl3::Cos(v) + frame.binormal * sdl3::Sin(v)).Normalize();
				math::FVector3 pos = center + normal * radius;
				math::FVector2 uv{u, float(j) / float(radialSegments)};
				vertices.push_back({pos, normal, uv, sdl3::Color::WHITE()});
			}
		}

		uint32_t stride = uint32_t(radialSegments) + 1;
		for (int i = 0; i < tubularSegments; ++i) {
			for (int j = 0; j < radialSegments; ++j) {
				uint32_t a = uint32_t(i) * stride + uint32_t(j);
				uint32_t b = a + stride;
				indices.push_back(a);
				indices.push_back(a + 1);
				indices.push_back(b);
				indices.push_back(b);
				indices.push_back(a + 1);
				indices.push_back(b + 1);
			}
		}
		return Mesh(std::move(vertices), std::move(indices));
	}

	/// Nœud toroïdal (p, q) — three.js TorusKnotGeometry, construit en
	/// balayant un tube (Tube() ci-dessus) le long de la courbe paramétrique
	/// du nœud plutôt qu'en dupliquant la logique de repère de Frenet.
	[[nodiscard]] static Mesh TorusKnot(float radius = 1.f, float tube = 0.4f, int tubularSegments = 64,
										int radialSegments = 8, int p = 2, int q = 3) noexcept {
		class TorusKnotCurve : public Curve3 {
			float m_radius;
			int m_p, m_q;

		public:
			TorusKnotCurve(float radius, int p, int q) noexcept : m_radius(radius), m_p(p), m_q(q) {}
			[[nodiscard]] math::FVector3 GetPoint(float t) const noexcept override {
				float u = t * float(m_p) * 2.f * sdl3::PI_F;
				float quOverP = float(m_q) / float(m_p) * u; // == t * q * 2π
				float cs = sdl3::Cos(quOverP);
				float cu = sdl3::Cos(u), su = sdl3::Sin(u);
				return {m_radius * (2.f + cs) * 0.5f * cu, m_radius * (2.f + cs) * 0.5f * su,
					   m_radius * sdl3::Sin(quOverP) * 0.5f};
			}
		};
		TorusKnotCurve curve(radius, p, q);
		return Tube(curve, tubularSegments, tube, radialSegments, true);
	}

	/// Révolution d'un profil 2D `points` (x = rayon, y = hauteur) autour de
	/// l'axe Y — three.js LatheGeometry.
	[[nodiscard]] static Mesh Lathe(std::span<const math::FVector2> points, int segments = 12, float phiStart = 0.f,
									float phiLength = 2.f * 3.14159265f) noexcept {
		std::vector<Vertex3D> vertices;
		std::vector<uint32_t> indices;
		size_t pointCount = points.size();
		if (pointCount < 2)
			return Mesh();

		for (int s = 0; s <= segments; ++s) {
			float v = float(s) / float(segments);
			float phi = phiStart + v * phiLength;
			float sinPhi = sdl3::Sin(phi), cosPhi = sdl3::Cos(phi);
			for (size_t p = 0; p < pointCount; ++p) {
				float radius = points[p].x;
				float y = points[p].y;
				math::FVector3 pos{radius * sinPhi, y, radius * cosPhi};

				// Tangente locale au profil (voisin suivant, ou précédent au
				// dernier point) puis normale perpendiculaire dans le plan
				// (rayon, hauteur), tournée par phi comme la position.
				float tx, ty;
				if (p + 1 < pointCount) {
					tx = points[p + 1].x - points[p].x;
					ty = points[p + 1].y - points[p].y;
				} else {
					tx = points[p].x - points[p - 1].x;
					ty = points[p].y - points[p - 1].y;
				}
				float nx = ty, ny = -tx; // perpendiculaire dans le plan (rayon, hauteur)
				math::FVector3 normal = math::FVector3{nx * sinPhi, ny, nx * cosPhi}.Normalize();

				math::FVector2 uv{v, float(p) / float(pointCount - 1)};
				vertices.push_back({pos, normal, uv, sdl3::Color::WHITE()});
			}
		}

		uint32_t stride = uint32_t(pointCount);
		for (int s = 0; s < segments; ++s) {
			for (size_t p = 0; p + 1 < pointCount; ++p) {
				uint32_t a = uint32_t(s) * stride + uint32_t(p);
				uint32_t b = a + stride;
				// Trouvé inversé face à ExpectConsistentWinding — voir
				// Render3dMesh::LatheInvariants.
				indices.push_back(a);
				indices.push_back(b);
				indices.push_back(a + 1);
				indices.push_back(b);
				indices.push_back(b + 1);
				indices.push_back(a + 1);
			}
		}
		return Mesh(std::move(vertices), std::move(indices));
	}

	/// Extrude un Shape2D le long de Z sur `depth`, capuchons avant/arrière
	/// triangulés inclus — three.js ExtrudeGeometry (biseaux non supportés
	/// cette phase, seulement l'extrusion droite).
	[[nodiscard]] static Mesh Extrude(const Shape2D &shape, float depth = 1.f) noexcept {
		auto [points, triIndices] = shape.Triangulate();

		std::vector<Vertex3D> vertices;
		std::vector<uint32_t> indices;
		float halfDepth = depth * 0.5f;

		// Face avant (z = +halfDepth, normale +Z) : le triangulateur produit
		// un contour CCW vu depuis +Z (voir Shape2D), donc directement
		// utilisable tel quel.
		uint32_t frontBase = uint32_t(vertices.size());
		for (const auto &p : points)
			vertices.push_back({{p.x, p.y, halfDepth}, {0.f, 0.f, 1.f}, {p.x, p.y}, sdl3::Color::WHITE()});
		for (uint32_t index : triIndices)
			indices.push_back(frontBase + index);

		// Face arrière (z = -halfDepth, normale -Z) : même triangulation,
		// winding inversé pour rester CCW vu depuis -Z.
		uint32_t backBase = uint32_t(vertices.size());
		for (const auto &p : points)
			vertices.push_back({{p.x, p.y, -halfDepth}, {0.f, 0.f, -1.f}, {p.x, p.y}, sdl3::Color::WHITE()});
		for (size_t i = 0; i + 2 < triIndices.size(); i += 3) {
			indices.push_back(backBase + triIndices[i]);
			indices.push_back(backBase + triIndices[i + 2]);
			indices.push_back(backBase + triIndices[i + 1]);
		}

		// Parois latérales : une pour le contour extérieur, une par trou. Le
		// contour d'un trou est déjà de sens opposé au contour extérieur
		// (convention Shape2D — nécessaire pour que MergeHole()/Triangulate()
		// produisent un polygone simple valide) : la même formule de normale
		// suffit donc pour les deux, sans flip séparé — un flip supplémentaire
		// ici inverserait la normale sans inverser l'enroulement assorti,
		// recréant l'incohérence que ce commentaire documente.
		auto extrudeLoop = [&](const std::vector<math::FVector2> &loop) {
			size_t n = loop.size();
			for (size_t i = 0; i < n; ++i) {
				const auto &a2 = loop[i];
				const auto &b2 = loop[(i + 1) % n];
				float dx = b2.x - a2.x, dy = b2.y - a2.y;
				float len = std::sqrt(dx * dx + dy * dy);
				math::FVector3 normal =
					len > 1e-8f ? math::FVector3{dy / len, -dx / len, 0.f} : math::FVector3{0.f, 0.f, 0.f};

				uint32_t base = uint32_t(vertices.size());
				vertices.push_back({{a2.x, a2.y, halfDepth}, normal, {0.f, 1.f}, sdl3::Color::WHITE()});
				vertices.push_back({{b2.x, b2.y, halfDepth}, normal, {1.f, 1.f}, sdl3::Color::WHITE()});
				vertices.push_back({{b2.x, b2.y, -halfDepth}, normal, {1.f, 0.f}, sdl3::Color::WHITE()});
				vertices.push_back({{a2.x, a2.y, -halfDepth}, normal, {0.f, 0.f}, sdl3::Color::WHITE()});
				// Trouvé inversé face à ExpectConsistentWinding — voir
				// Render3dMesh::ExtrudeInvariants.
				indices.push_back(base + 0);
				indices.push_back(base + 2);
				indices.push_back(base + 1);
				indices.push_back(base + 0);
				indices.push_back(base + 3);
				indices.push_back(base + 2);
			}
		};
		extrudeLoop(shape.Contour());
		for (const auto &hole : shape.Holes())
			extrudeLoop(hole);

		return Mesh(std::move(vertices), std::move(indices));
	}

	/// Nuage de points (three.js Points/PointsMaterial, voir M15 du plan) —
	/// un sommet par point, indices identité (0..N-1) : Mesh exige toujours
	/// des indices non vides (voir EnsureGpuBuffers), même sans intérêt réel
	/// ici (aucune réutilisation de sommet possible pour un nuage de points).
	/// La topologie réelle (POINT_LIST) est portée par PipelineKey/DrawCall,
	/// pas par Mesh — voir Points (object3d.hpp) et Canvas::DrawMesh.
	[[nodiscard]] static Mesh FromPoints(std::span<const math::FVector3> points, sdl3::Color color) noexcept {
		std::vector<Vertex3D> vertices;
		vertices.reserve(points.size());
		for (const auto &p : points)
			vertices.push_back({p, {0.f, 1.f, 0.f}, {0.f, 0.f}, color});
		std::vector<uint32_t> indices(points.size());
		for (uint32_t i = 0; i < indices.size(); ++i)
			indices[i] = i;
		return Mesh(std::move(vertices), std::move(indices));
	}

	/// Segments de droite indépendants (three.js LineSegments — pas LineStrip :
	/// chaque paire consécutive de `points` est un segment séparé, voir M15).
	/// `points.size()` doit être pair ; indices identité, même remarque que
	/// FromPoints() ci-dessus.
	[[nodiscard]] static Mesh FromLineSegments(std::span<const math::FVector3> points, sdl3::Color color) noexcept {
		std::vector<Vertex3D> vertices;
		vertices.reserve(points.size());
		for (const auto &p : points)
			vertices.push_back({p, {0.f, 1.f, 0.f}, {0.f, 0.f}, color});
		std::vector<uint32_t> indices(points.size());
		for (uint32_t i = 0; i < indices.size(); ++i)
			indices[i] = i;
		return Mesh(std::move(vertices), std::move(indices));
	}

private:
	/// Disque plat (capuchon de cylindre/cône si `y != 0`, Circle() si `y ==
	/// 0`) — éventail de triangles depuis un sommet central. `top` choisit le
	/// sens d'enroulement (capuchon haut vs bas ont des normales opposées).
	static void AddDisc(std::vector<Vertex3D> &vertices, std::vector<uint32_t> &indices, float radius, float y,
						int segments, bool top) noexcept {
		if (radius <= 0.f)
			return;
		math::FVector3 normal{0.f, top ? 1.f : -1.f, 0.f};
		uint32_t center = uint32_t(vertices.size());
		vertices.push_back({{0.f, y, 0.f}, normal, {0.5f, 0.5f}, sdl3::Color::WHITE()});
		uint32_t ringStart = uint32_t(vertices.size());
		for (int x = 0; x <= segments; ++x) {
			float u = float(x) / float(segments);
			float theta = u * 2.f * sdl3::PI_F;
			float sinTheta = sdl3::Sin(theta), cosTheta = sdl3::Cos(theta);
			math::FVector3 pos{radius * sinTheta, y, radius * cosTheta};
			math::FVector2 uv{sinTheta * 0.5f + 0.5f, cosTheta * 0.5f + 0.5f};
			vertices.push_back({pos, normal, uv, sdl3::Color::WHITE()});
		}
		for (int x = 0; x < segments; ++x) {
			if (top) {
				indices.push_back(center);
				indices.push_back(ringStart + uint32_t(x));
				indices.push_back(ringStart + uint32_t(x) + 1);
			} else {
				indices.push_back(center);
				indices.push_back(ringStart + uint32_t(x) + 1);
				indices.push_back(ringStart + uint32_t(x));
			}
		}
	}

	static void SubdivideTriangle(math::FVector3 a, math::FVector3 b, math::FVector3 c, int detail,
								  std::vector<math::FVector3> &out) noexcept {
		if (detail <= 0) {
			out.push_back(a);
			out.push_back(b);
			out.push_back(c);
			return;
		}
		math::FVector3 ab = (a + b) * 0.5f;
		math::FVector3 bc = (b + c) * 0.5f;
		math::FVector3 ca = (c + a) * 0.5f;
		SubdivideTriangle(a, ab, ca, detail - 1, out);
		SubdivideTriangle(ab, b, bc, detail - 1, out);
		SubdivideTriangle(ca, bc, c, detail - 1, out);
		SubdivideTriangle(ab, bc, ca, detail - 1, out);
	}
};

} // namespace render3d
