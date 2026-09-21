/**
 * @file ecs.hpp
 * @brief Implémentation d'un ECS (Entity-Component-System) basé sur les archétypes.
 *
 * @details
 * ## Vue d'ensemble
 *
 * Ce fichier implémente un ECS orienté archétypes, inspiré de Bevy/Unity DOTS.
 * L'idée centrale est de regrouper ensemble en mémoire toutes les entités qui
 * partagent exactement le même ensemble de types de composants (leur *signature*),
 * ce qui maximise la cohérence de cache lors des requêtes système.
 *
 * ## Architecture — concepts clés
 *
 * | Concept             | Rôle                                                              |
 * |---------------------|-------------------------------------------------------------------|
 * | `Entity`            | Identifiant léger (id + génération) sans données propres          |
 * | `EntityAllocator`   | Alloue / libère des entités via une freelist générationnelle      |
 * | `AllocEntry`        | Entrée interne de l'allocateur (génération, lien freelist)        |
 * | `IAnyVec`           | Interface type-erased pour un vecteur de composants homogène      |
 * | `AnyVecImpl<T>`     | Implémentation concrète de `IAnyVec` pour le type `T`             |
 * | `Archetype`         | Groupe d'entités partageant la même signature de composants       |
 * | `EntityLocation`    | Cache : archetype_id + row pour accès O(1) aux composants         |
 * | `ArchetypeRegistry` | Registre central — gère entités, archétypes et ressources         |
 * | `Resources`         | Stockage global de singletons typés (non liés à une entité)       |
 * | Filtres             | `All`, `With<T>`, `Without<T>`, `And<A,B,...>`, `Or<A,B,...>`     |
 *
 * ## Diagramme de relations
 *
 * @dot
 * digraph ecs_archetype {
 *   rankdir=TB;
 *   graph [fontname="Helvetica", fontsize=11, bgcolor="transparent", pad=0.4, splines=ortho];
 *   node  [fontname="Helvetica", fontsize=11, shape=record, style="filled,rounded",
 *          fillcolor="#f5f5f5", color="#888888", penwidth=1.2];
 *   edge  [fontname="Helvetica", fontsize=10, color="#666666", penwidth=1.0];
 *
 *   // ── Nœuds ──────────────────────────────────────────────────────────────
 *
 *   ArchetypeRegistry [
 *     fillcolor="#dce8f7", color="#3a7fc1",
 *     label="{ArchetypeRegistry|+ Spawn() : Entity\l+ SpawnBundle\<Ts...\>() : Entity\l+ Despawn(Entity) : bool\l+
 * add_component\<T\>(Entity, T)\l+ remove_component\<T\>(Entity)\l+ GetComponent\<T\>(Entity) : T*\l+
 * query\<Comps...\>(Fn)\l+ query_single\<Comps...\>()\l}",
 *   ];
 *
 *   EntityAllocator [
 *     fillcolor="#d6f0e4", color="#2e8b57",
 *     label="{EntityAllocator|+ allocate() : Entity\l+ free(Entity) : bool\l+ IsAlive(Entity) : bool\l+ AliveCount()
 * : uint32_t\l}",
 *   ];
 *
 *   AllocEntry [
 *     fillcolor="#edf7ed", color="#2e8b57",
 *     label="{AllocEntry|+ is_alive : bool\l+ generation : uint32_t\l+ next_free : uint32_t\l}",
 *   ];
 *
 *   Entity [
 *     fillcolor="#fff8dc", color="#b8860b",
 *     label="{Entity|+ id : uint32_t\l+ generation : uint32_t\l}",
 *   ];
 *
 *   EntityLocation [
 *     fillcolor="#fff8dc", color="#b8860b",
 *     label="{EntityLocation|+ archetype_id : size_t\l+ archetype_row : size_t\l}",
 *   ];
 *
 *   Archetype [
 *     fillcolor="#ece0f5", color="#6a3d9a",
 *     label="{Archetype|+ id : size_t\l+ types : vector\<type_index\>\l+ entities : vector\<Entity\>\l+ components :
 * map\<type_index, IAnyVec\>\l--|+ push_entity(Entity) : size_t\l+ swap_remove_entity(size_t)\l+ migrate_row_to(size_t,
 * Archetype\&)\l+ drop_row(size_t)\l}",
 *   ];
 *
 *   IAnyVec [
 *     fillcolor="#fde8d8", color="#c0522a",
 *     label="{«interface»\nIAnyVec|+ swap_remove_and_move_to(size_t, IAnyVec*)\l+ swap_remove_drop(size_t)\l+ len() :
 * size_t\l+ new_empty_of_same_type() : unique_ptr\l}",
 *   ];
 *
 *   AnyVecImpl [
 *     fillcolor="#fde8d8", color="#c0522a",
 *     label="{AnyVecImpl\<T\>|+ data : vector\<T\>}",
 *   ];
 *
 *   Resources [
 *     fillcolor="#f0f0f0", color="#555555",
 *     label="{Resources|+ insert\<R\>(R)\l+ get\<R\>() : R*\l+ get_or_throw\<R\>() : R\&\l+ contains\<R\>() : bool\l+
 * remove\<R\>() : bool\l}",
 *   ];
 *
 *   Filters [
 *     fillcolor="#f0f0f0", color="#555555",
 *     label="{Filtres|All | With\<T\> | Without\<T\>\lAnd\<A,B\> | Or\<A,B\>\l}",
 *   ];
 *
 *   // ── Relations ──────────────────────────────────────────────────────────
 *
 *   ArchetypeRegistry -> EntityAllocator [label=" possède", style=solid];
 *   ArchetypeRegistry -> Archetype       [label=" 0..*", style=solid];
 *   ArchetypeRegistry -> EntityLocation  [label=" Entity →", style=dashed];
 *   ArchetypeRegistry -> Resources       [label=" possède", style=solid];
 *
 *   EntityAllocator -> AllocEntry [label=" entries[ ]", style=solid];
 *   EntityAllocator -> Entity     [label=" produit", style=dashed, arrowhead=open];
 *
 *   EntityLocation -> Archetype [label=" pointe vers", style=dashed, arrowhead=open];
 *
 *   Archetype -> IAnyVec  [label=" components[ ]", style=solid];
 *   Archetype -> Entity   [label=" entities[ ]",   style=solid];
 *
 *   IAnyVec -> AnyVecImpl [label=" implémenté par", style=dashed, arrowhead=empty];
 *
 *   ArchetypeRegistry -> Filters [label=" filtre Query()", style=dashed, arrowhead=open];
 * }
 * @enddot
 *
 * ## Cycle de vie d'une entité
 *
 * @startuml
 * [*] --> Vivante : Spawn() / SpawnBundle()
 * Vivante --> Vivante : AddComponent() — migration vers nouvel archétype
 * Vivante --> Vivante : remove_component() — migration vers archétype réduit
 * Vivante --> [*]    : Despawn() — libération + freelist
 * @enduml
 *
 * ## Exemple d'utilisation
 *
 * @code{.cpp}
 * ecs::ArchetypeRegistry world;
 *
 * struct Position { float x, y; };
 * struct Velocity { float dx, dy; };
 * struct Health   { int hp; };
 * struct Player   { char[80] id; };
 *
 * // Créer des entités avec un bundle de composants (aucune migration inutile)
 * auto e1 = world.SpawnBundle(Position{0, 0}, Velocity{1, 0}, Health{100});
 * auto e2 = world.SpawnBundle(Position{5, 5}, Velocity{0,-1});
 *
 * // Ajouter un composant a posteriori (migration d'archétype)
 * world.AddComponent(e2, Health{50});
 *
 * // Requête système — version visiteur, sans allocation heap
 * world.Query<Position, Velocity>([](ecs::Entity, Position& p, Velocity& v) {
 *     p.x += v.dx;
 *     p.y += v.dy;
 * });
 *
 * // Requête filtrée : entités avec Position mais sans Health
 * world.Query<Position, ecs::Without<Health>>([](ecs::Entity e, Position& p) {
 *     // ...
 * });
 *
 * // Ressource globale (singleton)
 * world.resources.insert(DeltaTime{0.016f});
 * float dt = world.resources.get_or_throw<DeltaTime>().value;
 * @endcode
 */

