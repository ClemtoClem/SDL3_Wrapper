// Définitions de voxel/content.hpp
#include <voxel/content.hpp>
#include <voxel/json.hpp>

#include <algorithm>
#include <filesystem>

namespace voxel {

namespace {

String Upper(const String &s) { return s.ToUpper(); }
String Lower(const String &s) { return s.ToLower(); }

/// Chemin d'un fichier de mod : absolu tel quel, sinon relatif au mod.
String ResolvePath(const String &path, const String &baseDir) {
	if (path.IsEmpty() || path.CStr()[0] == '/' || baseDir.IsEmpty())
		return path;
	return String((std::filesystem::path(baseDir.CStr()) / path.CStr()).lexically_normal().string().c_str());
}

/// Renvoi vers une autre définition : qualifié par le mod courant.
String Ref(const NodePtr &node, const char *path, const String &mod) {
	const String value = json::Str(node, path);
	return value.IsEmpty() ? value : ContentRegistry::Qualify(value, mod);
}

std::vector<String> RefList(const NodePtr &node, const char *path, const String &mod) {
	std::vector<String> out = json::StrList(node, path);
	for (String &s : out)
		s = ContentRegistry::Qualify(s, mod);
	return out;
}

std::array<uint8_t, 4> Color4(const NodePtr &node, const char *path, std::array<uint8_t, 4> fallback) {
	NodePtr n = json::At(node, path);
	if (!n || !n->IsArray() || n->GetSize() < 3)
		return fallback;
	auto byte = [&](size_t i, uint8_t def) {
		if (i >= n->GetSize())
			return def;
		const double v = json::Num(n->At(i), "", def);
		// Composantes en [0, 1] (flottants) ou en [0, 255].
		const double scaled = (n->At(i)->IsFloat() && v <= 1.0) ? v * 255.0 : v;
		return uint8_t(std::clamp(scaled, 0.0, 255.0));
	};
	return {byte(0, 255), byte(1, 255), byte(2, 255), byte(3, 255)};
}

Ingredient ParseIngredient(const NodePtr &node, const String &mod) {
	Ingredient ing;
	if (!node)
		return ing;
	if (node->IsString()) {
		ing.item = node->stringValue;
	} else {
		ing.item = json::Str(node, "item", json::Str(node, "tag"));
		if (json::Has(node, "tag") && !ing.item.StartsWith("#"))
			ing.item = String("#") + ing.item;
		ing.count = std::max(1, json::Int(node, "count", 1));
	}
	if (!ing.item.IsEmpty() && !ing.IsTag())
		ing.item = ContentRegistry::Qualify(ing.item, mod);
	else if (ing.IsTag())
		ing.item = String("#") + ContentRegistry::Qualify(ing.item.Substring(1), mod);
	return ing;
}

/// "item" ou {"item": …, "count": …}.
std::pair<String, int> ParseStack(const NodePtr &node, const String &mod) {
	if (!node)
		return {String(), 0};
	if (node->IsString())
		return {ContentRegistry::Qualify(node->stringValue, mod), 1};
	if (node->IsArray() && node->GetSize() >= 1)
		return {ContentRegistry::Qualify(json::Str(node->At(0), ""), mod), node->GetSize() > 1 ? json::Int(node->At(1), "", 1) : 1};
	return {Ref(node, "item", mod), std::max(1, json::Int(node, "count", 1))};
}

const char *const BUILTIN_BEHAVIORS[] = {"wander", "follow", "flee", "idle", "float", "look_at_player", "hop"};

} // namespace

const char *DefKindName(DefKind kind) noexcept {
	switch (kind) {
	case DefKind::BLOCK: return "block";
	case DefKind::ITEM: return "item";
	case DefKind::CREATURE: return "creature";
	case DefKind::ENTITY: return "entity";
	case DefKind::RECIPE: return "recipe";
	case DefKind::BIOME: return "biome";
	case DefKind::STRUCTURE: return "structure";
	case DefKind::GENERATOR: return "generator";
	case DefKind::TEXTURE: return "texture";
	case DefKind::MODEL: return "model";
	case DefKind::SOUND: return "sound";
	case DefKind::SOUND_SET: return "soundset";
	case DefKind::SHADER: return "shader";
	case DefKind::BEHAVIOR: return "behavior";
	case DefKind::CONFIG: return "config";
	case DefKind::DIMENSION: return "dimension";
	}
	return "?";
}

String SoundSet::Get(const char *event) const {
	auto it = events.find(event);
	return it == events.end() ? String() : it->second;
}

bool BlockDef::HasTag(const String &tag) const { return std::find(tags.begin(), tags.end(), tag) != tags.end(); }
bool ItemDef::HasTag(const String &tag) const { return std::find(tags.begin(), tags.end(), tag) != tags.end(); }

// ── Sous-structures ──────────────────────────────────────────────────────────

namespace parse {

std::vector<Box> NodeBoxes(const NodePtr &list) {
	std::vector<Box> boxes;
	for (const std::vector<double> &row : json::NumRows(list, "")) {
		if (row.size() < 6)
			continue;
		Box b;
		b.min = {float(std::min(row[0], row[3]) + 0.5), float(std::min(row[1], row[4]) + 0.5), float(std::min(row[2], row[5]) + 0.5)};
		b.max = {float(std::max(row[0], row[3]) + 0.5), float(std::max(row[1], row[4]) + 0.5), float(std::max(row[2], row[5]) + 0.5)};
		boxes.push_back(b);
	}
	return boxes;
}

RenderMaterial Material(const String &name) {
	const String n = Upper(name);
	if (n == "CUTOUT" || n == "ALPHA_TEST" || n == "CLIP")
		return RenderMaterial::CUTOUT;
	if (n == "TRANSLUCENT" || n == "BLEND")
		return RenderMaterial::TRANSLUCENT;
	if (n == "TRANSPARENT")
		return RenderMaterial::TRANSPARENT;
	if (n == "INVISIBLE" || n == "NONE")
		return RenderMaterial::INVISIBLE;
	return RenderMaterial::OPAQUE;
}

Appearance Look(const NodePtr &graphics) {
	Appearance look;
	if (!graphics)
		return look;
	const String all = json::Str(graphics, "texture", json::Str(graphics, "textures.all"));
	for (String &f : look.faces)
		f = all;
	NodePtr textures = json::At(graphics, "textures");
	if (textures && textures->IsObject()) {
		const String side = json::Str(textures, "side", all);
		for (Face f : {Face::EAST, Face::WEST, Face::SOUTH, Face::NORTH})
			look.faces[size_t(f)] = side;
		look.faces[size_t(Face::TOP)] = json::Str(textures, "top", side);
		look.faces[size_t(Face::BOTTOM)] = json::Str(textures, "bottom", side);
		const char *names[FACE_COUNT] = {"east", "west", "top", "bottom", "south", "north"};
		for (int i = 0; i < FACE_COUNT; ++i)
			if (json::Has(textures, names[i]))
				look.faces[size_t(i)] = json::Str(textures, names[i]);
	} else if (textures && textures->IsArray() && textures->GetSize() > 0) {
		// Ordre des « tiles » de Minetest : +Y, −Y, +X, −X, +Z, −Z (la
		// dernière texture se répète).
		const Face order[FACE_COUNT] = {Face::TOP, Face::BOTTOM, Face::EAST, Face::WEST, Face::SOUTH, Face::NORTH};
		for (int i = 0; i < FACE_COUNT; ++i)
			look.faces[size_t(order[i])] = json::Str(textures->At(std::min<size_t>(size_t(i), textures->GetSize() - 1)), "");
	}
	look.material = Material(json::Str(graphics, "material", "OPAQUE"));
	look.backfaceCulling = json::Bool(graphics, "backface_culling", json::Bool(graphics, "backfaceCulling", true));
	look.tint = Color4(graphics, "tint", Color4(graphics, "color", look.tint));
	look.lightEmission = std::clamp(json::Int(graphics, "light", json::Int(graphics, "lightEmission", 0)), 0, 15);
	look.shader = json::Str(graphics, "shader");
	look.biomeTint = json::Str(graphics, "biomeTint");
	if (NodePtr faces = json::At(graphics, "tintFaces"); faces && faces->IsArray()) {
		look.tintFaces = 0;
		for (size_t i = 0; i < faces->GetSize(); ++i) {
			const String f = json::Str(faces->At(i), "");
			const char *names[FACE_COUNT] = {"east", "west", "top", "bottom", "south", "north"};
			for (int k = 0; k < FACE_COUNT; ++k)
				if (f == names[k] || (f == "side" && k != int(Face::TOP) && k != int(Face::BOTTOM)))
					look.tintFaces |= uint8_t(1u << k);
		}
	}
	return look;
}

SoundSet Sounds(const NodePtr &sounds) {
	SoundSet set;
	if (sounds && sounds->IsString()) { // renvoi à un jeu de sons
		set.group = sounds->stringValue;
		return set;
	}
	if (!sounds || !sounds->IsObject())
		return set;
	for (const String &key : sounds->Keys())
		if (NodePtr v = sounds->Get(key); v && v->IsString()) {
			if (key == "group")
				set.group = v->stringValue;
			else
				set.events[std::string(key.CStr())] = v->stringValue;
		}
	return set;
}

ShapeDesc Shape(const NodePtr &def, const String &type, const String &baseDir) {
	ShapeDesc shape;
	shape.size = json::Vec3(def, "size", {1.f, 1.f, 1.f});
	String kind = Lower(type);
	NodePtr node = json::At(def, "shape");
	if (node && node->IsString())
		kind = Lower(node->stringValue);
	else if (node && node->IsObject())
		kind = Lower(json::Str(node, "type", kind));
	if (json::Has(def, "drawtype"))
		kind = Lower(json::Str(def, "drawtype"));

	if (kind == "nodebox" || kind == "boxes") {
		shape.kind = ShapeKind::NODEBOX;
		NodePtr nb = json::At(def, "nodebox");
		if (!nb && node && node->IsObject())
			nb = node;
		if (nb && nb->IsArray())
			shape.boxes = NodeBoxes(nb);
		else if (nb)
			shape.boxes = NodeBoxes(json::Has(nb, "fixed") ? json::At(nb, "fixed") : json::At(nb, "boxes"));
	} else if (kind == "obj" || kind == "b3d" || kind == "mesh" || kind == "model") {
		shape.kind = ShapeKind::MODEL;
		String model = json::Str(def, "path", json::Str(def, "model", node && node->IsObject() ? json::Str(node, "path", json::Str(node, "model")) : String()));
		// Un id de ModelDef (« mod:nom ») ou un fichier relatif au mod.
		const bool isId = model.Contains(":") && !model.Contains("/") && !model.Contains(".");
		shape.model = isId ? model : ResolvePath(model, baseDir);
	} else if (kind == "cross" || kind == "plant" || kind == "plantlike") {
		shape.kind = ShapeKind::CROSS;
	} else if (kind == "none" || kind == "air" || kind == "invisible") {
		shape.kind = ShapeKind::NONE;
	} else {
		shape.kind = ShapeKind::CUBE;
	}
	return shape;
}

} // namespace parse

// ── Registre ─────────────────────────────────────────────────────────────────

ContentRegistry::ContentRegistry() : m_config(dataio::Node::MakeObject()) {
	BlockDef air;
	air.id = AIR_ID;
	air.mod = "core";
	air.displayName = "Air";
	air.numeric = AIR;
	air.shape.kind = ShapeKind::NONE;
	air.look.material = RenderMaterial::INVISIBLE;
	air.solid = false;
	air.collision = CollisionKind::NONE;
	air.walkable = false;
	air.replaceable = true;
	air.hardness = -1.f;
	m_blocks.push_back(air);
	m_blockIds[AIR_ID] = AIR;

	// Schéma 0 → 1 : anciens noms de champs (à la Minetest).
	RegisterMigration(0, [](NodePtr &def) {
		if (json::Has(def, "tiles") && !json::Has(def, "graphics")) {
			NodePtr graphics = dataio::Node::MakeObject();
			graphics->Set("textures", json::At(def, "tiles")->Clone());
			def->Set("graphics", graphics);
			def->Remove("tiles");
		}
		if (json::Has(def, "collision_box") && !json::Has(def, "physics")) {
			NodePtr physics = dataio::Node::MakeObject();
			NodePtr collision = dataio::Node::MakeObject();
			collision->Set("shape", json::MakeStr("FIXED"));
			NodePtr box = json::At(def, "collision_box");
			collision->Set("fixed", (json::Has(box, "fixed") ? json::At(box, "fixed") : box)->Clone());
			physics->Set("collision", collision);
			def->Set("physics", physics);
		}
		if (json::Has(def, "walkable") && !json::Has(def, "physics.solid")) {
			NodePtr physics = json::At(def, "physics");
			if (!physics) {
				physics = dataio::Node::MakeObject();
				def->Set("physics", physics);
			}
			physics->Set("solid", json::MakeBool(json::Bool(def, "walkable", true)));
		}
		if (json::Has(def, "description") && !json::Has(def, "displayName"))
			def->Set("displayName", json::MakeStr(json::Str(def, "description")));
	});
}

void ContentRegistry::RegisterMigration(int fromVersion, Migration migration) {
	m_migrations[fromVersion] = std::move(migration);
}

void ContentRegistry::Migrate(NodePtr &definition) const {
	int version = json::Int(definition, "schemaVersion", 0);
	while (version < SCHEMA_VERSION) {
		if (auto it = m_migrations.find(version); it != m_migrations.end())
			it->second(definition);
		++version;
	}
	definition->Set("schemaVersion", json::MakeInt(version));
}

String ContentRegistry::Qualify(const String &id, const String &mod) {
	if (id.IsEmpty() || id.Contains(":"))
		return id;
	return mod + String(":") + id;
}

Result<std::vector<String>, String> ContentRegistry::LoadFile(const String &path, const LoadContext &context) {
	Result<NodePtr, String> root = json::ReadFile(path);
	if (root.IsError())
		return Err(root.Error());
	LoadContext ctx = context;
	ctx.source = path;
	std::vector<String> ids;
	auto one = [&](const NodePtr &def) -> Option<String> {
		Result<String, String> id = Load(def, ctx);
		if (id.IsError())
			return Some(id.Error());
		ids.push_back(id.Unwrap());
		return NONE;
	};
	if (root.Value()->IsArray()) {
		for (size_t i = 0; i < root.Value()->GetSize(); ++i)
			if (Option<String> err = one(root.Value()->At(i)); err.IsSome())
				return Err(err.Unwrap());
	} else if (Option<String> err = one(root.Value()); err.IsSome()) {
		return Err(err.Unwrap());
	}
	return Ok(std::move(ids));
}

Result<String, String> ContentRegistry::Load(const NodePtr &definition, const LoadContext &ctx) {
	if (!definition || !definition->IsObject())
		return Err(String::Format("%s : une définition doit être un objet JSON", ctx.source.CStr()));
	NodePtr def = definition->Clone();
	Migrate(def);
	const String type = Lower(json::Str(def, "type", "block"));
	if (type != "config" && json::Str(def, "id").IsEmpty())
		return Err(String::Format("%s : définition « %s » sans « id »", ctx.source.CStr(), type.CStr()));
	Result<String, String> loaded = Err(String());
	if (type == "block" || type == "cube" || type == "nodebox" || type == "cross" || type == "plant" || type == "obj" ||
		type == "b3d" || type == "mesh")
		loaded = LoadBlock(def, ctx, type);
	else if (type == "item")
		loaded = LoadItem(def, ctx, false);
	else if (type == "tool")
		loaded = LoadItem(def, ctx, true);
	else if (type == "creature" || type == "entity" || type == "interactive" || type == "projectile" || type == "vehicle")
		loaded = LoadEntity(def, ctx, type);
	else if (type == "recipe")
		loaded = LoadRecipe(def, ctx);
	else if (type == "biome")
		loaded = LoadBiome(def, ctx);
	else if (type == "structure")
		loaded = LoadStructure(def, ctx);
	else if (type == "generator" || type == "terrain_generator")
		loaded = LoadGenerator(def, ctx);
	else if (type == "texture")
		loaded = LoadTexture(def, ctx);
	else if (type == "model")
		loaded = LoadModel(def, ctx);
	else if (type == "sound")
		loaded = LoadSound(def, ctx);
	else if (type == "soundset" || type == "sound_set")
		loaded = LoadSoundSet(def, ctx);
	else if (type == "shader")
		loaded = LoadShader(def, ctx);
	else if (type == "behavior")
		loaded = LoadBehavior(def, ctx);
	else if (type == "config")
		loaded = LoadConfig(def, ctx);
	else if (type == "dimension")
		loaded = LoadDimension(def, ctx);
	else
		return Err(String::Format("%s : type de définition inconnu « %s »", ctx.source.CStr(), type.CStr()));
	if (loaded.IsError())
		return Err(String::Format("%s : %s", ctx.source.CStr(), loaded.Error().CStr()));
	return loaded;
}

Result<String, String> ContentRegistry::LoadBlock(const NodePtr &def, const LoadContext &ctx, const String &type) {
	BlockDef b;
	b.id = Qualify(json::Str(def, "id"), ctx.mod);
	if (b.id == AIR_ID)
		return Err(String("« core:air » est réservé"));
	b.mod = ctx.mod;
	b.displayName = json::Str(def, "displayName", json::Str(def, "name", b.id));
	b.tags = json::StrList(def, "tags");
	b.shape = parse::Shape(def, type, ctx.baseDir);
	b.look = parse::Look(json::At(def, "graphics"));
	for (String &face : b.look.faces)
		face = TextureRef(face, ctx);
	if (b.shape.kind == ShapeKind::NONE)
		b.look.material = RenderMaterial::INVISIBLE;

	// Physique et collision.
	b.solid = json::Bool(def, "physics.solid", b.shape.kind != ShapeKind::CROSS && b.shape.kind != ShapeKind::NONE);
	String collision = Upper(json::Str(def, "physics.collision.shape", ""));
	if (collision.IsEmpty())
		collision = b.shape.kind == ShapeKind::NODEBOX ? String("FROM_NODEBOX")
					: (b.shape.kind == ShapeKind::CROSS || b.shape.kind == ShapeKind::NONE) ? String("NONE")
																							 : String("FULL_CUBE");
	if (collision == "NONE") {
		b.collision = CollisionKind::NONE;
	} else if (collision == "FROM_NODEBOX" || collision == "NODEBOX") {
		b.collision = CollisionKind::BOXES;
		b.collisionBoxes = b.shape.boxes;
	} else if (collision == "FIXED" || collision == "BOXES") {
		b.collision = CollisionKind::BOXES;
		NodePtr col = json::At(def, "physics.collision");
		b.collisionBoxes = parse::NodeBoxes(json::Has(col, "fixed") ? json::At(col, "fixed") : json::At(col, "boxes"));
		if (b.collisionBoxes.empty() && b.shape.kind == ShapeKind::NODEBOX)
			b.collisionBoxes = b.shape.boxes; // « FIXED » sans liste : celle de la nodebox
	} else {
		b.collision = CollisionKind::FULL_CUBE;
		if (b.shape.size.x != 1.f || b.shape.size.y != 1.f || b.shape.size.z != 1.f) {
			// Bloc plus petit/grand qu'une cellule : centré en x/z, posé en y.
			b.collision = CollisionKind::BOXES;
			const math::FVector3 s = b.shape.size;
			b.collisionBoxes.push_back(Box{{0.5f - s.x * 0.5f, 0.f, 0.5f - s.z * 0.5f}, {0.5f + s.x * 0.5f, s.y, 0.5f + s.z * 0.5f}});
		}
	}
	if (b.collision == CollisionKind::BOXES && b.collisionBoxes.empty())
		b.collision = CollisionKind::NONE;
	b.slipperiness = float(json::Num(def, "physics.surface.slipperiness", 0.0));
	b.walkable = json::Bool(def, "physics.surface.walkable", b.solid);
	b.climbable = json::Bool(def, "physics.climbable", b.HasTag("climbable"));
	b.fluid = json::Bool(def, "physics.fluid", b.HasTag("fluid"));
	b.viscosity = float(json::Num(def, "physics.viscosity", b.fluid ? 0.5 : 0.0));
	b.sinking = std::max(0.f, float(json::Num(def, "physics.sinking", 0.0)));
	b.selectable = json::Bool(def, "gameplay.selectable", true);
	b.requiresSupport = json::Bool(def, "physics.structural.requiresSupport", false);
	b.supportStrength = float(json::Num(def, "physics.structural.supportStrength", 0.0));
	b.falling = json::Bool(def, "simulation.falling.enabled", false);
	b.fallingBehavior = json::Str(def, "simulation.falling.behavior", "FALL_AS_BLOCK");

	// Jeu.
	b.hardness = float(json::Num(def, "gameplay.hardness", 1.0));
	b.preferredTool = json::Str(def, "gameplay.tool");
	b.replaceable = json::Bool(def, "gameplay.replaceable", b.fluid || b.HasTag("replaceable"));
	if (NodePtr drops = json::At(def, "gameplay.drops"); drops) {
		const auto [item, count] = ParseStack(drops, ctx.mod);
		b.drops = item;
		b.dropChance = float(json::Num(drops, "chance", 1.0));
		b.dropCount = count;
	} else {
		b.drops = b.fluid ? String() : b.id;
	}
	b.sounds = parse::Sounds(json::At(def, "sounds"));
	for (auto &[event, sound] : b.sounds.events)
		sound = Qualify(sound, ctx.mod);
	b.sounds.group = Qualify(b.sounds.group, ctx.mod);
	b.behavior = Ref(def, "behavior", ctx.mod);
	// Plusieurs cellules, clic droit, rangement.
	auto cell = [](const NodePtr &n) {
		if (!n || !n->IsArray() || n->GetSize() < 3)
			return BlockPos{0, 0, 0};
		return BlockPos{json::Int(n->At(0), "", 0), json::Int(n->At(1), "", 0), json::Int(n->At(2), "", 0)};
	};
	if (NodePtr parts = json::At(def, "parts"); parts && parts->IsArray())
		for (size_t i = 0; i < parts->GetSize(); ++i)
			b.parts.push_back(BlockDef::Part{cell(json::At(parts->At(i), "offset")), Ref(parts->At(i), "block", ctx.mod)});
	if (json::Has(def, "partOf"))
		b.partOf = cell(json::At(def, "partOf"));
	b.toggle = Ref(def, "interact.toggle", ctx.mod);
	b.toggleSound = Qualify(json::Str(def, "interact.sound"), ctx.mod);
	b.use = json::Str(def, "interact.action");
	b.containerSlots = std::clamp(json::Int(def, "container.slots", 0), 0, 108);
	if (b.containerSlots > 0 && b.use.IsEmpty())
		b.use = "container";
	b.raw = def;

	// Identifiant numérique : conservé quand un mod surcharge un bloc.
	const std::string key(b.id.CStr());
	if (auto it = m_blockIds.find(key); it != m_blockIds.end()) {
		b.numeric = it->second;
		m_blocks[b.numeric] = b;
	} else {
		if (m_blocks.size() >= 0xFFFF)
			return Err(String("trop de blocs (65535 au plus)"));
		b.numeric = BlockId(m_blocks.size());
		m_blockIds[key] = b.numeric;
		m_blocks.push_back(b);
	}

	// Objet correspondant (posable), sauf « "item": false ».
	if (json::Bool(def, "item", true) && !m_items.Find(b.id)) {
		ItemDef item;
		item.id = b.id;
		item.mod = ctx.mod;
		item.displayName = b.displayName;
		item.tags = b.tags;
		item.icon = b.look.Texture(Face::SOUTH).IsEmpty() ? b.look.Texture(Face::TOP) : b.look.Texture(Face::SOUTH);
		if (json::Has(def, "icon")) // objet dessiné à plat (porte, lit…)
			item.icon = TextureRef(json::Str(def, "icon"), ctx);
		item.placesBlock = b.id;
		item.maxStack = json::Int(def, "gameplay.maxStack", 64);
		item.raw = def;
		m_items.Put(item);
	}
	return Ok(b.id);
}

Result<String, String> ContentRegistry::LoadItem(const NodePtr &def, const LoadContext &ctx, bool tool) {
	ItemDef item;
	item.id = Qualify(json::Str(def, "id"), ctx.mod);
	item.mod = ctx.mod;
	item.displayName = json::Str(def, "displayName", item.id);
	item.tags = json::StrList(def, "tags");
	item.icon = TextureRef(json::Str(def, "icon", json::Str(def, "graphics.texture")), ctx);
	item.placesBlock = Ref(def, "places", ctx.mod);
	item.maxStack = std::max(1, json::Int(def, "maxStack", json::Int(def, "gameplay.maxStack", tool ? 1 : 64)));
	if (tool || json::Has(def, "tool")) {
		ToolProps t;
		NodePtr node = json::At(def, "tool");
		t.kind = json::Str(node, "kind", json::Str(def, "toolKind"));
		t.speed = float(json::Num(node, "speed", 1.0));
		t.durability = json::Int(node, "durability", 0);
		t.damage = float(json::Num(node, "damage", 1.0));
		t.level = json::Int(node, "level", 0);
		item.tool = t;
		item.maxStack = 1;
	}
	item.food = float(json::Num(def, "food", 0.0));
	item.onUse = json::Str(def, "onUse");
	item.raw = def;
	m_items.Put(item);
	return Ok(item.id);
}

Result<String, String> ContentRegistry::LoadEntity(const NodePtr &def, const LoadContext &ctx, const String &kind) {
	EntityDef e;
	e.id = Qualify(json::Str(def, "id"), ctx.mod);
	e.mod = ctx.mod;
	e.kind = kind;
	e.displayName = json::Str(def, "displayName", e.id);
	e.tags = json::StrList(def, "tags");
	e.colliderSize = json::Vec3(def, "physics.collider", json::Vec3(def, "physics.collision.size", json::Vec3(def, "size", e.colliderSize)));
	e.shape = parse::Shape(def, json::Str(def, "shape", "cube"), ctx.baseDir);
	if (!json::Has(def, "size"))
		e.shape.size = e.colliderSize;
	e.look = parse::Look(json::At(def, "graphics"));
	for (String &face : e.look.faces)
		face = TextureRef(face, ctx);
	// Modèle articulé : "rig": {"model": "core:biped", "skins": [...], "scale": 1}
	// ou "rig": "core:pig_model".
	if (NodePtr rig = json::At(def, "rig"); rig && rig->IsString()) {
		e.rig.model = Qualify(rig->stringValue, ctx.mod);
	} else if (rig && rig->IsObject()) {
		e.rig.model = Ref(rig, "model", ctx.mod);
		if (const String skin = json::Str(rig, "skin"); !skin.IsEmpty())
			e.rig.skins.push_back(TextureRef(skin, ctx));
		for (const String &skin : json::StrList(rig, "skins"))
			e.rig.skins.push_back(TextureRef(skin, ctx));
		e.rig.scale = float(json::Num(rig, "scale", 1.0));
		e.rig.yaw = float(json::Num(rig, "yaw", 0.0));
	}
	e.mass = float(json::Num(def, "physics.mass", 1.0));
	e.gravityScale = float(json::Num(def, "physics.gravityScale", 1.0));
	e.friction = float(json::Num(def, "physics.friction", 0.6));
	e.bounciness = float(json::Num(def, "physics.bounciness", 0.0));
	e.drag = float(json::Num(def, "physics.drag", 0.0));
	e.maxSpeed = float(json::Num(def, "physics.maxSpeed", 4.0));
	e.jumpSpeed = float(json::Num(def, "physics.jumpSpeed", 8.0));
	e.stepHeight = float(json::Num(def, "physics.stepHeight", 0.6));
	e.swims = json::Bool(def, "physics.swims", true);
	e.flies = json::Bool(def, "physics.flies", false);
	e.pushable = json::Bool(def, "physics.pushable", true);
	e.collidesWithEntities = json::Bool(def, "physics.collidesWithEntities", true);
	e.persistent = json::Bool(def, "persistent", true);
	e.health = float(json::Num(def, "gameplay.health", json::Num(def, "health", 10.0)));
	if (NodePtr list = json::At(def, "behaviors"); list && list->IsArray()) {
		for (size_t i = 0; i < list->GetSize(); ++i) {
			NodePtr b = list->At(i);
			BehaviorRef ref;
			if (b && b->IsString()) {
				ref.id = b->stringValue;
			} else {
				ref.id = json::Str(b, "id", json::Str(b, "behavior"));
				ref.params = b;
			}
			// Comportements intégrés : nom court sans espace de noms.
			const bool builtin = std::any_of(std::begin(BUILTIN_BEHAVIORS), std::end(BUILTIN_BEHAVIORS),
											 [&](const char *n) { return ref.id == n; });
			if (!builtin)
				ref.id = Qualify(ref.id, ctx.mod);
			e.behaviors.push_back(ref);
		}
	}
	e.sounds = parse::Sounds(json::At(def, "sounds"));
	for (auto &[event, sound] : e.sounds.events)
		sound = Qualify(sound, ctx.mod);
	e.sounds.group = Qualify(e.sounds.group, ctx.mod);
	if (NodePtr drops = json::At(def, "drops"); drops && drops->IsArray())
		for (size_t i = 0; i < drops->GetSize(); ++i)
			if (auto stack = ParseStack(drops->At(i), ctx.mod); !stack.first.IsEmpty())
				e.drops.push_back(stack);
	e.onInteract = json::Str(def, "onInteract", json::Str(def, "interact"));
	e.raw = def;
	m_entities.Put(e);
	return Ok(e.id);
}

Result<String, String> ContentRegistry::LoadRecipe(const NodePtr &def, const LoadContext &ctx) {
	RecipeDef r;
	r.id = Qualify(json::Str(def, "id"), ctx.mod);
	r.mod = ctx.mod;
	const String kind = Lower(json::Str(def, "kind", json::Str(def, "recipeType", json::Has(def, "pattern") ? "shaped" : "shapeless")));
	r.kind = kind == "shaped" ? RecipeKind::SHAPED : kind == "cooking" || kind == "smelting" ? RecipeKind::COOKING : RecipeKind::SHAPELESS;
	r.pattern = json::StrList(def, "pattern");
	if (NodePtr key = json::At(def, "key"); key && key->IsObject())
		for (const String &symbol : key->Keys())
			if (symbol.GetSize() >= 1)
				r.key[symbol.CharAt(0)] = ParseIngredient(key->Get(symbol), ctx.mod);
	if (NodePtr list = json::At(def, "ingredients"); list && list->IsArray())
		for (size_t i = 0; i < list->GetSize(); ++i)
			r.ingredients.push_back(ParseIngredient(list->At(i), ctx.mod));
	const auto [result, count] = ParseStack(json::At(def, "result"), ctx.mod);
	r.result = result;
	r.resultCount = std::max(1, count);
	r.time = float(json::Num(def, "time", 0.0));
	r.station = Ref(def, "station", ctx.mod);
	r.category = json::Str(def, "category");
	r.requiresLevel = json::Int(def, "requiresLevel", 0);
	if (r.result.IsEmpty())
		return Err(String::Format("recette %s sans résultat", r.id.CStr()));
	if (r.kind == RecipeKind::SHAPED && r.pattern.empty())
		return Err(String::Format("recette %s : « pattern » attendu", r.id.CStr()));
	if (r.kind != RecipeKind::SHAPED && r.ingredients.empty())
		return Err(String::Format("recette %s : « ingredients » attendu", r.id.CStr()));
	r.raw = def;
	m_recipes.Put(r);
	return Ok(r.id);
}

Result<String, String> ContentRegistry::LoadBiome(const NodePtr &def, const LoadContext &ctx) {
	BiomeDef b;
	b.id = Qualify(json::Str(def, "id"), ctx.mod);
	b.mod = ctx.mod;
	b.displayName = json::Str(def, "displayName", b.id);
	b.dimension = Ref(def, "dimension", ctx.mod);
	b.temperature = float(json::Num(def, "climate.temperature", json::Num(def, "temperature", 0.5)));
	b.humidity = float(json::Num(def, "climate.humidity", json::Num(def, "humidity", 0.5)));
	b.heightBase = float(json::Num(def, "terrain.height", 64.0));
	b.heightVariation = float(json::Num(def, "terrain.variation", 8.0));
	b.roughness = float(json::Num(def, "terrain.roughness", 0.5));
	b.surface = Ref(def, "blocks.surface", ctx.mod);
	b.subsurface = Ref(def, "blocks.subsurface", ctx.mod);
	b.filler = Ref(def, "blocks.filler", ctx.mod);
	b.underwater = Ref(def, "blocks.underwater", ctx.mod);
	b.subsurfaceDepth = json::Int(def, "blocks.subsurfaceDepth", 3);
	if (NodePtr list = json::At(def, "decorations"); list && list->IsArray())
		for (size_t i = 0; i < list->GetSize(); ++i) {
			NodePtr d = list->At(i);
			Decoration deco;
			deco.block = Ref(d, "block", ctx.mod);
			deco.structure = Ref(d, "structure", ctx.mod);
			deco.feature = json::Str(d, "feature");
			deco.params = d;
			deco.chance = float(json::Num(d, "chance", 0.0));
			deco.onBlocks = RefList(d, "on", ctx.mod);
			b.decorations.push_back(deco);
		}
	const auto sky = Color4(def, "sky", {b.skyColor[0], b.skyColor[1], b.skyColor[2], 255});
	b.skyColor = {sky[0], sky[1], sky[2]};
	const auto fog = Color4(def, "fog", {b.fogColor[0], b.fogColor[1], b.fogColor[2], 255});
	b.fogColor = {fog[0], fog[1], fog[2]};
	for (auto [key, slot] : {std::pair{"colors.grass", &b.grassColor}, {"colors.foliage", &b.foliageColor}, {"colors.water", &b.waterColor}})
		if (json::Has(def, key)) {
			const auto c = Color4(def, key, {255, 255, 255, 255});
			*slot = std::array<uint8_t, 3>{c[0], c[1], c[2]};
		}
	b.raw = def;
	m_biomes.Put(b);
	return Ok(b.id);
}

Result<String, String> ContentRegistry::LoadStructure(const NodePtr &def, const LoadContext &ctx) {
	StructureDef s;
	s.id = Qualify(json::Str(def, "id"), ctx.mod);
	s.mod = ctx.mod;
	s.replaceSolid = json::Bool(def, "replaceSolid", false);
	const math::FVector3 anchor = json::Vec3(def, "anchor", {0.f, 0.f, 0.f});
	s.anchor = {int(anchor.x), int(anchor.y), int(anchor.z)};
	if (NodePtr list = json::At(def, "blocks"); list && list->IsArray()) {
		for (size_t i = 0; i < list->GetSize(); ++i) {
			NodePtr b = list->At(i);
			if (b && b->IsArray() && b->GetSize() >= 4)
				s.blocks.push_back({BlockPos{json::Int(b->At(0), ""), json::Int(b->At(1), ""), json::Int(b->At(2), "")},
									Qualify(json::Str(b->At(3), ""), ctx.mod)});
			else if (b && b->IsObject()) {
				const math::FVector3 p = json::Vec3(b, "pos");
				s.blocks.push_back({BlockPos{int(p.x), int(p.y), int(p.z)}, Ref(b, "block", ctx.mod)});
			}
		}
	}
	// Couches : du bas vers le haut ; chaque couche est une liste de lignes
	// (z), chaque caractère une colonne (x). Palette : symbole → bloc ; un
	// symbole absent de la palette (espace…) ne pose rien.
	if (NodePtr layers = json::At(def, "layers"); layers && layers->IsArray()) {
		NodePtr palette = json::At(def, "palette");
		for (size_t y = 0; y < layers->GetSize(); ++y) {
			NodePtr rows = layers->At(y);
			for (size_t z = 0; rows && z < rows->GetSize(); ++z) {
				const String row = json::Str(rows->At(z), "");
				for (size_t x = 0; x < row.GetSize(); ++x) {
					const String symbol(row.CharAt(x));
					if (!palette || !palette->Has(symbol))
						continue;
					NodePtr entry = palette->Get(symbol);
					if (!entry || !entry->IsString())
						continue;
					s.blocks.push_back({BlockPos{int(x), int(y), int(z)}, Qualify(entry->stringValue, ctx.mod)});
				}
			}
		}
	}
	for (const auto &[pos, block] : s.blocks)
		s.size = {std::max(s.size.x, pos.x + 1), std::max(s.size.y, pos.y + 1), std::max(s.size.z, pos.z + 1)};
	if (s.blocks.empty())
		return Err(String::Format("structure %s vide (« blocks » ou « layers » attendu)", s.id.CStr()));
	s.raw = def;
	m_structures.Put(s);
	return Ok(s.id);
}

Result<String, String> ContentRegistry::LoadDimension(const NodePtr &def, const LoadContext &ctx) {
	DimensionDef d;
	d.id = Qualify(json::Str(def, "id"), ctx.mod);
	d.mod = ctx.mod;
	d.displayName = json::Str(def, "displayName", json::Str(def, "name", d.id));
	d.description = json::Str(def, "description");
	d.generator = Ref(def, "generator", ctx.mod);
	const auto sky = Color4(def, "sky.color", {d.skyColor[0], d.skyColor[1], d.skyColor[2], 255});
	d.skyColor = {sky[0], sky[1], sky[2]};
	const auto fog = Color4(def, "sky.fog", {sky[0], sky[1], sky[2], 255});
	d.fogColor = {fog[0], fog[1], fog[2]};
	d.skyFromBiome = json::Bool(def, "sky.fromBiome", !json::Has(def, "sky.color"));
	d.sun = json::Bool(def, "sky.sun", true);
	d.cubemap = json::Str(def, "sky.cubemap").IsEmpty() ? String() : TextureRef(json::Str(def, "sky.cubemap"), ctx);
	d.nightCubemap = json::Str(def, "sky.nightCubemap").IsEmpty() ? String() : TextureRef(json::Str(def, "sky.nightCubemap"), ctx);
	d.clouds = json::Bool(def, "sky.clouds", true);
	d.stars = json::Bool(def, "sky.stars", true);
	d.fogDistance = float(json::Num(def, "sky.fogDistance", 0.0));
	d.dayCycle = json::Bool(def, "time.cycle", true);
	d.fixedTime = float(json::Num(def, "time.fixed", 0.5));
	d.ambient = float(json::Num(def, "light.ambient", 1.0));
	d.skyLight = json::Bool(def, "light.sky", true);
	d.gravity = float(json::Num(def, "physics.gravity", 1.0));
	d.spawnTop = json::Int(def, "spawn.top", 192);
	d.spawnBottom = json::Int(def, "spawn.bottom", -64);
	d.enterOnDeath = json::StrList(def, "enterOnDeath");
	d.arrivalPortal = Ref(def, "arrival.portal", ctx.mod);
	d.music = Ref(def, "music", ctx.mod);
	const auto color = Color4(def, "color", {d.color[0], d.color[1], d.color[2], 255});
	d.color = {color[0], color[1], color[2]};
	d.raw = def;
	m_dimensions.Put(d);
	return Ok(d.id);
}

Result<String, String> ContentRegistry::LoadGenerator(const NodePtr &def, const LoadContext &ctx) {
	GeneratorDef g;
	g.id = Qualify(json::Str(def, "id"), ctx.mod);
	g.mod = ctx.mod;
	g.kind = Lower(json::Str(def, "kind", json::Str(def, "generator", "noise")));
	g.seaLevel = json::Int(def, "seaLevel", 62);
	g.fluid = Ref(def, "fluid", ctx.mod);
	g.bedrock = Ref(def, "bedrock", ctx.mod);
	g.frequency = float(json::Num(def, "frequency", 0.008));
	g.octaves = json::Int(def, "octaves", 5);
	g.caves = float(json::Num(def, "caves", 0.0));
	g.biomes = RefList(def, "biomes", ctx.mod);
	if (NodePtr layers = json::At(def, "layers"); layers && layers->IsArray())
		for (size_t i = 0; i < layers->GetSize(); ++i)
			if (auto stack = ParseStack(layers->At(i), ctx.mod); !stack.first.IsEmpty())
				g.layers.push_back(stack);
	g.script = json::Str(def, "script");
	g.raw = def;
	m_generators.Put(g);
	return Ok(g.id);
}

Result<String, String> ContentRegistry::LoadTexture(const NodePtr &def, const LoadContext &ctx) {
	TextureDef t;
	t.id = Qualify(json::Str(def, "id"), ctx.mod);
	t.mod = ctx.mod;
	const String path = json::Str(def, "path", json::Str(def, "file"));
	t.compose = json::Str(def, "compose");
	if (t.compose.IsEmpty() && (path.Contains("^") || path.Contains("(")))
		t.compose = path; // « path » écrit comme une composition
	else
		t.path = path.IsEmpty() ? path : ResolvePath(path, ctx.baseDir);
	t.baseDir = ctx.baseDir;
	t.procedural = json::At(def, "procedural");
	if (!t.procedural)
		t.procedural = json::At(def, "generate");
	if (t.path.IsEmpty() && !t.procedural && t.compose.IsEmpty())
		return Err(String::Format("texture %s : « path », « compose » ou « procedural » attendu", t.id.CStr()));
	t.frames = std::max(1, json::Int(def, "frames", 1));
	t.biomeTint = json::Str(def, "biomeTint");
	t.frameTime = float(json::Num(def, "frameTime", 0.25));
	t.raw = def;
	m_textures.Put(t);
	return Ok(t.id);
}

bool ContentRegistry::IsTextureExpression(const String &ref) {
	if (ref.Contains("^") || ref.Contains("("))
		return true;
	const String lower = Lower(ref);
	for (const char *ext : {".png", ".jpg", ".jpeg", ".bmp", ".tga"})
		if (lower.EndsWith(ext))
			return true;
	return false;
}

String ContentRegistry::TextureRef(const String &ref, const LoadContext &ctx) {
	if (ref.IsEmpty() || !IsTextureExpression(ref))
		return ref.IsEmpty() ? ref : Qualify(ref, ctx.mod);
	// Texture implicite, partagée par toutes les définitions du mod qui
	// écrivent la même expression.
	const String id = ctx.mod + String(":{") + ref + String("}");
	if (!m_textures.Find(id)) {
		TextureDef t;
		t.id = id;
		t.mod = ctx.mod;
		t.compose = ref;
		t.baseDir = ctx.baseDir;
		m_textures.Put(t);
	}
	return id;
}

Result<String, String> ContentRegistry::LoadModel(const NodePtr &def, const LoadContext &ctx) {
	ModelDef m;
	m.id = Qualify(json::Str(def, "id"), ctx.mod);
	m.mod = ctx.mod;
	const String path = json::Str(def, "path");
	m.path = ResolvePath(path, ctx.baseDir);
	m.data = json::At(def, "data");
	String format = Lower(json::Str(def, "format"));
	if (format.IsEmpty() && json::Has(def, "parts"))
		format = "rig";
	if (format == "rig") // modèle articulé décrit en ligne (voir rig.hpp)
		m.data = def;
	if (format.IsEmpty())
		format = path.EndsWith(".obj") ? String("obj") : path.EndsWith(".b3d") || path.EndsWith(".b3b") ? String("b3d") : String("json");
	m.format = format;
	if (m.path.IsEmpty() && !m.data)
		return Err(String::Format("modèle %s : « path » ou « data » attendu", m.id.CStr()));
	m.raw = def;
	m_models.Put(m);
	return Ok(m.id);
}

Result<String, String> ContentRegistry::LoadSound(const NodePtr &def, const LoadContext &ctx) {
	SoundDef s;
	s.id = Qualify(json::Str(def, "id"), ctx.mod);
	s.mod = ctx.mod;
	if (const String path = json::Str(def, "path"); !path.IsEmpty())
		s.paths.push_back(ResolvePath(path, ctx.baseDir));
	for (const String &variant : json::StrList(def, "paths"))
		s.paths.push_back(ResolvePath(variant, ctx.baseDir));
	s.path = s.paths.empty() ? String() : s.paths.front();
	s.volume = float(json::Num(def, "volume", 1.0));
	s.pitchVariation = float(json::Num(def, "pitchVariation", 0.0));
	if (s.path.IsEmpty())
		return Err(String::Format("son %s sans « path » ni « paths »", s.id.CStr()));
	s.raw = def;
	m_sounds.Put(s);
	return Ok(s.id);
}

Result<String, String> ContentRegistry::LoadSoundSet(const NodePtr &def, const LoadContext &ctx) {
	SoundSetDef set;
	set.id = Qualify(json::Str(def, "id"), ctx.mod);
	set.mod = ctx.mod;
	set.sounds = parse::Sounds(json::Has(def, "events") ? json::At(def, "events") : json::At(def, "sounds"));
	for (auto &[event, sound] : set.sounds.events)
		sound = Qualify(sound, ctx.mod);
	set.sounds.group = Qualify(set.sounds.group, ctx.mod);
	if (set.sounds.events.empty() && set.sounds.group.IsEmpty())
		return Err(String::Format("jeu de sons %s vide (« events » attendu)", set.id.CStr()));
	set.raw = def;
	m_soundSets.Put(set);
	return Ok(set.id);
}

void ContentRegistry::Finalize() {
	// Événements d'un jeu de sons (et de ses parents), sans écraser ceux
	// écrits directement sur la définition.
	auto complete = [this](SoundSet &sounds) {
		String group = sounds.group;
		for (int depth = 0; depth < 8 && !group.IsEmpty(); ++depth) {
			const SoundSetDef *set = FindSoundSet(group);
			if (!set)
				break;
			for (const auto &[event, sound] : set->sounds.events)
				sounds.events.emplace(event, sound);
			group = set->sounds.group;
		}
	};
	for (BlockDef &b : m_blocks)
		complete(b.sounds);
	for (auto &[id, e] : m_entities.byId)
		complete(e.sounds);
}

Result<String, String> ContentRegistry::LoadShader(const NodePtr &def, const LoadContext &ctx) {
	ShaderDef s;
	s.id = Qualify(json::Str(def, "id"), ctx.mod);
	s.mod = ctx.mod;
	s.vertex = ResolvePath(json::Str(def, "vertex"), ctx.baseDir);
	s.fragment = ResolvePath(json::Str(def, "fragment"), ctx.baseDir);
	if (s.vertex.IsEmpty() || s.fragment.IsEmpty())
		return Err(String::Format("shader %s : « vertex » et « fragment » attendus", s.id.CStr()));
	s.raw = def;
	m_shaders.Put(s);
	return Ok(s.id);
}

Result<String, String> ContentRegistry::LoadBehavior(const NodePtr &def, const LoadContext &ctx) {
	BehaviorDef b;
	b.id = Qualify(json::Str(def, "id"), ctx.mod);
	b.mod = ctx.mod;
	b.builtin = json::Str(def, "builtin");
	b.script = json::Str(def, "script");
	b.params = json::At(def, "params");
	if (b.builtin.IsEmpty() && b.script.IsEmpty())
		return Err(String::Format("comportement %s : « builtin » ou « script » attendu", b.id.CStr()));
	b.raw = def;
	m_behaviors.Put(b);
	return Ok(b.id);
}

Result<String, String> ContentRegistry::LoadConfig(const NodePtr &def, const LoadContext &ctx) {
	NodePtr values = json::At(def, "values");
	if (!values)
		values = def;
	for (const String &key : values->Keys()) {
		if (key == "type" || key == "id" || key == "schemaVersion")
			continue;
		m_config->Set(key, values->Get(key)->Clone());
	}
	return Ok(Qualify(json::Str(def, "id", "config"), ctx.mod));
}

// ── Accès ────────────────────────────────────────────────────────────────────

const BlockDef *ContentRegistry::FindBlock(const String &id) const {
	auto it = m_blockIds.find(std::string(id.CStr()));
	return it == m_blockIds.end() ? nullptr : &m_blocks[it->second];
}

BlockId ContentRegistry::BlockIdOf(const String &id) const {
	auto it = m_blockIds.find(std::string(id.CStr()));
	return it == m_blockIds.end() ? AIR : it->second;
}

const ItemDef *ContentRegistry::FindItem(const String &id) const { return m_items.Find(id); }
const EntityDef *ContentRegistry::FindEntity(const String &id) const { return m_entities.Find(id); }
const RecipeDef *ContentRegistry::FindRecipe(const String &id) const { return m_recipes.Find(id); }
const BiomeDef *ContentRegistry::FindBiome(const String &id) const { return m_biomes.Find(id); }
const StructureDef *ContentRegistry::FindStructure(const String &id) const { return m_structures.Find(id); }
const GeneratorDef *ContentRegistry::FindGenerator(const String &id) const { return m_generators.Find(id); }
const TextureDef *ContentRegistry::FindTexture(const String &id) const { return m_textures.Find(id); }
const ModelDef *ContentRegistry::FindModel(const String &id) const { return m_models.Find(id); }
const SoundDef *ContentRegistry::FindSound(const String &id) const { return m_sounds.Find(id); }
const SoundSetDef *ContentRegistry::FindSoundSet(const String &id) const { return m_soundSets.Find(id); }
const ShaderDef *ContentRegistry::FindShader(const String &id) const { return m_shaders.Find(id); }
const BehaviorDef *ContentRegistry::FindBehavior(const String &id) const { return m_behaviors.Find(id); }
const DimensionDef *ContentRegistry::FindDimension(const String &id) const { return m_dimensions.Find(id); }

std::vector<const ItemDef *> ContentRegistry::Items() const { return m_items.All(); }
std::vector<const EntityDef *> ContentRegistry::Entities() const { return m_entities.All(); }
std::vector<const RecipeDef *> ContentRegistry::Recipes() const { return m_recipes.All(); }
std::vector<const BiomeDef *> ContentRegistry::Biomes() const { return m_biomes.All(); }
std::vector<const StructureDef *> ContentRegistry::Structures() const { return m_structures.All(); }
std::vector<const GeneratorDef *> ContentRegistry::Generators() const { return m_generators.All(); }
std::vector<const TextureDef *> ContentRegistry::Textures() const { return m_textures.All(); }
std::vector<const ModelDef *> ContentRegistry::Models() const { return m_models.All(); }
std::vector<const SoundDef *> ContentRegistry::Sounds() const { return m_sounds.All(); }
std::vector<const ShaderDef *> ContentRegistry::Shaders() const { return m_shaders.All(); }
std::vector<const BehaviorDef *> ContentRegistry::Behaviors() const { return m_behaviors.All(); }
std::vector<const DimensionDef *> ContentRegistry::Dimensions() const { return m_dimensions.All(); }

String ContentRegistry::DefaultDimension() const {
	const String configured = ConfigStr("world.dimension", "core:overworld");
	if (FindDimension(configured))
		return configured;
	return m_dimensions.order.empty() ? String() : String(m_dimensions.order.front().c_str());
}

bool ContentRegistry::ItemHasTag(const String &item, const String &tag) const {
	if (const ItemDef *def = FindItem(item); def && def->HasTag(tag))
		return true;
	const BlockDef *block = FindBlock(item);
	return block && block->HasTag(tag);
}

String ContentRegistry::ConfigStr(const char *path, const String &fallback) const { return json::Str(m_config, path, fallback); }
double ContentRegistry::ConfigNum(const char *path, double fallback) const { return json::Num(m_config, path, fallback); }

size_t ContentRegistry::DefinitionCount() const noexcept {
	return m_blocks.size() + m_items.order.size() + m_entities.order.size() + m_recipes.order.size() + m_biomes.order.size() +
		   m_structures.order.size() + m_generators.order.size() + m_textures.order.size() + m_models.order.size() +
		   m_sounds.order.size() + m_soundSets.order.size() + m_shaders.order.size() + m_behaviors.order.size() +
		   m_dimensions.order.size();
}

std::vector<String> ContentRegistry::Validate() const {
	std::vector<String> warnings;
	auto texture = [&](const String &owner, const String &id) {
		if (!id.IsEmpty() && !FindTexture(id))
			warnings.push_back(String::Format("%s : texture inconnue « %s »", owner.CStr(), id.CStr()));
	};
	auto block = [&](const String &owner, const String &id) {
		if (!id.IsEmpty() && !FindBlock(id))
			warnings.push_back(String::Format("%s : bloc inconnu « %s »", owner.CStr(), id.CStr()));
	};
	auto item = [&](const String &owner, const String &id) {
		if (!id.IsEmpty() && !FindItem(id))
			warnings.push_back(String::Format("%s : objet inconnu « %s »", owner.CStr(), id.CStr()));
	};
	auto sound = [&](const String &owner, const SoundSet &set) {
		if (!set.group.IsEmpty() && !FindSoundSet(set.group))
			warnings.push_back(String::Format("%s : jeu de sons inconnu « %s »", owner.CStr(), set.group.CStr()));
		for (const auto &[event, id] : set.events)
			if (!id.IsEmpty() && !FindSound(id))
				warnings.push_back(String::Format("%s : son inconnu « %s » (%s)", owner.CStr(), id.CStr(), event.c_str()));
	};
	auto model = [&](const String &owner, const ShapeDesc &shape) {
		if (shape.kind != ShapeKind::MODEL)
			return;
		const bool isId = shape.model.Contains(":") && !shape.model.Contains("/");
		if (isId && !FindModel(shape.model))
			warnings.push_back(String::Format("%s : modèle inconnu « %s »", owner.CStr(), shape.model.CStr()));
		if (!isId && !std::filesystem::exists(shape.model.CStr()))
			warnings.push_back(String::Format("%s : fichier de modèle introuvable « %s »", owner.CStr(), shape.model.CStr()));
	};
	for (const BlockDef &b : m_blocks) {
		if (b.IsAir())
			continue;
		if (b.look.material != RenderMaterial::INVISIBLE && b.shape.kind != ShapeKind::NONE)
			for (const String &face : b.look.faces)
				texture(b.id, face);
		item(b.id, b.drops);
		sound(b.id, b.sounds);
		model(b.id, b.shape);
		// Blocs interactifs : parties, bascule, sons d'ouverture.
		for (const BlockDef::Part &part : b.parts) {
			block(b.id, part.block);
			if (const BlockDef *other = FindBlock(part.block); other && !other->partOf)
				warnings.push_back(String::Format("%s : la partie « %s » n'a pas de « partOf »", b.id.CStr(), part.block.CStr()));
		}
		block(b.id, b.toggle);
		SoundSet extra;
		extra.events["interact"] = b.toggleSound;
		extra.events["open"] = Qualify(json::Str(b.raw, "container.openSound"), b.mod);
		extra.events["close"] = Qualify(json::Str(b.raw, "container.closeSound"), b.mod);
		sound(b.id, extra);
		if (!b.use.IsEmpty() && b.use != "sleep" && b.use != "container")
			warnings.push_back(String::Format("%s : action inconnue « %s »", b.id.CStr(), b.use.CStr()));
	}
	for (const ItemDef *i : Items()) {
		texture(i->id, i->icon);
		block(i->id, i->placesBlock);
	}
	for (const EntityDef *e : Entities()) {
		for (const String &face : e->look.faces)
			texture(e->id, face);
		sound(e->id, e->sounds);
		model(e->id, e->shape);
		for (const BehaviorRef &b : e->behaviors) {
			const bool builtin = std::any_of(std::begin(BUILTIN_BEHAVIORS), std::end(BUILTIN_BEHAVIORS),
											 [&](const char *n) { return b.id == n; });
			if (!builtin && !FindBehavior(b.id))
				warnings.push_back(String::Format("%s : comportement inconnu « %s »", e->id.CStr(), b.id.CStr()));
		}
		for (const auto &[drop, count] : e->drops)
			item(e->id, drop);
	}
	for (const RecipeDef *r : Recipes()) {
		item(r->id, r->result);
		auto check = [&](const Ingredient &ing) {
			if (!ing.IsTag()) {
				item(r->id, ing.item);
				return;
			}
			const String tag = ing.item.Substring(1);
			const String shortTag = tag.Contains(":") ? tag.Substring(tag.Find(":") + 1) : tag;
			bool any = false;
			for (const ItemDef *i : Items())
				any = any || ItemHasTag(i->id, tag) || ItemHasTag(i->id, shortTag);
			if (!any)
				warnings.push_back(String::Format("%s : aucun objet ne porte le tag « %s »", r->id.CStr(), tag.CStr()));
		};
		for (const auto &[symbol, ing] : r->key)
			check(ing);
		for (const Ingredient &ing : r->ingredients)
			check(ing);
	}
	for (const BiomeDef *b : Biomes()) {
		for (const String &id : {b->surface, b->subsurface, b->filler, b->underwater})
			block(b->id, id);
		for (const Decoration &d : b->decorations) {
			block(b->id, d.block);
			if (!d.structure.IsEmpty() && !FindStructure(d.structure))
				warnings.push_back(String::Format("%s : structure inconnue « %s »", b->id.CStr(), d.structure.CStr()));
		}
	}
	for (const StructureDef *s : Structures())
		for (const auto &[pos, id] : s->blocks)
			block(s->id, id);
	for (const GeneratorDef *g : Generators()) {
		block(g->id, g->fluid);
		block(g->id, g->bedrock);
		for (const String &biome : g->biomes)
			if (!FindBiome(biome))
				warnings.push_back(String::Format("%s : biome inconnu « %s »", g->id.CStr(), biome.CStr()));
		for (const auto &[layer, n] : g->layers)
			block(g->id, layer);
	}
	for (const DimensionDef *d : Dimensions()) {
		if (!d->generator.IsEmpty() && !FindGenerator(d->generator))
			warnings.push_back(String::Format("%s : générateur inconnu « %s »", d->id.CStr(), d->generator.CStr()));
		block(d->id, d->arrivalPortal);
		for (const String &sky : {d->cubemap, d->nightCubemap})
			if (!sky.IsEmpty() && !FindTexture(sky))
				warnings.push_back(String::Format("%s : texture de ciel inconnue « %s »", d->id.CStr(), sky.CStr()));
		if (!d->music.IsEmpty() && !FindSound(d->music))
			warnings.push_back(String::Format("%s : son inconnu « %s »", d->id.CStr(), d->music.CStr()));
	}
	for (const BlockDef &b : m_blocks)
		if (const String to = json::Str(b.raw, "portal.dimension"); !to.IsEmpty() && to != "@return" && !FindDimension(Qualify(to, b.mod)))
			warnings.push_back(String::Format("%s : portail vers une dimension inconnue « %s »", b.id.CStr(), to.CStr()));
	return warnings;
}

} // namespace voxel
