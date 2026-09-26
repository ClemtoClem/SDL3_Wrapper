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
 * arbre à profondeur libre. Ce qui était les champs d'un `ObjectDesc` est
 * devenu des COMPOSANTS attachés au nœud :
 *
 *     ObjectDesc{shape, dimensions, segments, source, material}
 *         -> composant « MeshInstance »   (cf. VisualDesc::Read/Write)
 *     ObjectDesc{body, collider, halfExtents, mass, ...}
 *         -> composant « RigidBody »      (cf. PhysicsDesc::Read/Write)
 *     ObjectDesc{tag}    -> propriété libre « tag » du nœud
 *     ObjectDesc{name, transform, visible} -> champs propres du nœud
 *
 * `ObjectDesc` SURVIT, mais comme CONSTRUCTEUR : il décrit un objet à créer
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
	/// `ObjectDesc::source`, la géométrie est relue à chaque construction du
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

[[nodiscard]] inline const char *ShapeKindName(ShapeKind kind) noexcept {
	switch (kind) {
		case ShapeKind::BOX:
			return "box";
		case ShapeKind::SPHERE:
			return "sphere";
		case ShapeKind::CYLINDER:
			return "cylinder";
		case ShapeKind::CONE:
			return "cone";
		case ShapeKind::TORUS:
			return "torus";
		case ShapeKind::PLANE:
			return "plane";
		case ShapeKind::ICOSAHEDRON:
			return "icosahedron";
		case ShapeKind::TORUS_KNOT:
			return "torus_knot";
		case ShapeKind::PORTAL_QUAD:
			return "portal";
		case ShapeKind::MODEL:
			return "model";
	}
	return "box";
}

[[nodiscard]] inline Option<ShapeKind> ShapeKindFromName(const String &name) {
	struct Entry {
		const char *name;
		ShapeKind kind;
	};
	static constexpr Entry TABLE[] = {
		{"box", ShapeKind::BOX},           {"cube", ShapeKind::BOX},
		{"sphere", ShapeKind::SPHERE},     {"cylinder", ShapeKind::CYLINDER},
		{"cone", ShapeKind::CONE},         {"torus", ShapeKind::TORUS},
		{"plane", ShapeKind::PLANE},       {"icosahedron", ShapeKind::ICOSAHEDRON},
		{"torus_knot", ShapeKind::TORUS_KNOT}, {"portal", ShapeKind::PORTAL_QUAD},
		{"model", ShapeKind::MODEL},           {"gltf", ShapeKind::MODEL},
	};
	String key = name.ToLower();
	for (const Entry &entry : TABLE)
		if (key == entry.name)
			return Some(entry.kind);
	return NONE;
}

[[nodiscard]] inline const char *MaterialKindName(MaterialKind kind) noexcept {
	switch (kind) {
		case MaterialKind::PLASTIC:
			return "plastic";
		case MaterialKind::METAL:
			return "metal";
		case MaterialKind::WOOD:
			return "wood";
		case MaterialKind::PBR:
			return "pbr";
		case MaterialKind::UNLIT:
			return "unlit";
		case MaterialKind::BASIC:
			return "basic";
	}
	return "plastic";
}

[[nodiscard]] inline Option<MaterialKind> MaterialKindFromName(const String &name) {
	struct Entry {
		const char *name;
		MaterialKind kind;
	};
	static constexpr Entry TABLE[] = {
		{"plastic", MaterialKind::PLASTIC}, {"metal", MaterialKind::METAL}, {"wood", MaterialKind::WOOD},
		{"pbr", MaterialKind::PBR},         {"unlit", MaterialKind::UNLIT}, {"basic", MaterialKind::BASIC},
	};
	String key = name.ToLower();
	for (const Entry &entry : TABLE)
		if (key == entry.name)
			return Some(entry.kind);
	return NONE;
}

[[nodiscard]] inline const char *BodyKindName(BodyKind kind) noexcept {
	switch (kind) {
		case BodyKind::NONE:
			return "none";
		case BodyKind::STATIC:
			return "static";
		case BodyKind::DYNAMIC:
			return "dynamic";
	}
	return "none";
}

[[nodiscard]] inline Option<BodyKind> BodyKindFromName(const String &name) {
	String key = name.ToLower();
	if (key == "none")
		return Some(BodyKind::NONE);
	if (key == "static")
		return Some(BodyKind::STATIC);
	if (key == "dynamic")
		return Some(BodyKind::DYNAMIC);
	return NONE;
}

[[nodiscard]] inline const char *ColliderKindName(ColliderKind kind) noexcept {
	switch (kind) {
		case ColliderKind::BOX:
			return "box";
		case ColliderKind::SPHERE:
			return "sphere";
		case ColliderKind::CAPSULE:
			return "capsule";
	}
	return "box";
}

[[nodiscard]] inline Option<ColliderKind> ColliderKindFromName(const String &name) {
	String key = name.ToLower();
	if (key == "box")
		return Some(ColliderKind::BOX);
	if (key == "sphere")
		return Some(ColliderKind::SPHERE);
	if (key == "capsule")
		return Some(ColliderKind::CAPSULE);
	return NONE;
}

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
[[nodiscard]] inline float Float(const data::NodePtr &node, float fallback = 0.f) noexcept {
	if (!node)
		return fallback;
	if (node->IsInt())
		return float(node->intValue);
	if (node->IsFloat())
		return float(node->floatValue);
	return fallback;
}

[[nodiscard]] inline int Int(const data::NodePtr &node, int fallback = 0) noexcept {
	if (!node)
		return fallback;
	if (node->IsInt())
		return int(node->intValue);
	if (node->IsFloat())
		return int(node->floatValue);
	return fallback;
}

[[nodiscard]] inline bool Bool(const data::NodePtr &node, bool fallback = false) noexcept {
	return (node && node->IsBool()) ? node->boolValue : fallback;
}

[[nodiscard]] inline String Str(const data::NodePtr &node, const char *fallback = "") {
	return (node && node->IsString()) ? node->stringValue : String(fallback);
}

