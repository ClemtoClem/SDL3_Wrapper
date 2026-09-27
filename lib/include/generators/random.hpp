#pragma once
/**
 * generators — hasard REPRODUCTIBLE, fondation de toute la génération
 * procédurale du module : même graine, même monde, sur toutes les machines.
 *
 *   - `Rng` : générateur PCG32 (O'Neill) — petit, rapide, statistiquement
 *     solide, et surtout défini au bit près (contrairement aux distributions
 *     de <random>, dont le résultat dépend de l'implémentation) ;
 *   - `Hash*` : hachages de coordonnées entières (bruit, placement d'objets)
 *     — le hasard « sans état » : la même case donne toujours la même valeur,
 *     quel que soit l'ordre de visite.
 */
#include <cmath>
#include <cstdint>
#include <utility>
#include <vector>

namespace generators {

/// Mélange 64 bits (SplitMix64) : dérive des graines indépendantes.
[[nodiscard]] constexpr uint64_t SplitMix64(uint64_t x) noexcept {
	x += 0x9E3779B97F4A7C15ull;
	x = (x ^ (x >> 30)) * 0xBF58476D1CE4E5B9ull;
	x = (x ^ (x >> 27)) * 0x94D049BB133111EBull;
	return x ^ (x >> 31);
}

/// Hachage de coordonnées entières (et d'une graine) vers 32 bits.
[[nodiscard]] constexpr uint32_t Hash2(int32_t x, int32_t y, uint32_t seed) noexcept {
	uint64_t h = SplitMix64((uint64_t(uint32_t(x)) << 32) ^ uint64_t(uint32_t(y)) ^ (uint64_t(seed) << 17));
	return uint32_t(h ^ (h >> 32));
}
[[nodiscard]] constexpr uint32_t Hash3(int32_t x, int32_t y, int32_t z, uint32_t seed) noexcept {
	uint64_t h = SplitMix64((uint64_t(uint32_t(x)) << 32) ^ uint64_t(uint32_t(y)));
	h = SplitMix64(h ^ (uint64_t(uint32_t(z)) << 21) ^ seed);
	return uint32_t(h ^ (h >> 32));
}
/// Même chose, ramené dans [0, 1[.
[[nodiscard]] constexpr float HashFloat2(int32_t x, int32_t y, uint32_t seed) noexcept {
	return float(Hash2(x, y, seed) >> 8) * (1.f / 16777216.f);
}
[[nodiscard]] constexpr float HashFloat3(int32_t x, int32_t y, int32_t z, uint32_t seed) noexcept {
	return float(Hash3(x, y, z, seed) >> 8) * (1.f / 16777216.f);
}

/**
 * PCG32 (XSH-RR) : 64 bits d'état, sorties de 32 bits. Toutes les
 * distributions sont écrites ici (pas celles de <random>) : le résultat est
 * identique partout.
 */
class Rng {
public:
	explicit constexpr Rng(uint64_t seed = 0x853C49E6748FEA9Bull, uint64_t stream = 0xDA3E39CB94B95BDBull) noexcept {
		Seed(seed, stream);
	}

	constexpr void Seed(uint64_t seed, uint64_t stream = 0xDA3E39CB94B95BDBull) noexcept {
		m_state = 0;
		m_inc = (stream << 1u) | 1u;
		Next();
		m_state += SplitMix64(seed);
		Next();
	}

	/// 32 bits uniformes.
	constexpr uint32_t Next() noexcept {
		const uint64_t old = m_state;
		m_state = old * 6364136223846793005ull + m_inc;
		const uint32_t xorshifted = uint32_t(((old >> 18u) ^ old) >> 27u);
		const uint32_t rot = uint32_t(old >> 59u);
		return (xorshifted >> rot) | (xorshifted << ((32u - rot) & 31u));
	}
	constexpr uint64_t Next64() noexcept { return (uint64_t(Next()) << 32) | Next(); }

	/// Réel dans [0, 1[.
	constexpr double Float() noexcept { return double(Next64() >> 11) * (1.0 / 9007199254740992.0); }
	/// Réel dans [lo, hi[.
	constexpr double Range(double lo, double hi) noexcept { return lo + (hi - lo) * Float(); }
	/// Entier dans [lo, hi] (bornes incluses), sans biais (rejet de Lemire).
	constexpr int64_t Int(int64_t lo, int64_t hi) noexcept {
		if (hi <= lo)
			return lo;
		const uint64_t span = uint64_t(hi - lo) + 1u;
		if (span == 0) // [INT64_MIN, INT64_MAX]
			return int64_t(Next64());
		const uint64_t limit = (~uint64_t(0)) - ((~uint64_t(0)) % span);
		uint64_t draw = Next64();
		while (draw >= limit)
			draw = Next64();
		return lo + int64_t(draw % span);
	}
	/// Vrai avec la probabilité `p`.
	constexpr bool Chance(double p) noexcept { return Float() < p; }
	/// Loi normale (Box-Muller).
	double Normal(double mean = 0.0, double stddev = 1.0) noexcept;
	/// Indice tiré selon des poids positifs (-1 si tous nuls).
	int64_t Weighted(const std::vector<double> &weights) noexcept;
	/// Mélange de Fisher-Yates.
	template <typename T> void Shuffle(std::vector<T> &items) noexcept {
		for (size_t i = items.size(); i > 1; --i)
			std::swap(items[i - 1], items[size_t(Int(0, int64_t(i - 1)))]);
	}
	/// Point uniforme dans le disque unité.
	std::pair<double, double> InDisk() noexcept;

private:
	uint64_t m_state = 0;
	uint64_t m_inc = 1;
};

} // namespace generators