#pragma once

#include <algorithm>
#include <any>
#include <cassert>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <stdexcept>
#include <tuple>
#include <typeindex>
#include <unordered_map>
#include <vector>

#include "../core/core.hpp"

namespace ecs {

// ---------------------------------------------------------------------------
// Entity
// ---------------------------------------------------------------------------

struct Entity {
	static constexpr uint32_t INVALID = ~0u;
	uint32_t id = INVALID;
	uint32_t generation;

	[[nodiscard]] bool Valid() const noexcept { return id != INVALID; }
	[[nodiscard]] explicit operator bool() const noexcept { return Valid(); }

	[[nodiscard]] bool operator==(const Entity &other) const noexcept {
		return id == other.id && generation == other.generation;
	}
	[[nodiscard]] bool operator!=(const Entity &other) const noexcept { return !(*this == other); }
	[[nodiscard]] bool operator<(const Entity &other) const noexcept {
		return generation == other.generation && id < other.id;
	}
};

} // namespace ecs

template <> struct std::hash<ecs::Entity> {
	size_t operator()(const ecs::Entity &e) const noexcept {
		// Meilleur mélange de bits que XOR simple
		size_t h = e.id;
		h ^= static_cast<size_t>(e.generation) + 0x9e3779b9ull + (h << 6) + (h >> 2);
		return h;
	}
};

