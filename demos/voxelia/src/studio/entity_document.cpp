// Définitions de voxelia/studio/entity_document.hpp
#include <voxelia/studio/entity_document.hpp>

#include <algorithm>

#include <voxel/json.hpp>
#include <voxel/lighting.hpp>

namespace voxelia::studio {

namespace json = voxel::json;
using dataio::Node;
using dataio::NodePtr;

namespace {

/// Tableau du document à `key` (créé vide s'il manque).
NodePtr ArrayAt(const NodePtr &root, const char *key) {
	NodePtr list = json::At(root, key);
	if (!list || !list->IsArray()) {
		list = Node::MakeArray();
		SetJsonPath(root, key, list);
	}
	return list;
}

/// Copie d'un tableau sans l'élément `index`.
NodePtr Without(const NodePtr &list, size_t index) {
	NodePtr out = Node::MakeArray();
	for (size_t i = 0; i < list->GetSize(); ++i)
		if (i != index)
			out->Push(list->At(i));
	return out;
}

} // namespace

Option<EntityDocument> EntityDocument::FromEntity(const voxel::ContentRegistry &content, const String &entityId) {
	const voxel::EntityDef *def = content.FindEntity(entityId);
	if (!def || !def->source)
		return NONE;
	EntityDocument doc;
	doc.m_json = def->source->Clone();
	doc.m_id = def->id;
	doc.m_mod = def->mod;
	doc.m_baseDir = def->baseDir;
	doc.m_file = def->sourceFile;
	return Some(std::move(doc));
}

EntityDocument EntityDocument::New(const String &id, const String &mod, const String &baseDir, const String &file, const String &kind) {
	EntityDocument doc;
	doc.m_mod = mod;
	doc.m_id = voxel::ContentRegistry::Qualify(id, mod);
	doc.m_baseDir = baseDir;
	doc.m_file = file;
	const size_t colon = id.Find(":");
	const String bare = colon == String::NPOS ? id : id.Substring(colon + 1);
	doc.m_json = Node::MakeObject();
	doc.m_json->Set("type", json::MakeStr(kind));
	doc.m_json->Set("id", json::MakeStr(bare));
	doc.m_json->Set("displayName", json::MakeStr(bare));
	NodePtr collider = Node::MakeArray();
	for (double v : {0.6, 1.8, 0.6})
		collider->Push(json::MakeNum(v));
	SetJsonPath(doc.m_json, "physics.collider", collider);
	SetJsonPath(doc.m_json, "physics.maxSpeed", json::MakeNum(2.0));
	doc.m_json->Set("health", json::MakeNum(10.0));
	NodePtr behaviors = Node::MakeArray();
	behaviors->Push(json::MakeStr(String("wander")));
	doc.m_json->Set("behaviors", behaviors);
	doc.m_dirty = true;
	return doc;
}

// ── Comportements ────────────────────────────────────────────────────────────

std::vector<EntityBehavior> EntityDocument::Behaviors() const {
	std::vector<EntityBehavior> out;
	const NodePtr list = Field("behaviors");
	for (size_t i = 0; list && list->IsArray() && i < list->GetSize(); ++i) {
		const NodePtr b = list->At(i);
		if (b && b->IsString())
			out.push_back(EntityBehavior{b->stringValue, nullptr});
		else
			out.push_back(EntityBehavior{json::Str(b, "id", json::Str(b, "behavior")), b});
	}
	return out;
}

Result<bool, String> EntityDocument::AddBehavior(const String &id) {
	if (id.Trim().IsEmpty())
		return Err(String("comportement vide"));
	Mutate([&](const NodePtr &root) { ArrayAt(root, "behaviors")->Push(json::MakeStr(id.Trim())); });
	return Ok(true);
}

Result<bool, String> EntityDocument::RemoveBehavior(size_t index) {
	const NodePtr list = Field("behaviors");
	if (!list || !list->IsArray() || index >= list->GetSize())
		return Err(String("comportement inconnu"));
	Mutate([&](const NodePtr &root) { root->Set("behaviors", Without(json::At(root, "behaviors"), index)); });
	return Ok(true);
}

Result<bool, String> EntityDocument::MoveBehavior(size_t from, size_t to) {
	const NodePtr list = Field("behaviors");
	if (!list || !list->IsArray() || from >= list->GetSize() || to >= list->GetSize())
		return Err(String("comportement inconnu"));
	if (from == to)
		return Ok(true);
	Mutate([&](const NodePtr &root) {
		const NodePtr old = json::At(root, "behaviors");
		const NodePtr moved = old->At(from);
		NodePtr rest = Without(old, from), out = Node::MakeArray();
		for (size_t i = 0; i <= rest->GetSize(); ++i) {
			if (i == to)
				out->Push(moved);
			if (i < rest->GetSize())
				out->Push(rest->At(i));
		}
		root->Set("behaviors", out);
	});
	return Ok(true);
}

Result<bool, String> EntityDocument::SetBehaviorParam(size_t index, const String &key, NodePtr value) {
	const NodePtr list = Field("behaviors");
	if (!list || !list->IsArray() || index >= list->GetSize())
		return Err(String("comportement inconnu"));
	if (key.IsEmpty() || key == "id")
		return Err(String("paramètre invalide (l'id se change en retirant / ajoutant)"));
	Mutate([&](const NodePtr &root) {
		const NodePtr behaviors = json::At(root, "behaviors");
		NodePtr b = behaviors->At(index);
		if (b && b->IsString()) { // « "wander" » devient un objet au premier paramètre
			NodePtr object = Node::MakeObject();
			object->Set("id", json::MakeStr(b->stringValue));
			NodePtr rebuilt = Node::MakeArray();
			for (size_t i = 0; i < behaviors->GetSize(); ++i)
				rebuilt->Push(i == index ? object : behaviors->At(i));
			root->Set("behaviors", rebuilt);
			b = object;
		}
		if (value)
			b->Set(key, value);
		else
			b->Remove(key);
	});
	return Ok(true);
}

// ── Butin, skins ─────────────────────────────────────────────────────────────

std::vector<std::pair<String, int>> EntityDocument::Drops() const {
	std::vector<std::pair<String, int>> out;
	const NodePtr list = Field("drops");
	for (size_t i = 0; list && list->IsArray() && i < list->GetSize(); ++i) {
		const NodePtr d = list->At(i);
		if (d && d->IsArray() && d->GetSize() >= 1)
			out.push_back({json::Str(d->At(0), "", String()), d->GetSize() > 1 ? json::Int(d->At(1), "", 1) : 1});
		else if (d && d->IsString())
			out.push_back({d->stringValue, 1});
		else
			out.push_back({json::Str(d, "item"), json::Int(d, "count", 1)});
	}
	return out;
}

Result<bool, String> EntityDocument::AddDrop(const String &item, int count) {
	if (item.Trim().IsEmpty() || count <= 0)
		return Err(String("objet et nombre (> 0) attendus"));
	Mutate([&](const NodePtr &root) {
		NodePtr pair = Node::MakeArray();
		pair->Push(json::MakeStr(item.Trim()));
		pair->Push(json::MakeInt(count));
		ArrayAt(root, "drops")->Push(pair);
	});
	return Ok(true);
}

Result<bool, String> EntityDocument::RemoveDrop(size_t index) {
	const NodePtr list = Field("drops");
	if (!list || !list->IsArray() || index >= list->GetSize())
		return Err(String("butin inconnu"));
	Mutate([&](const NodePtr &root) { root->Set("drops", Without(json::At(root, "drops"), index)); });
	return Ok(true);
}

std::vector<String> EntityDocument::Skins() const {
	std::vector<String> out;
	if (const String one = FieldText("rig.skin"); !one.IsEmpty())
		out.push_back(one);
	const NodePtr list = Field("rig.skins");
	for (size_t i = 0; list && list->IsArray() && i < list->GetSize(); ++i)
		if (list->At(i) && list->At(i)->IsString())
			out.push_back(list->At(i)->stringValue);
	return out;
}

Result<bool, String> EntityDocument::AddSkin(const String &texture) {
	if (texture.Trim().IsEmpty())
		return Err(String("texture vide"));
	if (Field("rig") && Field("rig")->IsString())
		return Err(String("« rig » est écrit en raccourci (un id) : passez-le en objet {\"model\": …} dans l'onglet JSON"));
	Mutate([&](const NodePtr &root) {
		NodePtr list = json::At(root, "rig.skins");
		if (!list || !list->IsArray()) {
			list = Node::MakeArray();
			SetJsonPath(root, "rig.skins", list);
		}
		list->Push(json::MakeStr(texture.Trim()));
	});
	return Ok(true);
}

Result<bool, String> EntityDocument::RemoveSkin(size_t index) {
	const NodePtr list = Field("rig.skins");
	const bool single = !FieldText("rig.skin").IsEmpty();
	const size_t offset = single ? 1 : 0;
	if (single && index == 0) {
		Mutate([&](const NodePtr &root) { json::At(root, "rig")->Remove("skin"); });
		return Ok(true);
	}
	if (!list || !list->IsArray() || index < offset || index - offset >= list->GetSize())
		return Err(String("skin inconnu"));
	Mutate([&](const NodePtr &root) { SetJsonPath(root, "rig.skins", Without(json::At(root, "rig.skins"), index - offset)); });
	return Ok(true);
}

// ── Contrôles ────────────────────────────────────────────────────────────────

std::vector<EntityDiagnostic> EntityDocument::Validate(const voxel::ContentRegistry &live, const std::vector<String> &scriptBehaviors) const {
	std::vector<EntityDiagnostic> out;
	auto warn = [&](const String &message, const char *field) {
		out.push_back(EntityDiagnostic{EntityDiagnostic::Severity::Warning, message, String(field)});
	};
	voxel::ContentRegistry scratch = live; // le contenu vivant n'est jamais touché
	scratch.ForgetLoadWarnings(m_id);
	Result<String, String> loaded = scratch.Load(m_json, Context());
	if (loaded.IsError()) {
		out.push_back(EntityDiagnostic{EntityDiagnostic::Severity::Error, loaded.Error(), String()});
		return out;
	}
	scratch.Finalize();
	const voxel::EntityDef *def = scratch.FindEntity(loaded.Value());
	if (!def) {
		out.push_back(EntityDiagnostic{EntityDiagnostic::Severity::Error, String("ce n'est pas une entité (« type »)"), String("type")});
		return out;
	}
	for (const String &warning : scratch.Validate())
		if (warning.StartsWith(def->id + String(" :")))
			warn(warning, "");
	// Physique.
	const math::FVector3 c = def->colliderSize;
	if (c.x <= 0.f || c.y <= 0.f || c.z <= 0.f || c.x > 16.f || c.y > 16.f || c.z > 16.f)
		warn(String::Format("boîte de collision %.2f × %.2f × %.2f : chaque côté entre 0 et 16 blocs", double(c.x), double(c.y), double(c.z)),
			 "physics.collider");
	if (def->health <= 0.f)
		warn(String("points de vie ≤ 0 : l'entité meurt en apparaissant"), "health");
	if (def->mass <= 0.f)
		warn(String("masse ≤ 0"), "physics.mass");
	if (def->maxSpeed < 0.f)
		warn(String("vitesse maximale négative"), "physics.maxSpeed");
	if (def->bounciness < 0.f || def->bounciness > 1.f)
		warn(String("rebond hors de 0..1"), "physics.bounciness");
	// Références.
	if (def->rig.IsSet() && !scratch.FindModel(def->rig.model))
		warn(String("modèle inconnu : ") + def->rig.model, "rig.model");
	for (const String &skin : def->rig.skins)
		if (!scratch.FindTexture(skin) && !voxel::ContentRegistry::IsTextureExpression(skin))
			warn(String("skin inconnu : ") + skin, "rig.skins");
	for (const auto &[event, sound] : def->sounds.events)
		if (!sound.IsEmpty() && !scratch.FindSound(sound))
			warn(String::Format("son « %s » inconnu : %s", event.c_str(), sound.CStr()), "sounds");
	if (!def->sounds.group.IsEmpty() && !scratch.FindSoundSet(def->sounds.group))
		warn(String("jeu de sons inconnu : ") + def->sounds.group, "sounds");
	for (const auto &[item, count] : def->drops)
		if (!scratch.FindItem(item))
			warn(String("butin inconnu : ") + item, "drops");
	// IA : intégrés, définis par le contenu, déclarés par un script.
	const std::vector<String> builtin = voxel::ContentRegistry::BuiltinBehaviors();
	for (const voxel::BehaviorRef &b : def->behaviors) {
		const bool known = std::find(builtin.begin(), builtin.end(), b.id) != builtin.end() || scratch.FindBehavior(b.id) ||
						   std::find(scriptBehaviors.begin(), scriptBehaviors.end(), b.id) != scriptBehaviors.end();
		const bool told = std::any_of(out.begin(), out.end(), [&](const EntityDiagnostic &d) { return d.message.Contains(b.id); });
		if (!known && !told) // le contrôle du contenu le dit déjà parfois
			warn(String("comportement inconnu : ") + b.id, "behaviors");
	}
	// Apparition.
	if (const NodePtr spawn = Field("spawn"); spawn && spawn->IsObject()) {
		const double chance = json::Num(spawn, "chance", 0.0);
		if (chance < 0.0 || chance > 1.0)
			warn(String::Format("chance d'apparition %.3f hors de 0..1", chance), "spawn.chance");
		if (json::Int(spawn, "max", 1) < 0)
			warn(String("nombre maximal négatif"), "spawn.max");
		for (const String &biome : json::StrList(spawn, "biomes"))
			if (!scratch.FindBiome(voxel::ContentRegistry::Qualify(biome, m_mod)))
				warn(String("biome d'apparition inconnu : ") + biome, "spawn.biomes");
		for (const String &block : json::StrList(spawn, "onBlocks"))
			if (!scratch.FindBlock(voxel::ContentRegistry::Qualify(block, m_mod)))
				warn(String("bloc d'apparition inconnu : ") + block, "spawn.onBlocks");
	}
	return out;
}

std::vector<std::pair<String, String>> EntityDocument::Components(const voxel::ContentRegistry &live) const {
	std::vector<std::pair<String, String>> out;
	voxel::ContentRegistry scratch = live;
	Result<String, String> loaded = scratch.Load(m_json, Context());
	if (loaded.IsError())
		return out;
	const voxel::EntityDef *def = scratch.FindEntity(loaded.Value());
	if (!def)
		return out;
	out.push_back({String("Transform"), String("position, orientation")});
	out.push_back({String("Velocity"), String()});
	out.push_back({String("Collider"), String::Format("%.2f × %.2f × %.2f%s%s", double(def->colliderSize.x), double(def->colliderSize.y),
													 double(def->colliderSize.z), def->pushable ? ", poussable" : "",
													 def->collidesWithEntities ? ", heurte les entités" : "")});
	out.push_back({String("PhysicsBody"), String::Format("masse %.1f, vitesse %.1f, saut %.1f%s%s", double(def->mass), double(def->maxSpeed),
														 double(def->jumpSpeed), def->flies ? ", vole" : "", def->swims ? ", nage" : "")});
	out.push_back({String("MoveIntent"), String::Format("vitesse %.1f", double(def->maxSpeed))});
	out.push_back({String("EntityKind"), def->kind});
	out.push_back({String("Health"), String::Format("%.0f PV", double(def->health))});
	String visual = def->rig.IsSet() ? String("modèle ") + def->rig.model + String::Format(" (%zu skin(s))", def->rig.skins.size())
									 : String("forme ") + json::Str(m_json, "shape", "cube");
	if (def->look.light.Active())
		visual += String::Format(", lumière %.1f / %.0f blocs", double(def->look.light.intensity), double(def->look.light.range));
	out.push_back({String("Visual"), visual});
	out.push_back({String("Properties"), String("JSON de la définition")});
	String sounds;
	for (const auto &[event, sound] : def->sounds.events)
		sounds += (sounds.IsEmpty() ? String() : String(", ")) + String(event.c_str());
	out.push_back({String("SoundEmitter"), sounds.IsEmpty() ? String("aucun son") : sounds});
	String behaviors;
	for (const voxel::BehaviorRef &b : def->behaviors)
		behaviors += (behaviors.IsEmpty() ? String() : String(" → ")) + b.id;
	out.push_back({String("Behaviors"), behaviors.IsEmpty() ? String("aucun") : behaviors});
	if (def->persistent)
		out.push_back({String("Persistent"), String("enregistrée avec le monde")});
	return out;
}

} // namespace voxelia::studio