[[nodiscard]] inline data::NodePtr FromVec3(const math::FVector3 &v) {
	auto array = data::Node::MakeArray();
	array->Push(data::Node::MakeFloat(double(v.x)));
	array->Push(data::Node::MakeFloat(double(v.y)));
	array->Push(data::Node::MakeFloat(double(v.z)));
	return array;
}

[[nodiscard]] inline math::FVector3 ToVec3(const data::NodePtr &node, math::FVector3 fallback = {}) {
	if (!node || !node->IsArray() || node->GetSize() < 3)
		return fallback;
	return {Float(node->At(0)), Float(node->At(1)), Float(node->At(2))};
}

[[nodiscard]] inline data::NodePtr FromColor(const sdl3::Color &c) {
	auto array = data::Node::MakeArray();
	array->Push(data::Node::MakeInt(c.r));
	array->Push(data::Node::MakeInt(c.g));
	array->Push(data::Node::MakeInt(c.b));
	return array;
}

[[nodiscard]] inline sdl3::Color ToColor(const data::NodePtr &node, sdl3::Color fallback = {255, 255, 255, 255}) {
	if (!node || !node->IsArray() || node->GetSize() < 3)
		return fallback;
	auto channel = [](const data::NodePtr &n) -> uint8_t {
		int v = Int(n, 255);
		return uint8_t(v < 0 ? 0 : (v > 255 ? 255 : v));
	};
	return sdl3::Color{channel(node->At(0)), channel(node->At(1)), channel(node->At(2)), 255};
}

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
} // namespace component

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
} // namespace node_kind

/// Lecture d'une propriété de composant, avec valeur par défaut — le motif
/// commun de tous les `Read()` ci-dessous.
[[nodiscard]] inline const scene::PropertyValue *ComponentProp(const scene::Node &node, const char *componentType,
															   const char *key) {
	const scene::Component *component = node.FindComponent(String(componentType));
	return component ? component->props.Find(String(key)) : nullptr;
}

/// Propriété libre portée par le nœud : étiquette lue par les scripts
/// (« checkpoint », « road », « portal_a »…).
inline constexpr const char *PROP_TAG = "tag";

[[nodiscard]] inline String TagOf(const scene::Node &node) {
	const scene::PropertyValue *tag = node.Get(String(PROP_TAG));
	return tag ? tag->AsString() : String();
}

inline void SetTag(scene::Node &node, const String &tag) {
	if (tag.IsEmpty())
		node.properties.Remove(String(PROP_TAG));
	else
		node.Set(String(PROP_TAG), scene::PropertyValue::Str(tag));
}

struct MaterialDesc {
	MaterialKind kind = MaterialKind::PLASTIC;
	sdl3::Color baseColor{180, 180, 190, 255};
	float metallic = 0.f;
	float roughness = 0.5f;
	bool doubleSided = false;
	bool wireframe = false;

	[[nodiscard]] data::NodePtr ToJson() const {
		auto node = data::Node::MakeObject();
		node->Set("kind", data::Node::MakeString(MaterialKindName(kind)));
		node->Set("base_color", json::FromColor(baseColor));
		node->Set("metallic", data::Node::MakeFloat(double(metallic)));
		node->Set("roughness", data::Node::MakeFloat(double(roughness)));
		node->Set("double_sided", data::Node::MakeBool(doubleSided));
		node->Set("wireframe", data::Node::MakeBool(wireframe));
		return node;
	}

	[[nodiscard]] static MaterialDesc FromJson(const data::NodePtr &node) {
		MaterialDesc desc;
		if (!node || !node->IsObject())
			return desc;
		desc.kind = MaterialKindFromName(json::Str(node->Get("kind"), "plastic")).UnwrapOr(MaterialKind::PLASTIC);
		desc.baseColor = json::ToColor(node->Get("base_color"), desc.baseColor);
		desc.metallic = json::Float(node->Get("metallic"), 0.f);
		desc.roughness = json::Float(node->Get("roughness"), 0.5f);
		desc.doubleSided = json::Bool(node->Get("double_sided"), false);
		desc.wireframe = json::Bool(node->Get("wireframe"), false);
		return desc;
	}
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

	[[nodiscard]] static bool Has(const scene::Node &node) noexcept {
		return node.HasComponent(String(component::BODY));
	}

	/// Lit le composant physique d'un nœud (valeurs par défaut s'il n'en a
	/// pas — un nœud sans corps se lit comme « BodyKind::NONE »).
	[[nodiscard]] static PhysicsDesc Read(const scene::Node &node) {
		PhysicsDesc desc;
		const scene::Component *component = node.FindComponent(String(component::BODY));
		if (!component)
			return desc;
		const scene::PropertyMap &props = component->props;
		auto str = [&props](const char *key, const char *fallback) -> String {
			const scene::PropertyValue *value = props.Find(String(key));
			return value ? value->AsString(fallback) : String(fallback);
		};
		auto number = [&props](const char *key, float fallback) -> float {
			const scene::PropertyValue *value = props.Find(String(key));
			return value ? value->AsFloat(fallback) : fallback;
		};
		desc.body = BodyKindFromName(str("body", "none")).UnwrapOr(BodyKind::NONE);
		desc.collider = ColliderKindFromName(str("collider", "box")).UnwrapOr(ColliderKind::BOX);
		if (const scene::PropertyValue *extents = props.Find(String("half_extents")))
			desc.halfExtents = extents->AsVec3(desc.halfExtents);
		desc.mass = number("mass", 1.f);
		desc.restitution = number("restitution", 0.3f);
		desc.friction = number("friction", 0.5f);
		return desc;
	}

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
	void Write(scene::Node &node) const {
		if (body == BodyKind::NONE && *this == PhysicsDesc{}) {
			(void)node.RemoveComponent(String(component::BODY));
			return;
		}
		scene::Component component;
		component.type = String(component::BODY);
		component.props.Set(String("body"), scene::PropertyValue::Str(String(BodyKindName(body))));
		component.props.Set(String("collider"), scene::PropertyValue::Str(String(ColliderKindName(collider))));
		component.props.Set(String("half_extents"), scene::PropertyValue::Vec3(halfExtents));
		component.props.Set(String("mass"), scene::PropertyValue::Float(double(mass)));
		component.props.Set(String("restitution"), scene::PropertyValue::Float(double(restitution)));
		component.props.Set(String("friction"), scene::PropertyValue::Float(double(friction)));
		node.SetComponent(std::move(component));
	}

