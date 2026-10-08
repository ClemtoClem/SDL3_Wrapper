#pragma once
/**
 * game_editor — modèle de DOCUMENT de l'éditeur (projet → scènes → objets).
 *
 * Séparation volontaire, calquée sur ce que font Godot/Unreal : le document
 * décrit ce que l'utilisateur a créé (des descriptions pures : forme,
 * transform, matériau, propriétés physiques, script attaché), le RUNTIME
 * (editor.hpp) en construit les `render3d::Object3D`/`physics::RigidBody`/
 * entités ECS correspondants. Conséquences directes :
 *
 *  - ce fichier ne dépend NI de render3d::, NI de physics::, NI de ui:: —
 *    donc il est entièrement testable sans fenêtre ni GPU (c'est ce que fait
 *    tests/game_editor_smoke_test.cpp) ;
 *  - la sauvegarde/chargement est une simple conversion vers `data::Node`,
 *    réutilisant les codecs existants (`sql::SaveProjectJson`) ;
 *  - l'éditeur peut recharger une scène entière (New/Load/changement de
 *    scène) en reconstruisant le runtime à partir du document, sans avoir à
 *    défaire l'état 3D pièce par pièce.
 *
 * ── Depuis le chantier « hiérarchie de nœuds » ──────────────────────────
 * Une scène n'est plus une LISTE d'objets avec un champ `parent` portant un
 * nom : c'est un `scene::NodeTree` (lib/include/scene/), c'est-à-dire un vrai
 * arbre à profondeur libre. Ce qui était les champs d'un `NodeDesc` est
 * devenu des COMPOSANTS attachés au nœud :
 *
 *     NodeDesc{shape, dimensions, segments, source, material}
 *         -> composant « MeshInstance »   (cf. VisualDesc::Read/Write)
 *     NodeDesc{body, collider, halfExtents, mass, ...}
 *         -> composant « RigidBody »      (cf. PhysicsDesc::Read/Write)
 *     NodeDesc{tag}    -> propriété libre « tag » du nœud
 *     NodeDesc{name, transform, visible} -> champs propres du nœud
 *
 * `NodeDesc` SURVIT, mais comme CONSTRUCTEUR : il décrit un objet à créer
 * (contenu livré, import glTF, scripts, tests) et sait se convertir en nœud.
 * L'édition, elle, passe par les accesseurs de composants — un nœud est la
 * seule source de vérité une fois qu'il existe.
 *
 * Les projets au format 2 (liste plate) restent LISIBLES : `SceneDesc::FromJson`
 * reconstruit l'arbre à partir des champs `parent`, cf. FromLegacyObjects.
 *
 * Aucune exception (cf. memory/feedback_no_exceptions.md) : la lecture d'un
 * document malformé rend `Result<..., String>`, et une valeur absente prend
 * simplement sa valeur par défaut.
 */
#include <vector>

#include "core/core.hpp"
#include "data/json.hpp"
#include "data/node.hpp"
#include "math/math.hpp"
#include "scene/scene.hpp"
#include "sdl3/structs.hpp"

