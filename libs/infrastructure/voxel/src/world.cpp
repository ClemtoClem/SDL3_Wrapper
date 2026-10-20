// Définitions de voxel/world.hpp
#include <voxel/world.hpp>

#include <algorithm>
#include <cmath>

namespace voxel {

// ── Chunk ────────────────────────────────────────────────────────────────────

void Chunk::Set(int x, int y, int z, BlockId id, uint8_t param) {
	const size_t i = size_t(LocalIndex(x, y, z));
	if (m_blocks.empty()) {
		if (id == m_uniform && param == 0)
			return;
		m_blocks.assign(CHUNK_VOLUME, m_uniform);
		m_nonAir = m_uniform == AIR ? 0 : CHUNK_VOLUME;
	}
	const BlockId before = m_blocks[i];
	m_nonAir += int(id != AIR) - int(before != AIR);
	m_blocks[i] = id;
	if (param != 0 || !m_params.empty()) {
		if (m_params.empty())
			m_params.assign(CHUNK_VOLUME, 0);
		m_params[i] = param;
	}
	++m_revision;
}

void Chunk::Fill(BlockId id) {
	m_blocks.clear();
	m_params.clear();
	m_uniform = id;
	m_nonAir = id == AIR ? 0 : CHUNK_VOLUME;
	++m_revision;
}

std::vector<BlockId> Chunk::Blocks() const {
	return m_blocks.empty() ? std::vector<BlockId>(CHUNK_VOLUME, m_uniform) : m_blocks;
}

void Chunk::Assign(std::vector<BlockId> blocks, std::vector<uint8_t> params) {
	if (blocks.size() != size_t(CHUNK_VOLUME))
		return;
	const bool uniform = std::all_of(blocks.begin(), blocks.end(), [&](BlockId b) { return b == blocks[0]; }) &&
						 std::all_of(params.begin(), params.end(), [](uint8_t p) { return p == 0; });
	if (uniform) {
		Fill(blocks[0]);
		return;
	}
	m_blocks = std::move(blocks);
	m_params = params.size() == size_t(CHUNK_VOLUME) ? std::move(params) : std::vector<uint8_t>{};
	m_nonAir = int(std::count_if(m_blocks.begin(), m_blocks.end(), [](BlockId b) { return b != AIR; }));
	++m_revision;
}

// ── World ────────────────────────────────────────────────────────────────────

World::World(const ContentRegistry &content, WorldSettings settings) : m_content(content), m_settings(std::move(settings)) {}

World::~World() = default;

Chunk *World::GetChunk(const ChunkPos &p) {
	auto it = m_chunks.find(p);
	return it == m_chunks.end() ? nullptr : it->second.get();
}

const Chunk *World::GetChunk(const ChunkPos &p) const {
	auto it = m_chunks.find(p);
	return it == m_chunks.end() ? nullptr : it->second.get();
}

BlockId World::GetBlock(const BlockPos &p) const {
	const Chunk *chunk = GetChunk(ChunkOf(p));
	if (!chunk)
		return AIR;
	const BlockPos l = LocalOf(p);
	return chunk->Get(l.x, l.y, l.z);
}

uint8_t World::GetParam(const BlockPos &p) const {
	const Chunk *chunk = GetChunk(ChunkOf(p));
	if (!chunk)
		return 0;
	const BlockPos l = LocalOf(p);
	return chunk->Param(l.x, l.y, l.z);
}

Chunk &World::EnsureChunk(const ChunkPos &p) {
	if (Chunk *existing = GetChunk(p))
		return *existing;
	auto owned = std::make_unique<Chunk>(p);
	Chunk &chunk = *owned;
	m_chunks.emplace(p, std::move(owned));
	const bool loaded = m_storage && m_storage->LoadChunk(chunk);
	if (!loaded) {
		if (m_generator) {
			m_generatingStack.push_back(p);
			m_generator->Generate(chunk, *this);
			m_generatingStack.pop_back();
			++m_generatedCount;
		}
		// Écritures de structures voisines arrivées avant ce chunk.
		if (auto pending = m_pending.find(p); pending != m_pending.end()) {
			for (const PendingWrite &w : pending->second) {
				const int x = w.index % CHUNK_SIZE, z = (w.index / CHUNK_SIZE) % CHUNK_SIZE, y = w.index / (CHUNK_SIZE * CHUNK_SIZE);
				if (w.replaceSolid || !m_content.Block(chunk.Get(x, y, z)).solid)
					chunk.Set(x, y, z, w.id);
			}
			m_pending.erase(pending);
		}
	} else {
		m_pending.erase(p); // un chunk enregistré contient déjà tout
	}
	onChunkLoaded.Emit(p);
	return chunk;
}

bool World::SetBlock(const BlockPos &p, const String &id, uint8_t param, ChangeCause cause) {
	const BlockDef *def = m_content.FindBlock(id);
	return def && SetBlock(p, def->numeric, param, cause);
}

bool World::SetBlock(const BlockPos &p, BlockId id, uint8_t param, ChangeCause cause) {
	Chunk &chunk = EnsureChunk(ChunkOf(p));
	const BlockPos l = LocalOf(p);
	const BlockId previous = chunk.Get(l.x, l.y, l.z);
	if (previous == id && chunk.Param(l.x, l.y, l.z) == param)
		return false;
	chunk.Set(l.x, l.y, l.z, id, param);
	if (cause != ChangeCause::GENERATION)
		chunk.SetModified(true);
	TouchNeighbors(p);
	onBlockChanged.Emit(BlockChange{p, previous, id, param, cause});
	// Le bloc posé, et celui posé au-dessus, peuvent devenir instables.
	CheckStability(p);
	CheckStability(p + BlockPos{0, 1, 0});
	return true;
}

void World::GenerationWrite(const BlockPos &p, BlockId id, bool replaceSolid) {
	const ChunkPos cp = ChunkOf(p);
	const BlockPos l = LocalOf(p);
	if (Chunk *chunk = GetChunk(cp)) {
		if (!replaceSolid && m_content.Block(chunk->Get(l.x, l.y, l.z)).solid)
			return;
		chunk->Set(l.x, l.y, l.z, id);
		// Un chunk DÉJÀ généré qui reçoit une écriture tardive (structure
		// d'un voisin) doit la conserver s'il est déchargé puis régénéré.
		if (m_generatingStack.empty() || !(m_generatingStack.back() == cp))
			chunk->SetModified(true);
		TouchNeighbors(p);
		return;
	}
	m_pending[cp].push_back(PendingWrite{LocalIndex(l.x, l.y, l.z), id, replaceSolid});
}

void World::PlaceStructure(const StructureDef &structure, const BlockPos &origin, int rotation, bool generation) {
	rotation = ((rotation % 4) + 4) % 4;
	for (const auto &[rel, blockId] : structure.blocks) {
		BlockPos r = rel - structure.anchor;
		for (int i = 0; i < rotation; ++i)
			r = {-r.z, r.y, r.x};
		const BlockId id = m_content.BlockIdOf(blockId);
		if (generation)
			GenerationWrite(origin + r, id, structure.replaceSolid);
		else
			(void)SetBlock(origin + r, id, 0, ChangeCause::SCRIPT);
	}
}

Option<int> World::HighestSolid(int x, int z, int fromY, int toY) const {
	for (int y = toY; y >= fromY; --y)
		if (IsLoaded({x, y, z}) && BlockAt({x, y, z}).solid)
			return Some(y);
	return NONE;
}

void World::TouchNeighbors(const BlockPos &p) {
	const BlockPos l = LocalOf(p);
	const ChunkPos c = ChunkOf(p);
	auto touch = [&](int dx, int dy, int dz) {
		if (Chunk *n = GetChunk({c.x + dx, c.y + dy, c.z + dz}))
			n->Touch();
	};
	if (l.x == 0)
		touch(-1, 0, 0);
	if (l.x == CHUNK_SIZE - 1)
		touch(1, 0, 0);
	if (l.y == 0)
		touch(0, -1, 0);
	if (l.y == CHUNK_SIZE - 1)
		touch(0, 1, 0);
	if (l.z == 0)
		touch(0, 0, -1);
	if (l.z == CHUNK_SIZE - 1)
		touch(0, 0, 1);
}

void World::CheckStability(const BlockPos &p) {
	if (!IsLoaded(p))
		return;
	const BlockDef &def = BlockAt(p);
	if (!def.falling && !def.requiresSupport)
		return;
	const BlockPos below = p + BlockPos{0, -1, 0};
	if (!IsLoaded(below))
		return;
	const BlockDef &support = BlockAt(below);
	if (support.solid && !support.fluid)
		return;
	if (m_unstableSet.insert(p).second)
		m_unstable.push_back(p);
}

std::vector<BlockPos> World::TakeUnstableBlocks() {
	std::vector<BlockPos> out(m_unstable.begin(), m_unstable.end());
	m_unstable.clear();
	m_unstableSet.clear();
	return out;
}

size_t World::UpdateLoadedArea(const std::vector<BlockPos> &anchors, int radius, int verticalRadius, size_t budget) {
	// Chunks voulus, triés par distance à l'ancre la plus proche.
	std::vector<std::pair<int, ChunkPos>> wanted;
	std::unordered_set<ChunkPos> keep;
	for (const BlockPos &anchor : anchors) {
		const ChunkPos c = ChunkOf(anchor);
		for (int dy = -verticalRadius; dy <= verticalRadius; ++dy)
			for (int dz = -radius; dz <= radius; ++dz)
				for (int dx = -radius; dx <= radius; ++dx) {
					if (dx * dx + dz * dz > radius * radius + radius)
						continue;
					const ChunkPos p{c.x + dx, c.y + dy, c.z + dz};
					if (keep.insert(p).second && !m_chunks.contains(p))
						wanted.push_back({dx * dx + dz * dz + dy * dy * 2, p});
				}
	}
	std::sort(wanted.begin(), wanted.end(), [](const auto &a, const auto &b) {
		return a.first != b.first ? a.first < b.first : a.second < b.second;
	});
	size_t loaded = 0;
	for (const auto &[distance, p] : wanted) {
		if (loaded >= budget)
			break;
		(void)EnsureChunk(p);
		++loaded;
	}
	// Déchargement : au-delà du rayon (+1 de marge, pour éviter le va-et-vient).
	std::vector<ChunkPos> far;
	for (const auto &[p, chunk] : m_chunks) {
		bool near = false;
		for (const BlockPos &anchor : anchors) {
			const ChunkPos c = ChunkOf(anchor);
			const int dx = p.x - c.x, dy = p.y - c.y, dz = p.z - c.z;
			near = near || (dx * dx + dz * dz <= (radius + 1) * (radius + 1) + radius + 1 && std::abs(dy) <= verticalRadius + 1);
		}
		if (!near)
			far.push_back(p);
	}
	for (const ChunkPos &p : far)
		UnloadChunk(p);
	return loaded;
}

void World::UnloadChunk(const ChunkPos &p) {
	auto it = m_chunks.find(p);
	if (it == m_chunks.end())
		return;
	if (it->second->IsModified() && m_storage) {
		m_storage->SaveChunk(*it->second);
		it->second->SetModified(false);
	}
	m_chunks.erase(it);
	onChunkUnloaded.Emit(p);
}

std::vector<ChunkPos> World::LoadedChunks() const {
	std::vector<ChunkPos> out;
	out.reserve(m_chunks.size());
	for (const auto &[p, chunk] : m_chunks)
		out.push_back(p);
	std::sort(out.begin(), out.end());
	return out;
}

size_t World::SaveModifiedChunks() {
	if (!m_storage)
		return 0;
	size_t count = 0;
	for (auto &[p, chunk] : m_chunks)
		if (chunk->IsModified()) {
			m_storage->SaveChunk(*chunk);
			chunk->SetModified(false);
			++count;
		}
	return count;
}

} // namespace voxel
