#include "script_owners.hpp"

#include "runtime.hpp"

namespace game_editor {

using data::script::Interpreter;
using data::script::MapObject;
using data::script::OwnerObject;
using data::script::OwnerTypeBuilder;
using data::script::ScriptError;
using data::script::Value;
namespace sd = script_detail;

namespace {

/// `super.init(nom, {options})` dans n'importe quel ordre : la première
/// chaîne est le nom, la première table les options.
struct InitArgs {
	Option<String> name;
	std::shared_ptr<MapObject> options;
};

InitArgs ReadInitArgs(const std::vector<Value>& args) {
	InitArgs out;
	for (const Value& arg : args) {
		if (arg.IsString() && out.name.IsNone())
			out.name = Some(arg.AsString());
		else if (arg.IsMap() && arg.AsMap() && !out.options)
			out.options = arg.AsMap();
	}
	return out;
}

/// Erreur d'une méthode de base, au nom qualifié de son type.
ScriptError BaseError(const OwnerObject& owner, const char* method, const String& message) {
	return Interpreter::MakeError(String::Format(
		"`%s.%s` : %s", owner.type ? owner.type->name.CStr() : "?", method, message.CStr()));
}

/// La base porteuse de nœud de l'instance (Node3D, Mesh3D ou Behaviour),
/// initialisée — ou nullptr.
std::shared_ptr<NodeOwner> NodeBaseOf(const OwnerObject& owner) {
	auto base = owner.Sibling<NodeOwner>();
	return base && base->initialized && !base->destroyed ? base : nullptr;
}

/// Rend vrai si le nœud de `owner` existe ; sinon une erreur explicite.
Result<scene::Node*, ScriptError> RequireNode(const NodeOwner& owner, const char* method) {
	scene::Node* node = owner.Node();
	if (!node)
		return Err(
			BaseError(owner, method, String("le nœud n'existe plus (retiré, ou scène changée)")));
	return Ok(node);
}

Value Bool(bool b) {
	return Value::Boolean(b);
}

/// Méthodes communes à toute base porteuse de nœud (2D ou 3D) : désignation,
/// propriétés libres, étiquette, enfants.
template <class T> void AddCommonNodeMethods(OwnerTypeBuilder<T>& builder) {
	builder.Method("node", 0, 0,
				   [](Interpreter&, T& self, std::vector<Value>&) -> Result<Value, ScriptError> {
					   return Ok(self.Node() ? Value::Str(self.Handle()) : Value::Nil());
				   });
	builder.Method("exists", 0, 0,
				   [](Interpreter&, T& self, std::vector<Value>&) -> Result<Value, ScriptError> {
					   return Ok(Bool(self.Node() != nullptr));
				   });
	builder.Method("path", 0, 0,
				   [](Interpreter&, T& self, std::vector<Value>&) -> Result<Value, ScriptError> {
					   if (!self.Node())
						   return Ok(Value::Nil());
					   return Ok(Value::Str(self.tree->PathOf(self.node)));
				   });
	builder.Method(
		"child", 1, 1,
		[](Interpreter&, T& self, std::vector<Value>& args) -> Result<Value, ScriptError> {
			auto name = data::script::detail::ArgString(args, 0, "child");
			if (name.IsError())
				return Err(name.Error());
			if (!self.Node())
				return Ok(Value::Nil());
			const String path = self.tree->PathOf(self.node) + String("/") + name.Value();
			return Ok(self.runtime->ResolveId(path).Valid() ? Value::Str(path) : Value::Nil());
		});
	builder.Method("tag", 0, 0,
				   [](Interpreter&, T& self, std::vector<Value>&) -> Result<Value, ScriptError> {
					   const scene::Node* node = self.Node();
					   return Ok(node ? Value::Str(TagOf(*node)) : Value::Nil());
				   });
	builder.Method(
		"set_tag", 1, 1,
		[](Interpreter&, T& self, std::vector<Value>& args) -> Result<Value, ScriptError> {
			auto tag = data::script::detail::ArgString(args, 0, "set_tag");
			if (tag.IsError())
				return Err(tag.Error());
			auto node = RequireNode(self, "set_tag");
			if (node.IsError())
				return Err(node.Error());
			return Ok(Bool(self.runtime->SetTagOf(self.node, tag.Value())));
		});
	// `prop(clé)` lit une propriété libre du nœud (posée dans l'inspecteur),
	// `prop(clé, valeur)` l'écrit — même conversion que `node.prop`.
	builder.Method(
		"prop", 1, 2,
		[](Interpreter&, T& self, std::vector<Value>& args) -> Result<Value, ScriptError> {
			auto key = data::script::detail::ArgString(args, 0, "prop");
			if (key.IsError())
				return Err(key.Error());
			scene::Node* node = self.Node();
			if (!node)
				return Ok(Value::Nil());
			if (args.size() == 1)
				return Ok(sd::PropertyToValue(node->Get(key.Value())));
			sd::WriteProperty(*node, key.Value(), args[1]);
			return Ok(Bool(true));
		});
	builder.Property("name", [](const T& self) -> Value {
		const scene::Node* node = self.Node();
		return node ? Value::Str(node->name) : Value::Nil();
	});
	builder.Display([](const T& self) -> String {
		const scene::Node* node = self.Node();
		return String::Format("%s(%s)", self.type ? self.type->name.CStr() : "?",
							  node ? node->name.CStr() : "—");
	});
}

/// Méthodes des bases porteuses d'un nœud 3D : transform, visibilité, couleur.
template <class T> void AddNodeMethods(OwnerTypeBuilder<T>& builder) {
	AddCommonNodeMethods(builder);
	builder.Method("position", 0, 0,
				   [](Interpreter&, T& self, std::vector<Value>&) -> Result<Value, ScriptError> {
					   const scene::Node* node = self.Node();
					   return Ok(node ? sd::Vec3ToValue(node->transform.position) : Value::Nil());
				   });
	builder.Method("world_position", 0, 0,
				   [](Interpreter&, T& self, std::vector<Value>&) -> Result<Value, ScriptError> {
					   if (!self.Node())
						   return Ok(Value::Nil());
					   return Ok(sd::Vec3ToValue(self.tree->GlobalPosition(self.node)));
				   });
	builder.Method(
		"set_position", 3, 3,
		[](Interpreter&, T& self, std::vector<Value>& args) -> Result<Value, ScriptError> {
			auto p = sd::ArgVec3(args, 0, "set_position");
			if (p.IsError())
				return Err(p.Error());
			auto node = RequireNode(self, "set_position");
			if (node.IsError())
				return Err(node.Error());
			return Ok(Bool(self.runtime->SetPosition(self.node, p.Value())));
		});
	builder.Method("rotation", 0, 0,
				   [](Interpreter&, T& self, std::vector<Value>&) -> Result<Value, ScriptError> {
					   const scene::Node* node = self.Node();
					   return Ok(node ? sd::Vec3ToValue(node->transform.EulerDegrees())
									  : Value::Nil());
				   });
	builder.Method(
		"set_rotation", 3, 3,
		[](Interpreter&, T& self, std::vector<Value>& args) -> Result<Value, ScriptError> {
			auto r = sd::ArgVec3(args, 0, "set_rotation");
			if (r.IsError())
				return Err(r.Error());
			auto node = RequireNode(self, "set_rotation");
			if (node.IsError())
				return Err(node.Error());
			return Ok(Bool(self.runtime->SetEulerDegrees(self.node, r.Value())));
		});
	builder.Method("scale", 0, 0,
				   [](Interpreter&, T& self, std::vector<Value>&) -> Result<Value, ScriptError> {
					   const scene::Node* node = self.Node();
					   return Ok(node ? sd::Vec3ToValue(node->transform.scale) : Value::Nil());
				   });
	builder.Method(
		"set_scale", 3, 3,
		[](Interpreter&, T& self, std::vector<Value>& args) -> Result<Value, ScriptError> {
			auto s = sd::ArgVec3(args, 0, "set_scale");
			if (s.IsError())
				return Err(s.Error());
			auto node = RequireNode(self, "set_scale");
			if (node.IsError())
				return Err(node.Error());
			return Ok(Bool(self.runtime->SetScale(self.node, s.Value())));
		});
	builder.Method(
		"set_visible", 1, 1,
		[](Interpreter&, T& self, std::vector<Value>& args) -> Result<Value, ScriptError> {
			auto node = RequireNode(self, "set_visible");
			if (node.IsError())
				return Err(node.Error());
			return Ok(Bool(self.runtime->SetVisible(self.node, args[0].IsTruthy())));
		});
	// Géométrie : dimensions de la forme, axe avant (+z local) en repère
	// monde, passages local <-> monde — de quoi raisonner sur un tronçon de
	// route incliné ou tourné sans recalculer ses angles d'Euler.
	builder.Method("size", 0, 0,
				   [](Interpreter&, T& self, std::vector<Value>&) -> Result<Value, ScriptError> {
					   const scene::Node* node = self.Node();
					   if (!node || !VisualDesc::Has(*node))
						   return Ok(Value::Nil());
					   return Ok(sd::Vec3ToValue(VisualDesc::Read(*node).dimensions));
				   });
	builder.Method("forward", 0, 0,
				   [](Interpreter&, T& self, std::vector<Value>&) -> Result<Value, ScriptError> {
					   if (!self.Node())
						   return Ok(Value::Nil());
					   const scene::Transform world = self.tree->GlobalTransform(self.node);
					   return Ok(
						   sd::Vec3ToValue(world.rotation.Rotate(math::FVector3{0.f, 0.f, 1.f})));
				   });
	builder.Method(
		"to_local", 3, 3,
		[](Interpreter&, T& self, std::vector<Value>& args) -> Result<Value, ScriptError> {
			auto p = sd::ArgVec3(args, 0, "to_local");
			if (p.IsError())
				return Err(p.Error());
			if (!self.Node())
				return Ok(Value::Nil());
			const math::FMatrix4 inverse = self.tree->GlobalTransform(self.node).Matrix().Inverse();
			return Ok(sd::Vec3ToValue(inverse.TransformPoint(p.Value())));
		});
	builder.Method(
		"to_world", 3, 3,
		[](Interpreter&, T& self, std::vector<Value>& args) -> Result<Value, ScriptError> {
			auto p = sd::ArgVec3(args, 0, "to_world");
			if (p.IsError())
				return Err(p.Error());
			if (!self.Node())
				return Ok(Value::Nil());
			return Ok(sd::Vec3ToValue(
				self.tree->GlobalTransform(self.node).Matrix().TransformPoint(p.Value())));
		});
	builder.Method(
		"set_color", 3, 3,
		[](Interpreter&, T& self, std::vector<Value>& args) -> Result<Value, ScriptError> {
			auto rgb = sd::ArgVec3(args, 0, "set_color");
			if (rgb.IsError())
				return Err(rgb.Error());
			auto node = RequireNode(self, "set_color");
			if (node.IsError())
				return Err(node.Error());
			return Ok(
				Bool(self.runtime->SetMaterialColor(self.node, sd::ColorFromVec3(rgb.Value()))));
		});
}

/// Enregistre `type` sous `game.<nom>` ET comme globale `<nom>`.
void Register(Interpreter& vm, const char* name,
			  const std::shared_ptr<data::script::HostType>& type) {
	(void)vm.RegisterHostType(String("game"), String(name), type);
	Option<Value> game = vm.GetGlobal(String("game"));
	if (game.IsSome() && game.Value().IsNamespace())
		if (Option<Value> member = game.Value().AsNamespace()->scope->Find(String(name));
			member.IsSome())
			vm.SetGlobal(String(name), member.Unwrap());
}

// ── Scene ────────────────────────────────────────────────────────────────────

struct SceneOwner : OwnerObject {
	Runtime* runtime = nullptr;
	String sceneName;

