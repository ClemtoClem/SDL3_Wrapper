// Définitions de voxelia/studio/outliner.hpp
#include <voxelia/studio/outliner.hpp>

#include <algorithm>
#include <cmath>

#include <voxel/components.hpp>

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
		s.rows.push_back({"Position", Vec(t.Unwrap()->position)});
		s.rows.push_back({"Bloc", voxel::BlockPos::FromWorld(t.Unwrap()->position).ToString()});
		s.rows.push_back({"Chunk", voxel::ChunkOf(voxel::BlockPos::FromWorld(t.Unwrap()->position)).ToString()});
		s.rows.push_back({"Cap (yaw)", Deg(t.Unwrap()->yaw)});
		s.rows.push_back({"Tangage (pitch)", Deg(t.Unwrap()->pitch)});
		if (auto v = reg.GetComponent<voxel::Velocity>(e); v.IsSome())
			s.rows.push_back({"Vitesse", Vec(v.Unwrap()->linear)});
		if (auto c = reg.GetComponent<voxel::Collider>(e); c.IsSome())
			s.rows.push_back({"Collision (l×h×p)", Vec(c.Unwrap()->size)});
		out.push_back(std::move(s));
	}
	if (auto b = reg.GetComponent<voxel::PhysicsBody>(e); b.IsSome()) {
		const voxel::PhysicsBody &p = *b.Unwrap();
		InspectorSection s{"Physique", {}};
		s.rows.push_back({"Masse", Num(p.mass)});
		s.rows.push_back({"Gravité", Num(p.gravityScale * p.gravityDirection)});
		s.rows.push_back({"Frottement", Num(p.friction)});
		s.rows.push_back({"Rebond", Num(p.bounciness)});
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
		s.rows.push_back({"PV", String::Format("%.1f / %.1f", double(h.Unwrap()->current), double(h.Unwrap()->max))});
		s.rows.push_back({"Mort", Bool(h.Unwrap()->IsDead())});
		if (!h.Unwrap()->lastCause.IsEmpty())
			s.rows.push_back({"Dernière cause", h.Unwrap()->lastCause});
		out.push_back(std::move(s));
	}
	if (auto sv = reg.GetComponent<voxel::Survival>(e); sv.IsSome()) {
		const voxel::Survival &v = *sv.Unwrap();
		InspectorSection s{"Survie", {}};
		s.rows.push_back({"Faim", String::Format("%.1f / 20", double(v.hunger))});
		s.rows.push_back({"Saturation", Num(v.saturation)});
		s.rows.push_back({"Endurance", Num(v.stamina)});
		s.rows.push_back({"Air", String::Format("%.1f / %.1f s", double(v.air), double(v.maxAir))});
		s.rows.push_back({"Suffoque", Bool(v.suffocating)});
		s.rows.push_back({"Expérience", String::Format("%d", v.xp)});
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
		out.push_back(std::move(s));
	}
	if (auto c = reg.GetComponent<voxel::Carrier>(e); c.IsSome()) {
		InspectorSection s{"Plateforme mobile", {}};
		s.rows.push_back({"Plancher", Num(c.Unwrap()->floor)});
		out.push_back(std::move(s));
	}
	if (auto v = reg.GetComponent<voxel::Visual>(e); v.IsSome()) {
		InspectorSection s{"Visuel", {}};
		s.rows.push_back({"Visible", Bool(v.Unwrap()->visible)});
		s.rows.push_back({"Échelle", Num(v.Unwrap()->scale)});
		if (!v.Unwrap()->skin.IsEmpty())
			s.rows.push_back({"Skin", v.Unwrap()->skin});
		s.rows.push_back({"Assemblage de blocs", Bool(v.Unwrap()->structure != nullptr)});
		out.push_back(std::move(s));
	}
	return out;
}

} // namespace voxelia::studio
