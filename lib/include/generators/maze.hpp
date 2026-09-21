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

    bool HasPoint(const math::FVector3& point) {
        return point.x >= min.x && point.x <= max.x 
            && point.y >= min.y && point.y <= max.y;
    }
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
    std::size_t GetIndex(Uint32 x, Uint32 y, Uint32 z) const noexcept {
        return static_cast<std::size_t>(z) * width * height
             + static_cast<std::size_t>(y) * width
             + x;
    }

    [[nodiscard]]
    bool IsValid(Uint32 x, Uint32 y, Uint32 z) const noexcept {
        return x < width &&
               y < height &&
               z < level;
    }

    [[nodiscard]]
    bool IsValidIndex(std::size_t index) const noexcept {
        return index < data.size();
    }

    template <typename T>
    [[nodiscard]]
    Result<T, Error> Fail(const char* message) const {
        sdl3::SetError(message);
        return Err<Error>(Error(message));
    }

    [[nodiscard]]
    Result<bool, Error> Success() const {
        return Ok(true);
    }

public:

    // =======================================================================
    // Construction
    // =======================================================================

    EntityMap(
        Uint32 width,
        Uint32 height,
        Uint32 level,
        ecs::Entity fill
    )
        : width(width),
          height(height),
          level(level),
          data(
              static_cast<std::size_t>(width) *
              static_cast<std::size_t>(height) *
              static_cast<std::size_t>(level),
              EntityList{fill}
          ) {
    }

    // =======================================================================
    // Informations générales
    // =======================================================================

    [[nodiscard]]
    Uint32 GetWidth() const noexcept {
        return width;
    }

    [[nodiscard]]
    Uint32 GetHeight() const noexcept {
        return height;
    }

    [[nodiscard]]
    Uint32 GetLevel() const noexcept {
        return level;
    }

    [[nodiscard]]
    std::size_t GetSize() const noexcept {
        return data.size();
    }

    // =======================================================================
    // Accès direct à une case par index
    // =======================================================================

    [[nodiscard]]
    Result<RefMut<EntityList>, Error> At(std::size_t index) {
        if (!IsValidIndex(index))
            return Fail<RefMut<EntityList>>(
                "EntityMap: index out of range"
            );

        return Ok(
            RefMut<EntityList>(data[index])
        );
    }

    [[nodiscard]]
    Result<Ref<EntityList>, Error> At(std::size_t index) const {
        if (!IsValidIndex(index))
            return Fail<Ref<EntityList>>(
                "EntityMap: index out of range"
            );

        return Ok(
            Ref<EntityList>(data[index])
        );
    }

    // =======================================================================
    // Accès à une case par coordonnées
    // =======================================================================

    [[nodiscard]]
    Result<RefMut<EntityList>, Error> At(
        Uint32 x,
        Uint32 y,
        Uint32 z
    ) {
        if (!IsValid(x, y, z))
            return Fail<RefMut<EntityList>>(
                "EntityMap: coordinates out of range"
            );

        return At(GetIndex(x, y, z));
    }

    [[nodiscard]]
    Result<Ref<EntityList>, Error> At(
        Uint32 x,
        Uint32 y,
        Uint32 z
    ) const {
        if (!IsValid(x, y, z))
            return Fail<Ref<EntityList>>(
                "EntityMap: coordinates out of range"
            );

        return At(GetIndex(x, y, z));
    }

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
    ) const {
        if (!IsValidIndex(index))
            return Fail<Option<std::size_t>>(
                "EntityMap: index out of range"
            );

        const auto& entities = data[index];

        auto it = std::find(
            entities.begin(),
            entities.end(),
            entity
        );

        if (it == entities.end())
            return Ok(Option<std::size_t>(NONE));

        return Ok<Option<std::size_t>>(
            Some<std::size_t>(
                static_cast<std::size_t>(
                    std::distance(entities.begin(), it)
                )
            )
        );
    }

    [[nodiscard]]
    Result<Option<std::size_t>, Error> Find(
        Uint32 x,
        Uint32 y,
        Uint32 z,
        ecs::Entity entity
    ) const {
        if (!IsValid(x, y, z))
            return Fail<Option<std::size_t>>(
                "EntityMap: coordinates out of range"
            );

        return Find(
            GetIndex(x, y, z),
            entity
        );
    }

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
    ) const {
        if (!IsValidIndex(index))
            return Fail<bool>(
                "EntityMap: index out of range"
            );

        const auto& entities = data[index];

        return Ok(
            std::find(
                entities.begin(),
                entities.end(),
                entity
            ) != entities.end()
        );
    }

    [[nodiscard]]
    Result<bool, Error> Contains(
        Uint32 x,
        Uint32 y,
        Uint32 z,
        ecs::Entity entity
    ) const {
        if (!IsValid(x, y, z))
            return Fail<bool>(
                "EntityMap: coordinates out of range"
            );

        return Contains(
            GetIndex(x, y, z),
            entity
        );
    }

    // =======================================================================
    // Nombre d'entités
    // =======================================================================

    [[nodiscard]]
    Result<std::size_t, Error> GetCount(
        std::size_t index
    ) const {
        if (!IsValidIndex(index))
            return Fail<std::size_t>(
                "EntityMap: index out of range"
            );

        return Ok(data[index].size());
    }

    [[nodiscard]]
    Result<std::size_t, Error> GetCount(
        Uint32 x,
        Uint32 y,
        Uint32 z
    ) const {
        if (!IsValid(x, y, z))
            return Fail<std::size_t>(
                "EntityMap: coordinates out of range"
            );

        return GetCount(GetIndex(x, y, z));
    }

    // =======================================================================
    // Ajouter à la fin
    // =======================================================================

    [[nodiscard]]
    Result<bool, Error> PushBack(
        std::size_t index,
        ecs::Entity entity
    ) {
        if (!IsValidIndex(index))
            return Fail<bool>(
                "EntityMap: index out of range"
            );

        data[index].push_back(entity);

        return Success();
    }

    [[nodiscard]]
    Result<bool, Error> PushBack(
        Uint32 x,
        Uint32 y,
        Uint32 z,
        ecs::Entity entity
    ) {
        if (!IsValid(x, y, z))
            return Fail<bool>(
                "EntityMap: coordinates out of range"
            );

        return PushBack(
            GetIndex(x, y, z),
            entity
        );
    }

    // =======================================================================
    // Ajouter au début
    // =======================================================================

    [[nodiscard]]
    Result<bool, Error> PushFront(
        std::size_t index,
        ecs::Entity entity
    ) {
        if (!IsValidIndex(index))
            return Fail<bool>(
                "EntityMap: index out of range"
            );

        auto& entities = data[index];

        entities.insert(
            entities.begin(),
            entity
        );

        return Success();
    }

    [[nodiscard]]
    Result<bool, Error> PushFront(
        Uint32 x,
        Uint32 y,
        Uint32 z,
        ecs::Entity entity
    ) {
        if (!IsValid(x, y, z))
            return Fail<bool>(
                "EntityMap: coordinates out of range"
            );

        return PushFront(
            GetIndex(x, y, z),
            entity
        );
    }

    // =======================================================================
    // Insérer à une position précise
    // =======================================================================

    [[nodiscard]]
    Result<bool, Error> Insert(
        std::size_t index,
        std::size_t position,
        ecs::Entity entity
    ) {
        if (!IsValidIndex(index))
            return Fail<bool>(
                "EntityMap: index out of range"
            );

        auto& entities = data[index];

        if (position > entities.size())
            return Fail<bool>(
                "EntityMap: insertion position out of range"
            );

        entities.insert(
            entities.begin() + position,
            entity
        );

        return Success();
    }

    [[nodiscard]]
    Result<bool, Error> Insert(
        Uint32 x,
        Uint32 y,
        Uint32 z,
        std::size_t position,
        ecs::Entity entity
    ) {
        if (!IsValid(x, y, z))
            return Fail<bool>(
                "EntityMap: coordinates out of range"
            );

        return Insert(
            GetIndex(x, y, z),
            position,
            entity
        );
    }

    // =======================================================================
    // Retirer par position
    // =======================================================================

    [[nodiscard]]
    Result<bool, Error> Erase(
        std::size_t index,
        std::size_t position
    ) {
        if (!IsValidIndex(index))
            return Fail<bool>(
                "EntityMap: index out of range"
            );

        auto& entities = data[index];

        if (position >= entities.size())
            return Fail<bool>(
                "EntityMap: erase position out of range"
            );

        entities.erase(
            entities.begin() + position
        );

        return Success();
    }

    [[nodiscard]]
    Result<bool, Error> Erase(
        Uint32 x,
        Uint32 y,
        Uint32 z,
        std::size_t position
    ) {
        if (!IsValid(x, y, z))
            return Fail<bool>(
                "EntityMap: coordinates out of range"
            );

        return Erase(
            GetIndex(x, y, z),
            position
        );
    }

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
    ) {
        if (!IsValidIndex(index))
            return Fail<bool>(
                "EntityMap: index out of range"
            );

        auto& entities = data[index];

        auto it = std::find(
            entities.begin(),
            entities.end(),
            entity
        );

        if (it == entities.end())
            return Ok(false);

        entities.erase(it);

        return Ok(true);
    }

    [[nodiscard]]
    Result<bool, Error> Remove(
        Uint32 x,
        Uint32 y,
        Uint32 z,
        ecs::Entity entity
    ) {
        if (!IsValid(x, y, z))
            return Fail<bool>(
                "EntityMap: coordinates out of range"
            );

        return Remove(
            GetIndex(x, y, z),
            entity
        );
    }

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
    ) {
        if (!IsValidIndex(index))
            return Fail<std::size_t>(
                "EntityMap: index out of range"
            );

        auto& entities = data[index];

        const std::size_t oldSize = entities.size();

        entities.erase(
            std::remove(
                entities.begin(),
                entities.end(),
                entity
            ),
            entities.end()
        );

        return Ok(oldSize - entities.size());
    }

    [[nodiscard]]
    Result<std::size_t, Error> RemoveAll(
        Uint32 x,
        Uint32 y,
        Uint32 z,
        ecs::Entity entity
    ) {
        if (!IsValid(x, y, z))
            return Fail<std::size_t>(
                "EntityMap: coordinates out of range"
            );

        return RemoveAll(
            GetIndex(x, y, z),
            entity
        );
    }

    // =======================================================================
    // Vider une case
    // =======================================================================

    [[nodiscard]]
    Result<bool, Error> Clear(
        std::size_t index
    ) {
        if (!IsValidIndex(index))
            return Fail<bool>(
                "EntityMap: index out of range"
            );

        data[index].clear();

        return Success();
    }

    [[nodiscard]]
    Result<bool, Error> Clear(
        Uint32 x,
        Uint32 y,
        Uint32 z
    ) {
        if (!IsValid(x, y, z))
            return Fail<bool>(
                "EntityMap: coordinates out of range"
            );

        return Clear(GetIndex(x, y, z));
    }

    // =======================================================================
    // Vider toute la map
    // =======================================================================

    void ClearAll() noexcept {
        for (auto& entities : data)
            entities.clear();
    }
};

class Maze: public Generator<EntityMap> {


};

} /* namespace generators::maze */