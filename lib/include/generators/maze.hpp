#pragma once

#include "generator.hpp"
#include "../core/core.hpp"
#include "../sdl3/sdl3.hpp"
#include "../ecs/ecs.hpp"

namespace generators::maze {

struct Config3D {
	// Probability to not remove deadends
	float deadendChance = 0.5;
	// True if use reconnect_deadends() step - connect deadends adjacent to rooms with a door
	float reconnectDeadendsChance = 0.5;
	// Probability for a hall to change direction during growth
	float wiggleChance = 0.5;
	// All unneccessary doors are removed with this probability
	float extraConnectionChance = 0.0;
	// How many times room placement is attempted
	uint32_t roomBaseNumber = 30;
	// Room minimum dimension
	math::FVector3 roomSizeMin = {7, 7, 4};
	// Room maximum dimension
	math::FVector3 roomSizeMax = {9, 9, 4};
	// True if hall constaraints are to be exclusively in halls, not in rooms
	bool constrainHallOnly = false;
};

// Represents a room with dimensions and id
struct Room3D: public math::FAABB {
	int id;

	Room3D(const math::FVector3& pmin, const math::FVector3& pmax, int _id)
		: math::FAABB(pmin, pmax), id(_id) {}

	bool TooClose(const Room3D& another, int distance) {
		return min.x - distance < another.max.x 
			&& max.x + distance > another.min.x 
			&& min.y - distance < another.max.y 
			&& max.y + distance > another.min.y;
	}

	bool HasPoint(const math::FVector3& point);
};

class EntityMap {
public:
	using EntityList = std::vector<ecs::Entity>;
	using Error = String;

private:
	std::vector<EntityList> data;
	Uint32 width;
	Uint32 height;
	Uint32 level;

	// -----------------------------------------------------------------------
	// Utilitaires internes
	// -----------------------------------------------------------------------

	[[nodiscard]]
	std::size_t GetIndex(Uint32 x, Uint32 y, Uint32 z) const noexcept;

	[[nodiscard]]
	bool IsValid(Uint32 x, Uint32 y, Uint32 z) const noexcept;

	[[nodiscard]]
	bool IsValidIndex(std::size_t index) const noexcept;

	template <typename T>
	[[nodiscard]]
	Result<T, Error> Fail(const char* message) const {
		sdl3::SetError(message);
		return Err<Error>(Error(message));
	}

	[[nodiscard]]
	Result<bool, Error> Success() const;

public:

	// =======================================================================
	// Construction
	// =======================================================================

	EntityMap(
		Uint32 width,
		Uint32 height,
		Uint32 level,
		ecs::Entity fill
	);

	// =======================================================================
	// Informations générales
	// =======================================================================

	[[nodiscard]]
	Uint32 GetWidth() const noexcept;

	[[nodiscard]]
	Uint32 GetHeight() const noexcept;

	[[nodiscard]]
	Uint32 GetLevel() const noexcept;

	[[nodiscard]]
	std::size_t GetSize() const noexcept;

	// =======================================================================
	// Accès direct à une case par index
	// =======================================================================

	[[nodiscard]]
	Result<RefMut<EntityList>, Error> At(std::size_t index);

	[[nodiscard]]
	Result<Ref<EntityList>, Error> At(std::size_t index) const;

	// =======================================================================
	// Accès à une case par coordonnées
	// =======================================================================

	[[nodiscard]]
	Result<RefMut<EntityList>, Error> At(
		Uint32 x,
		Uint32 y,
		Uint32 z
	);

	[[nodiscard]]
	Result<Ref<EntityList>, Error> At(
		Uint32 x,
		Uint32 y,
		Uint32 z
	) const;

	// =======================================================================
	// Recherche d'une entité
	// =======================================================================

	/**
	 * Retourne la position de la première occurrence.
	 *
	 * Ok(Some(position)) -> trouvée
	 * Ok(NONE)            -> absente
	 * Err(...)             -> index invalide
	 */
	[[nodiscard]]
	Result<Option<std::size_t>, Error> Find(
		std::size_t index,
		ecs::Entity entity
	) const;