namespace ecs {

// ---------------------------------------------------------------------------
// EntityAllocator
// ---------------------------------------------------------------------------

struct AllocEntry {
	uint32_t generation = 0;
	uint32_t nextFree = static_cast<uint32_t>(-1);
	bool isAlive = false;
};

class EntityAllocator {
private:
	std::vector<AllocEntry> m_entries;
	uint32_t m_freeHead = static_cast<uint32_t>(-1);
	uint32_t m_aliveCount = 0;

public:
	Entity Allocate() {
		if (m_freeHead != static_cast<uint32_t>(-1)) {
			uint32_t idx = m_freeHead;
			AllocEntry &e = m_entries[idx];
			m_freeHead = e.nextFree;
			e.isAlive = true;
			++e.generation; // la génération est incrémentée lors de la réutilisation
			++m_aliveCount;
			return {idx, e.generation};
		}

		uint32_t idx = static_cast<uint32_t>(m_entries.size());
		m_entries.push_back({0, static_cast<uint32_t>(~0), true});
		++m_aliveCount;
		return {idx, 0};
	}

	bool Free(Entity entity) {
		if (entity.id >= m_entries.size())
			return false;
		AllocEntry &e = m_entries[entity.id];
		if (!e.isAlive || e.generation != entity.generation)
			return false;

		e.isAlive = false;
		e.nextFree = m_freeHead;
		m_freeHead = entity.id;
		--m_aliveCount;
		return true;
	}

	bool IsAlive(Entity entity) const noexcept {
		if (entity.id >= m_entries.size())
			return false;
		const AllocEntry &e = m_entries[entity.id];
		return e.isAlive && e.generation == entity.generation;
	}

	uint32_t AliveCount() const noexcept { return m_aliveCount; }
};

// ---------------------------------------------------------------------------
// AnyVec (Type Erasure)
// ---------------------------------------------------------------------------

class IAnyVec {
public:
	virtual ~IAnyVec() = default;
	virtual void SwapRemoveAndMoveTo(size_t row, IAnyVec *other) = 0;
	virtual void SwapRemoveDrop(size_t row) = 0;
	virtual size_t Len() const = 0;
	virtual std::unique_ptr<IAnyVec> NewEmptyOfSameType() const = 0;
};

template <typename T> class AnyVecImpl final : public IAnyVec {
public:
	std::vector<T> data;

	void SwapRemoveAndMoveTo(size_t row, IAnyVec *other) override {
		auto *dst = static_cast<AnyVecImpl<T> *>(other);
		dst->data.push_back(std::move(data[row]));
		if (row + 1 < data.size())
			data[row] = std::move(data.back());
		data.pop_back();
	}

	void SwapRemoveDrop(size_t row) override {
		if (row + 1 < data.size())
			data[row] = std::move(data.back());
		data.pop_back();
	}

	size_t Len() const override { return data.size(); }

	std::unique_ptr<IAnyVec> NewEmptyOfSameType() const override { return std::make_unique<AnyVecImpl<T>>(); }
};

// ---------------------------------------------------------------------------
// Archetype
// ---------------------------------------------------------------------------

struct EntityLocation {
	size_t archetypeId;
	size_t archetypeRow;
};

class Archetype {
public:
	size_t id;
	std::vector<std::type_index> types;
	std::vector<Entity> entities;
	std::unordered_map<std::type_index, std::unique_ptr<IAnyVec>> components;

	Archetype(size_t id, std::vector<std::type_index> types) : id(id), types(std::move(types)) {}

	size_t Len() const { return entities.size(); }
	bool IsEmpty() const { return entities.empty(); }

	bool Contains(std::type_index tid) const { return std::find(types.begin(), types.end(), tid) != types.end(); }

	size_t PushEntity(Entity e) {
		size_t row = entities.size();
		entities.push_back(e);
		return row;
	}

	// Retire l'entité à `row` par swap-remove.
	// Retourne l'entité qui a été déplacée depuis la fin (si elle existe),
	// c'est-à-dire celle dont EntityLocation doit être mise à jour vers `row`.
	Option<Entity> SwapRemoveEntity(size_t row) {
		if (row + 1 < entities.size()) {
			// BUG CORRIGÉ : on sauvegarde l'entité déplacée AVANT l'écrasement
			Entity moved = entities.back();
			entities[row] = std::move(entities.back());
			entities.pop_back();
			return Some(std::move(moved));
		}
		entities.pop_back();
		return NONE;
	}