	// ── Format 2 (liste plate) : lecture seule, pour les projets existants ──

	[[nodiscard]] data::NodePtr ToJson() const {
		auto node = data::Node::MakeObject();
		node->Set("body", data::Node::MakeString(BodyKindName(body)));
		node->Set("collider", data::Node::MakeString(ColliderKindName(collider)));
		node->Set("half_extents", json::FromVec3(halfExtents));
		node->Set("mass", data::Node::MakeFloat(double(mass)));
		node->Set("restitution", data::Node::MakeFloat(double(restitution)));
		node->Set("friction", data::Node::MakeFloat(double(friction)));
		return node;
	}

	[[nodiscard]] static PhysicsDesc FromJson(const data::NodePtr &node) {
		PhysicsDesc desc;
		if (!node || !node->IsObject())
			return desc;
		desc.body = BodyKindFromName(json::Str(node->Get("body"), "none")).UnwrapOr(BodyKind::NONE);
		desc.collider = ColliderKindFromName(json::Str(node->Get("collider"), "box")).UnwrapOr(ColliderKind::BOX);
		desc.halfExtents = json::ToVec3(node->Get("half_extents"), math::FVector3{0.5f, 0.5f, 0.5f});
		desc.mass = json::Float(node->Get("mass"), 1.f);
		desc.restitution = json::Float(node->Get("restitution"), 0.3f);
		desc.friction = json::Float(node->Get("friction"), 0.5f);
		return desc;
	}
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

	[[nodiscard]] static bool Has(const scene::Node &node) noexcept {
		return node.HasComponent(String(component::VISUAL));
	}

	[[nodiscard]] static VisualDesc Read(const scene::Node &node) {
		VisualDesc desc;
		const scene::Component *component = node.FindComponent(String(component::VISUAL));
		if (!component)
			return desc;
		const scene::PropertyMap &props = component->props;
		auto value = [&props](const char *key) { return props.Find(String(key)); };
		if (const scene::PropertyValue *shapeValue = value("shape"))
			desc.shape = ShapeKindFromName(shapeValue->AsString("box")).UnwrapOr(ShapeKind::BOX);
		if (const scene::PropertyValue *dims = value("dimensions"))
			desc.dimensions = dims->AsVec3(desc.dimensions);
		if (const scene::PropertyValue *segments = value("segments"))
			desc.segments = int(segments->AsInt(24));
		if (const scene::PropertyValue *source = value("source"))
			desc.source = source->AsString();
		if (const scene::PropertyValue *kind = value("material"))
			desc.material.kind = MaterialKindFromName(kind->AsString("plastic")).UnwrapOr(MaterialKind::PLASTIC);
		if (const scene::PropertyValue *color = value("base_color"))
			desc.material.baseColor = color->AsColor(desc.material.baseColor);
		if (const scene::PropertyValue *metallic = value("metallic"))
			desc.material.metallic = metallic->AsFloat(0.f);
		if (const scene::PropertyValue *roughness = value("roughness"))
			desc.material.roughness = roughness->AsFloat(0.5f);
		if (const scene::PropertyValue *doubleSided = value("double_sided"))
			desc.material.doubleSided = doubleSided->AsBool(false);
		if (const scene::PropertyValue *wireframe = value("wireframe"))
			desc.material.wireframe = wireframe->AsBool(false);
		return desc;
	}

	void Write(scene::Node &node) const {
		scene::Component component;
		component.type = String(component::VISUAL);
		component.props.Set(String("shape"), scene::PropertyValue::Str(String(ShapeKindName(shape))));
		component.props.Set(String("dimensions"), scene::PropertyValue::Vec3(dimensions));
		component.props.Set(String("segments"), scene::PropertyValue::Int(segments));
		if (!source.IsEmpty())
			component.props.Set(String("source"), scene::PropertyValue::Resource(source));
		component.props.Set(String("material"), scene::PropertyValue::Str(String(MaterialKindName(material.kind))));
		component.props.Set(String("base_color"), scene::PropertyValue::Color(material.baseColor));
		component.props.Set(String("metallic"), scene::PropertyValue::Float(double(material.metallic)));
		component.props.Set(String("roughness"), scene::PropertyValue::Float(double(material.roughness)));
		component.props.Set(String("double_sided"), scene::PropertyValue::Bool(material.doubleSided));
		component.props.Set(String("wireframe"), scene::PropertyValue::Bool(material.wireframe));
		node.SetComponent(std::move(component));
	}

	/// Retire l'apparence : le nœud devient un groupe.
	static void Clear(scene::Node &node) { (void)node.RemoveComponent(String(component::VISUAL)); }
};

// ============================================================================
// Composants de lumière, caméra, déclencheur et script
// ============================================================================

enum class LightKind : uint8_t { POINT, SPOT };

[[nodiscard]] inline const char *LightKindName(LightKind kind) noexcept {
	return kind == LightKind::SPOT ? "spot" : "point";
}

