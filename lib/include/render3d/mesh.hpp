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

	void AddGroup(uint32_t start, uint32_t count, uint32_t materialIndex);
	[[nodiscard]] const std::vector<Group> &Groups() const noexcept { return m_groups; }

	/// Boîte englobante des sommets, dans l'espace LOCAL du maillage.
	/// Invalide (min > max) pour un maillage vide — `FAABB::IsValid()` le dit,
	/// et le culling laisse alors passer le dessin plutôt que de l'écarter.
	[[nodiscard]] const math::FAABB &LocalBounds() const noexcept;

	void MarkDirty() noexcept;
	[[nodiscard]] bool IsDirty() const noexcept { return m_dirty; }
	[[nodiscard]] bool HasGpuBuffers() const noexcept { return m_vertexBuffer.IsSome() && m_indexBuffer.IsSome(); }

	/// Upload paresseux vers le GPU. `cmd` doit être un command buffer qui n'a
	/// PAS encore de render pass actif : cette méthode ouvre elle-même un
	/// GpuCopyPass, incompatible avec un GpuRenderPass déjà ouvert sur le même
	/// command buffer.
	[[nodiscard]] Result<bool, StringView> EnsureGpuBuffers(sdl3::GpuDevice &device, sdl3::GpuCommandBuffer &cmd);

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
	[[nodiscard]] static Mesh Box(float width, float height, float depth) noexcept;

	[[nodiscard]] static Mesh Sphere(float radius = 1.f, int segments = 24, int rings = 16) noexcept;

	[[nodiscard]] static Mesh Plane(float width = 1.f, float height = 1.f, int segmentsX = 1,
									int segmentsY = 1) noexcept;

	/// Cylindre tronqué (radiusTop=0 -> cône, radiusBottom=0 -> cône inversé).
	/// `openEnded` désactive les capuchons haut/bas.
	[[nodiscard]] static Mesh Cylinder(float radiusTop = 1.f, float radiusBottom = 1.f, float height = 1.f,
									   int radialSegments = 32, int heightSegments = 1,
									   bool openEnded = false) noexcept;

	[[nodiscard]] static Mesh Cone(float radius = 1.f, float height = 1.f, int radialSegments = 32,
								   int heightSegments = 1, bool openEnded = false) noexcept;

	[[nodiscard]] static Mesh Torus(float radius = 1.f, float tube = 0.4f, int radialSegments = 12,
									int tubularSegments = 48) noexcept;

	[[nodiscard]] static Mesh Circle(float radius = 1.f, int segments = 32) noexcept;

	[[nodiscard]] static Mesh Ring(float innerRadius = 0.5f, float outerRadius = 1.f, int thetaSegments = 32,
								   int phiSegments = 1) noexcept;

	/// Subdivise les triangles de `baseVertices`/`baseIndices` `detail` fois
	/// puis projette chaque sommet sur la sphère de rayon `radius` — sortie
	/// non indexée (comme three.js PolyhedronGeometry), une normale par
	/// sommet égale à sa position normalisée.
	[[nodiscard]] static Mesh Polyhedron(std::span<const math::FVector3> baseVertices,
										std::span<const uint32_t> baseIndices, float radius = 1.f,
										int detail = 0) noexcept;

	[[nodiscard]] static Mesh Tetrahedron(float radius = 1.f, int detail = 0) noexcept;

	[[nodiscard]] static Mesh Octahedron(float radius = 1.f, int detail = 0) noexcept;

	[[nodiscard]] static Mesh Icosahedron(float radius = 1.f, int detail = 0) noexcept;

	[[nodiscard]] static Mesh Dodecahedron(float radius = 1.f, int detail = 0) noexcept;

	/// Balaie un cercle de rayon `radius` le long de `path` en utilisant ses
	/// repères de Frenet (Curve3::ComputeFrenetFrames) — three.js TubeGeometry.
	[[nodiscard]] static Mesh Tube(const Curve3 &path, int tubularSegments = 64, float radius = 1.f,
								   int radialSegments = 8, bool closed = false) noexcept;

	/// Nœud toroïdal (p, q) — three.js TorusKnotGeometry, construit en
	/// balayant un tube (Tube() ci-dessus) le long de la courbe paramétrique
	/// du nœud plutôt qu'en dupliquant la logique de repère de Frenet.
	[[nodiscard]] static Mesh TorusKnot(float radius = 1.f, float tube = 0.4f, int tubularSegments = 64,
										int radialSegments = 8, int p = 2, int q = 3) noexcept;

	/// Révolution d'un profil 2D `points` (x = rayon, y = hauteur) autour de
	/// l'axe Y — three.js LatheGeometry.
	[[nodiscard]] static Mesh Lathe(std::span<const math::FVector2> points, int segments = 12, float phiStart = 0.f,
									float phiLength = 2.f * 3.14159265f) noexcept;

	/// Extrude un Shape2D le long de Z sur `depth`, capuchons avant/arrière
	/// triangulés inclus — three.js ExtrudeGeometry (biseaux non supportés
	/// cette phase, seulement l'extrusion droite).
	[[nodiscard]] static Mesh Extrude(const Shape2D &shape, float depth = 1.f) noexcept;

	/// Nuage de points (three.js Points/PointsMaterial, voir M15 du plan) —
	/// un sommet par point, indices identité (0..N-1) : Mesh exige toujours
	/// des indices non vides (voir EnsureGpuBuffers), même sans intérêt réel
	/// ici (aucune réutilisation de sommet possible pour un nuage de points).
	/// La topologie réelle (POINT_LIST) est portée par PipelineKey/DrawCall,
	/// pas par Mesh — voir Points (object3d.hpp) et Canvas::DrawMesh.
	[[nodiscard]] static Mesh FromPoints(std::span<const math::FVector3> points, sdl3::Color color) noexcept;

	/// Segments de droite indépendants (three.js LineSegments — pas LineStrip :
	/// chaque paire consécutive de `points` est un segment séparé, voir M15).
	/// `points.size()` doit être pair ; indices identité, même remarque que
	/// FromPoints() ci-dessus.
	[[nodiscard]] static Mesh FromLineSegments(std::span<const math::FVector3> points, sdl3::Color color) noexcept;

private:
	/// Disque plat (capuchon de cylindre/cône si `y != 0`, Circle() si `y ==
	/// 0`) — éventail de triangles depuis un sommet central. `top` choisit le
	/// sens d'enroulement (capuchon haut vs bas ont des normales opposées).
	static void AddDisc(std::vector<Vertex3D> &vertices, std::vector<uint32_t> &indices, float radius, float y,
						int segments, bool top) noexcept;

	static void SubdivideTriangle(math::FVector3 a, math::FVector3 b, math::FVector3 c, int detail,
								  std::vector<math::FVector3> &out) noexcept;
};

} // namespace render3d
