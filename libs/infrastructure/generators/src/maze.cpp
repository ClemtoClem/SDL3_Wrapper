// Définitions de generators/maze.hpp
#include <generators/maze.hpp>

namespace generators::maze {

// ── Room3D ───────────────────────────────────────────────────────────────────

bool Room3D::HasPoint(const math::FVector3& point) {
    return point.x >= min.x && point.x <= max.x 
        && point.y >= min.y && point.y <= max.y;
}

// ── EntityMap ────────────────────────────────────────────────────────────────

std::size_t EntityMap::GetIndex(std::uint32_t x, std::uint32_t y, std::uint32_t z) const noexcept {
    return static_cast<std::size_t>(z) * width * height
         + static_cast<std::size_t>(y) * width
         + x;
}

bool EntityMap::IsValid(std::uint32_t x, std::uint32_t y, std::uint32_t z) const noexcept {
    return x < width &&
           y < height &&
           z < level;
}

bool EntityMap::IsValidIndex(std::size_t index) const noexcept {
    return index < data.size();
}

Result<bool, EntityMap::Error> EntityMap::Success() const {
    return Ok(true);
}

EntityMap::EntityMap(std::uint32_t width, std::uint32_t height, std::uint32_t level, ecs::Entity fill)
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

std::uint32_t EntityMap::GetWidth() const noexcept {
    return width;
}

std::uint32_t EntityMap::GetHeight() const noexcept {
    return height;
}

std::uint32_t EntityMap::GetLevel() const noexcept {
    return level;
}

std::size_t EntityMap::GetSize() const noexcept {
    return data.size();
}

Result<RefMut<EntityMap::EntityList>, EntityMap::Error> EntityMap::At(std::size_t index) {
    if (!IsValidIndex(index))
        return Fail<RefMut<EntityList>>(
            "EntityMap: index out of range"
        );

    return Ok(
        RefMut<EntityList>(data[index])
    );
}

Result<Ref<EntityMap::EntityList>, EntityMap::Error> EntityMap::At(std::size_t index) const {
    if (!IsValidIndex(index))
        return Fail<Ref<EntityList>>(
            "EntityMap: index out of range"
        );

    return Ok(
        Ref<EntityList>(data[index])
    );
}

Result<RefMut<EntityMap::EntityList>, EntityMap::Error> EntityMap::At(std::uint32_t x, std::uint32_t y, std::uint32_t z) {
    if (!IsValid(x, y, z))
        return Fail<RefMut<EntityList>>(
            "EntityMap: coordinates out of range"
        );

    return At(GetIndex(x, y, z));
}

Result<Ref<EntityMap::EntityList>, EntityMap::Error> EntityMap::At(std::uint32_t x, std::uint32_t y, std::uint32_t z) const {
    if (!IsValid(x, y, z))
        return Fail<Ref<EntityList>>(
            "EntityMap: coordinates out of range"
        );

    return At(GetIndex(x, y, z));
}

Result<Option<std::size_t>, EntityMap::Error> EntityMap::Find(std::size_t index, ecs::Entity entity) const {
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

Result<Option<std::size_t>, EntityMap::Error> EntityMap::Find(std::uint32_t x, std::uint32_t y, std::uint32_t z, ecs::Entity entity) const {
    if (!IsValid(x, y, z))
        return Fail<Option<std::size_t>>(
            "EntityMap: coordinates out of range"
        );

    return Find(
        GetIndex(x, y, z),
        entity
    );
}

Result<bool, EntityMap::Error> EntityMap::Contains(std::size_t index, ecs::Entity entity) const {
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

Result<bool, EntityMap::Error> EntityMap::Contains(std::uint32_t x, std::uint32_t y, std::uint32_t z, ecs::Entity entity) const {
    if (!IsValid(x, y, z))
        return Fail<bool>(
            "EntityMap: coordinates out of range"
        );

    return Contains(
        GetIndex(x, y, z),
        entity
    );
}

Result<std::size_t, EntityMap::Error> EntityMap::GetCount(std::size_t index) const {
    if (!IsValidIndex(index))
        return Fail<std::size_t>(
            "EntityMap: index out of range"
        );

    return Ok(data[index].size());
}

Result<std::size_t, EntityMap::Error> EntityMap::GetCount(std::uint32_t x, std::uint32_t y, std::uint32_t z) const {
    if (!IsValid(x, y, z))
        return Fail<std::size_t>(
            "EntityMap: coordinates out of range"
        );

    return GetCount(GetIndex(x, y, z));
}

Result<bool, EntityMap::Error> EntityMap::PushBack(std::size_t index, ecs::Entity entity) {
    if (!IsValidIndex(index))
        return Fail<bool>(
            "EntityMap: index out of range"
        );

    data[index].push_back(entity);

    return Success();
}

Result<bool, EntityMap::Error> EntityMap::PushBack(std::uint32_t x, std::uint32_t y, std::uint32_t z, ecs::Entity entity) {
    if (!IsValid(x, y, z))
        return Fail<bool>(
            "EntityMap: coordinates out of range"
        );

    return PushBack(
        GetIndex(x, y, z),
        entity
    );
}

Result<bool, EntityMap::Error> EntityMap::PushFront(std::size_t index, ecs::Entity entity) {
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

Result<bool, EntityMap::Error> EntityMap::PushFront(std::uint32_t x, std::uint32_t y, std::uint32_t z, ecs::Entity entity) {
    if (!IsValid(x, y, z))
        return Fail<bool>(
            "EntityMap: coordinates out of range"
        );

    return PushFront(
        GetIndex(x, y, z),
        entity
    );
}

Result<bool, EntityMap::Error> EntityMap::Insert(std::size_t index, std::size_t position, ecs::Entity entity) {
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

Result<bool, EntityMap::Error> EntityMap::Insert(std::uint32_t x, std::uint32_t y, std::uint32_t z, std::size_t position, ecs::Entity entity) {
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

Result<bool, EntityMap::Error> EntityMap::Erase(std::size_t index, std::size_t position) {
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

Result<bool, EntityMap::Error> EntityMap::Erase(std::uint32_t x, std::uint32_t y, std::uint32_t z, std::size_t position) {
    if (!IsValid(x, y, z))
        return Fail<bool>(
            "EntityMap: coordinates out of range"
        );

    return Erase(
        GetIndex(x, y, z),
        position
    );
}

Result<bool, EntityMap::Error> EntityMap::Remove(std::size_t index, ecs::Entity entity) {
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

Result<bool, EntityMap::Error> EntityMap::Remove(std::uint32_t x, std::uint32_t y, std::uint32_t z, ecs::Entity entity) {
    if (!IsValid(x, y, z))
        return Fail<bool>(
            "EntityMap: coordinates out of range"
        );

    return Remove(
        GetIndex(x, y, z),
        entity
    );
}

Result<std::size_t, EntityMap::Error> EntityMap::RemoveAll(std::size_t index, ecs::Entity entity) {
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

Result<std::size_t, EntityMap::Error> EntityMap::RemoveAll(std::uint32_t x, std::uint32_t y, std::uint32_t z, ecs::Entity entity) {
    if (!IsValid(x, y, z))
        return Fail<std::size_t>(
            "EntityMap: coordinates out of range"
        );

    return RemoveAll(
        GetIndex(x, y, z),
        entity
    );
}

Result<bool, EntityMap::Error> EntityMap::Clear(std::size_t index) {
    if (!IsValidIndex(index))
        return Fail<bool>(
            "EntityMap: index out of range"
        );

    data[index].clear();

    return Success();
}

Result<bool, EntityMap::Error> EntityMap::Clear(std::uint32_t x, std::uint32_t y, std::uint32_t z) {
    if (!IsValid(x, y, z))
        return Fail<bool>(
            "EntityMap: coordinates out of range"
        );

    return Clear(GetIndex(x, y, z));
}

void EntityMap::ClearAll() noexcept {
    for (auto& entities : data)
        entities.clear();
}

} // namespace generators::maze