	Option<ScriptError> OnInit(Interpreter&, std::vector<Value>&) override {
		const SceneDesc* scene = runtime->ActiveScene();
		sceneName = scene ? scene->name : String();
		return NONE;
	}
};

void InstallScene(Interpreter& vm, Runtime& runtime) {
	OwnerTypeBuilder<SceneOwner> builder([&runtime] {
		auto owner = std::make_shared<SceneOwner>();
		owner->runtime = &runtime;
		return owner;
	});
	builder.Method(
		"time", 0, 0,
		[](Interpreter&, SceneOwner& self, std::vector<Value>&) -> Result<Value, ScriptError> {
			return Ok(Value::Number(double(self.runtime->PlayTime())));
		});
	builder.Method(
		"load", 1, 1,
		[](Interpreter&, SceneOwner& self, std::vector<Value>& args) -> Result<Value, ScriptError> {
			auto name = data::script::detail::ArgString(args, 0, "Scene.load");
			if (name.IsError())
				return Err(name.Error());
			if (!self.runtime->RequestScene(name.Value()))
				return Err(BaseError(
					self, "load", String::Format("scène « %s » introuvable", name.Value().CStr())));
			return Ok(Bool(true));
		});
	builder.Method(
		"quit", 0, 0,
		[](Interpreter&, SceneOwner& self, std::vector<Value>&) -> Result<Value, ScriptError> {
			self.runtime->RequestQuit();
			return Ok(Value::Nil());
		});
	builder.Method(
		"state", 0, 0,
		[](Interpreter&, SceneOwner& self, std::vector<Value>&) -> Result<Value, ScriptError> {
			return Ok(self.runtime->GameState());
		});
	builder.Property("scene_name",
					 [](const SceneOwner& self) { return Value::Str(self.sceneName); });
	builder.Display(
		[](const SceneOwner& self) { return String::Format("Scene(%s)", self.sceneName.CStr()); });
	Register(vm, "Scene", builder.Type());
}

// ── Behaviour ────────────────────────────────────────────────────────────────

/// S'attache au nœud que le moteur lui désigne (composant Script), ou, créée
/// à la main, au nœud nommé par `super.init("Nom")`.
struct BehaviourOwner : NodeOwner {
	Option<ScriptError> OnInit(Interpreter&, std::vector<Value>& args) override {
		if (auto error = RejectSecondNode(); error.IsSome())
			return error;
		scene::NodeId target = runtime->BehaviourBindingNode();
		if (!target.Valid()) {
			const InitArgs init = ReadInitArgs(args);
			if (init.name.IsNone())
				return Some(BaseError(
					*this, "init",
					String("une Behaviour s'attache à un nœud : le composant Script de l'éditeur "
						   "s'en charge, ou `super.init(\"NomDuNœud\")`")));
			target = runtime->ResolveId(init.name.Value());
			if (!target.Valid())
				return Some(
					BaseError(*this, "init",
							  String::Format("nœud « %s » introuvable", init.name.Value().CStr())));
		}
		node = target;
		tree = runtime->Tree();
		ownsNode = false;
		return NONE;
	}
};

void InstallBehaviour(Interpreter& vm, Runtime& runtime) {
	OwnerTypeBuilder<BehaviourOwner> builder([&runtime] {
		auto owner = std::make_shared<BehaviourOwner>();
		owner->runtime = &runtime;
		return owner;
	});
	AddNodeMethods(builder);
	Register(vm, "Behaviour", builder.Type());
}

// ── Node3D / Mesh3D ──────────────────────────────────────────────────────────

/// Crée le nœud décrit par les options (table de `scene.spawn`). `visual` :
/// un Mesh3D a toujours une apparence (boîte par défaut), un Node3D n'en a
/// que si les options donnent une forme.
Option<ScriptError> SpawnOwnedNode(NodeOwner& self, std::vector<Value>& args,
								   const char* defaultName, bool visual) {
	if (auto error = self.RejectSecondNode(); error.IsSome())
		return error;
	const InitArgs init = ReadInitArgs(args);
	ObjectDesc desc = init.options ? sd::ObjectFromTable(*init.options) : ObjectDesc();
	desc.name = init.name.IsSome()
					? init.name.Unwrap()
					: (init.options ? sd::FieldString(*init.options, "name", defaultName)
									: String(defaultName));
	const bool hasShape = init.options && (init.options->Find(String("shape")) ||
										   init.options->Find(String("model")));
	if (!visual && !hasShape) {
		ObjectDesc group = ObjectDesc::Group(desc.name);
		group.parent = desc.parent;
		group.tag = desc.tag;
		group.transform = desc.transform;
		desc = std::move(group);
	}
	if (init.options)
		if (const Value* light = init.options->Find(String("light"));
			light && light->IsMap() && light->AsMap())
			desc.light = Some(sd::LightFromTable(*light->AsMap(), LightDesc()));
	Option<scene::NodeId> id = self.runtime->SpawnNode(std::move(desc));
	if (id.IsNone())
		return Some(
			BaseError(self, "init", String("impossible de créer le nœud (aucune scène active)")));
	self.node = id.Unwrap();
	self.tree = self.runtime->Tree();
	self.ownsNode = true;
	return NONE;
}

struct Node3DOwner : NodeOwner {
	Option<ScriptError> OnInit(Interpreter&, std::vector<Value>& args) override {
		return SpawnOwnedNode(*this, args, "Node3D", false);
	}
};

struct Mesh3DOwner : NodeOwner {
	Option<ScriptError> OnInit(Interpreter&, std::vector<Value>& args) override {
		return SpawnOwnedNode(*this, args, "Mesh3D", true);
	}
};

void InstallNode3D(Interpreter& vm, Runtime& runtime) {
	OwnerTypeBuilder<Node3DOwner> builder([&runtime] {
		auto owner = std::make_shared<Node3DOwner>();
		owner->runtime = &runtime;
		return owner;
	});
	AddNodeMethods(builder);
	Register(vm, "Node3D", builder.Type());
}

void InstallMesh3D(Interpreter& vm, Runtime& runtime) {
	OwnerTypeBuilder<Mesh3DOwner> builder([&runtime] {
		auto owner = std::make_shared<Mesh3DOwner>();
		owner->runtime = &runtime;
		return owner;
	});
	AddNodeMethods(builder);
	// `set_mesh("modele.gltf")` : un modèle importé (chemin relatif au projet).
	builder.Method("set_mesh", 1, 1,
				   [](Interpreter&, Mesh3DOwner& self,
					  std::vector<Value>& args) -> Result<Value, ScriptError> {
					   auto source = data::script::detail::ArgString(args, 0, "Mesh3D.set_mesh");
					   if (source.IsError())
						   return Err(source.Error());
					   auto node = RequireNode(self, "set_mesh");
					   if (node.IsError())
						   return Err(node.Error());
					   VisualDesc visual = VisualDesc::Read(*node.Value());
					   visual.shape = ShapeKind::MODEL;
					   visual.source = self.runtime->ResolveProjectPath(source.Value());
					   return Ok(Bool(self.runtime->SetVisual(self.node, visual)));
				   });
	// `set_shape("sphere"[, sx, sy, sz])`.
	builder.Method(
		"set_shape", 1, 4,
		[](Interpreter&, Mesh3DOwner& self,
		   std::vector<Value>& args) -> Result<Value, ScriptError> {
			auto kind = data::script::detail::ArgString(args, 0, "Mesh3D.set_shape");
			if (kind.IsError())
				return Err(kind.Error());
			Option<ShapeKind> shape = ShapeKindFromName(kind.Value());
			if (shape.IsNone())
				return Err(BaseError(self, "set_shape",
									 String::Format("forme inconnue `%s`", kind.Value().CStr())));
			auto node = RequireNode(self, "set_shape");
			if (node.IsError())
				return Err(node.Error());
			VisualDesc visual = VisualDesc::Read(*node.Value());
			visual.shape = shape.Unwrap();
			if (args.size() == 4) {
				auto size = sd::ArgVec3(args, 1, "Mesh3D.set_shape");
				if (size.IsError())
					return Err(size.Error());
				visual.dimensions = size.Value();
			}
			return Ok(Bool(self.runtime->SetVisual(self.node, visual)));
		});
	// `set_material({kind, color, metallic, roughness, …})` : table partielle.
	builder.Method("set_material", 1, 1,
				   [](Interpreter&, Mesh3DOwner& self,
					  std::vector<Value>& args) -> Result<Value, ScriptError> {
					   auto table = data::script::detail::ArgMap(args, 0, "Mesh3D.set_material");
					   if (table.IsError())
						   return Err(table.Error());
					   auto node = RequireNode(self, "set_material");
					   if (node.IsError())
						   return Err(node.Error());
					   MaterialDesc material = VisualDesc::Read(*node.Value()).material;
					   sd::ReadMaterialTable(*table.Value(), material);
					   return Ok(Bool(self.runtime->SetMaterial(self.node, material)));
				   });
	Register(vm, "Mesh3D", builder.Type());
}

// ── Node2D ───────────────────────────────────────────────────────────────────

/// Fonction `node2d.<name>` de l'interpréteur (nil si l'API 2D manque).
Value Node2DFunction(Interpreter& vm, const char* name) {
	Option<Value> space = vm.GetGlobal(String("node2d"));
	if (space.IsNone() || !space.Value().IsNamespace())
		return Value::Nil();
	return space.Value().AsNamespace()->scope->Find(String(name)).UnwrapOr(Value::Nil());
}

/// Un nœud 2D créé par l'objet (options de `node2d.spawn` : kind, pos, size,
/// color, text, points…), retiré à sa destruction. Ses méthodes sont celles
/// de `node2d.*`, appliquées à son nœud.
struct Node2DOwner : NodeOwner {
	Option<ScriptError> OnInit(Interpreter& vm, std::vector<Value>& args) override {
		if (auto error = RejectSecondNode(); error.IsSome())
			return error;
		const InitArgs init = ReadInitArgs(args);
		auto options = init.options ? init.options : std::make_shared<MapObject>();
		if (init.name.IsSome())
			options->SetKey(String("name"), Value::Str(init.name.Unwrap()));
		const Value spawn = Node2DFunction(vm, "spawn");
		if (!spawn.IsCallable())
			return Some(BaseError(*this, "init", String("API 2D indisponible")));
		auto made = vm.CallValue(spawn, {Value::Map(options)}, 0, 0);
		if (made.IsError())
			return Some(made.Error());
		if (!made.Value().IsString())
			return Some(BaseError(*this, "init",
								  String("impossible de créer le nœud (aucune scène active)")));
		node = runtime->ResolveId(made.Value().AsString());
		tree = runtime->Tree();
		ownsNode = true;
		return NONE;
	}
};

void InstallNode2D(Interpreter& vm, Runtime& runtime) {
	OwnerTypeBuilder<Node2DOwner> builder([&runtime] {
		auto owner = std::make_shared<Node2DOwner>();
		owner->runtime = &runtime;
		return owner;
	});
	AddCommonNodeMethods(builder);
	struct Delegate {
		const char* name;
		int minArity, maxArity;
	};
	// Arités SANS le premier argument de `node2d.*` (le nœud lui-même).
	static constexpr Delegate DELEGATES[] = {
		{"position", 0, 0},	   {"set_position", 2, 2}, {"move", 2, 2},
		{"rotation", 0, 0},	   {"set_rotation", 1, 1}, {"scale", 0, 0},
		{"set_scale", 1, 2},   {"set_visible", 1, 1},  {"global_position", 0, 0},
		{"color", 0, 0},	   {"set_color", 1, 1},	   {"text", 0, 0},
		{"set_text", 1, 1},	   {"size", 0, 0},		   {"set_size", 2, 2},
		{"set_texture", 1, 1}, {"set_z", 1, 1},		   {"set_style", 1, 1},
		{"bounds", 0, 0},	   {"overlaps", 1, 1},
	};
	for (const Delegate& delegate : DELEGATES) {
		const char* name = delegate.name;
		builder.Method(
			name, delegate.minArity, delegate.maxArity,
			[name](Interpreter& vm, Node2DOwner& self,
				   std::vector<Value>& args) -> Result<Value, ScriptError> {
				if (!self.Node())
					return Ok(Value::Nil());
				const Value function = Node2DFunction(vm, name);
				if (!function.IsCallable())
					return Err(BaseError(self, name, String("API 2D indisponible")));
				std::vector<Value> full;
				full.reserve(args.size() + 1);
				full.push_back(Value::Str(self.Handle()));
				// Un autre objet 2D se désigne par lui-même (`a.overlaps(b)`).
				for (const Value& arg : args) {
					std::shared_ptr<NodeOwner> other;
					if (arg.IsInstance() && arg.AsInstance())
						for (const auto& base : arg.AsInstance()->owners)
							if (auto carrier = std::dynamic_pointer_cast<NodeOwner>(base))
								other = carrier;
					full.push_back(other && other->Node() ? Value::Str(other->Handle()) : arg);
				}
				return vm.CallValue(function, std::move(full), 0, 0);
			});
	}
	Register(vm, "Node2D", builder.Type());
}

// ── PhysicsBody ──────────────────────────────────────────────────────────────

struct PhysicsBodyOwner : OwnerObject {
	Runtime* runtime = nullptr;
	std::weak_ptr<NodeOwner> carrier;