namespace game_editor {

// ============================================================================
// Énumérations du document
// ============================================================================

/// Forme géométrique d'un objet — chaque valeur correspond à une fabrique
/// `render3d::Mesh::*` côté runtime (cf. editor.hpp::BuildMesh).
enum class ShapeKind : uint8_t {
	BOX,
	SPHERE,
	CYLINDER,
	CONE,
	TORUS,
	PLANE,
	ICOSAHEDRON,
	TORUS_KNOT,
	PORTAL_QUAD,
	/// Maillage importé d'un fichier glTF — le chemin vit dans
	/// `NodeDesc::source`, la géométrie est relue à chaque construction du
	/// runtime (le document ne contient JAMAIS de sommets : un projet reste
	/// un fichier texte lisible, et le modèle reste modifiable dans son
	/// logiciel d'origine).
	MODEL,
};

/// Famille de matériau — correspond aux fabriques `render3d::Material::*`.
enum class MaterialKind : uint8_t { PLASTIC, METAL, WOOD, PBR, UNLIT, BASIC };

/// Rôle physique : aucun corps, corps statique (masse infinie) ou dynamique.
enum class BodyKind : uint8_t { NONE, STATIC, DYNAMIC };

/// Volume de collision, indépendant de la forme d'AFFICHAGE (une piste peut
/// s'afficher comme un tore et collisionner comme une boîte).
enum class ColliderKind : uint8_t { BOX, SPHERE, CAPSULE };

[[nodiscard]] const char *ShapeKindName(ShapeKind kind) noexcept;

[[nodiscard]] Option<ShapeKind> ShapeKindFromName(const String &name);

[[nodiscard]] const char *MaterialKindName(MaterialKind kind) noexcept;

[[nodiscard]] Option<MaterialKind> MaterialKindFromName(const String &name);

[[nodiscard]] const char *BodyKindName(BodyKind kind) noexcept;

[[nodiscard]] Option<BodyKind> BodyKindFromName(const String &name);

[[nodiscard]] const char *ColliderKindName(ColliderKind kind) noexcept;

[[nodiscard]] Option<ColliderKind> ColliderKindFromName(const String &name);

// ============================================================================
// JSON — helpers de lecture/écriture
// ============================================================================

namespace json {

/// Lit un scalaire JSON comme `float` en acceptant AUSSI un nœud INT.
///
/// L'encodeur JSON de `data::` force désormais le point décimal sur les
/// FLOAT (correctif apporté avec cette démo, cf. data/json.hpp), donc un
/// aller-retour maison reste typé ; cette tolérance reste nécessaire pour
/// les documents écrits À LA MAIN ou par un autre outil, où `"mass": 2` est
/// parfaitement légitime.
[[nodiscard]] float Float(const data::NodePtr &node, float fallback = 0.f) noexcept;

[[nodiscard]] int Int(const data::NodePtr &node, int fallback = 0) noexcept;

[[nodiscard]] bool Bool(const data::NodePtr &node, bool fallback = false) noexcept;

[[nodiscard]] String Str(const data::NodePtr &node, const char *fallback = "");

[[nodiscard]] data::NodePtr FromVec2(const math::FVector2 &v);

[[nodiscard]] math::FVector2 ToVec2(const data::NodePtr &node, math::FVector2 fallback = {});

[[nodiscard]] data::NodePtr FromVec3(const math::FVector3 &v);

[[nodiscard]] math::FVector3 ToVec3(const data::NodePtr &node, math::FVector3 fallback = {});

[[nodiscard]] data::NodePtr FromVec4(const math::FVector4 &v);

[[nodiscard]] math::FVector4 ToVec4(const data::NodePtr &node, math::FVector4 fallback = {});

[[nodiscard]] data::NodePtr FromColor(const sdl3::Color &c);

[[nodiscard]] sdl3::Color ToColor(const data::NodePtr &node, sdl3::Color fallback = {255, 255, 255, 255});

} // namespace json

// ============================================================================
// Descriptions
// ============================================================================

/// Le transform d'édition EST celui de la bibliothèque (`scene::Transform`) :
/// rotation en quaternion en mémoire, angles d'Euler en degrés dans le fichier
/// et dans l'inspecteur (cf. `EulerDegrees()`/`SetEulerDegrees()`). L'alias
/// est conservé parce que tout l'éditeur, les scripts et les tests parlent de
/// « TransformDesc ».
using TransformDesc = scene::Transform;

/// Noms des composants posés par l'éditeur sur ses nœuds. Ce sont des
/// chaînes, et non un enum : c'est ce qui permet à un futur greffon (ou à
/// l'utilisateur) d'en déclarer d'autres sans toucher à la bibliothèque, cf.
/// scene::NodeTypeRegistry.
namespace component {
/// Apparence : forme, dimensions, tesselation, fichier source, matériau.
inline constexpr const char *VISUAL = "MeshInstance";
/// Rôle physique : corps, volume de collision, masse, frottements.
inline constexpr const char *BODY = "RigidBody";
/// Source de lumière : ponctuelle ou conique, couleur, intensité, portée.
inline constexpr const char *LIGHT = "Light";
/// Point de vue : champ de vision ; la caméra « courante » est celle du jeu.
inline constexpr const char *CAMERA = "Camera";
/// Zone de déclenchement : notifie le script de jeu quand on y entre.
inline constexpr const char *TRIGGER = "Trigger";
/// Script de comportement attaché au nœud (cf. Project::scripts).
inline constexpr const char *SCRIPT = "Script";
/// Élément 2D dessiné sur le calque 2D : rectangle, cercle, polygone,
/// trait, image ou texte (cf. CanvasItemDesc).
inline constexpr const char *CANVAS_ITEM = "CanvasItem";
/// Point de vue 2D : ce que la caméra 2D courante vise est au centre de
/// l'écran, agrandi de `zoom` (cf. Camera2DDesc).
inline constexpr const char *CAMERA_2D = "Camera2D";
} // namespace component

/// Vrai pour les composants qu'`NodeDesc` lit dans ses champs dédiés ;
/// les autres voyagent dans `NodeDesc::components`.
[[nodiscard]] bool IsBuiltinComponent(const String &type);

/// Types de nœuds de l'éditeur (cf. scene::NodeTypeRegistry — l'éditeur les
/// déclare au démarrage, la bibliothèque n'en connaît aucun).
namespace node_kind {
inline constexpr const char *GROUP = "Node";          ///< groupe purement structurel
inline constexpr const char *MESH = "MeshInstance";   ///< forme visible
inline constexpr const char *BODY = "RigidBody";      ///< corps physique (avec ou sans forme)
inline constexpr const char *SPAWN = "SpawnPoint";    ///< repère sans géométrie
inline constexpr const char *FOLDER = "Folder";       ///< dossier d'organisation de l'arbre
inline constexpr const char *LIGHT = "Light";         ///< source de lumière
inline constexpr const char *CAMERA = "Camera";       ///< point de vue
inline constexpr const char *TRIGGER = "Trigger";     ///< zone de déclenchement
// ── 2D (cf. canvas2d.hpp) ────────────────────────────────────────────────────
inline constexpr const char *NODE2D = "Node2D";            ///< pivot 2D (groupe)
inline constexpr const char *SHAPE2D = "Shape2D";          ///< forme 2D colorée
inline constexpr const char *SPRITE2D = "Sprite2D";        ///< image 2D
inline constexpr const char *LABEL2D = "Label2D";          ///< texte 2D
inline constexpr const char *CAMERA2D = "Camera2D";        ///< point de vue 2D
/// Calque d'interface : ses descendants ignorent la caméra 2D et restent
/// fixes à l'écran (HUD, menus) — comme le CanvasLayer de Godot.
inline constexpr const char *CANVAS_LAYER = "CanvasLayer";
} // namespace node_kind

/// Lecture d'une propriété de composant, avec valeur par défaut — le motif
/// commun de tous les `Read()` ci-dessous.
[[nodiscard]] const scene::PropertyValue *ComponentProp(const scene::Node &node, const char *componentType,
															   const char *key);

/// Propriété libre portée par le nœud : étiquette lue par les scripts
/// (« checkpoint », « road », « portal_a »…).
inline constexpr const char *PROP_TAG = "tag";

[[nodiscard]] String TagOf(const scene::Node &node);

void SetTag(scene::Node &node, const String &tag);

struct MaterialDesc {
	MaterialKind kind = MaterialKind::PLASTIC;
	sdl3::Color baseColor{180, 180, 190, 255};
	float metallic = 0.f;
	float roughness = 0.5f;
	bool doubleSided = false;
	bool wireframe = false;

