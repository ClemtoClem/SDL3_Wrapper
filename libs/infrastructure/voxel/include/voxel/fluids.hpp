#pragma once
/**
 * Liquides — écoulement de l'eau, de la lave et des autres fluides, à la
 * manière de Minecraft / Minetest.
 *
 * État d'une cellule liquide (dans le param du bloc, bits 2 à 6 : les bits
 * 0-1 de l'orientation restent libres, une structure tournée garde ses
 * niveaux) :
 *   niveau 0      SOURCE : ne s'écoule jamais d'elle-même ;
 *   niveau 1..N   liquide qui coule, à N cellules au plus d'une source
 *                 (« liquid.range ») ;
 *   « tombant »   alimenté par le dessus (colonne de chute).
 *
 * Règles (un pas toutes les « liquid.tick » secondes, par liquide) :
 *   - le liquide tombe d'abord dans le vide sous lui ;
 *   - il ne s'étale sur les côtés que s'il ne peut pas descendre (une source
 *     s'étale toujours), d'une cellule à la fois, le niveau croissant ;
 *   - privé de ce qui l'alimente, il se retire ;
 *   - deux sources voisines au-dessus d'un sol (« liquid.renewable ») en
 *     créent une troisième ;
 *   - au contact d'un autre liquide (« liquid.mix » : lave + eau), il se
 *     fige : « source » pour une source, « flowing » sinon (obsidienne, pierre).
 * Le liquide emporte l'air et les blocs « replaceable » (herbes hautes) ; les
 * autres blocs l'arrêtent. Les chunks non chargés aussi.
 *
 * Seules les modifications du monde (joueur, scripts…) et les corps qui
 * entrent dans un liquide (Disturb) déclenchent l'écoulement : les lacs et
 * océans générés restent calmes tant que rien ne les dérange. Un chunk chargé
 * reprend l'écoulement de ses liquides en cours.
 */
#include <functional>
#include <map>
#include <set>
#include <vector>

#include "world.hpp"

namespace voxel {

// ── État d'une cellule ───────────────────────────────────────────────────────

inline constexpr int MAX_LIQUID_RANGE = 15;
[[nodiscard]] constexpr int LiquidLevel(uint8_t param) noexcept { return (param >> 2) & 0x0F; }
[[nodiscard]] constexpr bool LiquidFalling(uint8_t param) noexcept { return (param & 0x40) != 0; }
[[nodiscard]] constexpr bool LiquidSource(uint8_t param) noexcept { return LiquidLevel(param) == 0 && !LiquidFalling(param); }
[[nodiscard]] constexpr uint8_t LiquidParam(int level, bool falling) noexcept {
	return uint8_t(((level & 0x0F) << 2) | (falling ? 0x40 : 0));
}

/// Hauteur de la surface d'une cellule liquide (0..1) : source 7/8,
/// décroissante avec le niveau, pleine si le même liquide est au-dessus.
[[nodiscard]] float LiquidHeight(const BlockDef &def, uint8_t param, bool coveredBySame);

/// Sens du courant dans la cellule `p` (horizontal, normé ; y < 0 : chute) ;
/// nul dans une source sans pente ou hors d'un liquide.
[[nodiscard]] math::FVector3 LiquidFlow(const World &world, const BlockPos &p);

// ── Simulation ───────────────────────────────────────────────────────────────

struct BlockPosOrder {
	bool operator()(const BlockPos &a, const BlockPos &b) const noexcept {
		return a.x != b.x ? a.x < b.x : a.y != b.y ? a.y < b.y : a.z < b.z;
	}
};

class FluidSimulation {
public:
	/// Se branche sur `world.onBlockChanged` et `onChunkLoaded`.
	explicit FluidSimulation(World &world);
	~FluidSimulation();
	FluidSimulation(const FluidSimulation &) = delete;
	FluidSimulation &operator=(const FluidSimulation &) = delete;

	/// Avance l'horloge de `dt` et traite les cellules arrivées à échéance
	/// (au plus `budget`) ; rend le nombre de cellules modifiées.
	int Step(float dt, int budget = 4096);
	/// Programme la cellule `p` (et rien si aucun liquide n'est autour).
	void Schedule(const BlockPos &p);
	/// Un corps (joueur, créature, bloc qui tombe…) remue le liquide : les
	/// cellules de `region` et leurs voisines sont réexaminées — un lac généré
	/// instable se remet à couler dès qu'on y entre. Rend le nombre de
	/// cellules programmées.
	int Disturb(const Box &region);
	/// Cellules en attente.
	[[nodiscard]] size_t Pending() const noexcept { return m_pending.size(); }
	/// Tout traiter jusqu'au calme (tests, outils) ; rend les modifications.
	int Settle(float maxSeconds = 120.f);

	/// Appelé quand le liquide emporte un bloc (herbe haute…) : butin éventuel.
	std::function<void(const BlockPos &, const BlockDef &)> onWashedAway;

private:
	struct Desired {
		const BlockDef *def = nullptr; ///< nul : vide
		uint8_t param = 0;
	};
	void Update(const BlockPos &p);
	[[nodiscard]] Desired Compute(const BlockPos &p, const BlockDef &here, uint8_t param) const;
	[[nodiscard]] bool CanFill(const BlockDef &target, uint8_t targetParam, const BlockDef &liquid) const;
	[[nodiscard]] bool Spreads(const BlockPos &p, const BlockDef &def, uint8_t param) const;
	/// Figer `p` (liquide `def`) au contact d'un autre liquide ; vrai si fait.
	bool Mix(const BlockPos &p, const BlockDef &def, uint8_t param);
	void ScheduleAround(const BlockPos &p);

	World &m_world;
	double m_time = 0.0;
	std::multimap<double, BlockPos> m_queue;
	std::set<BlockPos, BlockPosOrder> m_pending;
	events::ScopedConnection m_changed, m_loaded;
};

} // namespace voxel
