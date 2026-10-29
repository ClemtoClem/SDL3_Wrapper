#pragma once
/**
 * modeling::Model — maillage ÉDITABLE à la manière de SketchUp : des
 * sommets, des arêtes et des faces PLANES (un contour extérieur et
 * d'éventuels trous), chaque face portant un matériau et un calque.
 *
 * C'est la représentation sur laquelle travaillent les outils de
 * modélisation (Pousser/Tirer, Décalage, Suivez-moi…) ; pour l'affichage,
 * BuildMesh (triangulate.hpp) en tire des triangles.
 *
 * API de construction calquée sur le writer d'openskp (`add_face`,
 * `add_circle`, `add_arc`, `add_polyline`, `add_material`, `add_layer`) :
 *
 * @code{.cpp}
 * modeling::Model m;
 * const uint32_t wood = m.AddMaterial("Bois", {0.55f, 0.37f, 0.22f});
 * m.AddFace({{0, 0, 0}, {2, 0, 0}, {2, 0, 1}, {0, 0, 1}}, {}, {.material = wood});
 * m.AddCircle({1, 0, 3}, {0, 1, 0}, 0.5f, {.segments = 32});
 * @endcode
 *
 * ── Identifiants ────────────────────────────────────────────────────────
 * VertexId/EdgeId/FaceId sont des index STABLES : supprimer un élément le
 * marque mort (`alive = false`) sans décaler les autres, ce qui permet aux
 * outils et à l'annulation de garder leurs références. Compact() élimine
 * les éléments morts (renumérotation) — fait avant l'enregistrement.
 *
 * ── Orientation ─────────────────────────────────────────────────────────
 * Le contour extérieur tourne dans le sens TRIGONOMÉTRIQUE vu depuis la
 * normale de la face ; les trous tournent dans le sens inverse. AddFace
 * corrige d'elle-même l'orientation fournie.
 *
 * Repère : Y vers le haut (comme render3d) ; unité : le mètre.
 */
#include <cstdint>
#include <unordered_map>
#include <vector>

#include <core/core.hpp>
#include <math/math.hpp>
#include <math/polygon.hpp>

namespace modeling {

using Vec3 = math::FVector3;

using VertexId = uint32_t;
using EdgeId = uint32_t;
using FaceId = uint32_t;
inline constexpr uint32_t INVALID_ID = 0xFFFFFFFFu;

/// Distance en dessous de laquelle deux points sont considérés confondus.
inline constexpr float WELD_EPSILON = 1e-4f;

struct Color {
	float r = 0.8f, g = 0.8f, b = 0.8f, a = 1.f;
	constexpr bool operator==(const Color &) const = default;
};

struct Material {
	String name;
	Color color;
	/// Taille (en mètres) d'une répétition de texture pour les UV planaires.
	float textureScale = 1.f;
};

/// Calque (« balise » dans SketchUp récent) : regroupe des faces et arêtes
/// que l'on peut masquer ensemble.
struct Layer {
	String name;
	bool visible = true;
};

struct Vertex {
	Vec3 position;
	bool alive = true;
};

struct Edge {
	VertexId a = INVALID_ID, b = INVALID_ID;
	/// Arête adoucie : non dessinée (contour invisible), comme dans SketchUp.
	bool soft = false;
	/// Arête lisse : les normales des faces voisines y sont moyennées.
	bool smooth = false;
	/// Courbe (cercle, arc, polyligne) à laquelle appartient l'arête ; INVALID_ID sinon.
	uint32_t curve = INVALID_ID;
	uint32_t layer = 0;
	bool alive = true;

	[[nodiscard]] bool Has(VertexId v) const noexcept { return a == v || b == v; }
	[[nodiscard]] VertexId Other(VertexId v) const noexcept { return a == v ? b : a; }
};

using Loop = std::vector<VertexId>;

struct Face {
	Loop outer;
	std::vector<Loop> holes;
	Vec3 normal{0.f, 1.f, 0.f};
	/// Matériau de la face avant (index dans Model::Materials()) ; INVALID_ID = défaut.
	uint32_t material = INVALID_ID;
	uint32_t layer = 0;
	bool alive = true;
};

/// Options de Model::AddFace (cf. openskp `FaceOptions`).
struct FaceOptions {
	uint32_t material = INVALID_ID;
	uint32_t layer = 0;
	/// Arêtes du contour adoucies (invisibles) et lissées.
	bool softEdges = false;
	bool smoothEdges = false;
	/// Contour non plan : découpé en triangles plans au lieu d'être refusé
	/// (openskp `auto_triangulate`).
	bool autoTriangulate = false;
};

/// Options des courbes : cercle, polygone, arc (cf. openskp `CircleOptions`).
struct CurveOptions {
	int segments = 24;
	/// Cercle/polygone : crée aussi la face qui le remplit (sinon arêtes seules).
	bool filled = true;
	uint32_t material = INVALID_ID;
	uint32_t layer = 0;
};

/// Plan d'une face : point + normale unitaire.
struct PlaneInfo {
	Vec3 origin;
	Vec3 normal{0.f, 1.f, 0.f};
	/// Base orthonormée du plan (u, v, normal) pour les projections 2D.
	Vec3 u{1.f, 0.f, 0.f}, v{0.f, 0.f, 1.f};