	Option<ScriptError> OnInit(Interpreter&, std::vector<Value>& args) override {
		std::shared_ptr<NodeOwner> base = NodeBaseOf(*this);
		if (!base)
			return Some(BaseError(
				*this, "init",
				String(
					"PhysicsBody s'applique au nœud d'une base déclarée AVANT elle dans `extends` "
					"(Node3D, Mesh3D ou Behaviour)")));
		carrier = base;
		// Les options de construction peuvent donner le corps (`body:
		// "dynamic"` ou `body: {kind, mass…}`) — appliqué seulement si le nœud
		// n'en a pas déjà un (un Mesh3D créé avec ces mêmes options l'a).
		const InitArgs init = ReadInitArgs(args);
		scene::Node* node = base->Node();
		if (init.options && node && init.options->Find(String("body")) &&
			PhysicsDesc::Read(*node).body == BodyKind::NONE) {
			PhysicsDesc physics = PhysicsDesc::Read(*node);
			sd::ReadPhysicsTable(*init.options, physics);
			(void)runtime->SetPhysics(base->node, physics);
		}
		return NONE;
	}

	[[nodiscard]] std::shared_ptr<NodeOwner> Carrier() const {
		auto base = carrier.lock();
		return base && !base->destroyed && base->Node() ? base : nullptr;
	}
};

void InstallPhysicsBody(Interpreter& vm, Runtime& runtime) {
	OwnerTypeBuilder<PhysicsBodyOwner> builder([&runtime] {
		auto owner = std::make_shared<PhysicsBodyOwner>();
		owner->runtime = &runtime;
		return owner;
	});
	// `set_body({kind: "dynamic", mass: 80, collider: "sphere", restitution, friction})`.
	builder.Method("set_body", 1, 1,
				   [](Interpreter&, PhysicsBodyOwner& self,
					  std::vector<Value>& args) -> Result<Value, ScriptError> {
					   auto table = data::script::detail::ArgMap(args, 0, "PhysicsBody.set_body");
					   if (table.IsError())
						   return Err(table.Error());
					   auto base = self.Carrier();
					   if (!base)
						   return Err(BaseError(self, "set_body", String("le nœud n'existe plus")));
					   PhysicsDesc physics = PhysicsDesc::Read(*base->Node());
					   sd::ReadPhysicsTable(*table.Value(), physics);
					   return Ok(Bool(self.runtime->SetPhysics(base->node, physics)));
				   });
	builder.Method("velocity", 0, 0,
				   [](Interpreter&, PhysicsBodyOwner& self,
					  std::vector<Value>&) -> Result<Value, ScriptError> {
					   auto base = self.Carrier();
					   Option<math::FVector3> v =
						   base ? self.runtime->GetVelocity(base->node) : NONE;
					   return Ok(v.IsSome() ? sd::Vec3ToValue(v.Unwrap()) : Value::Nil());
				   });
	builder.Method("set_velocity", 3, 3,
				   [](Interpreter&, PhysicsBodyOwner& self,
					  std::vector<Value>& args) -> Result<Value, ScriptError> {
					   auto v = sd::ArgVec3(args, 0, "PhysicsBody.set_velocity");
					   if (v.IsError())
						   return Err(v.Error());
					   auto base = self.Carrier();
					   return Ok(Bool(base && self.runtime->SetVelocity(base->node, v.Value())));
				   });
	builder.Method("impulse", 3, 3,
				   [](Interpreter&, PhysicsBodyOwner& self,
					  std::vector<Value>& args) -> Result<Value, ScriptError> {
					   auto v = sd::ArgVec3(args, 0, "PhysicsBody.impulse");
					   if (v.IsError())
						   return Err(v.Error());
					   auto base = self.Carrier();
					   return Ok(Bool(base && self.runtime->ApplyImpulse(base->node, v.Value())));
				   });
	builder.Method("body_kind", 0, 0,
				   [](Interpreter&, PhysicsBodyOwner& self,
					  std::vector<Value>&) -> Result<Value, ScriptError> {
					   auto base = self.Carrier();
					   if (!base)
						   return Ok(Value::Nil());
					   return Ok(
						   Value::Str(String(BodyKindName(PhysicsDesc::Read(*base->Node()).body))));
				   });
	Register(vm, "PhysicsBody", builder.Type());
}

// ── Light3D ──────────────────────────────────────────────────────────────────

struct Light3DOwner : OwnerObject {
	Runtime* runtime = nullptr;
	std::weak_ptr<NodeOwner> carrier;

