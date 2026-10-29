#pragma once
/**
 * generators — dispersion d'objets (arbres, rochers, ennemis, trésors) :
 *
 *   - `PoissonDisk` : points aléatoires jamais plus proches que `radius`
 *     (Bridson, 2007) — une répartition « naturelle », sans amas ni trous ;
 *     une fonction de DENSITÉ (0..1) peut raréfier les points (forêt qui
 *     s'éclaircit vers la montagne : densité lue dans un masque de biome) ;
 *   - `JitteredGrid` : une grille dont chaque point est décalé au hasard —
 *     moins naturel, mais en O(n) et au nombre de points garanti.
 */
#include <algorithm>
#include <cmath>
#include <functional>
#include <vector>

#include "random.hpp"

namespace generators {

struct Point2 {
	float x = 0.f, y = 0.f;
};

/**
 * Échantillonnage de Poisson dans [0, width] × [0, height]. `density(x, y)`
 * (facultative) ∈ [0, 1] : probabilité de garder un point accepté.
 * `maxPoints` borne la sortie.
 */
[[nodiscard]] std::vector<Point2> PoissonDisk(float width, float height, float radius, uint64_t seed,
													 const std::function<float(float, float)> &density = nullptr,
													 int attempts = 30, size_t maxPoints = 100000);

/// Grille `spacing` dont chaque point est décalé de ± jitter × spacing / 2.
[[nodiscard]] std::vector<Point2> JitteredGrid(float width, float height, float spacing, float jitter,
													  uint64_t seed);

} // namespace generators