	[[nodiscard]] math::FVector2 Project(const Vec3 &p) const noexcept {
		const Vec3 d = p - origin;
		return {d.Dot(u), d.Dot(v)};
	}
	[[nodiscard]] Vec3 Unproject(const math::FVector2 &q) const noexcept { return origin + u * q.x + v * q.y; }
	[[nodiscard]] float Distance(const Vec3 &p) const noexcept { return (p - origin).Dot(normal); }

	/// Plan passant par `origin`, de normale `normal` (normalisée ici).
	[[nodiscard]] static PlaneInfo Make(const Vec3 &origin, const Vec3 &normal) noexcept;
};

/// Normale d'un polygone par la méthode de Newell (robuste aux sommets
/// alignés) ; vecteur nul si le polygone est dégénéré.
[[nodiscard]] Vec3 NewellNormal(const std::vector<Vec3> &points) noexcept;

/// Aire signée d'un polygone 2D (cf. math/polygon.hpp).
using math::SignedArea;

class Model {
public:
	// ── Matériaux et calques ─────────────────────────────────────────────

	/// Ajoute un matériau ; un nom déjà présent rend le matériau existant
	/// (sa couleur est mise à jour).
	uint32_t AddMaterial(const String &name, Color color, float textureScale = 1.f);
	[[nodiscard]] Option<uint32_t> FindMaterial(const String &name) const;
	[[nodiscard]] const std::vector<Material> &Materials() const noexcept { return m_materials; }
	[[nodiscard]] std::vector<Material> &Materials() noexcept { return m_materials; }

	/// Calque 0 « Layer0 » toujours présent ; un nom déjà présent rend l'existant.
	uint32_t AddLayer(const String &name, bool visible = true);
	[[nodiscard]] const std::vector<Layer> &Layers() const noexcept { return m_layers; }
	[[nodiscard]] std::vector<Layer> &Layers() noexcept { return m_layers; }

	// ── Géométrie élémentaire ────────────────────────────────────────────

	/// Sommet à `p` ; réutilise un sommet vivant à moins de WELD_EPSILON.
	VertexId AddVertex(const Vec3 &p);
	/// Arête entre deux sommets ; réutilise une arête existante. INVALID_ID
	/// si les deux sommets sont confondus.
	EdgeId AddEdge(VertexId a, VertexId b);
	EdgeId AddEdge(const Vec3 &a, const Vec3 &b) { return AddEdge(AddVertex(a), AddVertex(b)); }

	/// Face plane d'après un contour (≥ 3 points) et des trous ; crée les
	/// arêtes manquantes. NONE si le contour est dégénéré ou non plan (sauf
	/// avec `autoTriangulate`, qui rend alors la première des faces créées).
	/// Si une face identique existe déjà, elle est rendue telle quelle.
	Option<FaceId> AddFace(const std::vector<Vec3> &outer, const std::vector<std::vector<Vec3>> &holes = {},
						   const FaceOptions &options = {});
	/// Même chose avec des sommets existants.
	Option<FaceId> AddFaceFromVertices(const Loop &outer, const std::vector<Loop> &holes = {},
									   const FaceOptions &options = {});

	/// Polyligne libre (arêtes seules), comme openskp `add_polyline`.
	std::vector<EdgeId> AddPolyline(const std::vector<Vec3> &points, bool closed = false, uint32_t layer = 0);

	/// Cercle (polygone régulier à `segments` côtés) ; arêtes adoucies entre
	/// elles comme une courbe. Rend la face si `filled`, sinon NONE.
	Option<FaceId> AddCircle(const Vec3 &center, const Vec3 &normal, float radius, const CurveOptions &options = {});
	/// Polygone régulier : comme un cercle, mais ses arêtes restent visibles.
	Option<FaceId> AddPolygon(const Vec3 &center, const Vec3 &normal, float radius, int sides,
							  const CurveOptions &options = {});
	/// Arc ouvert de `startAngle` à `endAngle` (radians) dans le plan de
	/// normale `normal` ; `xAxis` donne la direction de l'angle 0.
	std::vector<EdgeId> AddArc(const Vec3 &center, const Vec3 &normal, const Vec3 &xAxis, float radius, float startAngle,
							   float endAngle, const CurveOptions &options = {});
	/// Rectangle d'après un coin et deux côtés (vecteurs).
	Option<FaceId> AddRectangle(const Vec3 &corner, const Vec3 &side1, const Vec3 &side2, const FaceOptions &options = {});