	[[nodiscard]] data::NodePtr ToJson() const;

	[[nodiscard]] static MaterialDesc FromJson(const data::NodePtr &node);
};

struct PhysicsDesc {
	BodyKind body = BodyKind::NONE;
	ColliderKind collider = ColliderKind::BOX;
	/// Demi-dimensions de la boîte, ou rayon (composante x) pour une sphère.
	math::FVector3 halfExtents{0.5f, 0.5f, 0.5f};
	float mass = 1.f;
	float restitution = 0.3f;
	float friction = 0.5f;

	// ── Composant « RigidBody » d'un nœud ────────────────────────────────────

	[[nodiscard]] static bool Has(const scene::Node &node) noexcept;

	/// Lit le composant physique d'un nœud (valeurs par défaut s'il n'en a
	/// pas — un nœud sans corps se lit comme « BodyKind::NONE »).
	[[nodiscard]] static PhysicsDesc Read(const scene::Node &node);

	[[nodiscard]] bool operator==(const PhysicsDesc &o) const noexcept {
		return body == o.body && collider == o.collider && mass == o.mass && restitution == o.restitution &&
			   friction == o.friction && halfExtents.x == o.halfExtents.x && halfExtents.y == o.halfExtents.y &&
			   halfExtents.z == o.halfExtents.z;
	}
	[[nodiscard]] bool operator!=(const PhysicsDesc &o) const noexcept { return !(*this == o); }

	/// Écrit (ou retire) le composant physique.
	///
	/// Sans corps ET avec des réglages restés par défaut, le composant est
	/// RETIRÉ : un groupe purement structurel ne doit pas traîner une
	/// physique vide dans le fichier ni dans l'inspecteur. Mais des réglages
	/// explicites (masse, frottement) sont CONSERVÉS même sans corps actif —
	/// sinon couper puis réactiver le corps d'un objet perdrait silencieusement
	/// sa masse, ce qu'un éditeur ne doit jamais faire.
	void Write(scene::Node &node) const;

	// ── Format 2 (liste plate) : lecture seule, pour les projets existants ──

	[[nodiscard]] data::NodePtr ToJson() const;

	[[nodiscard]] static PhysicsDesc FromJson(const data::NodePtr &node);
};

/// Apparence d'un nœud : sa forme géométrique et son matériau. C'est le
/// composant « MeshInstance » — un nœud qui n'en a pas est purement
/// structurel (un groupe, un point d'ancrage), et c'est un cas NORMAL.
struct VisualDesc {
	ShapeKind shape = ShapeKind::BOX;
	/// Dimensions propres à la forme (boîte : côtés ; sphère : rayon en x ;
	/// tore : rayon en x et section en y ; cylindre : rayon x, hauteur y…).
	math::FVector3 dimensions{1.f, 1.f, 1.f};
	int segments = 24; ///< finesse de tesselation des formes courbes
	String source;     ///< fichier `.gltf` pour ShapeKind::MODEL
	MaterialDesc material;

