// Définitions de ecs/ecs.hpp
#include "ecs/ecs.hpp"

namespace ecs {

// ── EntityAllocator ──────────────────────────────────────────────────────────

Entity EntityAllocator::Allocate() {
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

bool EntityAllocator::Free(Entity entity) {
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

bool EntityAllocator::IsAlive(Entity entity) const noexcept {
	if (entity.id >= m_entries.size())
		return false;
	const AllocEntry &e = m_entries[entity.id];
	return e.isAlive && e.generation == entity.generation;
}

// ── Archetype ────────────────────────────────────────────────────────────────

size_t Archetype::PushEntity(Entity e) {
	size_t row = entities.size();
	entities.push_back(e);
	return row;
}

Option<Entity> Archetype::SwapRemoveEntity(size_t row) {
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

void Archetype::DropRow(size_t row) {
	for (auto &[_, vec] : components)
		vec->SwapRemoveDrop(row);
}

// ── ArchetypeRegistry ────────────────────────────────────────────────────────

size_t ArchetypeRegistry::GetOrCreateArchetype(std::vector<std::type_index> types) {
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

void ArchetypeRegistry::MigrateEntity(Entity entity, EntityLocation loc, size_t oldArchId, size_t newArchId) {
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

ArchetypeRegistry::ArchetypeRegistry() {
	GetOrCreateArchetype({}); // archétype racine (entité sans composant)
}

Entity ArchetypeRegistry::Spawn() {
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

bool ArchetypeRegistry::Despawn(Entity entity) {
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

void ArchetypeRegistry::Clear() {
	std::vector<Entity> all;
	for (const auto &archPtr : archetypes)
		all.insert(all.end(), archPtr->entities.begin(), archPtr->entities.end());
	for (Entity e : all)
		Despawn(e);
}

// ── CommandBuffer ────────────────────────────────────────────────────────────

void CommandBuffer::Despawn(Entity e) {
	m_commands.push_back([e](ArchetypeRegistry &reg) { reg.Despawn(e); });
}

void CommandBuffer::Flush(ArchetypeRegistry &registry) {
	for (auto &cmd : m_commands)
		cmd(registry);
	m_commands.clear();
}

} // namespace ecs
