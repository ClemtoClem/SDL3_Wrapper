#pragma once
/**
 * Systèmes de coordonnées du monde voxel.
 *
 *  - MONDE   : flottants, Y vers le haut ; le bloc (x, y, z) occupe le cube
 *              [x, x+1[ × [y, y+1[ × [z, z+1[.
 *  - BLOC    : BlockPos, entiers (positions de blocs).
 *  - CHUNK   : ChunkPos, cubes de CHUNK_SIZE³ blocs (chunks CUBIQUES : le
 *              monde n'a pas de hauteur maximale).
 *  - LOCAL   : position d'un bloc dans son chunk, [0, CHUNK_SIZE[³.
 *
 * Les divisions sont des divisions ENTIÈRES PAR DÉFAUT (vers −∞) : le bloc
 * −1 est dans le chunk −1, à la position locale 15.
 */
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <functional>

#include <core/core.hpp>
#include <math/math.hpp>

namespace voxel {

inline constexpr int CHUNK_SHIFT = 4;
inline constexpr int CHUNK_SIZE = 1 << CHUNK_SHIFT; ///< 16
inline constexpr int CHUNK_VOLUME = CHUNK_SIZE * CHUNK_SIZE * CHUNK_SIZE;

[[nodiscard]] constexpr int FloorDiv(int v, int d) noexcept { return v >= 0 ? v / d : -((-v + d - 1) / d); }
[[nodiscard]] constexpr int FloorMod(int v, int d) noexcept { return v - FloorDiv(v, d) * d; }

/// Les six faces d'un bloc, dans l'ordre des textures de BlockDef.
enum class Face : uint8_t { EAST, WEST, TOP, BOTTOM, SOUTH, NORTH }; // +X −X +Y −Y +Z −Z
inline constexpr int FACE_COUNT = 6;

struct BlockPos {
	int x = 0, y = 0, z = 0;

	[[nodiscard]] constexpr bool operator==(const BlockPos &) const noexcept = default;
	[[nodiscard]] constexpr BlockPos operator+(const BlockPos &o) const noexcept { return {x + o.x, y + o.y, z + o.z}; }
	[[nodiscard]] constexpr BlockPos operator-(const BlockPos &o) const noexcept { return {x - o.x, y - o.y, z - o.z}; }
	[[nodiscard]] math::FVector3 ToVector() const noexcept { return {float(x), float(y), float(z)}; }
	/// Centre du bloc, en coordonnées monde.
	[[nodiscard]] math::FVector3 Center() const noexcept { return {float(x) + 0.5f, float(y) + 0.5f, float(z) + 0.5f}; }
	[[nodiscard]] static BlockPos FromWorld(const math::FVector3 &p) noexcept {
		return {int(std::floor(p.x)), int(std::floor(p.y)), int(std::floor(p.z))};
	}
	[[nodiscard]] BlockPos Neighbor(Face face) const noexcept;
	[[nodiscard]] String ToString() const { return String::Format("%d,%d,%d", x, y, z); }
};

struct ChunkPos {
	int x = 0, y = 0, z = 0;
	[[nodiscard]] constexpr bool operator==(const ChunkPos &) const noexcept = default;
	[[nodiscard]] constexpr bool operator<(const ChunkPos &o) const noexcept {
		return x != o.x ? x < o.x : y != o.y ? y < o.y : z < o.z;
	}
	/// Origine du chunk (son bloc de coordonnées locales 0, 0, 0).
	[[nodiscard]] constexpr BlockPos Origin() const noexcept { return {x * CHUNK_SIZE, y * CHUNK_SIZE, z * CHUNK_SIZE}; }
	[[nodiscard]] String ToString() const { return String::Format("%d,%d,%d", x, y, z); }
};

[[nodiscard]] constexpr ChunkPos ChunkOf(const BlockPos &p) noexcept {
	return {FloorDiv(p.x, CHUNK_SIZE), FloorDiv(p.y, CHUNK_SIZE), FloorDiv(p.z, CHUNK_SIZE)};
}
[[nodiscard]] constexpr BlockPos LocalOf(const BlockPos &p) noexcept {
	return {FloorMod(p.x, CHUNK_SIZE), FloorMod(p.y, CHUNK_SIZE), FloorMod(p.z, CHUNK_SIZE)};
}
/// Indice d'un bloc local dans le tableau d'un chunk (x le plus rapide, puis z, puis y).
[[nodiscard]] constexpr int LocalIndex(int x, int y, int z) noexcept {
	return (y * CHUNK_SIZE + z) * CHUNK_SIZE + x;
}

[[nodiscard]] constexpr BlockPos FaceNormal(Face face) noexcept {
	switch (face) {
	case Face::EAST: return {1, 0, 0};
	case Face::WEST: return {-1, 0, 0};
	case Face::TOP: return {0, 1, 0};
	case Face::BOTTOM: return {0, -1, 0};
	case Face::SOUTH: return {0, 0, 1};
	case Face::NORTH: return {0, 0, -1};
	}
	return {0, 0, 0};
}
[[nodiscard]] constexpr Face Opposite(Face face) noexcept {
	return Face(uint8_t(face) ^ 1u);
}
inline BlockPos BlockPos::Neighbor(Face face) const noexcept { return *this + FaceNormal(face); }

/// Boîte alignée (coordonnées monde, ou locales à un bloc selon le contexte).
struct Box {
	math::FVector3 min{0.f, 0.f, 0.f}, max{1.f, 1.f, 1.f};

	[[nodiscard]] bool Intersects(const Box &o) const noexcept {
		return min.x < o.max.x && max.x > o.min.x && min.y < o.max.y && max.y > o.min.y && min.z < o.max.z && max.z > o.min.z;
	}
	[[nodiscard]] Box Offset(const math::FVector3 &d) const noexcept { return {min + d, max + d}; }
	[[nodiscard]] math::FVector3 Size() const noexcept { return max - min; }
	[[nodiscard]] math::FVector3 Center() const noexcept { return (min + max) * 0.5f; }
	[[nodiscard]] bool Contains(const math::FVector3 &p) const noexcept {
		return p.x >= min.x && p.x <= max.x && p.y >= min.y && p.y <= max.y && p.z >= min.z && p.z <= max.z;
	}
	/// Boîte de taille `size` dont le centre de la BASE est en `foot`.
	[[nodiscard]] static Box AtFeet(const math::FVector3 &foot, const math::FVector3 &size) noexcept {
		return {{foot.x - size.x * 0.5f, foot.y, foot.z - size.z * 0.5f}, {foot.x + size.x * 0.5f, foot.y + size.y, foot.z + size.z * 0.5f}};
	}
};

} // namespace voxel

template <> struct std::hash<voxel::BlockPos> {
	size_t operator()(const voxel::BlockPos &p) const noexcept {
		return size_t(uint32_t(p.x) * 73856093u ^ uint32_t(p.y) * 19349663u ^ uint32_t(p.z) * 83492791u);
	}
};
template <> struct std::hash<voxel::ChunkPos> {
	size_t operator()(const voxel::ChunkPos &p) const noexcept {
		return size_t(uint32_t(p.x) * 73856093u ^ uint32_t(p.y) * 19349663u ^ uint32_t(p.z) * 83492791u);
	}
};