	[[nodiscard]] static bool Has(const scene::Node &node) noexcept;

	[[nodiscard]] static VisualDesc Read(const scene::Node &node);

	void Write(scene::Node &node) const;

	/// Retire l'apparence : le nœud devient un groupe.
	static void Clear(scene::Node &node) { (void)node.RemoveComponent(String(component::VISUAL)); }
};

// ============================================================================
// Composants 2D
// ============================================================================
//
// Le calque 2D se dessine PAR-DESSUS la vue 3D (ou seul, cf.
// Canvas2DDesc::render3d). Un nœud 2D garde le Transform commun à tous les
// nœuds et n'en lit que la partie plane : position x/y en PIXELS de la
// résolution de référence, Y vers le BAS (comme un écran), rotation = angle
// autour de Z en degrés (sens horaire à l'écran), échelle x/y.

/// Ce que dessine un `CanvasItem`.
enum class CanvasItemKind : uint8_t { RECT, CIRCLE, POLYGON, LINE, SPRITE, TEXT };

[[nodiscard]] const char *CanvasItemKindName(CanvasItemKind kind) noexcept;

[[nodiscard]] const char *CanvasItemKindLabel(CanvasItemKind kind) noexcept;

inline constexpr CanvasItemKind CANVAS_ITEM_KINDS[] = {CanvasItemKind::RECT,	CanvasItemKind::CIRCLE,
														CanvasItemKind::POLYGON, CanvasItemKind::LINE,
														CanvasItemKind::SPRITE,	CanvasItemKind::TEXT};

[[nodiscard]] Option<CanvasItemKind> CanvasItemKindFromName(const String &name);

/// Alignement horizontal d'un texte 2D par rapport à son pivot.
enum class TextAlign2D : uint8_t { LEFT, CENTER, RIGHT };

[[nodiscard]] const char *TextAlign2DName(TextAlign2D align) noexcept;

[[nodiscard]] TextAlign2D TextAlign2DFromName(const String &name) noexcept;

/// Composant « CanvasItem » : l'apparence d'un nœud 2D.
struct CanvasItemDesc {
	CanvasItemKind kind = CanvasItemKind::RECT;
	/// Rectangle, image : largeur × hauteur ; cercle : diamètres (ellipse) ;
	/// texte : ignoré (la taille suit le texte).
	math::FVector2 size{64.f, 64.f};
	sdl3::Color color{255, 255, 255, 255}; ///< remplissage, teinte de l'image, couleur du texte
	bool filled = true;                    ///< faux : contour seul
	float outline = 0.f;                   ///< épaisseur du contour, en pixels (0 : aucun)
	sdl3::Color outlineColor{0, 0, 0, 255};
	/// Pivot au CENTRE (défaut) ou au coin haut-gauche — le point que la
	/// position désigne et autour duquel l'objet tourne.
	bool centered = true;
	/// Sommets (repère local) d'un polygone ou d'un trait (ligne brisée).
	std::vector<math::FVector2> points;
	String texture; ///< image : chemin (projet, puis ressources)
	bool flipH = false, flipV = false;
	String text;
	float fontSize = 24.f;
	TextAlign2D align = TextAlign2D::CENTER;
	/// Ordre de dessin : les grands `z` passent devant ; à `z` égal, l'ordre
	/// de l'arbre (un enfant devant son parent, un cadet devant son aîné).
	int z = 0;

	[[nodiscard]] static bool Has(const scene::Node &node) noexcept;

	[[nodiscard]] static CanvasItemDesc Read(const scene::Node &node);

	void Write(scene::Node &node) const;

	static void Clear(scene::Node &node) { (void)node.RemoveComponent(String(component::CANVAS_ITEM)); }
};

/// Composant « Camera2D ».
struct Camera2DDesc {
	float zoom = 1.f;     ///< > 1 : on voit moins de monde, en plus gros
	bool current = false; ///< caméra du jeu (la première marquée l'emporte)

	[[nodiscard]] static bool Has(const scene::Node &node) noexcept;

	[[nodiscard]] static Camera2DDesc Read(const scene::Node &node);

	void Write(scene::Node &node) const;