	// Migre tous les composants communs avec `target` de la ligne `row`.
	void MigrateRowTo(size_t row, Archetype &target) {
		for (const auto &tid : types) {
			auto dst = target.components.find(tid);
			if (dst == target.components.end())
				continue;
			auto src = components.find(tid);
			if (src == components.end())
				continue;
			src->second->SwapRemoveAndMoveTo(row, dst->second.get());
		}
	}

	void DropRow(size_t row) {
		for (auto &[_, vec] : components)
			vec->SwapRemoveDrop(row);
	}
};

// ---------------------------------------------------------------------------
// Filters
// ---------------------------------------------------------------------------

struct All {
	static bool Matches(const Archetype &) { return true; }
};

template <typename T> struct With {
	static bool Matches(const Archetype &arch) { return arch.Contains(typeid(T)); }
};

template <typename T> struct Without {
	static bool Matches(const Archetype &arch) { return !arch.Contains(typeid(T)); }
};

/// Filtre composé AND : TOUS les filtres doivent être satisfaits.
/// Exemple : And<With<A>, With<B>, Without<C>>
template <typename... Filters> struct And {
	static bool Matches(const Archetype &arch) {
		// Fold expression (C++17) : on applique l'opérateur && sur tout le pack.
		// Si la liste est vide, cela évalue à true par défaut.
		return (Filters::Matches(arch) && ... && true);
	}
};

/// Filtre composé OR : AU MOINS UN des filtres doit être satisfait.
/// Exemple : Or<With<A>, With<B>>
template <typename... Filters> struct Or {
	static bool Matches(const Archetype &arch) {
		// Fold expression (C++17) : on applique l'opérateur || sur tout le pack.
		// Si la liste est vide, cela évalue à false par défaut.
		return (Filters::Matches(arch) || ... || false);
	}
};

// Trait pour distinguer les types-filtres des types-composants dans un pack.
template <typename T> struct IsFilterType : std::false_type {};
template <> struct IsFilterType<All> : std::true_type {};
template <typename T> struct IsFilterType<With<T>> : std::true_type {};
template <typename T> struct IsFilterType<Without<T>> : std::true_type {};
template <typename... Fs> struct IsFilterType<And<Fs...>> : std::true_type {};
template <typename... Fs> struct IsFilterType<Or<Fs...>> : std::true_type {};

// ---------------------------------------------------------------------------
// Resources
// ---------------------------------------------------------------------------

class Resources {
	std::unordered_map<std::type_index, std::any> m_map;

public:
	template <typename R> void Insert(R resource) { m_map[typeid(R)] = std::make_any<R>(std::move(resource)); }

	template <typename R> Option<RefMut<R>> Get() {
		auto it = m_map.find(typeid(R));
		if (it != m_map.end()) {
			return Some(MakeRefMut(std::any_cast<R &>(it->second)));
		}
		return NONE;
	}

	template <typename R> Option<Ref<R>> Get() const {
		auto it = m_map.find(typeid(R));
		if (it != m_map.end()) {
			return Some(MakeRef(std::any_cast<const R &>(it->second)));
		}
		return NONE;
	}

	// Remplace "get_or_throw" par un Result explicite
	template <typename R> Result<RefMut<R>, String> GetResult() { return Get<R>().OkOr(String("Resource not found")); }

	template <typename R> bool Contains() const { return m_map.count(typeid(R)) > 0; }

	template <typename R> bool Remove() { return m_map.erase(typeid(R)) > 0; }
};

// ---------------------------------------------------------------------------
// ArchetypeRegistry
// ---------------------------------------------------------------------------

struct ArchetypeSignatureHash {
	size_t operator()(const std::vector<std::type_index> &v) const noexcept {
		size_t seed = v.size();
		for (const auto &ti : v)
			seed ^= ti.hash_code() + 0x9e3779b9ull + (seed << 6) + (seed >> 2);
		return seed;
	}
};

class ArchetypeRegistry {
	using VecConstructor = std::unique_ptr<IAnyVec> (*)();