	Option<ScriptError> OnInit(Interpreter&, std::vector<Value>& args) override {
		std::shared_ptr<NodeOwner> base = NodeBaseOf(*this);
		if (!base)
			return Some(BaseError(
				*this, "init",
				String("Light3D s'applique au nœud d'une base déclarée AVANT elle dans `extends` "
					   "(Node3D, Mesh3D ou Behaviour)")));
		carrier = base;
		const InitArgs init = ReadInitArgs(args);
		scene::Node* node = base->Node();
		if (node && !LightDesc::Has(*node)) {
			LightDesc light;
			if (init.options)
				if (const Value* table = init.options->Find(String("light"));
					table && table->IsMap() && table->AsMap())
					light = sd::LightFromTable(*table->AsMap(), light);
			(void)runtime->SetLight(base->node, light);
		}
		return NONE;
	}

	/// Le nœud porteur s'il a (encore) une lumière.
	[[nodiscard]] scene::Node* LightNode() const {
		auto base = carrier.lock();
		scene::Node* node = base && !base->destroyed ? base->Node() : nullptr;
		return node && LightDesc::Has(*node) ? node : nullptr;
	}
};

void InstallLight3D(Interpreter& vm, Runtime& runtime) {
	OwnerTypeBuilder<Light3DOwner> builder([&runtime] {
		auto owner = std::make_shared<Light3DOwner>();
		owner->runtime = &runtime;
		return owner;
	});
	builder.Method(
		"intensity", 0, 0,
		[](Interpreter&, Light3DOwner& self, std::vector<Value>&) -> Result<Value, ScriptError> {
			scene::Node* node = self.LightNode();
			return Ok(node ? Value::Number(double(LightDesc::Read(*node).intensity))
						   : Value::Nil());
		});
	// Écriture directe (comme `light.set_intensity`) : appelée à chaque image,
	// elle n'empile pas d'historique et ne recrée pas le repère.
	builder.Method("set_intensity", 1, 1,
				   [](Interpreter&, Light3DOwner& self,
					  std::vector<Value>& args) -> Result<Value, ScriptError> {
					   auto value =
						   data::script::detail::ArgNumber(args, 0, "Light3D.set_intensity");
					   if (value.IsError())
						   return Err(value.Error());
					   scene::Node* node = self.LightNode();
					   if (!node)
						   return Ok(Bool(false));
					   LightDesc light = LightDesc::Read(*node);
					   light.intensity = float(sdl3::Max(0.0, value.Value()));
					   light.Write(*node);
					   return Ok(Bool(true));
				   });
	builder.Method("set_color", 3, 3,
				   [](Interpreter&, Light3DOwner& self,
					  std::vector<Value>& args) -> Result<Value, ScriptError> {
					   auto rgb = sd::ArgVec3(args, 0, "Light3D.set_color");
					   if (rgb.IsError())
						   return Err(rgb.Error());
					   scene::Node* node = self.LightNode();
					   if (!node)
						   return Ok(Bool(false));
					   LightDesc light = LightDesc::Read(*node);
					   light.color = sd::ColorFromVec3(rgb.Value());
					   light.Write(*node);
					   return Ok(Bool(true));
				   });
	builder.Method("set_light", 1, 1,
				   [](Interpreter&, Light3DOwner& self,
					  std::vector<Value>& args) -> Result<Value, ScriptError> {
					   auto table = data::script::detail::ArgMap(args, 0, "Light3D.set_light");
					   if (table.IsError())
						   return Err(table.Error());
					   auto base = self.carrier.lock();
					   scene::Node* node = base && !base->destroyed ? base->Node() : nullptr;
					   if (!node)
						   return Ok(Bool(false));
					   const LightDesc current =
						   LightDesc::Has(*node) ? LightDesc::Read(*node) : LightDesc();
					   return Ok(Bool(self.runtime->SetLight(
						   base->node, sd::LightFromTable(*table.Value(), current))));
				   });
	Register(vm, "Light3D", builder.Type());
}

// ── Gameplay ─────────────────────────────────────────────────────────────────

struct GameplayOwner : OwnerObject {
	std::vector<std::pair<String, Value>> listeners; ///< (évènement, fonction)