	static void Clear(scene::Node &node) { (void)node.RemoveComponent(String(component::CAMERA_2D)); }
};

/// Nœud du monde 2D : par son type, ou parce qu'il porte un composant 2D.
[[nodiscard]] bool Is2DNode(const scene::Node &node);

// ============================================================================
// Composants de lumière, caméra, déclencheur et script
// ============================================================================

enum class LightKind : uint8_t { POINT, SPOT };

[[nodiscard]] const char *LightKindName(LightKind kind) noexcept;

[[nodiscard]] Option<LightKind> LightKindFromName(const String &name);

/// Source de lumière (composant « Light »). La position est celle du nœud
/// (en MONDE : une torche enfant d'un mur suit le mur), la direction d'un
/// spot est l'axe -Y local du nœud — une lampe pend « vers le bas » tant
/// qu'on ne la tourne pas.
struct LightDesc {
	LightKind kind = LightKind::POINT;
	sdl3::Color color{255, 196, 120, 255};
	float intensity = 2.f;
	/// Portée de coupure de l'atténuation, en unités (0 = sans limite).
	float range = 8.f;
	/// Demi-angle du cône d'un spot, en degrés.
	float spotAngle = 35.f;
	/// Adoucissement du bord du cône (0 = net, 1 = très diffus).
	float penumbra = 0.3f;
	/// Ombres portées. Le moteur n'en calcule que pour UNE lumière ponctuelle
	/// à la fois (cf. render3d::Canvas) : la première qui la demande.
	bool castShadow = false;

	[[nodiscard]] static bool Has(const scene::Node &node) noexcept;

	[[nodiscard]] static LightDesc Read(const scene::Node &node);

	void Write(scene::Node &node) const;

	static void Clear(scene::Node &node) { (void)node.RemoveComponent(String(component::LIGHT)); }
};

/// Point de vue (composant « Camera »). En mode Jeu, la caméra COURANTE de
/// la scène (la première marquée `current`) donne la vue — c'est ainsi
/// qu'un joueur à la première personne porte sa caméra à hauteur d'yeux.
struct CameraNodeDesc {
	float fovDegrees = 70.f;
	bool current = false;

	[[nodiscard]] static bool Has(const scene::Node &node) noexcept;

	[[nodiscard]] static CameraNodeDesc Read(const scene::Node &node);

	void Write(scene::Node &node) const;

	static void Clear(scene::Node &node) { (void)node.RemoveComponent(String(component::CAMERA)); }
};

/// Zone de déclenchement (composant « Trigger ») : boîte alignée sur les
/// axes, centrée sur le nœud. En mode Jeu, un objet étiqueté `player` (ou
/// tout corps dynamique) qui y entre appelle `on_trigger(zone, objet,
/// évènement)` dans le script de la scène et dans ceux attachés à la zone.
struct TriggerDesc {
	math::FVector3 halfExtents{1.f, 1.f, 1.f};
	/// Nom d'évènement libre transmis au script (« open_door », « win »…).
	String event;
	/// Ne se déclenche qu'une fois par partie.
	bool once = false;

	[[nodiscard]] static bool Has(const scene::Node &node) noexcept;

	[[nodiscard]] static TriggerDesc Read(const scene::Node &node);

	void Write(scene::Node &node) const;

	static void Clear(scene::Node &node) { (void)node.RemoveComponent(String(component::TRIGGER)); }
};

/// Script attaché (composant « Script ») : le NOM d'un script de la
/// bibliothèque du projet (cf. Project::scripts). Plusieurs nœuds peuvent
/// partager le même script — chacun le reçoit comme `self`.
struct ScriptRef {
	String script;

	[[nodiscard]] static bool Has(const scene::Node &node) noexcept;

	[[nodiscard]] static ScriptRef Read(const scene::Node &node);

	void Write(scene::Node &node) const;

	static void Clear(scene::Node &node) { (void)node.RemoveComponent(String(component::SCRIPT)); }
};

/// Un script de la bibliothèque du projet — c'est la ressource qu'ouvre
/// l'éditeur de code, et celle que les nœuds référencent par son nom.
struct ScriptAsset {
	String name;        ///< identifiant, ex. `torchlight` (sans extension)
	String description; ///< une ligne, affichée dans le navigateur de ressources
	String source;

	[[nodiscard]] data::NodePtr ToJson() const;