[[nodiscard]] inline Option<LightKind> LightKindFromName(const String &name) {
	String key = name.ToLower();
	if (key == "point")
		return Some(LightKind::POINT);
	if (key == "spot")
		return Some(LightKind::SPOT);
	return NONE;
}

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

	[[nodiscard]] static bool Has(const scene::Node &node) noexcept {
		return node.HasComponent(String(component::LIGHT));
	}

	[[nodiscard]] static LightDesc Read(const scene::Node &node) {
		LightDesc desc;
		auto prop = [&node](const char *key) { return ComponentProp(node, component::LIGHT, key); };
		if (const auto *v = prop("kind"))
			desc.kind = LightKindFromName(v->AsString("point")).UnwrapOr(LightKind::POINT);
		if (const auto *v = prop("color"))
			desc.color = v->AsColor(desc.color);
		if (const auto *v = prop("intensity"))
			desc.intensity = v->AsFloat(desc.intensity);
		if (const auto *v = prop("range"))
			desc.range = v->AsFloat(desc.range);
		if (const auto *v = prop("spot_angle"))
			desc.spotAngle = v->AsFloat(desc.spotAngle);
		if (const auto *v = prop("penumbra"))
			desc.penumbra = v->AsFloat(desc.penumbra);
		if (const auto *v = prop("cast_shadow"))
			desc.castShadow = v->AsBool(desc.castShadow);
		return desc;
	}

	void Write(scene::Node &node) const {
		scene::Component component;
		component.type = String(component::LIGHT);
		component.props.Set(String("kind"), scene::PropertyValue::Str(String(LightKindName(kind))));
		component.props.Set(String("color"), scene::PropertyValue::Color(color));
		component.props.Set(String("intensity"), scene::PropertyValue::Float(double(intensity)));
		component.props.Set(String("range"), scene::PropertyValue::Float(double(range)));
		component.props.Set(String("spot_angle"), scene::PropertyValue::Float(double(spotAngle)));
		component.props.Set(String("penumbra"), scene::PropertyValue::Float(double(penumbra)));
		component.props.Set(String("cast_shadow"), scene::PropertyValue::Bool(castShadow));
		node.SetComponent(std::move(component));
	}

	static void Clear(scene::Node &node) { (void)node.RemoveComponent(String(component::LIGHT)); }
};

/// Point de vue (composant « Camera »). En mode Jeu, la caméra COURANTE de
/// la scène (la première marquée `current`) donne la vue — c'est ainsi
/// qu'un joueur à la première personne porte sa caméra à hauteur d'yeux.
struct CameraNodeDesc {
	float fovDegrees = 70.f;
	bool current = false;

	[[nodiscard]] static bool Has(const scene::Node &node) noexcept {
		return node.HasComponent(String(component::CAMERA));
	}

	[[nodiscard]] static CameraNodeDesc Read(const scene::Node &node) {
		CameraNodeDesc desc;
		if (const auto *v = ComponentProp(node, component::CAMERA, "fov"))
			desc.fovDegrees = v->AsFloat(desc.fovDegrees);
		if (const auto *v = ComponentProp(node, component::CAMERA, "current"))
			desc.current = v->AsBool(desc.current);
		return desc;
	}

	void Write(scene::Node &node) const {
		scene::Component component;
		component.type = String(component::CAMERA);
		component.props.Set(String("fov"), scene::PropertyValue::Float(double(fovDegrees)));
		component.props.Set(String("current"), scene::PropertyValue::Bool(current));
		node.SetComponent(std::move(component));
	}

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

	[[nodiscard]] static bool Has(const scene::Node &node) noexcept {
		return node.HasComponent(String(component::TRIGGER));
	}

	[[nodiscard]] static TriggerDesc Read(const scene::Node &node) {
		TriggerDesc desc;
		if (const auto *v = ComponentProp(node, component::TRIGGER, "half_extents"))
			desc.halfExtents = v->AsVec3(desc.halfExtents);
		if (const auto *v = ComponentProp(node, component::TRIGGER, "event"))
			desc.event = v->AsString();
		if (const auto *v = ComponentProp(node, component::TRIGGER, "once"))
			desc.once = v->AsBool(false);
		return desc;
	}

	void Write(scene::Node &node) const {
		scene::Component component;
		component.type = String(component::TRIGGER);
		component.props.Set(String("half_extents"), scene::PropertyValue::Vec3(halfExtents));
		component.props.Set(String("event"), scene::PropertyValue::Str(event));
		component.props.Set(String("once"), scene::PropertyValue::Bool(once));
		node.SetComponent(std::move(component));
	}

	static void Clear(scene::Node &node) { (void)node.RemoveComponent(String(component::TRIGGER)); }
};

/// Script attaché (composant « Script ») : le NOM d'un script de la
/// bibliothèque du projet (cf. Project::scripts). Plusieurs nœuds peuvent
/// partager le même script — chacun le reçoit comme `self`.
struct ScriptRef {
	String script;

	[[nodiscard]] static bool Has(const scene::Node &node) noexcept {
		return node.HasComponent(String(component::SCRIPT));
	}

	[[nodiscard]] static ScriptRef Read(const scene::Node &node) {
		ScriptRef ref;
		if (const auto *v = ComponentProp(node, component::SCRIPT, "script"))
			ref.script = v->AsString();
		return ref;
	}

	void Write(scene::Node &node) const {
		scene::Component component;
		component.type = String(component::SCRIPT);
		component.props.Set(String("script"), scene::PropertyValue::Str(script));
		node.SetComponent(std::move(component));
	}

	static void Clear(scene::Node &node) { (void)node.RemoveComponent(String(component::SCRIPT)); }
};

/// Un script de la bibliothèque du projet — c'est la ressource qu'ouvre
/// l'éditeur de code, et celle que les nœuds référencent par son nom.
struct ScriptAsset {
	String name;        ///< identifiant, ex. `torchlight` (sans extension)
	String description; ///< une ligne, affichée dans le navigateur de ressources
	String source;

	[[nodiscard]] data::NodePtr ToJson() const {
		auto node = data::Node::MakeObject();
		node->Set("name", data::Node::MakeString(name));
		node->Set("description", data::Node::MakeString(description));
		node->Set("source", data::Node::MakeString(source));
		return node;
	}

	[[nodiscard]] static Result<ScriptAsset, String> FromJson(const data::NodePtr &node) {
		if (!node || !node->IsObject())
			return Err(String("script : objet JSON attendu"));
		ScriptAsset asset;
		asset.name = json::Str(node->Get("name"));
		if (asset.name.IsEmpty())
			return Err(String("script : champ `name` manquant ou vide"));
		asset.description = json::Str(node->Get("description"));
		asset.source = json::Str(node->Get("source"));
		return Ok(std::move(asset));
	}
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
struct ObjectDesc {
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