	EntityAllocator allocator;
	std::unordered_map<Entity, EntityLocation> entityLocations;
	std::vector<std::unique_ptr<Archetype>> archetypes;
	std::unordered_map<std::vector<std::type_index>, size_t, ArchetypeSignatureHash> archetypeIndex;
	std::unordered_map<std::type_index, VecConstructor> componentConstructors;
	// Index composant → liste d'archétypes le contenant, alimenté par
	// get_or_create_archetype() — cf. Query<>() : sans lui, chaque Query<T>()
	// scanne TOUS les archétypes existants (coût O(archétypes), pas
	// O(entités correspondantes)), même pour retrouver une poignée
	// d'entités. Avec des dizaines de types de widgets ui:: combinés à des
	// composants optionnels (UiName/UiCallbacks/UiTooltip...), le nombre
	// d'archétypes DISTINCTS croît vite et peut approcher celui des entités
	// elles-mêmes (mesuré : 251 archétypes pour 265 entités sur une seule
	// page de ui_showcase) — chaque Query<>() y payait donc quasiment le
	// même coût que s'il n'y avait aucun filtrage par type du tout. Les
	// vecteurs stockés ici restent à adresse stable tant qu'aucune clé n'est
	// erase()-ée (garantie std::unordered_map) — jamais fait ici, seulement
	// alimenté en insertion.
	std::unordered_map<std::type_index, std::vector<size_t>> typeToArchetypes;

	template <typename T> static std::unique_ptr<IAnyVec> CreateAnyVec() { return std::make_unique<AnyVecImpl<T>>(); }

	// Retourne ou crée un archétype pour la signature triée donnée.
	size_t GetOrCreateArchetype(std::vector<std::type_index> types) {
		std::sort(types.begin(), types.end());

		auto it = archetypeIndex.find(types);
		if (it != archetypeIndex.end())
			return it->second;

		size_t newId = archetypes.size();
		auto arch = std::make_unique<Archetype>(newId, types);
		for (const auto &tid : types) {
			auto ctor = componentConstructors.find(tid);
			if (ctor != componentConstructors.end())
				arch->components[tid] = ctor->second();
		}

		archetypeIndex[types] = newId;
		archetypes.push_back(std::move(arch));
		for (const auto &tid : types)
			typeToArchetypes[tid].push_back(newId);
		return newId;
	}

	// Déplace une entité de old_arch_id vers new_arch_id (composants communs migrés).
	void MigrateEntity(Entity entity, EntityLocation loc, size_t oldArchId, size_t newArchId) {
		Archetype &oldArch = *archetypes[oldArchId];
		Archetype &newArch = *archetypes[newArchId];

		size_t row = loc.archetypeRow;
		size_t newRow = newArch.PushEntity(entity);

		oldArch.MigrateRowTo(row, newArch);
		auto moved = oldArch.SwapRemoveEntity(row);

		entityLocations[entity] = {newArchId, newRow};

		if (moved.IsSome())
			entityLocations[moved.Value()] = {oldArchId, row};
	}

public:
	Resources resources;

	ArchetypeRegistry() {
		GetOrCreateArchetype({}); // archétype racine (entité sans composant)
	}

	/// Nombre d'archétypes distincts actuellement enregistrés — diagnostic/
	/// profiling (cf. Query<>() : son coût par appel est O(archétypes), pas
	/// O(entités) tant qu'aucun index composant→archétypes n'existe, donc
	/// cette valeur importe directement pour tout code qui appelle Query<>()
	/// souvent, comme ui::InputSystem::dispatch()).
	[[nodiscard]] size_t ArchetypeCount() const noexcept { return archetypes.size(); }

	/// Nombre total d'entités vivantes, toutes archétypes confondus.
	[[nodiscard]] size_t EntityCount() const noexcept { return entityLocations.size(); }

	// --- Entités ---

	Entity Spawn() {
		Entity entity = allocator.Allocate();

		// Remplace .at({}) par une recherche sécurisée sans exception
		auto it = archetypeIndex.find({});
		if (it == archetypeIndex.end())
			std::abort();
		size_t emptyId = it->second;

		size_t row = archetypes[emptyId]->PushEntity(entity);
		entityLocations[entity] = {emptyId, row};
		return entity;
	}

	// Crée une entité avec un bundle de composants en une seule migration.
	template <typename... Ts> Entity SpawnBundle(Ts &&...comps) {
		(RegisterComponent<std::decay_t<Ts>>(), ...);

		std::vector<std::type_index> types = {typeid(std::decay_t<Ts>)...};
		size_t archId = GetOrCreateArchetype(types);
		Archetype &arch = *archetypes[archId];

		Entity entity = allocator.Allocate();
		size_t row = arch.PushEntity(entity);

		// Remplacement de .at() par .find()->second
		(..., static_cast<AnyVecImpl<std::decay_t<Ts>> *>(arch.components.find(typeid(std::decay_t<Ts>))->second.get())
				  ->data.push_back(std::forward<Ts>(comps)));

		entityLocations[entity] = {archId, row};
		return entity;
	}

