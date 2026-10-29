#pragma once
/**
 * Relations entre entités.
 *
 *  - Hiérarchie : `ChildOf` (sur l'enfant) et `Children` (sur le parent),
 *    tenus cohérents par SetParent / RemoveParent ; DespawnRecursive détruit
 *    une entité et toute sa descendance (et émet EntityDespawned).
 *  - Relations typées quelconques : `Relation<Tag>{cible}` (« possédé par »,
 *    « cible de », « monté sur »…) ; `RelatedTo<Tag>(world, cible)` liste
 *    les entités qui pointent vers une cible.
 *
 * Les relations vers une entité morte sont nettoyées par DespawnRecursive
 * (hiérarchie) ; pour les relations typées, `PruneRelations<Tag>` retire
 * celles dont la cible n'existe plus.
 */
#include <algorithm>
#include <vector>

#include "ecs.hpp"
#include "events.hpp"

namespace ecs {

struct ChildOf {
	Entity parent;
};
struct Children {
	std::vector<Entity> entities;
};

template <typename Tag> struct Relation {
	Entity target;
};

[[nodiscard]] inline Option<Entity> ParentOf(const ArchetypeRegistry &world, Entity e) {
	if (auto c = world.GetComponent<ChildOf>(e); c.IsSome())
		return Some(c.Unwrap()->parent);
	return NONE;
}

[[nodiscard]] inline std::vector<Entity> ChildrenOf(const ArchetypeRegistry &world, Entity e) {
	if (auto c = world.GetComponent<Children>(e); c.IsSome())
		return c.Unwrap()->entities;
	return {};
}

inline void RemoveParent(ArchetypeRegistry &world, Entity child) {
	Option<Entity> parent = ParentOf(world, child);
	if (parent.IsNone())
		return;
	if (auto children = world.GetComponent<Children>(parent.Unwrap()); children.IsSome())
		std::erase(children.Unwrap()->entities, child);
	world.RemoveComponent<ChildOf>(child);
}

/// Faux si cela créerait un cycle (parent descendant de l'enfant).
inline bool SetParent(ArchetypeRegistry &world, Entity child, Entity parent) {
	if (child == parent || !world.IsAlive(child) || !world.IsAlive(parent))
		return false;
	for (Option<Entity> up = Some(parent); up.IsSome(); up = ParentOf(world, up.Unwrap()))
		if (up.Unwrap() == child)
			return false;
	RemoveParent(world, child);
	world.AddComponent(child, ChildOf{parent});
	if (auto children = world.GetComponent<Children>(parent); children.IsSome())
		children.Unwrap()->entities.push_back(child);
	else
		world.AddComponent(parent, Children{{child}});
	return true;
}

/// Détruit `e` et tous ses descendants ; rend le nombre d'entités détruites.
inline size_t DespawnRecursive(ArchetypeRegistry &world, Entity e) {
	if (!world.IsAlive(e))
		return 0;
	size_t count = 0;
	for (Entity child : ChildrenOf(world, e))
		count += DespawnRecursive(world, child);
	RemoveParent(world, e);
	EventsOf<EntityDespawned>(world).Send(EntityDespawned{e});
	if (world.Despawn(e))
		++count;
	return count;
}

/// Entités dont la relation `Tag` vise `target`.
template <typename Tag> [[nodiscard]] std::vector<Entity> RelatedTo(ArchetypeRegistry &world, Entity target) {
	std::vector<Entity> out;
	world.Query<Relation<Tag>>([&](Entity e, Relation<Tag> &r) {
		if (r.target == target)
			out.push_back(e);
	});
	return out;
}

/// Retire les relations `Tag` dont la cible est morte ; rend leur nombre.
template <typename Tag> size_t PruneRelations(ArchetypeRegistry &world) {
	std::vector<Entity> dangling;
	world.Query<Relation<Tag>>([&](Entity e, Relation<Tag> &r) {
		if (!world.IsAlive(r.target))
			dangling.push_back(e);
	});
	for (Entity e : dangling)
		world.RemoveComponent<Relation<Tag>>(e);
	return dangling.size();
}

} // namespace ecs