	[[nodiscard]] static Result<ScriptAsset, String> FromJson(const data::NodePtr &node);
};

/**
 * Description d'un objet à CRÉER. Ce n'est plus la forme de stockage (c'est
 * un `scene::Node` dans l'arbre de la scène) mais un constructeur : contenu
 * livré, import glTF, scripts, tests et copier-coller s'en servent pour dire
 * « je veux tel objet, là », et `ToNode()` fabrique le nœud correspondant.
 *
 * Les champs sont restés PLATS (shape, dimensions, material… au même niveau)
 * alors que le nœud, lui, les range dans des composants : un constructeur
 * gagne à être lisible d'un coup d'œil, et la conversion est faite une fois
 * ici plutôt que sur chaque site d'appel.
 */
struct NodeDesc {
	String name;
	/// Nom OU chemin du parent dans la scène ; vide = directement sous la
	/// racine. Un nom est cherché dans tout l'arbre (cf. SceneDesc::FindId),
	/// un chemin est résolu tel quel (`/Scene/Car/Wheels`).
	String parent;
	String tag;    ///< libre : "checkpoint", "road", "spawn"… lu par les scripts
	String source; ///< chemin du fichier source pour `ShapeKind::MODEL`
	ShapeKind shape = ShapeKind::BOX;
	math::FVector3 dimensions{1.f, 1.f, 1.f};
	int segments = 24;
	TransformDesc transform;
	MaterialDesc material;
	PhysicsDesc physics;
	bool visible = true;
	/// Type de nœud. Vide = déduit à la construction (un objet avec une forme
	/// est un `MeshInstance`, une lumière un `Light`, etc. ; sans rien, un
	/// groupe).
	String type;
	/// Composants facultatifs (absents par défaut).
	Option<LightDesc> light;
	Option<CameraNodeDesc> camera;
	Option<TriggerDesc> trigger;
	String script; ///< script attaché (nom dans Project::scripts), vide = aucun
	/// Autres composants, recopiés tels quels (2D, composants de greffons…).
	std::vector<scene::Component> components;

	/// Dossier d'organisation : un groupe dont le rôle est de RANGER.
	[[nodiscard]] static NodeDesc Folder(String folderName);

	/// Source de lumière sans géométrie.
	[[nodiscard]] static NodeDesc Light(String lightName, LightDesc lightDesc);

	/// Objet sans géométrie : un groupe, un point d'ancrage, un pivot.
	[[nodiscard]] static NodeDesc Group(String groupName);

	/// `false` pour un groupe : aucun composant d'apparence ne sera posé.
	bool hasVisual = true;

	[[nodiscard]] scene::Node ToNode() const;

	[[nodiscard]] static NodeDesc FromNode(const scene::Node &node);

	// ── Format 2 (liste plate) : lecture des projets existants ──────────────

	[[nodiscard]] static Result<NodeDesc, String> FromLegacyJson(const data::NodePtr &node);
};

// ============================================================================
// Modèles de nœuds (menu « Créer un nœud enfant »)
// ============================================================================

/// Une entrée du menu de création : `category` regroupe les entrées en
/// sous-menus (« 3D », « Lumière », « Logique », « Nœud »).
struct NodeTemplate {
	const char *key;
	const char *label;
	const char *category;
};

inline constexpr NodeTemplate NODE_TEMPLATES[] = {
	{"box", "Cube", "3D"},
	{"sphere", "Sphère", "3D"},
	{"cylinder", "Cylindre", "3D"},
	{"cone", "Cône", "3D"},
	{"plane", "Plan", "3D"},
	{"torus", "Tore", "3D"},
	{"point_light", "Lumière ponctuelle", "Lumière"},
	{"spot_light", "Projecteur", "Lumière"},
	{"trigger", "Déclencheur", "Logique"},
	{"camera", "Caméra", "Logique"},
	{"spawn", "Repère", "Logique"},
	{"node3d", "Nœud 3D", "Nœud"},
	{"folder", "Dossier", "Nœud"},
	{"rect2d", "Rectangle 2D", "2D"},
	{"circle2d", "Cercle 2D", "2D"},
	{"polygon2d", "Polygone 2D", "2D"},
	{"line2d", "Trait 2D", "2D"},
	{"sprite2d", "Image 2D", "2D"},
	{"label2d", "Texte 2D", "2D"},
	{"camera2d", "Caméra 2D", "2D"},
	{"node2d", "Nœud 2D", "2D"},
	{"canvas_layer", "Calque d'interface", "2D"},
};

/// Modèles 2D (cf. NODE_TEMPLATES, catégorie « 2D ») ; NONE pour une autre clé.
[[nodiscard]] Option<NodeDesc> MakeNode2DFromTemplate(const String &key, const String &label);

/// Fabrique le nœud d'un modèle, prêt à `Runtime::SpawnNode` (position
/// locale nulle : il apparaît au pivot de son parent). NONE pour une clé
/// inconnue.
[[nodiscard]] Option<NodeDesc> MakeNodeFromTemplate(const String &key);

/// Réglages d'ambiance d'une scène (lumière directionnelle + ambiante +
/// couleur de fond) — ce que `render3d::Canvas::SetLighting` consomme.
struct EnvironmentDesc {
	math::FVector3 sunDirection{0.3f, -0.7f, 0.45f};
	float sunIntensity = 0.95f;
	sdl3::Color sunColor{255, 247, 230, 255};
	sdl3::Color ambientColor{30, 32, 44, 255};
	sdl3::Color backgroundColor{18, 20, 28, 255};
	math::FVector3 gravity{0.f, -9.81f, 0.f};