	bool Despawn(Entity entity) {
		if (!allocator.IsAlive(entity))
			return false;

		auto it = entityLocations.find(entity);
		if (it == entityLocations.end())
			return false;

		EntityLocation loc = it->second;
		Archetype &arch = *archetypes[loc.archetypeId];

		arch.DropRow(loc.archetypeRow);
		auto moved = arch.SwapRemoveEntity(loc.archetypeRow);

		if (moved.IsSome())
			entityLocations[moved.Value()] = {loc.archetypeId, loc.archetypeRow};

		entityLocations.erase(it);
		allocator.Free(entity);
		return true;
	}

	bool IsAlive(Entity entity) const { return allocator.IsAlive(entity); }

	uint32_t AliveCount() const { return allocator.AliveCount(); }

	// --- Composants ---

	template <typename T> void RegisterComponent() { componentConstructors.emplace(typeid(T), &CreateAnyVec<T>); }

	template <typename T> void AddComponent(Entity entity, T component) {
		auto it = entityLocations.find(entity);
		if (it == entityLocations.end())
			return;

		RegisterComponent<T>();
		EntityLocation loc = it->second;
		size_t oldArchId = loc.archetypeId;
		Archetype &oldArch = *archetypes[oldArchId];
		std::type_index tid = typeid(T);

		// Mise à jour sur place si le composant existe déjà
		if (oldArch.Contains(tid)) {
			static_cast<AnyVecImpl<T> *>(oldArch.components[tid].get())->data[loc.archetypeRow] =
				std::move(component);
			return;
		}

		// Migration vers la nouvelle signature
		std::vector<std::type_index> newTypes = oldArch.types;
		newTypes.push_back(tid);
		size_t newArchId = GetOrCreateArchetype(std::move(newTypes));

		MigrateEntity(entity, loc, oldArchId, newArchId);

		// Pousse la valeur dans le nouveau vecteur
		static_cast<AnyVecImpl<T> *>(archetypes[newArchId]->components[tid].get())
			->data.push_back(std::move(component));
	}

	template <typename T> bool RemoveComponent(Entity entity) {
		auto it = entityLocations.find(entity);
		if (it == entityLocations.end())
			return false;

		EntityLocation loc = it->second;
		size_t oldArchId = loc.archetypeId;
		Archetype &oldArch = *archetypes[oldArchId];
		std::type_index tid = typeid(T);

		if (!oldArch.Contains(tid))
			return false;

		// Nouvelle signature sans T
		std::vector<std::type_index> newTypes;
		newTypes.reserve(oldArch.types.size() - 1);
		for (const auto &t : oldArch.types)
			if (t != tid)
				newTypes.push_back(t);

		size_t newArchId = GetOrCreateArchetype(std::move(newTypes));
		Archetype &newArch = *archetypes[newArchId];

		size_t row = loc.archetypeRow;
		size_t newRow = newArch.PushEntity(entity);

		// Migre tous les composants sauf T, puis drop T
		for (const auto &t : oldArch.types) {
			if (t == tid) {
				oldArch.components[tid]->SwapRemoveDrop(row);
			} else {
				oldArch.components[t]->SwapRemoveAndMoveTo(row, newArch.components[t].get());
			}
		}

		auto moved = oldArch.SwapRemoveEntity(row);
		entityLocations[entity] = {newArchId, newRow};
		if (moved.IsSome())
			entityLocations[moved.Value()] = {oldArchId, row};

		return true;
	}

	template <typename T> Option<RefMut<T>> GetComponent(Entity entity) {
		auto it = entityLocations.find(entity);
		if (it == entityLocations.end())
			return NONE;

		EntityLocation loc = it->second;
		Archetype &arch = *archetypes[loc.archetypeId];
		std::type_index tid = typeid(T);

		if (!arch.Contains(tid))
			return NONE;
		return Some(MakeRefMut(
			static_cast<AnyVecImpl<T> *>(arch.components.find(tid)->second.get())->data[loc.archetypeRow]));
	}

	template <typename T> Option<Ref<T>> GetComponent(Entity entity) const {
		auto it = entityLocations.find(entity);
		if (it == entityLocations.end())
			return NONE;

		EntityLocation loc = it->second;
		const Archetype &arch = *archetypes[loc.archetypeId];
		std::type_index tid = typeid(T);

		if (!arch.Contains(tid))
			return NONE;
		return Some(MakeRef(
			static_cast<const AnyVecImpl<T> *>(arch.components.find(tid)->second.get())->data[loc.archetypeRow]));
	}

	template <typename T> bool HasComponent(Entity entity) const {
		auto it = entityLocations.find(entity);
		if (it == entityLocations.end())
			return false;
		return archetypes[it->second.archetypeId]->Contains(typeid(T));
	}