	// ── Accès ────────────────────────────────────────────────────────────

	[[nodiscard]] const std::vector<Vertex> &Vertices() const noexcept { return m_vertices; }
	[[nodiscard]] const std::vector<Edge> &Edges() const noexcept { return m_edges; }
	[[nodiscard]] const std::vector<Face> &Faces() const noexcept { return m_faces; }
	/// Accès en écriture. Toute modification STRUCTURELLE faite par ce biais
	/// (positions, extrémités d'arêtes, contours, ajout/suppression) doit être
	/// suivie d'InvalidateIndices() ; changer un matériau ou un drapeau n'en
	/// a pas besoin.
	[[nodiscard]] std::vector<Vertex> &Vertices() noexcept { return m_vertices; }
	[[nodiscard]] std::vector<Edge> &Edges() noexcept { return m_edges; }
	[[nodiscard]] std::vector<Face> &Faces() noexcept { return m_faces; }
	/// Reconstruit les index internes au prochain accès.
	void InvalidateIndices() noexcept {
		m_vertexIndexDirty = m_edgeIndexDirty = true;
		++m_faceVersion;
		Touch();
	}
	/// Adoucit/lisse une arête sans invalider les index (à préférer à Edges()).
	void SetEdgeSmoothing(EdgeId e, bool soft, bool smooth);

	[[nodiscard]] const Vec3 &Position(VertexId v) const { return m_vertices[v].position; }
	[[nodiscard]] bool IsAlive(FaceId f) const noexcept { return f < m_faces.size() && m_faces[f].alive; }
	[[nodiscard]] bool IsEdgeAlive(EdgeId e) const noexcept { return e < m_edges.size() && m_edges[e].alive; }

	[[nodiscard]] size_t VertexCount() const noexcept;
	[[nodiscard]] size_t EdgeCount() const noexcept;
	[[nodiscard]] size_t FaceCount() const noexcept;
	[[nodiscard]] bool IsEmpty() const noexcept { return FaceCount() == 0 && EdgeCount() == 0; }

	/// Arête vivante reliant deux sommets, INVALID_ID sinon.
	[[nodiscard]] EdgeId FindEdge(VertexId a, VertexId b) const;
	/// Sommet vivant à moins de `tolerance` de `p`, INVALID_ID sinon.
	[[nodiscard]] VertexId FindVertex(const Vec3 &p, float tolerance = WELD_EPSILON) const;
	/// Faces vivantes qui utilisent l'arête.
	[[nodiscard]] std::vector<FaceId> FacesOfEdge(EdgeId e) const;
	/// Arêtes du contour et des trous d'une face.
	[[nodiscard]] std::vector<EdgeId> EdgesOfFace(FaceId f) const;
	/// Positions du contour extérieur.
	[[nodiscard]] std::vector<Vec3> OuterPoints(FaceId f) const;
	[[nodiscard]] PlaneInfo FacePlane(FaceId f) const;
	[[nodiscard]] Vec3 FaceCentroid(FaceId f) const;
	/// Aire de la face, trous déduits.
	[[nodiscard]] float FaceArea(FaceId f) const;
	/// Boîte englobante des sommets vivants (vide = min > max).
	[[nodiscard]] math::FAABB Bounds() const;

	/// `p` (supposé dans le plan de la face) est-il à l'intérieur, trous exclus ?
	[[nodiscard]] bool FaceContains(FaceId f, const Vec3 &p, float tolerance = 1e-5f) const;

	// ── Édition ──────────────────────────────────────────────────────────

	/// Supprime la face (ses arêtes restent, comme dans SketchUp).
	void RemoveFace(FaceId f);
	/// Supprime l'arête et les faces qui s'appuient dessus (gomme de SketchUp).
	void RemoveEdge(EdgeId e);
	/// Supprime les arêtes qui ne bordent plus aucune face et ne sont pas
	/// des arêtes libres voulues (`onlyFromFaces` = arêtes jamais tracées seules).
	void RemoveOrphanVertices();
	/// Inverse l'orientation (normale) de la face.
	void FlipFace(FaceId f);
	/// Recalcule la normale d'une face après déplacement de ses sommets.
	void UpdateFaceNormal(FaceId f);
	/// Applique une matrice aux sommets donnés (puis met à jour les normales).
	void TransformVertices(const std::vector<VertexId> &vertices, const math::FMatrix4 &matrix);
	/// Applique une matrice à tout le modèle.
	void Transform(const math::FMatrix4 &matrix);
	/// Fusionne les sommets confondus (après un déplacement).
	void WeldVertices(float tolerance = WELD_EPSILON);