	[[nodiscard]]
	Result<Option<std::size_t>, Error> Find(
		Uint32 x,
		Uint32 y,
		Uint32 z,
		ecs::Entity entity
	) const;

	// =======================================================================
	// Contains
	// =======================================================================

	/**
	 * L'entité absente n'est PAS une erreur.
	 */
	[[nodiscard]]
	Result<bool, Error> Contains(
		std::size_t index,
		ecs::Entity entity
	) const;

	[[nodiscard]]
	Result<bool, Error> Contains(
		Uint32 x,
		Uint32 y,
		Uint32 z,
		ecs::Entity entity
	) const;

	// =======================================================================
	// Nombre d'entités
	// =======================================================================

	[[nodiscard]]
	Result<std::size_t, Error> GetCount(
		std::size_t index
	) const;

	[[nodiscard]]
	Result<std::size_t, Error> GetCount(
		Uint32 x,
		Uint32 y,
		Uint32 z
	) const;

	// =======================================================================
	// Ajouter à la fin
	// =======================================================================

	[[nodiscard]]
	Result<bool, Error> PushBack(
		std::size_t index,
		ecs::Entity entity
	);

	[[nodiscard]]
	Result<bool, Error> PushBack(
		Uint32 x,
		Uint32 y,
		Uint32 z,
		ecs::Entity entity
	);

	// =======================================================================
	// Ajouter au début
	// =======================================================================

	[[nodiscard]]
	Result<bool, Error> PushFront(
		std::size_t index,
		ecs::Entity entity
	);

	[[nodiscard]]
	Result<bool, Error> PushFront(
		Uint32 x,
		Uint32 y,
		Uint32 z,
		ecs::Entity entity
	);

	// =======================================================================
	// Insérer à une position précise
	// =======================================================================

	[[nodiscard]]
	Result<bool, Error> Insert(
		std::size_t index,
		std::size_t position,
		ecs::Entity entity
	);

	[[nodiscard]]
	Result<bool, Error> Insert(
		Uint32 x,
		Uint32 y,
		Uint32 z,
		std::size_t position,
		ecs::Entity entity
	);

	// =======================================================================
	// Retirer par position
	// =======================================================================

	[[nodiscard]]
	Result<bool, Error> Erase(
		std::size_t index,
		std::size_t position
	);

	[[nodiscard]]
	Result<bool, Error> Erase(
		Uint32 x,
		Uint32 y,
		Uint32 z,
		std::size_t position
	);

	// =======================================================================
	// Retirer la première occurrence
	// =======================================================================

	/**
	 * Ok(true)  -> supprimée
	 * Ok(false) -> l'entité n'était pas présente
	 * Err(...)  -> mauvais index
	 */
	[[nodiscard]]
	Result<bool, Error> Remove(
		std::size_t index,
		ecs::Entity entity
	);

	[[nodiscard]]
	Result<bool, Error> Remove(
		Uint32 x,
		Uint32 y,
		Uint32 z,
		ecs::Entity entity
	);

	// =======================================================================
	// Retirer toutes les occurrences
	// =======================================================================

	/**
	 * Retourne le nombre d'entités supprimées.
	 */
	[[nodiscard]]
	Result<std::size_t, Error> RemoveAll(
		std::size_t index,
		ecs::Entity entity
	);

	[[nodiscard]]
	Result<std::size_t, Error> RemoveAll(
		Uint32 x,
		Uint32 y,
		Uint32 z,
		ecs::Entity entity
	);

	// =======================================================================
	// Vider une case
	// =======================================================================

	[[nodiscard]]
	Result<bool, Error> Clear(
		std::size_t index
	);

	[[nodiscard]]
	Result<bool, Error> Clear(
		Uint32 x,
		Uint32 y,
		Uint32 z
	);

	// =======================================================================
	// Vider toute la map
	// =======================================================================

	void ClearAll() noexcept;
};

class Maze: public Generator<EntityMap> {


};

} /* namespace generators::maze */