	[[nodiscard]] data::NodePtr ToJson() const;

	[[nodiscard]] static EnvironmentDesc FromJson(const data::NodePtr &node);
};

/// Caméra enregistrée avec la scène (point de vue d'édition ET point de
/// départ du mode Jeu).
struct CameraDesc {
	math::FVector3 editPosition{0.f, 6.f, -14.f};
	float editYaw = 0.f;
	float editPitch = -0.25f;
	math::FVector3 playPosition{0.f, 2.f, -8.f};
	float playYaw = 0.f;
	float playPitch = -0.1f;

	[[nodiscard]] data::NodePtr ToJson() const;

	[[nodiscard]] static CameraDesc FromJson(const data::NodePtr &node);
};

/// Réglages du calque 2D d'une scène.
struct Canvas2DDesc {
	/// Résolution de RÉFÉRENCE : les coordonnées 2D sont des pixels de cet
	/// écran virtuel, mis à l'échelle (bandes si le rapport diffère) dans la
	/// vue réelle — une interface pensée en 1280×720 reste juste en 4K.
	int width = 1280;
	int height = 720;
	/// Faux : scène purement 2D — ni monde 3D, ni ciel : fond uni.
	bool render3d = true;
	sdl3::Color background{24, 26, 34, 255}; ///< fond d'une scène purement 2D

	[[nodiscard]] math::FVector2 Size() const noexcept;

	[[nodiscard]] data::NodePtr ToJson() const;

	[[nodiscard]] static Canvas2DDesc FromJson(const data::NodePtr &node);
};

/// Genre d'un document du projet : une SCÈNE (élément final : monde,
/// caméra, calque 2D, script de jeu) ou un OBJET (arborescence de nœuds
/// réutilisable, instanciée dans des scènes ou d'autres objets — cf.
/// objects.hpp).
enum class SceneKind : uint8_t { SCENE, OBJECT };

struct SceneDesc {
	String name;
	SceneKind kind = SceneKind::SCENE;
	String description;
	/// Script joué quand la scène passe en mode Jeu (chemin relatif ou
	/// source intégrée, cf. Project::ResolveScript).
	String gameplayScript;
	EnvironmentDesc environment;
	CameraDesc camera;
	Canvas2DDesc canvas; ///< calque 2D (résolution de référence, rendu 3D ou non)
	/// L'ARBRE de la scène. Sa racine porte le nom de la scène et n'est pas
	/// un objet : c'est le point d'accroche de tout le reste.
	scene::NodeTree tree{String("Scene")};

	[[nodiscard]] bool IsObject() const noexcept { return kind == SceneKind::OBJECT; }

	// ── Nom ──────────────────────────────────────────────────────────────────

	/// Renomme la scène ET la racine de son arbre. Les deux doivent rester
	/// d'accord : c'est le nom de la RACINE qui apparaît dans les chemins
	/// (`/Vitrine/Tore`), et un chemin écrit à la main dans un script part
	/// naturellement du nom de la scène.
	void SetName(String sceneName);

	// ── Recherche ────────────────────────────────────────────────────────────

	[[nodiscard]] scene::NodeId RootId() const noexcept { return tree.Root(); }

	/// Cherche par NOM dans tout l'arbre (premier trouvé en parcours préfixe).
	/// L'éditeur garde des noms uniques à l'échelle de la scène (cf.
	/// UniqueName) pour que cette recherche — celle qu'emploient les scripts
	/// et les tests — reste sans ambiguïté, même si la bibliothèque, elle,
	/// n'impose l'unicité qu'entre frères.
	[[nodiscard]] scene::NodeId FindId(const String &objectName) const;

	[[nodiscard]] scene::Node *Find(const String &objectName) noexcept;

	[[nodiscard]] const scene::Node *Find(const String &objectName) const noexcept;

	/// Résout un chemin (`/Scene/Car/Wheels`) ou, à défaut, un nom.
	[[nodiscard]] scene::NodeId Resolve(const String &pathOrName) const;

	/// Nombre d'objets (l'arbre moins sa racine).
	[[nodiscard]] size_t NodeCount() const noexcept { return tree.Size() - 1; }

	/// Tous les objets, dans l'ordre d'affichage (racine exclue).
	[[nodiscard]] std::vector<scene::NodeId> Nodes() const;

	/// `base`, `base 2`, `base 3`… — le premier nom libre DANS TOUTE LA SCÈNE
	/// (et non seulement entre frères) : les scripts et le rapport désignent
	/// les objets par leur nom seul.
	[[nodiscard]] String UniqueName(const String &base) const;

	// ── Mutations ────────────────────────────────────────────────────────────