	/// Dossier d'organisation : un groupe dont le rôle est de RANGER.
	[[nodiscard]] static ObjectDesc Folder(String folderName) {
		ObjectDesc desc = Group(std::move(folderName));
		desc.type = String(node_kind::FOLDER);
		return desc;
	}

	/// Source de lumière sans géométrie.
	[[nodiscard]] static ObjectDesc Light(String lightName, LightDesc lightDesc) {
		ObjectDesc desc = Group(std::move(lightName));
		desc.type = String(node_kind::LIGHT);
		desc.light = Some(lightDesc);
		return desc;
	}

	/// Objet sans géométrie : un groupe, un point d'ancrage, un pivot.
	[[nodiscard]] static ObjectDesc Group(String groupName) {
		ObjectDesc desc;
		desc.name = std::move(groupName);
		desc.type = String(node_kind::GROUP);
		desc.hasVisual = false;
		return desc;
	}

	/// `false` pour un groupe : aucun composant d'apparence ne sera posé.
	bool hasVisual = true;

	[[nodiscard]] scene::Node ToNode() const {
		scene::Node node;
		node.name = name;
		node.transform = transform;
		node.visible = visible;
		node.type = !type.IsEmpty()  ? type
					: hasVisual        ? String(node_kind::MESH)
					: light.IsSome()   ? String(node_kind::LIGHT)
					: camera.IsSome()  ? String(node_kind::CAMERA)
					: trigger.IsSome() ? String(node_kind::TRIGGER)
									   : String(node_kind::GROUP);
		if (light.IsSome())
			light.Value().Write(node);
		if (camera.IsSome())
			camera.Value().Write(node);
		if (trigger.IsSome())
			trigger.Value().Write(node);
		if (!script.IsEmpty())
			ScriptRef{script}.Write(node);
		if (hasVisual) {
			VisualDesc visual;
			visual.shape = shape;
			visual.dimensions = dimensions;
			visual.segments = segments;
			visual.source = source;
			visual.material = material;
			visual.Write(node);
		}
		physics.Write(node);
		SetTag(node, tag);
		return node;
	}

	[[nodiscard]] static ObjectDesc FromNode(const scene::Node &node) {
		ObjectDesc desc;
		desc.name = node.name;
		desc.type = node.type;
		desc.transform = node.transform;
		desc.visible = node.visible;
		desc.tag = TagOf(node);
		desc.hasVisual = VisualDesc::Has(node);
		VisualDesc visual = VisualDesc::Read(node);
		desc.shape = visual.shape;
		desc.dimensions = visual.dimensions;
		desc.segments = visual.segments;
		desc.source = visual.source;
		desc.material = visual.material;
		desc.physics = PhysicsDesc::Read(node);
		if (LightDesc::Has(node))
			desc.light = Some(LightDesc::Read(node));
		if (CameraNodeDesc::Has(node))
			desc.camera = Some(CameraNodeDesc::Read(node));
		if (TriggerDesc::Has(node))
			desc.trigger = Some(TriggerDesc::Read(node));
		desc.script = ScriptRef::Read(node).script;
		return desc;
	}

	// ── Format 2 (liste plate) : lecture des projets existants ──────────────

	[[nodiscard]] static Result<ObjectDesc, String> FromLegacyJson(const data::NodePtr &node) {
		if (!node || !node->IsObject())
			return Err(String("objet : objet JSON attendu"));
		ObjectDesc desc;
		desc.name = json::Str(node->Get("name"));
		if (desc.name.IsEmpty())
			return Err(String("objet : champ `name` manquant ou vide"));
		desc.parent = json::Str(node->Get("parent"));
		desc.tag = json::Str(node->Get("tag"));
		desc.source = json::Str(node->Get("source"));
		desc.shape = ShapeKindFromName(json::Str(node->Get("shape"), "box")).UnwrapOr(ShapeKind::BOX);
		desc.dimensions = json::ToVec3(node->Get("dimensions"), math::FVector3{1.f, 1.f, 1.f});
		desc.segments = json::Int(node->Get("segments"), 24);
		desc.visible = json::Bool(node->Get("visible"), true);
		if (auto transform = node->Get("transform"); transform && transform->IsObject()) {
			desc.transform.position = json::ToVec3(transform->Get("position"));
			desc.transform.SetEulerDegrees(json::ToVec3(transform->Get("rotation")));
			desc.transform.scale = json::ToVec3(transform->Get("scale"), math::FVector3{1.f, 1.f, 1.f});
		}
		desc.material = MaterialDesc::FromJson(node->Get("material"));
		desc.physics = PhysicsDesc::FromJson(node->Get("physics"));
		return Ok(std::move(desc));
	}
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
};

/// Fabrique le nœud d'un modèle, prêt à `Runtime::SpawnNode` (position
/// locale nulle : il apparaît au pivot de son parent). NONE pour une clé
/// inconnue.
[[nodiscard]] inline Option<ObjectDesc> MakeNodeFromTemplate(const String &key) {
	const char *label = nullptr;
	for (const NodeTemplate &entry : NODE_TEMPLATES)
		if (key == entry.key)
			label = entry.label;
	if (!label)
		return NONE;

	if (Option<ShapeKind> shape = ShapeKindFromName(key); shape.IsSome()) {
		ObjectDesc desc;
		desc.name = String(label);
		desc.shape = shape.Unwrap();
		desc.dimensions = shape.Unwrap() == ShapeKind::PLANE   ? math::FVector3{4.f, 1.f, 4.f}
						  : shape.Unwrap() == ShapeKind::TORUS ? math::FVector3{2.f, 0.6f, 2.f}
															   : math::FVector3{1.f, 1.f, 1.f};
		desc.material.baseColor = sdl3::Color{168, 176, 196, 255};
		desc.physics.halfExtents = desc.dimensions * 0.5f;
		return Some(std::move(desc));
	}
	if (key == "point_light" || key == "spot_light") {
		LightDesc light;
		light.kind = key == "spot_light" ? LightKind::SPOT : LightKind::POINT;
		light.color = sdl3::Color{255, 238, 210, 255};
		return Some(ObjectDesc::Light(String(label), light));
	}
	if (key == "folder")
		return Some(ObjectDesc::Folder(String(label)));
	ObjectDesc desc = ObjectDesc::Group(String(label));
	if (key == "trigger") {
		desc.type = String(node_kind::TRIGGER);
		desc.trigger = Some(TriggerDesc{});
	} else if (key == "camera") {
		desc.type = String(node_kind::CAMERA);
		desc.camera = Some(CameraNodeDesc{});
	} else if (key == "spawn") {
		desc.type = String(node_kind::SPAWN);
	} else {
		desc.type = String(scene::node_type::NODE3D);
	}
	return Some(std::move(desc));
}

/// Réglages d'ambiance d'une scène (lumière directionnelle + ambiante +
/// couleur de fond) — ce que `render3d::Canvas::SetLighting` consomme.
struct EnvironmentDesc {
	math::FVector3 sunDirection{0.3f, -0.7f, 0.45f};
	float sunIntensity = 0.95f;
	sdl3::Color sunColor{255, 247, 230, 255};
	sdl3::Color ambientColor{30, 32, 44, 255};
	sdl3::Color backgroundColor{18, 20, 28, 255};
	math::FVector3 gravity{0.f, -9.81f, 0.f};

