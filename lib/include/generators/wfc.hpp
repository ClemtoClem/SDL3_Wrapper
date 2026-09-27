#pragma once
/**
 * generators — Wave Function Collapse (modèle « par tuiles ») : remplir une
 * grille de tuiles qui respectent des règles d'adjacence, par propagation
 * de contraintes — cartes, niveaux, textures à motifs.
 *
 *   1. Chaque case peut être n'importe quelle tuile (superposition).
 *   2. On FIXE la case la moins incertaine (entropie minimale), au hasard
 *      pondéré parmi ses possibilités.
 *   3. On PROPAGE : chaque voisine perd les tuiles incompatibles.
 *   4. Contradiction (une case sans possibilité) : on recommence avec une
 *      autre graine, jusqu'à `maxAttempts` ; sinon, échec explicite.
 *
 * Les règles s'écrivent par « prises » : chaque tuile a une étiquette par
 * côté (haut, droite, bas, gauche) ; deux tuiles se touchent si les
 * étiquettes en regard sont égales (ex. « herbe » contre « herbe »).
 */
#include <algorithm>
#include <cmath>
#include <deque>
#include <vector>

#include "../core/core.hpp"
#include "random.hpp"

namespace generators {

struct WfcTile {
	String name;
	/// Étiquettes des côtés : 0 haut, 1 droite, 2 bas, 3 gauche.
	String sides[4];
	float weight = 1.f;
};

struct WfcResult {
	int width = 0, height = 0;
	std::vector<int> tiles; ///< indice de tuile par case (ligne par ligne)
	int attempts = 0;
	[[nodiscard]] int At(int x, int y) const noexcept { return tiles[size_t(y) * size_t(width) + size_t(x)]; }
};

class WaveFunctionCollapse {
public:
	explicit WaveFunctionCollapse(std::vector<WfcTile> tiles);

	[[nodiscard]] const std::vector<WfcTile> &Tiles() const noexcept { return m_tiles; }

	/// Fixe à l'avance la tuile d'une case (bordures, entrée…).
	void Constrain(int x, int y, int tile) { m_fixed.push_back({x, y, tile}); }

	[[nodiscard]] Result<WfcResult, String> Run(int width, int height, uint64_t seed, int maxAttempts = 10);

private:
	struct Fixed {
		int x, y, tile;
	};

	[[nodiscard]] Option<WfcResult> Attempt(int width, int height, Rng &rng);

	std::vector<WfcTile> m_tiles;
	std::vector<std::vector<bool>> m_compatible[4];
	std::vector<Fixed> m_fixed;
};

} // namespace generators