	// --- Requêtes ---

	// Sépare automatiquement les types-filtres (With/Without/And/Or/All) des types-composants.
	// Usage : registry.Query<Pos, Vel>([](Entity e, Pos& p, Vel& v){ … });
	//         registry.Query<Pos, Without<Health>>([](Entity e, Pos& p){ … });
	template <typename... Comps, typename Fn> void Query(Fn &&fn) {
		// Restreint le scan aux archétypes du type RÉEL (non-filtre) le
		// moins répandu parmi Comps... (cf. type_to_archetypes) — reste un
		// SUR-ensemble sûr de l'intersection exacte (jamais un sous-
		// ensemble), donc les vérifications ok/contains() ci-dessous restent
		// inchangées et gardent la sémantique identique à un scan complet ;
		// seul le nombre d'archétypes REJETÉS pour rien diminue.
		static const std::vector<size_t> K_EMPTY_CANDIDATES{};
		const std::vector<size_t> *bestList = nullptr;
		bool anyRealType = false;
		(
			[&] {
				if constexpr (!IsFilterType<Comps>::value) {
					anyRealType = true;
					auto it = typeToArchetypes.find(typeid(Comps));
					const std::vector<size_t> &list = (it != typeToArchetypes.end()) ? it->second : K_EMPTY_CANDIDATES;
					if (!bestList || list.size() < bestList->size())
						bestList = &list;
				}
			}(),
			...);

		auto visit = [&](size_t archId) {
			Archetype &arch = *archetypes[archId];

			bool ok = true;
			(
				[&] {
					if constexpr (IsFilterType<Comps>::value) {
						if (!Comps::Matches(arch))
							ok = false;
					} else {
						if (!arch.Contains(typeid(Comps)))
							ok = false;
					}
				}(),
				...);
			if (!ok)
				return;

			// Construit un tuple contenant uniquement les pointeurs vers les vecteurs de composants
			// (les types-filtres produisent un tuple<> vide qui disparaît dans tuple_cat).
			auto vecs = std::tuple_cat([&] {
				if constexpr (IsFilterType<Comps>::value) {
					return std::tuple<>{};
				} else {
					return std::make_tuple(
						static_cast<AnyVecImpl<Comps> *>(arch.components.find(typeid(Comps))->second.get()));
				}
			}()...);

			size_t n = arch.Len();
			for (size_t row = 0; row < n; ++row) {
				std::apply([&](auto *...vptrs) { std::invoke(fn, arch.entities[row], vptrs->data[row]...); }, vecs);
			}
		};

		if (anyRealType) {
			for (size_t archId : *bestList)
				visit(archId);
		} else {
			// Comps... ne contient QUE des types-filtres (With/Without/And/
			// Or/All) — jamais le cas dans ce dépôt à ce jour, mais rien ne
			// permet de restreindre le scan sans au moins un type réel.
			for (size_t i = 0; i < archetypes.size(); ++i)
				visit(i);
		}
	}

	// Version const du visiteur — même optimisation d'index que la version
	// mutable ci-dessus (cf. son commentaire pour la justification complète).
	template <typename... Comps, typename Fn> void Query(Fn &&fn) const {
		static const std::vector<size_t> K_EMPTY_CANDIDATES{};
		const std::vector<size_t> *bestList = nullptr;
		bool anyRealType = false;
		(
			[&] {
				if constexpr (!IsFilterType<Comps>::value) {
					anyRealType = true;
					auto it = typeToArchetypes.find(typeid(Comps));
					const std::vector<size_t> &list = (it != typeToArchetypes.end()) ? it->second : K_EMPTY_CANDIDATES;
					if (!bestList || list.size() < bestList->size())
						bestList = &list;
				}
			}(),
			...);

		auto visit = [&](size_t archId) {
			const Archetype &arch = *archetypes[archId];

			bool ok = true;
			(
				[&] {
					if constexpr (IsFilterType<Comps>::value) {
						if (!Comps::Matches(arch))
							ok = false;
					} else {
						if (!arch.Contains(typeid(Comps)))
							ok = false;
					}
				}(),
				...);
			if (!ok)
				return;

			auto vecs = std::tuple_cat([&] {
				if constexpr (IsFilterType<Comps>::value) {
					return std::tuple<>{};
				} else {
					return std::make_tuple(
						static_cast<const AnyVecImpl<Comps> *>(arch.components.find(typeid(Comps))->second.get()));
				}
			}()...);

			size_t n = arch.Len();
			for (size_t row = 0; row < n; ++row) {
				std::apply([&](const auto *...vptrs) { std::invoke(fn, arch.entities[row], vptrs->data[row]...); },
						   vecs);
			}
		};

		if (anyRealType) {
			for (size_t archId : *bestList)
				visit(archId);
		} else {
			for (size_t i = 0; i < archetypes.size(); ++i)
				visit(i);
		}
	}