	[[nodiscard]] data::NodePtr ToJson() const {
		auto node = data::Node::MakeObject();
		node->Set("sun_direction", json::FromVec3(sunDirection));
		node->Set("sun_intensity", data::Node::MakeFloat(double(sunIntensity)));
		node->Set("sun_color", json::FromColor(sunColor));
		node->Set("ambient_color", json::FromColor(ambientColor));
		node->Set("background_color", json::FromColor(backgroundColor));
		node->Set("gravity", json::FromVec3(gravity));
		return node;
	}

	[[nodiscard]] static EnvironmentDesc FromJson(const data::NodePtr &node) {
		EnvironmentDesc desc;
		if (!node || !node->IsObject())
			return desc;
		desc.sunDirection = json::ToVec3(node->Get("sun_direction"), desc.sunDirection);
		desc.sunIntensity = json::Float(node->Get("sun_intensity"), desc.sunIntensity);
		desc.sunColor = json::ToColor(node->Get("sun_color"), desc.sunColor);
		desc.ambientColor = json::ToColor(node->Get("ambient_color"), desc.ambientColor);
		desc.backgroundColor = json::ToColor(node->Get("background_color"), desc.backgroundColor);
		desc.gravity = json::ToVec3(node->Get("gravity"), desc.gravity);
		return desc;
	}
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

	[[nodiscard]] data::NodePtr ToJson() const {
		auto node = data::Node::MakeObject();
		node->Set("edit_position", json::FromVec3(editPosition));
		node->Set("edit_yaw", data::Node::MakeFloat(double(editYaw)));
		node->Set("edit_pitch", data::Node::MakeFloat(double(editPitch)));
		node->Set("play_position", json::FromVec3(playPosition));
		node->Set("play_yaw", data::Node::MakeFloat(double(playYaw)));
		node->Set("play_pitch", data::Node::MakeFloat(double(playPitch)));
		return node;
	}

	[[nodiscard]] static CameraDesc FromJson(const data::NodePtr &node) {
		CameraDesc desc;
		if (!node || !node->IsObject())
			return desc;
		desc.editPosition = json::ToVec3(node->Get("edit_position"), desc.editPosition);
		desc.editYaw = json::Float(node->Get("edit_yaw"), desc.editYaw);
		desc.editPitch = json::Float(node->Get("edit_pitch"), desc.editPitch);
		desc.playPosition = json::ToVec3(node->Get("play_position"), desc.playPosition);
		desc.playYaw = json::Float(node->Get("play_yaw"), desc.playYaw);
		desc.playPitch = json::Float(node->Get("play_pitch"), desc.playPitch);
		return desc;
	}
};

struct SceneDesc {
	String name;
	String description;
	/// Script joué quand la scène passe en mode Jeu (chemin relatif ou
	/// source intégrée, cf. Project::ResolveScript).
	String gameplayScript;
	EnvironmentDesc environment;
	CameraDesc camera;
	/// L'ARBRE de la scène. Sa racine porte le nom de la scène et n'est pas
	/// un objet : c'est le point d'accroche de tout le reste.
	scene::NodeTree tree{String("Scene")};

	// ── Nom ──────────────────────────────────────────────────────────────────

	/// Renomme la scène ET la racine de son arbre. Les deux doivent rester
	/// d'accord : c'est le nom de la RACINE qui apparaît dans les chemins
	/// (`/Vitrine/Tore`), et un chemin écrit à la main dans un script part
	/// naturellement du nom de la scène.
	void SetName(String sceneName) {
		name = sceneName;
		(void)tree.Rename(tree.Root(), std::move(sceneName));
	}

	// ── Recherche ────────────────────────────────────────────────────────────

	[[nodiscard]] scene::NodeId RootId() const noexcept { return tree.Root(); }

	/// Cherche par NOM dans tout l'arbre (premier trouvé en parcours préfixe).
	/// L'éditeur garde des noms uniques à l'échelle de la scène (cf.
	/// UniqueName) pour que cette recherche — celle qu'emploient les scripts
	/// et les tests — reste sans ambiguïté, même si la bibliothèque, elle,
	/// n'impose l'unicité qu'entre frères.
	[[nodiscard]] scene::NodeId FindId(const String &objectName) const {
		return tree.FindByName(objectName);
	}

	[[nodiscard]] scene::Node *Find(const String &objectName) noexcept {
		scene::NodeId id = FindId(objectName);
		return id.Valid() ? tree.Get(id) : nullptr;
	}

	[[nodiscard]] const scene::Node *Find(const String &objectName) const noexcept {
		scene::NodeId id = FindId(objectName);
		return id.Valid() ? tree.Get(id) : nullptr;
	}