	/// Ajoute un objet et rend son identifiant. Le parent est celui nommé par
	/// `object.parent` (nom ou chemin), la racine à défaut.
	scene::NodeId AddNode(NodeDesc object);

	/// Compat : ajoute et rend le NOM retenu (l'ancienne signature).
	String Add(NodeDesc object);

	/// Supprime un objet ET tout son sous-arbre. C'est un changement de
	/// comportement assumé par rapport à la liste plate, où les enfants
	/// remontaient à la racine : supprimer une voiture doit supprimer ses
	/// roues, pas les éparpiller dans la scène.
	bool Remove(const String &objectName);

	[[nodiscard]] std::vector<scene::NodeId> WithTag(const String &wantedTag) const;

	// ── Sérialisation ────────────────────────────────────────────────────────

	[[nodiscard]] data::NodePtr ToJson() const;

	[[nodiscard]] static Result<SceneDesc, String> FromJson(const data::NodePtr &node);

	/**
	 * Reconstruit l'arbre à partir d'une liste plate (format 2). Deux passes :
	 * tous les objets sont d'abord créés sous la racine, puis re-parentés —
	 * un objet peut en effet citer comme parent un objet déclaré APRÈS lui,
	 * ce que la liste plate autorisait.
	 *
	 * Le re-parentage est fait en KEEP_LOCAL : dans l'ancien format, le
	 * transform écrit était déjà relatif au parent (c'est ce que faisait
	 * `render3d::SetSceneParent` côté runtime), il ne faut donc surtout pas
	 * le recalculer.
	 */
	[[nodiscard]] static Option<String> FromLegacyObjects(SceneDesc &scene, const data::NodePtr &array);
};

// ============================================================================
// Project
// ============================================================================

class ProjectFs;

class Project {
public:
	/// Le projet vu comme un système de fichiers (cf. project_fs.hpp) : LA
	/// façon d'accéder aux scènes, objets, scripts, ressources et nœuds.
	[[nodiscard]] ProjectFs Fs() noexcept;
	[[nodiscard]] ProjectFs Fs() const noexcept;

	String name = "Projet sans titre";
	std::vector<SceneDesc> scenes;
	String activeScene;
	/// Bibliothèque de scripts de comportement, attachés aux nœuds par leur
	/// nom (cf. ScriptRef). Distincte du script de JEU de chaque scène, qui
	/// orchestre la partie entière.
	std::vector<ScriptAsset> scripts;

	[[nodiscard]] ScriptAsset *FindScript(const String &scriptName) noexcept;

	[[nodiscard]] const ScriptAsset *FindScript(const String &scriptName) const noexcept;

	[[nodiscard]] SceneDesc *FindScene(const String &sceneName) noexcept;

	[[nodiscard]] const SceneDesc *FindScene(const String &sceneName) const noexcept;

	/// Objet (document de genre OBJECT) de ce nom, nul sinon.
	[[nodiscard]] SceneDesc *FindObject(const String &objectName) noexcept;

	[[nodiscard]] const SceneDesc *FindObject(const String &objectName) const noexcept;

	/// Noms des objets du projet, dans l'ordre du projet.
	[[nodiscard]] std::vector<String> ObjectNames() const;

	/// Nombre de SCÈNES (objets exclus).
	[[nodiscard]] size_t SceneCount() const noexcept;

	/// Scène active, ou la première scène si `activeScene` ne résout pas —
	/// `nullptr` uniquement pour un projet sans AUCUNE scène.
	[[nodiscard]] SceneDesc *ActiveScene() noexcept;

	[[nodiscard]] const SceneDesc *ActiveScene() const noexcept;

	bool SetActiveScene(const String &sceneName);

	/// Noms des SCÈNES (objets exclus).
	[[nodiscard]] std::vector<String> SceneNames() const;

	[[nodiscard]] size_t TotalNodeCount() const noexcept;

	// ── Sérialisation ────────────────────────────────────────────────────────

	[[nodiscard]] data::NodePtr ToJson() const;

	[[nodiscard]] static Result<Project, String> FromJson(const data::NodePtr &root);

	/// Version du format de projet. Un fichier plus récent est REFUSÉ
	/// explicitement plutôt que lu de travers ; un fichier PLUS ANCIEN est
	/// lu (la version 2 — liste plate d'objets — est convertie en arbre, cf.
	/// SceneDesc::FromLegacyObjects).
	/// Version 4 : bibliothèque de scripts (`scripts`) et composants Light /
	/// Camera / Trigger / Script — un fichier v3 se lit tel quel.
	static constexpr int FORMAT_VERSION = 4;

	[[nodiscard]] String EncodeJson() const;

	[[nodiscard]] static Result<Project, String> DecodeJson(const String &text);
};

} // namespace game_editor