	/// Fusionne en une seule face les faces COPLANAIRES et voisines de `f`
	/// (même matériau) ; les arêtes intérieures disparaissent. Rend la face
	/// résultante (peut être `f`).
	FaceId MergeCoplanar(FaceId f, float angleTolerance = 1e-3f);

	/// Une face tracée sur une face coplanaire existante s'y imprime
	/// (comportement de SketchUp quand on dessine un rectangle sur une face :
	/// la nouvelle face peut ensuite être poussée/tirée seule) :
	///  - entièrement à l'intérieur : elle y découpe un TROU ;
	///  - touchant le bord (une porte posée sur le bas d'un mur) : l'hôte est
	///    DÉCOUPÉ le long du contour intérieur de la forme, et la partie qui
	///    doublerait la nouvelle face disparaît.
	/// La face intérieure prend l'orientation de l'hôte. Rend la face hôte,
	/// NONE s'il n'y en a pas.
	Option<FaceId> Imprint(FaceId inner);

	/// Coupe la face `f` le long d'un chemin de sommets dont les extrémités
	/// sont sur son contour et les sommets intermédiaires à l'intérieur
	/// (SplitFace généralisé à une ligne brisée). Rend la nouvelle face.
	Option<FaceId> SplitFaceAlong(FaceId f, const std::vector<VertexId> &path);

	/// Coupe l'arête en `point` (projeté sur le segment) : le nouveau sommet
	/// est inséré dans l'arête et dans les contours des faces qui la
	/// bordent. Rend ce sommet (un sommet d'extrémité si `point` en est
	/// confondu), INVALID_ID pour une arête morte.
	VertexId SplitEdge(EdgeId e, const Vec3 &point);

	/// Trace une arête entre deux sommets du contour d'une même face : la
	/// face est coupée en deux (comportement de l'outil Ligne de SketchUp).
	/// Rend la nouvelle face créée, NONE si l'arête ne coupe aucune face.
	Option<FaceId> SplitFace(VertexId a, VertexId b);

	/// Ajoute toute la géométrie de `other`, transformée par `transform` :
	/// sommets soudés aux existants, matériaux et calques rapprochés par nom.
	/// Rend les faces ajoutées (dans ce modèle).
	std::vector<FaceId> Append(const Model &other, const math::FMatrix4 &transform = math::FMatrix4::Identity());

	/// Élimine les éléments morts et renumérote (sommets, arêtes, faces).
	void Compact();
	void Clear();

	/// Version incrémentée à chaque modification (cache d'affichage).
	[[nodiscard]] uint64_t Revision() const noexcept { return m_revision; }
	void Touch() noexcept { ++m_revision; }

	/// Courbe suivante (identifiant pour Edge::curve).
	uint32_t NewCurveId() noexcept { return m_nextCurve++; }
	/// Garantit que les prochaines courbes auront un identifiant ≥ `next`
	/// (après le chargement d'un fichier).
	void ReserveCurveIds(uint32_t next) noexcept { m_nextCurve = math::Max(m_nextCurve, next); }

private:
	/// Imprint d'une forme touchant le bord de l'hôte (cf. Imprint).
	bool CutAlongInner(FaceId host, FaceId inner);
	/// (a, b) est-elle une arête du contour d'une de ces faces ?
	[[nodiscard]] bool IsBorderEdge(const std::vector<FaceId> &faces, VertexId a, VertexId b) const;
	void EnsureDefaultLayer();
	/// Orientation et normale d'une face fraîchement construite.
	bool FinishFace(Face &face) const;

	void FacesChanged() noexcept { ++m_faceVersion; }
	void IndexVertex(VertexId v);
	void RebuildVertexIndex() const;
	void RebuildEdgeIndex() const;
	void RebuildFaceCache() const;
	[[nodiscard]] static uint64_t EdgeKey(VertexId a, VertexId b) noexcept {
		return a < b ? (uint64_t(a) << 32) | b : (uint64_t(b) << 32) | a;
	}

	std::vector<Vertex> m_vertices;
	std::vector<Edge> m_edges;
	std::vector<Face> m_faces;

	// Index d'accélération (reconstruits paresseusement) : grille de hachage
	// des sommets, arêtes par paire de sommets, faces de chaque arête.
	mutable std::unordered_map<uint64_t, std::vector<VertexId>> m_vertexGrid;
	mutable bool m_vertexIndexDirty = true;
	mutable std::unordered_map<uint64_t, EdgeId> m_edgeIndex;
	mutable bool m_edgeIndexDirty = true;
	mutable std::vector<std::vector<FaceId>> m_faceCache;
	mutable uint64_t m_faceCacheVersion = ~uint64_t(0);
	uint64_t m_faceVersion = 0;
	std::vector<Material> m_materials;
	std::vector<Layer> m_layers;
	uint32_t m_nextCurve = 0;
	uint64_t m_revision = 0;
};

} // namespace modeling