	/// Résout un chemin (`/Scene/Car/Wheels`) ou, à défaut, un nom.
	[[nodiscard]] scene::NodeId Resolve(const String &pathOrName) const {
		if (pathOrName.IsEmpty())
			return tree.Root();
		if (pathOrName.Contains('/')) {
			if (scene::NodeId id = tree.Resolve(pathOrName); id.Valid())
				return id;
		}
		return FindId(pathOrName);
	}

	/// Nombre d'objets (l'arbre moins sa racine).
	[[nodiscard]] size_t ObjectCount() const noexcept { return tree.Size() - 1; }

	/// Tous les objets, dans l'ordre d'affichage (racine exclue).
	[[nodiscard]] std::vector<scene::NodeId> Objects() const {
		std::vector<scene::NodeId> out;
		tree.Traverse(tree.Root(), [&](scene::NodeId id, const scene::Node &) {
			if (id != tree.Root())
				out.push_back(id);
		});
		return out;
	}

	/// `base`, `base 2`, `base 3`… — le premier nom libre DANS TOUTE LA SCÈNE
	/// (et non seulement entre frères) : les scripts et le rapport désignent
	/// les objets par leur nom seul.
	[[nodiscard]] String UniqueName(const String &base) const {
		String wanted = base.IsEmpty() ? String("Object") : base;
		if (!FindId(wanted).Valid())
			return wanted;
		for (int suffix = 2; suffix < 100000; ++suffix) {
			String candidate = String::Format("%s %d", wanted.CStr(), suffix);
			if (!FindId(candidate).Valid())
				return candidate;
		}
		return wanted;
	}

	// ── Mutations ────────────────────────────────────────────────────────────

	/// Ajoute un objet et rend son identifiant. Le parent est celui nommé par
	/// `object.parent` (nom ou chemin), la racine à défaut.
	scene::NodeId AddNode(ObjectDesc object) {
		scene::NodeId parent = object.parent.IsEmpty() ? tree.Root() : Resolve(object.parent);
		if (!parent.Valid())
			parent = tree.Root();
		object.name = UniqueName(object.name.IsEmpty() ? String("Object") : object.name);
		return tree.Add(parent, object.ToNode());
	}

	/// Compat : ajoute et rend le NOM retenu (l'ancienne signature).
	String Add(ObjectDesc object) {
		scene::NodeId id = AddNode(std::move(object));
		const scene::Node *node = tree.Get(id);
		return node ? node->name : String();
	}

	/// Supprime un objet ET tout son sous-arbre. C'est un changement de
	/// comportement assumé par rapport à la liste plate, où les enfants
	/// remontaient à la racine : supprimer une voiture doit supprimer ses
	/// roues, pas les éparpiller dans la scène.
	bool Remove(const String &objectName) {
		scene::NodeId id = FindId(objectName);
		return id.Valid() && tree.Remove(id);
	}

	[[nodiscard]] std::vector<scene::NodeId> WithTag(const String &wantedTag) const {
		std::vector<scene::NodeId> found;
		tree.Traverse(tree.Root(), [&](scene::NodeId id, const scene::Node &node) {
			if (id != tree.Root() && TagOf(node) == wantedTag)
				found.push_back(id);
		});
		return found;
	}

	// ── Sérialisation ────────────────────────────────────────────────────────

	[[nodiscard]] data::NodePtr ToJson() const {
		auto node = data::Node::MakeObject();
		node->Set("name", data::Node::MakeString(name));
		node->Set("description", data::Node::MakeString(description));
		node->Set("gameplay_script", data::Node::MakeString(gameplayScript));
		node->Set("environment", environment.ToJson());
		node->Set("camera", camera.ToJson());
		node->Set("tree", tree.ToJson());
		return node;
	}

	[[nodiscard]] static Result<SceneDesc, String> FromJson(const data::NodePtr &node) {
		if (!node || !node->IsObject())
			return Err(String("scène : objet JSON attendu"));
		SceneDesc scene;
		scene.SetName(json::Str(node->Get("name")));
		if (scene.name.IsEmpty())
			return Err(String("scène : champ `name` manquant ou vide"));
		scene.description = json::Str(node->Get("description"));
		scene.gameplayScript = json::Str(node->Get("gameplay_script"));
		scene.environment = EnvironmentDesc::FromJson(node->Get("environment"));
		scene.camera = CameraDesc::FromJson(node->Get("camera"));

		if (auto treeJson = node->Get("tree"); treeJson && treeJson->IsObject()) {
			auto tree = ::scene::NodeTree::FromJson(treeJson);
			if (tree.IsError())
				return Err(String::Format("scène « %s » : %s", scene.name.CStr(), tree.Error().CStr()));
			scene.tree = std::move(tree).Unwrap();
			scene.SetName(scene.name); // la racine suit toujours le nom de la scène
			return Ok(std::move(scene));
		}
		// Format 2 : liste plate + champ `parent` portant un nom.
		if (auto array = node->Get("objects"); array && array->IsArray()) {
			auto imported = FromLegacyObjects(scene, array);
			if (imported.IsSome())
				return Err(imported.Unwrap());
		}
		return Ok(std::move(scene));
	}

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
	[[nodiscard]] static Option<String> FromLegacyObjects(SceneDesc &scene, const data::NodePtr &array) {
		std::vector<ObjectDesc> objects;
		for (size_t i = 0; i < array->GetSize(); ++i) {
			auto object = ObjectDesc::FromLegacyJson(array->At(i));
			if (object.IsError())
				return Some(String::Format("scène « %s » : %s", scene.name.CStr(), object.Error().CStr()));
			objects.push_back(std::move(object).Unwrap());
		}
		std::vector<scene::NodeId> created;
		created.reserve(objects.size());
		for (const ObjectDesc &object : objects)
			created.push_back(scene.tree.Add(scene.tree.Root(), object.ToNode()));
		for (size_t i = 0; i < objects.size(); ++i) {
			if (objects[i].parent.IsEmpty())
				continue;
			scene::NodeId parent = scene.FindId(objects[i].parent);
			if (parent.Valid() && parent != created[i])
				(void)scene.tree.Reparent(created[i], parent, scene::ReparentMode::KEEP_LOCAL);
		}
		return NONE;
	}
};

// ============================================================================
// Project
// ============================================================================

class Project {
public:
	String name = "Projet sans titre";
	std::vector<SceneDesc> scenes;
	String activeScene;
	/// Bibliothèque de scripts de comportement, attachés aux nœuds par leur
	/// nom (cf. ScriptRef). Distincte du script de JEU de chaque scène, qui
	/// orchestre la partie entière.
	std::vector<ScriptAsset> scripts;