	// Version collectant dans un vecteur.
	// Le type de retour exclut automatiquement les types-filtres du tuple résultat.
	template <typename... Comps> auto QueryVec() {
		using ResultTuple = decltype(std::tuple_cat(
			std::declval<std::tuple<Entity>>(),
			std::declval<
				std::conditional_t<IsFilterType<Comps>::value, std::tuple<>, std::tuple<RefMut<Comps>>>>()...));
		std::vector<ResultTuple> results;
		Query<Comps...>([&](Entity e, auto &...cs) { results.emplace_back(e, MakeRefMut(cs)...); });
		return results;
	}

	// Recherche d'une entité unique satisfaisant la query.
	// Retourne NONE si aucune ou plusieurs entités correspondent.
	template <typename... Comps> auto QuerySingle() {
		using ResultTuple = decltype(std::tuple_cat(
			std::declval<std::tuple<Entity>>(),
			std::declval<
				std::conditional_t<IsFilterType<Comps>::value, std::tuple<>, std::tuple<RefMut<Comps>>>>()...));
		Option<ResultTuple> result = NONE;
		bool foundMore = false;

		Query<Comps...>([&](Entity e, auto &...cs) {
			if (result.IsSome()) {
				foundMore = true;
				return;
			}
			result = Some(std::make_tuple(e, MakeRefMut(cs)...));
		});

		if (foundMore)
			result = NONE;
		return result;
	}

	// --- Helpers de commodité (ajoutés pour le module ui) ---

	// Garantit la présence du composant : l'ajoute avec `default_value` s'il
	// manque, puis retourne la référence mutable (toujours Some après appel).
	template <typename T> Option<RefMut<T>> GetOrAddComponent(Entity entity, T defaultValue = T{}) {
		if (!HasComponent<T>(entity))
			AddComponent(entity, std::move(defaultValue));
		return GetComponent<T>(entity);
	}

	// Liste des entités possédant le composant T (copie — sûre vis-à-vis des
	// changements structurels effectués pendant qu'on itère dessus ensuite).
	template <typename T> [[nodiscard]] std::vector<Entity> EntitiesWith() const {
		std::vector<Entity> out;
		auto it = typeToArchetypes.find(typeid(T));
		if (it == typeToArchetypes.end())
			return out;
		for (size_t archId : it->second) {
			const auto &ents = archetypes[archId]->entities;
			out.insert(out.end(), ents.begin(), ents.end());
		}
		return out;
	}

	// Détruit toutes les entités vivantes (les ressources sont conservées).
	void Clear() {
		std::vector<Entity> all;
		for (const auto &archPtr : archetypes)
			all.insert(all.end(), archPtr->entities.begin(), archPtr->entities.end());
		for (Entity e : all)
			Despawn(e);
	}
};

// ---------------------------------------------------------------------------
// CommandBuffer — changements structurels différés
//
// add_component/remove_component/despawn migrent les entités entre archétypes,
// ce qui invalide l'itération en cours d'une query. Ce tampon enregistre les
// opérations pendant l'itération et les applique d'un bloc via Flush().
// ---------------------------------------------------------------------------

class CommandBuffer {
	std::vector<std::function<void(ArchetypeRegistry &)>> m_commands;

public:
	template <typename T> void AddComponent(Entity e, T component) {
		m_commands.push_back(
			[e, c = std::move(component)](ArchetypeRegistry &reg) mutable { reg.AddComponent(e, std::move(c)); });
	}

	template <typename T> void RemoveComponent(Entity e) {
		m_commands.push_back([e](ArchetypeRegistry &reg) { reg.RemoveComponent<T>(e); });
	}

	void Despawn(Entity e) {
		m_commands.push_back([e](ArchetypeRegistry &reg) { reg.Despawn(e); });
	}

	/// Enregistre une opération arbitraire.
	void Push(std::function<void(ArchetypeRegistry &)> fn) { m_commands.push_back(std::move(fn)); }

	[[nodiscard]] bool IsEmpty() const noexcept { return m_commands.empty(); }
	[[nodiscard]] size_t GetSize() const noexcept { return m_commands.size(); }

	/// Applique toutes les opérations dans l'ordre d'enregistrement, puis vide le tampon.
	void Flush(ArchetypeRegistry &registry) {
		for (auto &cmd : m_commands)
			cmd(registry);
		m_commands.clear();
	}
};

} // namespace ecs

