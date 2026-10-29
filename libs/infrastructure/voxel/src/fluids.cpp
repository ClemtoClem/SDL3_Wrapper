// Définitions de voxel/fluids.hpp
#include <voxel/fluids.hpp>

#include <algorithm>
#include <cmath>

namespace voxel {

namespace {

constexpr BlockPos SIDES[4] = {{1, 0, 0}, {-1, 0, 0}, {0, 0, 1}, {0, 0, -1}};
constexpr BlockPos AROUND[6] = {{1, 0, 0}, {-1, 0, 0}, {0, 0, 1}, {0, 0, -1}, {0, 1, 0}, {0, -1, 0}};
constexpr BlockPos UP{0, 1, 0}, DOWN{0, -1, 0};

bool Flows(const BlockDef &def) { return def.fluid && def.liquid.flows; }

} // namespace

float LiquidHeight(const BlockDef &def, uint8_t param, bool coveredBySame) {
	if (coveredBySame)
		return 1.f;
	if (LiquidSource(param) || LiquidFalling(param))
		return 0.875f;
	const int range = std::max(1, def.liquid.range);
	const int level = std::min(LiquidLevel(param), range);
	return std::max(0.125f, 0.875f * float(range + 1 - level) / float(range + 1));
}

math::FVector3 LiquidFlow(const World &world, const BlockPos &p) {
	const BlockDef &def = world.BlockAt(p);
	if (!Flows(def))
		return {0.f, 0.f, 0.f};
	const uint8_t param = world.GetParam(p);
	if (LiquidFalling(param))
		return {0.f, -1.f, 0.f};
	const float here = LiquidHeight(def, param, false);
	math::FVector3 flow{0.f, 0.f, 0.f};
	for (const BlockPos &d : SIDES) {
		const BlockPos n = p + d;
		if (!world.IsLoaded(n))
			continue;
		const BlockDef &nd = world.BlockAt(n);
		float drop = 0.f;
		if (&nd == &def) {
			drop = here - LiquidHeight(nd, world.GetParam(n), false); // vers le plus bas
		} else if (nd.IsAir() || (nd.replaceable && !nd.fluid)) {
			const BlockDef &below = world.BlockAt(n + DOWN);
			drop = (below.IsAir() || &below == &def) ? here + 1.f : here; // une chute attire davantage
		}
		flow.x += float(d.x) * drop;
		flow.z += float(d.z) * drop;
	}
	const float length = std::hypot(flow.x, flow.z);
	return length > 1e-4f ? math::FVector3{flow.x / length, 0.f, flow.z / length} : math::FVector3{0.f, 0.f, 0.f};
}

// ── Simulation ───────────────────────────────────────────────────────────────

FluidSimulation::FluidSimulation(World &world) : m_world(world) {
	m_changed = m_world.onBlockChanged.Connect([this](const BlockChange &c) {
		if (c.cause != ChangeCause::GENERATION)
			ScheduleAround(c.pos);
	});
	// Un chunk qui revient (sauvegarde) : ses liquides en cours d'écoulement repartent.
	m_loaded = m_world.onChunkLoaded.Connect([this](const ChunkPos &cp) {
		const Chunk *chunk = m_world.GetChunk(cp);
		if (!chunk || chunk->IsUniform())
			return;
		const ContentRegistry &content = m_world.Content();
		const BlockPos origin = cp.Origin();
		for (int y = 0; y < CHUNK_SIZE; ++y)
			for (int z = 0; z < CHUNK_SIZE; ++z)
				for (int x = 0; x < CHUNK_SIZE; ++x)
					if (Flows(content.Block(chunk->Get(x, y, z))) && !LiquidSource(chunk->Param(x, y, z)))
						Schedule(origin + BlockPos{x, y, z});
	});
}

FluidSimulation::~FluidSimulation() = default;

void FluidSimulation::Schedule(const BlockPos &p) {
	if (m_pending.contains(p) || !m_world.IsLoaded(p))
		return;
	// Pas de liquide qui coule ici ni autour : rien à faire.
	float tick = -1.f;
	auto consider = [&](const BlockPos &q) {
		if (!m_world.IsLoaded(q))
			return;
		if (const BlockDef &d = m_world.BlockAt(q); Flows(d))
			tick = tick < 0.f ? d.liquid.tick : std::min(tick, d.liquid.tick);
	};
	consider(p);
	for (const BlockPos &d : AROUND)
		consider(p + d);
	if (tick < 0.f)
		return;
	m_pending.insert(p);
	m_queue.emplace(m_time + double(tick), p);
}

int FluidSimulation::Disturb(const Box &region) {
	const size_t before = m_pending.size();
	const BlockPos lo{int(std::floor(region.min.x)) - 1, int(std::floor(region.min.y)) - 1, int(std::floor(region.min.z)) - 1};
	const BlockPos hi{int(std::floor(region.max.x)) + 1, int(std::floor(region.max.y)) + 1, int(std::floor(region.max.z)) + 1};
	for (int y = lo.y; y <= hi.y; ++y)
		for (int z = lo.z; z <= hi.z; ++z)
			for (int x = lo.x; x <= hi.x; ++x)
				if (m_world.IsLoaded({x, y, z}) && Flows(m_world.BlockAt({x, y, z})))
					ScheduleAround({x, y, z}); // le liquide et les cellules où il pourrait aller
	return int(m_pending.size() - before);
}

void FluidSimulation::ScheduleAround(const BlockPos &p) {
	Schedule(p);
	for (const BlockPos &d : AROUND)
		Schedule(p + d);
}

int FluidSimulation::Step(float dt, int budget) {
	m_time += double(dt);
	int changed = 0;
	for (int done = 0; done < budget && !m_queue.empty() && m_queue.begin()->first <= m_time; ++done) {
		const BlockPos p = m_queue.begin()->second;
		m_queue.erase(m_queue.begin());
		m_pending.erase(p);
		const BlockId before = m_world.IsLoaded(p) ? m_world.GetBlock(p) : AIR;
		const uint8_t param = m_world.IsLoaded(p) ? m_world.GetParam(p) : 0;
		Update(p);
		changed += m_world.IsLoaded(p) && (m_world.GetBlock(p) != before || m_world.GetParam(p) != param);
	}
	return changed;
}

int FluidSimulation::Settle(float maxSeconds) {
	int changed = 0;
	for (float t = 0.f; t < maxSeconds && !m_queue.empty(); t += 0.05f)
		changed += Step(0.05f, 1 << 20);
	return changed;
}

bool FluidSimulation::CanFill(const BlockDef &target, uint8_t targetParam, const BlockDef &liquid) const {
	if (target.IsAir())
		return true;
	if (target.fluid)
		return &target == &liquid && !LiquidSource(targetParam); // le même liquide, qui coule
	return target.replaceable;
}

bool FluidSimulation::Spreads(const BlockPos &p, const BlockDef &def, uint8_t param) const {
	if (LiquidSource(param))
		return true; // une source s'étale toujours
	const BlockPos below = p + DOWN;
	if (!m_world.IsLoaded(below))
		return true;
	// Tant qu'il peut descendre, le liquide ne s'étale pas.
	return !CanFill(m_world.BlockAt(below), m_world.GetParam(below), def);
}

FluidSimulation::Desired FluidSimulation::Compute(const BlockPos &p, const BlockDef &here, uint8_t param) const {
	// D'abord le dessus : une colonne qui tombe.
	if (m_world.IsLoaded(p + UP))
		if (const BlockDef &above = m_world.BlockAt(p + UP); Flows(above) && CanFill(here, param, above))
			return {&above, LiquidParam(0, true)};
	// Puis les côtés : le niveau le plus bas (le plus près d'une source) l'emporte.
	const BlockDef *best = nullptr;
	int bestLevel = MAX_LIQUID_RANGE + 1, sources = 0;
	for (const BlockPos &d : SIDES) {
		const BlockPos n = p + d;
		if (!m_world.IsLoaded(n))
			continue;
		const BlockDef &nd = m_world.BlockAt(n);
		const uint8_t np = m_world.GetParam(n);
		if (!Flows(nd) || !CanFill(here, param, nd))
			continue;
		if (LiquidSource(np) && nd.liquid.renewable)
			++sources;
		if (!Spreads(n, nd, np))
			continue;
		const int level = LiquidSource(np) || LiquidFalling(np) ? 1 : LiquidLevel(np) + 1;
		if (level <= nd.liquid.range && level < bestLevel) {
			bestLevel = level;
			best = &nd;
		}
	}
	if (!best)
		return {};
	// Source renouvelable : entre deux sources, sur un sol (ou une source).
	if (best->liquid.renewable && sources >= 2 && m_world.IsLoaded(p + DOWN)) {
		const BlockDef &below = m_world.BlockAt(p + DOWN);
		const uint8_t bp = m_world.GetParam(p + DOWN);
		if ((&below == best && LiquidSource(bp)) || (!below.IsAir() && !below.fluid && !below.replaceable))
			return {best, LiquidParam(0, false)};
	}
	return {best, LiquidParam(bestLevel, false)};
}

bool FluidSimulation::Mix(const BlockPos &p, const BlockDef &def, uint8_t param) {
	for (const LiquidProps::Mix &mix : def.liquid.mixes)
		for (const BlockPos &d : AROUND) {
			const BlockPos n = p + d;
			if (!m_world.IsLoaded(n) || m_world.BlockAt(n).id != mix.with)
				continue;
			const String &into = LiquidSource(param) ? mix.source : mix.flowing;
			if (const BlockDef *result = m_world.Content().FindBlock(into.IsEmpty() ? mix.source : into)) {
				(void)m_world.SetBlock(p, result->numeric, 0, ChangeCause::SIMULATION);
				return true;
			}
		}
	return false;
}

void FluidSimulation::Update(const BlockPos &p) {
	if (!m_world.IsLoaded(p))
		return;
	const BlockDef &here = m_world.BlockAt(p);
	const uint8_t param = m_world.GetParam(p);
	if (here.fluid) {
		if (!here.liquid.flows)
			return;
		if (Mix(p, here, param))
			return;
		if (LiquidSource(param))
			return; // une source ne bouge pas d'elle-même
	} else if (!here.IsAir() && !here.replaceable) {
		return;
	}
	const Desired want = Compute(p, here, param);
	if (!want.def) {
		if (here.fluid) // plus rien ne l'alimente : il se retire
			(void)m_world.SetBlock(p, AIR, 0, ChangeCause::SIMULATION);
		return;
	}
	if (&here == want.def && (param & ~3u) == want.param)
		return;
	if (!here.IsAir() && !here.fluid && onWashedAway)
		onWashedAway(p, here);
	(void)m_world.SetBlock(p, want.def->numeric, want.param, ChangeCause::SIMULATION);
	(void)Mix(p, *want.def, want.param);
}

} // namespace voxel
