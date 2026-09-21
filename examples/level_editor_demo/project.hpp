#pragma once
/**
 * level_editor — modèle de DOCUMENT de l'éditeur (projet → scènes → objets).
 *
 * Séparation volontaire, calquée sur ce que font Godot/Unreal : le document
 * décrit ce que l'utilisateur a créé (des descriptions pures : forme,
 * transform, matériau, propriétés physiques, script attaché), le RUNTIME
 * (editor.hpp) en construit les `render3d::Object3D`/`physics::RigidBody`/
 * entités ECS correspondants. Conséquences directes :
 *
 *  - ce fichier ne dépend NI de render3d::, NI de physics::, NI de ui:: —
 *    donc il est entièrement testable sans fenêtre ni GPU (c'est ce que fait
 *    tests/level_editor_smoke_test.cpp) ;
 *  - la sauvegarde/chargement est une simple conversion vers `data::Node`,
 *    réutilisant les codecs existants (`sql::SaveProjectJson`) ;
 *  - l'éditeur peut recharger une scène entière (New/Load/changement de
 *    scène) en reconstruisant le runtime à partir du document, sans avoir à
 *    défaire l'état 3D pièce par pièce.
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
#include "sdl3/structs.hpp"

namespace level_editor {

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

/// Transform d'édition. La rotation est stockée en ANGLES D'EULER (degrés),
/// pas en quaternion : c'est la forme que l'utilisateur édite, celle qui
/// reste lisible dans le fichier de projet, et `math::FQuaternion::FromEuler`/
/// `ToEuler` (cette dernière ajoutée pour cette démo) font l'aller-retour
/// exact avec la forme runtime.
struct TransformDesc {
	math::FVector3 position;
	math::FVector3 eulerDeg;
	math::FVector3 scale{1.f, 1.f, 1.f};

	[[nodiscard]] math::FQuaternion Rotation() const noexcept {
		constexpr float DEG2RAD = 3.14159265358979323846f / 180.f;
		return math::FQuaternion::FromEuler(eulerDeg.x * DEG2RAD, eulerDeg.y * DEG2RAD, eulerDeg.z * DEG2RAD);
	}

	void SetRotation(const math::FQuaternion &q) noexcept {
		constexpr float RAD2DEG = 180.f / 3.14159265358979323846f;
		math::FVector3 euler = q.ToEuler();
		eulerDeg = {euler.x * RAD2DEG, euler.y * RAD2DEG, euler.z * RAD2DEG};
	}

	[[nodiscard]] data::NodePtr ToJson() const {
		auto node = data::Node::MakeObject();
		node->Set("position", json::FromVec3(position));
		node->Set("rotation", json::FromVec3(eulerDeg));
		node->Set("scale", json::FromVec3(scale));
		return node;
	}

	[[nodiscard]] static TransformDesc FromJson(const data::NodePtr &node) {
		TransformDesc desc;
		if (!node || !node->IsObject())
			return desc;
		desc.position = json::ToVec3(node->Get("position"));
		desc.eulerDeg = json::ToVec3(node->Get("rotation"));
		desc.scale = json::ToVec3(node->Get("scale"), math::FVector3{1.f, 1.f, 1.f});
		return desc;
	}
};

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

/// Un objet de la scène. `name` est l'IDENTITÉ : c'est par lui que les
/// scripts, le lien de parenté et le rapport désignent l'objet, et
/// `SceneDesc::UniqueName` garantit son unicité à la création.
struct ObjectDesc {
	String name;
	String parent; ///< nom d'un autre objet de la même scène, ou vide
	String tag;    ///< libre : "checkpoint", "road", "spawn"… lu par les scripts
	/// Chemin du fichier source pour `ShapeKind::MODEL` (un `.gltf`).
	String source;
	ShapeKind shape = ShapeKind::BOX;
	/// Dimensions propres à la forme (boîte : côtés ; sphère : rayon en x ;
	/// tore : rayon en x et section en y ; cylindre : rayon x, hauteur y…).
	math::FVector3 dimensions{1.f, 1.f, 1.f};
	int segments = 24; ///< finesse de tesselation des formes courbes
	TransformDesc transform;
	MaterialDesc material;
	PhysicsDesc physics;
	bool visible = true;

	[[nodiscard]] data::NodePtr ToJson() const {
		auto node = data::Node::MakeObject();
		node->Set("name", data::Node::MakeString(name));
		node->Set("parent", data::Node::MakeString(parent));
		node->Set("tag", data::Node::MakeString(tag));
		node->Set("source", data::Node::MakeString(source));
		node->Set("shape", data::Node::MakeString(ShapeKindName(shape)));
		node->Set("dimensions", json::FromVec3(dimensions));
		node->Set("segments", data::Node::MakeInt(segments));
		node->Set("visible", data::Node::MakeBool(visible));
		node->Set("transform", transform.ToJson());
		node->Set("material", material.ToJson());
		node->Set("physics", physics.ToJson());
		return node;
	}

	[[nodiscard]] static Result<ObjectDesc, String> FromJson(const data::NodePtr &node) {
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
		desc.transform = TransformDesc::FromJson(node->Get("transform"));
		desc.material = MaterialDesc::FromJson(node->Get("material"));
		desc.physics = PhysicsDesc::FromJson(node->Get("physics"));
		return Ok(std::move(desc));
	}
};

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
	std::vector<ObjectDesc> objects;

	[[nodiscard]] Option<size_t> IndexOf(const String &objectName) const {
		for (size_t i = 0; i < objects.size(); ++i)
			if (objects[i].name == objectName)
				return Some(i);
		return NONE;
	}

	[[nodiscard]] ObjectDesc *Find(const String &objectName) noexcept {
		for (ObjectDesc &object : objects)
			if (object.name == objectName)
				return &object;
		return nullptr;
	}

	[[nodiscard]] const ObjectDesc *Find(const String &objectName) const noexcept {
		for (const ObjectDesc &object : objects)
			if (object.name == objectName)
				return &object;
		return nullptr;
	}

	/// `base`, `base 2`, `base 3`… — le premier nom libre. Rend l'ajout
	/// d'objet idempotent côté script comme côté barre d'outils.
	[[nodiscard]] String UniqueName(const String &base) const {
		if (!Find(base))
			return base;
		for (int suffix = 2; suffix < 100000; ++suffix) {
			String candidate = String::Format("%s %d", base.CStr(), suffix);
			if (!Find(candidate))
				return candidate;
		}
		return base;
	}

	/// Ajoute `object` en lui donnant un nom unique si besoin ; rend le nom
	/// finalement retenu.
	String Add(ObjectDesc object) {
		object.name = UniqueName(object.name.IsEmpty() ? String("Object") : object.name);
		String assigned = object.name;
		objects.push_back(std::move(object));
		return assigned;
	}

	/// Retire un objet ET détache ses enfants (qui remontent à la racine) —
	/// un `parent` pointant vers un objet disparu produirait sinon un lien
	/// pendouillant au prochain chargement.
	bool Remove(const String &objectName) {
		Option<size_t> index = IndexOf(objectName);
		if (index.IsNone())
			return false;
		objects.erase(objects.begin() + ptrdiff_t(index.Unwrap()));
		for (ObjectDesc &object : objects)
			if (object.parent == objectName)
				object.parent.Clear();
		return true;
	}

	[[nodiscard]] std::vector<const ObjectDesc *> WithTag(const String &wantedTag) const {
		std::vector<const ObjectDesc *> found;
		for (const ObjectDesc &object : objects)
			if (object.tag == wantedTag)
				found.push_back(&object);
		return found;
	}

	[[nodiscard]] data::NodePtr ToJson() const {
		auto node = data::Node::MakeObject();
		node->Set("name", data::Node::MakeString(name));
		node->Set("description", data::Node::MakeString(description));
		node->Set("gameplay_script", data::Node::MakeString(gameplayScript));
		node->Set("environment", environment.ToJson());
		node->Set("camera", camera.ToJson());
		auto array = data::Node::MakeArray();
		for (const ObjectDesc &object : objects)
			array->Push(object.ToJson());
		node->Set("objects", array);
		return node;
	}

	[[nodiscard]] static Result<SceneDesc, String> FromJson(const data::NodePtr &node) {
		if (!node || !node->IsObject())
			return Err(String("scène : objet JSON attendu"));
		SceneDesc scene;
		scene.name = json::Str(node->Get("name"));
		if (scene.name.IsEmpty())
			return Err(String("scène : champ `name` manquant ou vide"));
		scene.description = json::Str(node->Get("description"));
		scene.gameplayScript = json::Str(node->Get("gameplay_script"));
		scene.environment = EnvironmentDesc::FromJson(node->Get("environment"));
		scene.camera = CameraDesc::FromJson(node->Get("camera"));

		auto array = node->Get("objects");
		if (array && array->IsArray()) {
			for (size_t i = 0; i < array->GetSize(); ++i) {
				auto object = ObjectDesc::FromJson(array->At(i));
				if (object.IsError())
					return Err(String::Format("scène `%s` : %s", scene.name.CStr(), object.Error().CStr()));
				scene.objects.push_back(std::move(object).Unwrap());
			}
		}
		return Ok(std::move(scene));
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
			total += scene.objects.size();
		return total;
	}

	// ── Sérialisation ────────────────────────────────────────────────────────

	[[nodiscard]] data::NodePtr ToJson() const {
		auto root = data::Node::MakeObject();
		root->Set("format", data::Node::MakeString("level_editor.project"));
		root->Set("version", data::Node::MakeInt(FORMAT_VERSION));
		root->Set("name", data::Node::MakeString(name));
		root->Set("active_scene", data::Node::MakeString(activeScene));
		auto array = data::Node::MakeArray();
		for (const SceneDesc &scene : scenes)
			array->Push(scene.ToJson());
		root->Set("scenes", array);
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
		if (!project.FindScene(project.activeScene))
			project.activeScene = project.scenes.front().name;
		return Ok(std::move(project));
	}

	/// Version du format de projet. Un fichier plus récent est REFUSÉ
	/// explicitement plutôt que lu de travers.
	static constexpr int FORMAT_VERSION = 2;

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

} // namespace level_editor