	/// Les fonctions retiennent `this` par leur fermeture : les oublier à
	/// la destruction rompt ce cycle.
	void OnDeinit(Interpreter&) override { listeners.clear(); }
};

void InstallGameplay(Interpreter& vm) {
	OwnerTypeBuilder<GameplayOwner> builder;
	builder.Method("on", 2, 2,
				   [](Interpreter&, GameplayOwner& self,
					  std::vector<Value>& args) -> Result<Value, ScriptError> {
					   auto event = data::script::detail::ArgString(args, 0, "Gameplay.on");
					   if (event.IsError())
						   return Err(event.Error());
					   if (!args[1].IsCallable())
						   return Err(BaseError(
							   self, "on", String("une fonction est attendue en second argument")));
					   self.listeners.emplace_back(event.Value(), args[1]);
					   return Ok(Value::Nil());
				   });
	builder.Method("off", 1, 1,
				   [](Interpreter&, GameplayOwner& self,
					  std::vector<Value>& args) -> Result<Value, ScriptError> {
					   auto event = data::script::detail::ArgString(args, 0, "Gameplay.off");
					   if (event.IsError())
						   return Err(event.Error());
					   const size_t before = self.listeners.size();
					   std::erase_if(self.listeners, [&](const auto& entry) {
						   return entry.first == event.Value();
					   });
					   return Ok(Value::Number(double(before - self.listeners.size())));
				   });
	builder.Method(
		"forget_listeners", 0, 0,
		[](Interpreter&, GameplayOwner& self, std::vector<Value>&) -> Result<Value, ScriptError> {
			self.listeners.clear();
			return Ok(Value::Nil());
		});
	// `emit(évènement, …)` : appelle chaque abonné, dans l'ordre d'abonnement,
	// et rend leur nombre. La liste est copiée : un abonné peut s'abonner ou
	// se désabonner pendant l'émission sans perturber celle-ci.
	builder.Method("emit", 1, -1,
				   [](Interpreter& vm, GameplayOwner& self,
					  std::vector<Value>& args) -> Result<Value, ScriptError> {
					   auto event = data::script::detail::ArgString(args, 0, "Gameplay.emit");
					   if (event.IsError())
						   return Err(event.Error());
					   std::vector<Value> targets;
					   for (const auto& [name, fn] : self.listeners)
						   if (name == event.Value())
							   targets.push_back(fn);
					   for (const Value& fn : targets) {
						   std::vector<Value> rest(args.begin() + 1, args.end());
						   auto called = vm.CallValue(fn, std::move(rest), 0, 0);
						   if (called.IsError())
							   return called;
					   }
					   return Ok(Value::Number(double(targets.size())));
				   });
	builder.Method("listeners", 0, 1,
				   [](Interpreter&, GameplayOwner& self,
					  std::vector<Value>& args) -> Result<Value, ScriptError> {
					   if (args.empty())
						   return Ok(Value::Number(double(self.listeners.size())));
					   const String wanted = args[0].ToDisplayString();
					   size_t n = 0;
					   for (const auto& entry : self.listeners)
						   n += entry.first == wanted ? 1 : 0;
					   return Ok(Value::Number(double(n)));
				   });
	Register(vm, "Gameplay", builder.Type());
}

// ── SceneAsset ───────────────────────────────────────────────────────────────

struct SceneAssetOwner : OwnerObject {
	Runtime* runtime = nullptr;
	String path;
	const scene::NodeTree* tree = nullptr;
	std::vector<scene::NodeId> instances;

