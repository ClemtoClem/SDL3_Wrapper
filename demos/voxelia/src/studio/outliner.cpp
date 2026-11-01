// Définitions de voxelia/studio/outliner.hpp
#include <voxelia/studio/outliner.hpp>

#include <algorithm>
#include <cmath>
#include <cstdlib>

#include <voxel/components.hpp>
#include <voxel/inventory.hpp>

namespace voxelia::studio {

namespace {

String Vec(const math::FVector3 &v) { return String::Format("%.2f, %.2f, %.2f", double(v.x), double(v.y), double(v.z)); }
String Num(double v) { return String::Format("%.2f", v); }
String Bool(bool v) { return String(v ? "oui" : "non"); }
String Deg(float radians) { return String::Format("%.1f°", double(radians) * 180.0 / 3.14159265358979323846); }

float Distance(const math::FVector3 &a, const math::FVector3 &b) {
	const float dx = a.x - b.x, dy = a.y - b.y, dz = a.z - b.z;
	return std::sqrt(dx * dx + dy * dy + dz * dz);
}

EntityCategory CategoryOf(voxel::Game &game, ecs::Entity e, const voxel::EntityKind *kind) {
	ecs::ArchetypeRegistry &reg = game.Registry();
	if (reg.HasComponent<voxel::Player>(e))
		return EntityCategory::Player;
	if (reg.HasComponent<voxel::Carrier>(e))
		return EntityCategory::Carrier;
	if (reg.HasComponent<voxel::DroppedItem>(e) || (kind && kind->id == "voxel:item"))
		return EntityCategory::Item;
	if (reg.HasComponent<voxel::FallingBlock>(e) || (kind && kind->id == "voxel:falling_block"))
		return EntityCategory::FallingBlock;
	if (reg.HasComponent<voxel::Health>(e) || reg.HasComponent<voxel::Behaviors>(e))
		return EntityCategory::Creature;
	return EntityCategory::Other;
}

String NameOf(voxel::Game &game, ecs::Entity e, const voxel::EntityKind *kind) {
	ecs::ArchetypeRegistry &reg = game.Registry();
	if (auto named = reg.GetComponent<voxel::Named>(e); named.IsSome() && !named.Unwrap()->name.IsEmpty())
		return named.Unwrap()->name;
	if (auto player = reg.GetComponent<voxel::Player>(e); player.IsSome())
		return player.Unwrap()->name;
	if (auto item = reg.GetComponent<voxel::DroppedItem>(e); item.IsSome())
		return item.Unwrap()->stack.item;
	if (kind && kind->def && !kind->def->displayName.IsEmpty())
		return kind->def->displayName;
	return kind ? kind->id : String::Format("entité %u", unsigned(e.id));
}

bool Contains(const String &haystack, const String &needle) {
	return needle.IsEmpty() || haystack.ToLower().Find(needle.ToLower()) != String::NPOS;
}

} // namespace

const char *EntityCategoryId(EntityCategory category) noexcept {
	switch (category) {
	case EntityCategory::Player:
		return "player";
	case EntityCategory::Creature:
		return "creature";
	case EntityCategory::Item:
		return "item";
	case EntityCategory::FallingBlock:
		return "falling_block";
	case EntityCategory::Carrier:
		return "carrier";
	case EntityCategory::Other:
		return "other";
	}
	return "other";
}

const char *EntityCategoryLabel(EntityCategory category) noexcept {
	switch (category) {
	case EntityCategory::Player:
		return "Joueurs";
	case EntityCategory::Creature:
		return "Créatures";
	case EntityCategory::Item:
		return "Objets au sol";
	case EntityCategory::FallingBlock:
		return "Blocs qui tombent";
	case EntityCategory::Carrier:
		return "Plateformes mobiles";
	case EntityCategory::Other:
		return "Autres";
	}
	return "Autres";
}

std::vector<OutlinerEntry> CollectEntities(voxel::Game &game, const OutlinerFilter &filter, const math::FVector3 &from) {
	std::vector<OutlinerEntry> out;
	if (!game.HasWorld())
		return out;
	const String dimension = game.CurrentDimension();
	game.Registry().Query<voxel::Transform>([&](ecs::Entity e, voxel::Transform &t) {
		auto kindRef = game.Registry().GetComponent<voxel::EntityKind>(e);
		const voxel::EntityKind *kind = kindRef.IsSome() ? kindRef.Unwrap().Get() : nullptr;
		OutlinerEntry entry;
		entry.entity = e;
		entry.id = kind ? kind->id : String();
		entry.category = CategoryOf(game, e, kind);
		entry.name = NameOf(game, e, kind);
		entry.dimension = dimension;
		entry.position = t.position;
		entry.chunk = voxel::ChunkOf(voxel::BlockPos::FromWorld(t.position));
		entry.distance = Distance(t.position, from);
		if (auto health = game.Registry().GetComponent<voxel::Health>(e); health.IsSome())
			entry.dead = health.Unwrap()->IsDead();
		if (filter.category.IsSome() && filter.category.Value() != entry.category)
			return;
		if (filter.maxDistance > 0.f && entry.distance > filter.maxDistance)
			return;
		if (filter.aliveOnly && entry.dead)
			return;
		if (!Contains(entry.name, filter.text) && !Contains(entry.id, filter.text))
			return;
		out.push_back(std::move(entry));
	});
	std::sort(out.begin(), out.end(), [](const OutlinerEntry &a, const OutlinerEntry &b) {
		if (a.category != b.category)
			return a.category < b.category;
		if (!(a.chunk == b.chunk))
			return a.chunk < b.chunk;
		if (a.distance != b.distance)
			return a.distance < b.distance;
		return a.entity.id < b.entity.id;
	});
	return out;
}

std::vector<InspectorSection> InspectEntity(voxel::Game &game, ecs::Entity e) {
	std::vector<InspectorSection> out;
	ecs::ArchetypeRegistry &reg = game.Registry();
	if (!e.Valid() || !reg.IsAlive(e))
		return out;

	InspectorSection identity{"Identité", {}};
	identity.rows.push_back({"Entité", String::Format("%u", unsigned(e.id))});
	if (auto kind = reg.GetComponent<voxel::EntityKind>(e); kind.IsSome()) {
		identity.rows.push_back({"Id", kind.Unwrap()->id});
		if (!kind.Unwrap()->kind.IsEmpty())
			identity.rows.push_back({"Genre", kind.Unwrap()->kind});
		if (kind.Unwrap()->def)
			identity.rows.push_back({"Mod", kind.Unwrap()->def->mod});
	}
	identity.rows.push_back({"Dimension", game.CurrentDimension()});
	out.push_back(std::move(identity));

	if (auto t = reg.GetComponent<voxel::Transform>(e); t.IsSome()) {
		InspectorSection s{"Transform", {}};
		s.rows.push_back({"Position", Vec(t.Unwrap()->position), "position"});
		s.rows.push_back({"Bloc", voxel::BlockPos::FromWorld(t.Unwrap()->position).ToString()});
		s.rows.push_back({"Chunk", voxel::ChunkOf(voxel::BlockPos::FromWorld(t.Unwrap()->position)).ToString()});
		s.rows.push_back({"Cap (yaw)", Deg(t.Unwrap()->yaw), "yaw"});
		s.rows.push_back({"Tangage (pitch)", Deg(t.Unwrap()->pitch), "pitch"});
		if (auto v = reg.GetComponent<voxel::Velocity>(e); v.IsSome())
			s.rows.push_back({"Vitesse", Vec(v.Unwrap()->linear), "velocity"});
		if (auto c = reg.GetComponent<voxel::Collider>(e); c.IsSome())
			s.rows.push_back({"Collision (l×h×p)", Vec(c.Unwrap()->size)});
		out.push_back(std::move(s));
	}
	if (auto b = reg.GetComponent<voxel::PhysicsBody>(e); b.IsSome()) {
		const voxel::PhysicsBody &p = *b.Unwrap();
		InspectorSection s{"Physique", {}};
		s.rows.push_back({"Masse", Num(p.mass), "mass"});
		s.rows.push_back({"Gravité", Num(p.gravityScale * p.gravityDirection), "gravity"});
		s.rows.push_back({"Vitesse max", Num(p.maxSpeed), "maxspeed"});
		s.rows.push_back({"Vole", Bool(p.flies), "flies"});
		s.rows.push_back({"Traverse les blocs", Bool(p.noclip), "noclip"});
		s.rows.push_back({"Frottement", Num(p.friction), "friction"});
		s.rows.push_back({"Rebond", Num(p.bounciness), "bounciness"});
		s.rows.push_back({"Au sol", Bool(p.onGround)});
		s.rows.push_back({"Dans un liquide", Bool(p.inFluid)});
		if (p.inFluid) {
			s.rows.push_back({"Viscosité", Num(p.fluidViscosity)});
			s.rows.push_back({"Courant", Vec(p.current)});
		}
		s.rows.push_back({"Sables mouvants", Bool(p.inSinking)});
		s.rows.push_back({"Coincé", Bool(p.trapped)});
		s.rows.push_back({"Chute", Num(p.fallDistance)});
		out.push_back(std::move(s));
	}
	if (auto h = reg.GetComponent<voxel::Health>(e); h.IsSome()) {
		InspectorSection s{"Santé", {}};
		s.rows.push_back({"PV", String::Format("%.1f / %.1f", double(h.Unwrap()->current), double(h.Unwrap()->max)), "health"});
		s.rows.push_back({"PV max", Num(h.Unwrap()->max), "maxhealth"});
		s.rows.push_back({"Mort", Bool(h.Unwrap()->IsDead())});
		if (!h.Unwrap()->lastCause.IsEmpty())
			s.rows.push_back({"Dernière cause", h.Unwrap()->lastCause});
		out.push_back(std::move(s));
	}
	if (auto sv = reg.GetComponent<voxel::Survival>(e); sv.IsSome()) {
		const voxel::Survival &v = *sv.Unwrap();
		InspectorSection s{"Survie", {}};
		s.rows.push_back({"Faim", String::Format("%.1f / 20", double(v.hunger)), "hunger"});
		s.rows.push_back({"Saturation", Num(v.saturation), "saturation"});
		s.rows.push_back({"Endurance", Num(v.stamina), "stamina"});
		s.rows.push_back({"Air", String::Format("%.1f / %.1f s", double(v.air), double(v.maxAir)), "air"});
		s.rows.push_back({"Suffoque", Bool(v.suffocating)});
		s.rows.push_back({"Expérience", String::Format("%d", v.xp), "xp"});
		out.push_back(std::move(s));
	}
	if (auto fx = reg.GetComponent<voxel::StatusEffects>(e); fx.IsSome() && !fx.Unwrap()->list.empty()) {
		InspectorSection s{"Effets", {}};
		for (const voxel::StatusEffect &effect : fx.Unwrap()->list)
			s.rows.push_back({effect.id, String::Format("niveau %d, %.1f s", effect.level, double(effect.remaining))});
		out.push_back(std::move(s));
	}
	if (auto ai = reg.GetComponent<voxel::Behaviors>(e); ai.IsSome() && !ai.Unwrap()->list.empty()) {
		InspectorSection s{"IA", {}};
		for (const voxel::BehaviorState &state : ai.Unwrap()->list)
			s.rows.push_back({state.id, state.hasTarget ? String("cible ") + Vec(state.target)
														: String::Format("sans cible (%.1f s)", double(state.timer))});
		const auto targeted = std::find_if(ai.Unwrap()->list.begin(), ai.Unwrap()->list.end(),
										   [](const voxel::BehaviorState &st) { return st.hasTarget; });
		s.rows.push_back({"Cible forcée", targeted == ai.Unwrap()->list.end() ? String("none") : Vec(targeted->target), "target"});
		out.push_back(std::move(s));
	}
	if (auto c = reg.GetComponent<voxel::Carrier>(e); c.IsSome()) {
		InspectorSection s{"Plateforme mobile", {}};
		s.rows.push_back({"Plancher", Num(c.Unwrap()->floor)});
		out.push_back(std::move(s));
	}
	if (auto v = reg.GetComponent<voxel::Visual>(e); v.IsSome()) {
		InspectorSection s{"Visuel", {}};
		s.rows.push_back({"Visible", Bool(v.Unwrap()->visible), "visible"});
		s.rows.push_back({"Échelle", Num(v.Unwrap()->scale), "scale"});
		if (!v.Unwrap()->skin.IsEmpty())
			s.rows.push_back({"Skin", v.Unwrap()->skin});
		s.rows.push_back({"Assemblage de blocs", Bool(v.Unwrap()->structure != nullptr)});
		out.push_back(std::move(s));
	}
	return out;
}

// ── Édition à la volée ───────────────────────────────────────────────────────

std::vector<float> ParseNumbers(const String &text) {
	std::vector<float> out;
	const char *p = text.CStr();
	while (*p) {
		const bool start = (*p >= '0' && *p <= '9') || ((*p == '-' || *p == '+' || *p == '.') && p[1] && ((p[1] >= '0' && p[1] <= '9') || p[1] == '.'));
		if (!start) {
			++p;
			continue;
		}
		char *end = nullptr;
		const float v = std::strtof(p, &end);
		if (end == p) {
			++p;
			continue;
		}
		out.push_back(v);
		p = end;
	}
	return out;
}

namespace {

constexpr float DEG = 3.14159265358979323846f / 180.f;

Option<bool> ParseBool(const String &text) {
	const String t = text.Trim().ToLower();
	if (t == "oui" || t == "on" || t == "true" || t == "1" || t == "yes" || t == "vrai")
		return Some(true);
	if (t == "non" || t == "off" || t == "false" || t == "0" || t == "no" || t == "faux")
		return Some(false);
	return NONE;
}

} // namespace

Result<String, String> ApplyEntityEdit(voxel::Game &game, ecs::Entity e, const String &field, const String &value) {
	ecs::ArchetypeRegistry &reg = game.Registry();
	if (!e.Valid() || !reg.IsAlive(e))
		return Err(String("entité introuvable"));
	const std::vector<float> n = ParseNumbers(value);
	auto need = [&](size_t count) -> Result<bool, String> {
		if (n.size() < count)
			return Err(String::Format("%s : %zu nombre(s) attendu(s)", field.CStr(), count));
		return Ok(true);
	};
	auto missing = [&](const char *component) { return Err(String::Format("%s : l'entité n'a pas de %s", field.CStr(), component)); };

	if (field == "position" || field == "yaw" || field == "pitch") {
		auto t = reg.GetComponent<voxel::Transform>(e);
		if (t.IsNone())
			return missing("Transform");
		if (field == "position") {
			if (auto r = need(3); r.IsError())
				return Err(r.Error());
			t.Unwrap()->position = t.Unwrap()->previous = {n[0], n[1], n[2]}; // pas d'interpolation depuis l'ancienne place
			if (auto v = reg.GetComponent<voxel::Velocity>(e); v.IsSome())
				v.Unwrap()->linear = {0.f, 0.f, 0.f};
			return Ok(String::Format("position = %.2f, %.2f, %.2f", double(n[0]), double(n[1]), double(n[2])));
		}
		if (auto r = need(1); r.IsError())
			return Err(r.Error());
		(field == "yaw" ? t.Unwrap()->yaw : t.Unwrap()->pitch) = n[0] * DEG;
		return Ok(String::Format("%s = %.1f°", field.CStr(), double(n[0])));
	}
	if (field == "velocity") {
		auto v = reg.GetComponent<voxel::Velocity>(e);
		if (v.IsNone())
			return missing("Velocity");
		if (auto r = need(3); r.IsError())
			return Err(r.Error());
		v.Unwrap()->linear = {n[0], n[1], n[2]};
		return Ok(String("vitesse modifiée"));
	}
	if (field == "health" || field == "maxhealth") {
		auto h = reg.GetComponent<voxel::Health>(e);
		if (h.IsNone())
			return missing("Health");
		if (auto r = need(1); r.IsError())
			return Err(r.Error());
		if (field == "maxhealth") {
			h.Unwrap()->max = std::max(0.5f, n[0]);
			h.Unwrap()->current = std::min(h.Unwrap()->current, h.Unwrap()->max);
		} else {
			h.Unwrap()->current = std::clamp(n[0], 0.f, h.Unwrap()->max);
			if (h.Unwrap()->current <= 0.f)
				h.Unwrap()->lastCause = String("studio");
		}
		return Ok(String::Format("PV %.1f / %.1f", double(h.Unwrap()->current), double(h.Unwrap()->max)));
	}
	if (field == "hunger" || field == "saturation" || field == "stamina" || field == "air" || field == "xp") {
		auto sv = reg.GetComponent<voxel::Survival>(e);
		if (sv.IsNone())
			return missing("Survival");
		if (auto r = need(1); r.IsError())
			return Err(r.Error());
		voxel::Survival &v = *sv.Unwrap();
		if (field == "hunger")
			v.hunger = std::clamp(n[0], 0.f, 20.f);
		else if (field == "saturation")
			v.saturation = std::clamp(n[0], 0.f, 20.f);
		else if (field == "stamina")
			v.stamina = std::clamp(n[0], 0.f, 1.f);
		else if (field == "air")
			v.air = std::clamp(n[0], 0.f, v.maxAir);
		else
			v.xp = std::max(0, int(n[0]));
		return Ok(field + String(" modifié"));
	}
	if (field == "gravity" || field == "mass" || field == "friction" || field == "bounciness" || field == "maxspeed" ||
		field == "flies" || field == "noclip") {
		auto b = reg.GetComponent<voxel::PhysicsBody>(e);
		if (b.IsNone())
			return missing("PhysicsBody");
		voxel::PhysicsBody &p = *b.Unwrap();
		if (field == "flies" || field == "noclip") {
			const Option<bool> on = ParseBool(value);
			if (on.IsNone())
				return Err(field + String(" : oui ou non"));
			(field == "flies" ? p.flies : p.noclip) = on.Value();
			return Ok(field + String(on.Value() ? " : oui" : " : non"));
		}
		if (auto r = need(1); r.IsError())
			return Err(r.Error());
		if (field == "gravity") {
			p.gravityScale = std::fabs(n[0]);
			p.gravityDirection = n[0] < 0.f ? -1.f : 1.f;
		} else if (field == "mass")
			p.mass = std::max(0.01f, n[0]);
		else if (field == "friction")
			p.friction = std::clamp(n[0], 0.f, 1.f);
		else if (field == "bounciness")
			p.bounciness = std::clamp(n[0], 0.f, 1.f);
		else
			p.maxSpeed = std::max(0.f, n[0]);
		return Ok(field + String(" modifié"));
	}
	if (field == "scale" || field == "visible") {
		auto v = reg.GetComponent<voxel::Visual>(e);
		if (v.IsNone())
			return missing("Visual");
		if (field == "visible") {
			const Option<bool> on = ParseBool(value);
			if (on.IsNone())
				return Err(String("visible : oui ou non"));
			v.Unwrap()->visible = on.Value();
			return Ok(String(on.Value() ? "visible" : "cachée"));
		}
		if (auto r = need(1); r.IsError())
			return Err(r.Error());
		v.Unwrap()->scale = std::clamp(n[0], 0.05f, 50.f);
		return Ok(String::Format("échelle %.2f", double(v.Unwrap()->scale)));
	}
	if (field == "target") {
		auto ai = reg.GetComponent<voxel::Behaviors>(e);
		if (ai.IsNone() || ai.Unwrap()->list.empty())
			return missing("comportement");
		const bool none = value.Trim().ToLower() == "none" || value.Trim().ToLower() == "aucune";
		if (!none)
			if (auto r = need(3); r.IsError())
				return Err(r.Error());
		for (voxel::BehaviorState &state : ai.Unwrap()->list) {
			state.hasTarget = !none;
			if (!none)
				state.target = {n[0], n[1], n[2]};
		}
		return Ok(String(none ? "cible retirée" : "cible forcée"));
	}
	if (field == "effect") {
		const std::vector<String> words = value.Trim().Split(' ');
		if (words.empty() || words[0].IsEmpty() || n.empty())
			return Err(String("effect <id> <secondes> [niveau]"));
		game.AddEffect(e, words[0], n[0], n.size() > 1 ? std::max(1, int(n[1])) : 1);
		return Ok(String("effet ") + words[0]);
	}
	return Err(String("champ inconnu : ") + field);
}

std::vector<InspectorSection> InspectBlock(voxel::Game &game, const voxel::BlockPos &pos) {
	std::vector<InspectorSection> out;
	const voxel::World &world = game.GetWorld();
	const voxel::BlockDef &def = world.BlockAt(pos);
	InspectorSection s{"Bloc", {}};
	s.rows.push_back({"Position", pos.ToString()});
	s.rows.push_back({"Chunk", voxel::ChunkOf(pos).ToString()});
	s.rows.push_back({"Id", def.id});
	if (!def.displayName.IsEmpty())
		s.rows.push_back({"Nom", def.displayName});
	if (!def.mod.IsEmpty())
		s.rows.push_back({"Mod", def.mod});
	s.rows.push_back({"Paramètre", String::Format("%u", unsigned(world.GetParam(pos)))});
	s.rows.push_back({"Solide", Bool(def.solid)});
	s.rows.push_back({"Liquide", Bool(def.fluid)});
	if (!def.behavior.IsEmpty())
		s.rows.push_back({"Comportement", def.behavior});
	out.push_back(std::move(s));
	const voxel::BlockPos main = game.MainPart(pos);
	const voxel::BlockDef &mainDef = world.BlockAt(main);
	if (!mainDef.stateGroup.IsEmpty() || !mainDef.toggle.IsEmpty() || !mainDef.use.IsEmpty()) {
		InspectorSection i{"Interaction", {}};
		if (!(main == pos))
			i.rows.push_back({"Bloc principal", main.ToString()});
		if (!mainDef.stateGroup.IsEmpty()) {
			i.rows.push_back({"État", mainDef.stateName, "state"});
			String all;
			for (const auto &[state, id] : mainDef.stateIds)
				all += (all.IsEmpty() ? String() : String(", ")) + state;
			i.rows.push_back({"États", all});
		}
		if (!mainDef.toggle.IsEmpty())
			i.rows.push_back({"Bascule vers", mainDef.toggle});
		if (!mainDef.use.IsEmpty())
			i.rows.push_back({"Action", mainDef.use});
		out.push_back(std::move(i));
	}
	if (voxel::Inventory *inventory = game.BlockInventory(main)) {
		InspectorSection c{"Rangement", {}};
		int used = 0;
		for (int slot = 0; slot < inventory->Size(); ++slot) {
			const voxel::ItemStack &stack = inventory->Slot(slot);
			if (stack.IsEmpty())
				continue;
			++used;
			c.rows.push_back({String::Format("Case %d", slot), String::Format("%s × %d", stack.item.CStr(), stack.count)});
		}
		c.rows.insert(c.rows.begin(), {String("Cases"), String::Format("%d / %d occupées", used, inventory->Size())});
		out.push_back(std::move(c));
	}
	return out;
}

Result<String, String> ApplyBlockEdit(voxel::Game &game, const voxel::BlockPos &pos, const String &field, const String &value) {
	const voxel::BlockPos main = game.MainPart(pos);
	if (field == "state") {
		const String state = value.Trim();
		if (!game.SetBlockState(main, state))
			return Err(String::Format("%s n'a pas d'état « %s »", game.GetWorld().BlockAt(main).id.CStr(), state.CStr()));
		return Ok(String("état ") + state);
	}
	if (field == "use") {
		if (!game.UseBlock(game.LocalPlayer(), main))
			return Err(String("ce bloc n'a pas d'action"));
		return Ok(String("bloc utilisé"));
	}
	if (field == "toggle") {
		if (!game.ToggleBlock(main))
			return Err(String("ce bloc ne bascule pas"));
		return Ok(String("bloc basculé"));
	}
	return Err(String("champ inconnu : ") + field);
}

} // namespace voxelia::studio