	[[nodiscard]] ScriptAsset *FindScript(const String &scriptName) noexcept {
		for (ScriptAsset &script : scripts)
			if (script.name == scriptName)
				return &script;
		return nullptr;
	}

	[[nodiscard]] const ScriptAsset *FindScript(const String &scriptName) const noexcept {
		for (const ScriptAsset &script : scripts)
			if (script.name == scriptName)
				return &script;
		return nullptr;
	}

	[[nodiscard]] SceneDesc *FindScene(const String &sceneName) noexcept {
		for (SceneDesc &scene : scenes)
			if (scene.name == sceneName)
				return &scene;
		return nullptr;
	}

	[[nodiscard]] const SceneDesc *FindScene(const String &sceneName) const noexcept {
		for (const SceneDesc &scene : scenes)
			if (scene.name == sceneName)
				return &scene;
		return nullptr;
	}

	/// Scène active, ou la première scène si `activeScene` ne résout pas —
	/// `nullptr` uniquement pour un projet sans AUCUNE scène.
	[[nodiscard]] SceneDesc *ActiveScene() noexcept {
		if (SceneDesc *scene = FindScene(activeScene))
			return scene;
		return scenes.empty() ? nullptr : &scenes.front();
	}

	[[nodiscard]] const SceneDesc *ActiveScene() const noexcept {
		if (const SceneDesc *scene = FindScene(activeScene))
			return scene;
		return scenes.empty() ? nullptr : &scenes.front();
	}

	bool SetActiveScene(const String &sceneName) {
		if (!FindScene(sceneName))
			return false;
		activeScene = sceneName;
		return true;
	}

	[[nodiscard]] std::vector<String> SceneNames() const {
		std::vector<String> names;
		names.reserve(scenes.size());
		for (const SceneDesc &scene : scenes)
			names.push_back(scene.name);
		return names;
	}

	[[nodiscard]] size_t TotalObjectCount() const noexcept {
		size_t total = 0;
		for (const SceneDesc &scene : scenes)
			total += scene.ObjectCount();
		return total;
	}

	// ── Sérialisation ────────────────────────────────────────────────────────

	[[nodiscard]] data::NodePtr ToJson() const {
		auto root = data::Node::MakeObject();
		root->Set("format", data::Node::MakeString("game_editor.project"));
		root->Set("version", data::Node::MakeInt(FORMAT_VERSION));
		root->Set("name", data::Node::MakeString(name));
		root->Set("active_scene", data::Node::MakeString(activeScene));
		auto array = data::Node::MakeArray();
		for (const SceneDesc &scene : scenes)
			array->Push(scene.ToJson());
		root->Set("scenes", array);
		auto scriptArray = data::Node::MakeArray();
		for (const ScriptAsset &script : scripts)
			scriptArray->Push(script.ToJson());
		root->Set("scripts", scriptArray);
		return root;
	}

	[[nodiscard]] static Result<Project, String> FromJson(const data::NodePtr &root) {
		if (!root || !root->IsObject())
			return Err(String("projet : racine JSON invalide (objet attendu)"));
		auto scenesNode = root->Get("scenes");
		if (!scenesNode || !scenesNode->IsArray())
			return Err(String("projet : tableau `scenes` manquant"));

		int version = json::Int(root->Get("version"), FORMAT_VERSION);
		if (version > FORMAT_VERSION)
			return Err(String::Format("projet : version de format %d trop récente (max supportée : %d)", version,
									  FORMAT_VERSION));

		Project project;
		project.name = json::Str(root->Get("name"), "Projet sans titre");
		project.activeScene = json::Str(root->Get("active_scene"));
		for (size_t i = 0; i < scenesNode->GetSize(); ++i) {
			auto scene = SceneDesc::FromJson(scenesNode->At(i));
			if (scene.IsError())
				return Err(scene.Error());
			project.scenes.push_back(std::move(scene).Unwrap());
		}
		if (project.scenes.empty())
			return Err(String("projet : aucune scène"));
		// Bibliothèque de scripts : facultative (absente des fichiers d'avant
		// la version 4).
		if (auto scriptsNode = root->Get("scripts"); scriptsNode && scriptsNode->IsArray()) {
			for (size_t i = 0; i < scriptsNode->GetSize(); ++i) {
				auto script = ScriptAsset::FromJson(scriptsNode->At(i));
				if (script.IsError())
					return Err(script.Error());
				project.scripts.push_back(std::move(script).Unwrap());
			}
		}
		if (!project.FindScene(project.activeScene))
			project.activeScene = project.scenes.front().name;
		return Ok(std::move(project));
	}

	/// Version du format de projet. Un fichier plus récent est REFUSÉ
	/// explicitement plutôt que lu de travers ; un fichier PLUS ANCIEN est
	/// lu (la version 2 — liste plate d'objets — est convertie en arbre, cf.
	/// SceneDesc::FromLegacyObjects).
	/// Version 4 : bibliothèque de scripts (`scripts`) et composants Light /
	/// Camera / Trigger / Script — un fichier v3 se lit tel quel.
	static constexpr int FORMAT_VERSION = 4;

	[[nodiscard]] String EncodeJson() const {
		data::JsonDocument document;
		document.SetRoot(ToJson());
		return document.EncodeStr();
	}

	[[nodiscard]] static Result<Project, String> DecodeJson(const String &text) {
		data::JsonDocument document;
		auto error = document.DecodeStr(text);
		if (error.IsSome())
			return Err(error.Unwrap().Format());
		return FromJson(document.GetRoot());
	}
};

} // namespace game_editor