	Option<ScriptError> OnInit(Interpreter&, std::vector<Value>& args) override {
		const InitArgs init = ReadInitArgs(args);
		if (init.name.IsNone())
			return Some(BaseError(
				*this, "init",
				String("chemin du fichier `.scene` attendu : super.init(\"scenes/x.scene\")")));
		path = init.name.Unwrap();
		tree = runtime->Tree();
		return NONE;
	}

	/// Retire les instances encore présentes dans la scène où elles ont été
	/// créées (une autre scène ne voit jamais ses nœuds retirés).
	void Clear() {
		if (runtime->Tree() == tree)
			for (scene::NodeId id : instances)
				if (tree && tree->Contains(id))
					(void)runtime->RemoveNode(id);
		instances.clear();
	}

	void OnDeinit(Interpreter&) override { Clear(); }
};

void InstallSceneAsset(Interpreter& vm, Runtime& runtime) {
	OwnerTypeBuilder<SceneAssetOwner> builder([&runtime] {
		auto owner = std::make_shared<SceneAssetOwner>();
		owner->runtime = &runtime;
		return owner;
	});
	// `instantiate({parent, pos, rot, scale})` → nom de la racine créée.
	builder.Method(
		"instantiate", 0, 1,
		[](Interpreter&, SceneAssetOwner& self,
		   std::vector<Value>& args) -> Result<Value, ScriptError> {
			scene::NodeId parent;
			Option<scene::Transform> placement = NONE;
			if (!args.empty() && args[0].IsMap() && args[0].AsMap())
				placement = sd::PlacementFromTable(*self.runtime, *args[0].AsMap(), parent);
			else if (!args.empty() && args[0].IsString())
				parent = self.runtime->ResolveId(args[0].AsString());
			auto created = self.runtime->InstantiateSceneFile(self.path, parent, placement);
			if (created.IsError())
				return Err(BaseError(
					self, "instantiate",
					String::Format("« %s » : %s", self.path.CStr(), created.Error().CStr())));
			self.tree = self.runtime->Tree();
			self.instances.push_back(created.Value());
			return Ok(Value::Str(self.runtime->ScriptHandle(created.Value())));
		});
	builder.Method(
		"instances", 0, 0,
		[](Interpreter&, SceneAssetOwner& self, std::vector<Value>&) -> Result<Value, ScriptError> {
			auto list = std::make_shared<data::script::ListObject>();
			if (self.runtime->Tree() == self.tree)
				for (scene::NodeId id : self.instances)
					if (self.tree && self.tree->Contains(id))
						list->items.push_back(Value::Str(self.runtime->ScriptHandle(id)));
			return Ok(Value::List(std::move(list)));
		});
	builder.Method(
		"clear", 0, 0,
		[](Interpreter&, SceneAssetOwner& self, std::vector<Value>&) -> Result<Value, ScriptError> {
			self.Clear();
			return Ok(Value::Nil());
		});
	builder.Property("path", [](const SceneAssetOwner& self) { return Value::Str(self.path); });
	builder.Display([](const SceneAssetOwner& self) {
		return String::Format("SceneAsset(%s, %d instance(s))", self.path.CStr(),
							  int(self.instances.size()));
	});
	Register(vm, "SceneAsset", builder.Type());
}

} // namespace

// ── NodeOwner ────────────────────────────────────────────────────────────────

scene::Node* NodeOwner::Node() const {
	if (!runtime || !tree || runtime->Tree() != tree)
		return nullptr;
	return runtime->FindObject(node);
}

String NodeOwner::Handle() const {
	return Node() ? runtime->ScriptHandle(node) : String();
}

void NodeOwner::OnDeinit(Interpreter&) {
	if (ownsNode && Node())
		(void)runtime->RemoveNode(node);
	node = scene::NodeId{};
}

Option<ScriptError> NodeOwner::RejectSecondNode() const {
	auto instance = Self();
	if (!instance)
		return NONE;
	for (const auto& other : instance->owners)
		if (other.get() != this && other->initialized &&
			dynamic_cast<const NodeOwner*>(other.get()))
			return Some(Interpreter::MakeError(String::Format(
				"`%s` : l'objet porte déjà un nœud par `%s` — une seule base parmi Node3D, Mesh3D "
				"et Behaviour",
				type ? type->name.CStr() : "?", other->type ? other->type->name.CStr() : "?")));
	return NONE;
}

Option<String> engine_base::ShortName(const String& name) {
	const String bare = name.StartsWith("game.") ? name.Substr(5) : name;
	for (const char* base : ALL)
		if (bare == base)
			return Some(bare);
	return NONE;
}

void InstallEngineBases(Interpreter& vm, Runtime& runtime) {
	InstallScene(vm, runtime);
	InstallBehaviour(vm, runtime);
	InstallNode3D(vm, runtime);
	InstallMesh3D(vm, runtime);
	InstallNode2D(vm, runtime);
	InstallPhysicsBody(vm, runtime);
	InstallLight3D(vm, runtime);
	InstallGameplay(vm);
	InstallSceneAsset(vm, runtime);
}

} // namespace game_editor
