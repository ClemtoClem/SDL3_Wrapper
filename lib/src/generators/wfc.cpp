// Définitions de generators/wfc.hpp
#include "generators/wfc.hpp"

namespace generators {

// ── WaveFunctionCollapse ─────────────────────────────────────────────────────

WaveFunctionCollapse::WaveFunctionCollapse(std::vector<WfcTile> tiles) : m_tiles(std::move(tiles)) {
	// compatible[d][a] : tuiles b pouvant être du côté d de a.
	const size_t n = m_tiles.size();
	for (int d = 0; d < 4; ++d) {
		m_compatible[d].assign(n, std::vector<bool>(n, false));
		const int opposite = (d + 2) % 4;
		for (size_t a = 0; a < n; ++a)
			for (size_t b = 0; b < n; ++b)
				m_compatible[d][a][b] = m_tiles[a].sides[d] == m_tiles[b].sides[opposite];
	}
}

Result<WfcResult, String> WaveFunctionCollapse::Run(int width, int height, uint64_t seed, int maxAttempts) {
	if (m_tiles.empty())
		return Err(String("WFC : aucune tuile"));
	if (width <= 0 || height <= 0 || size_t(width) * size_t(height) > 4000000)
		return Err(String("WFC : taille de grille invalide"));
	for (int attempt = 0; attempt < std::max(1, maxAttempts); ++attempt) {
		Rng rng(SplitMix64(seed + uint64_t(attempt)));
		if (Option<WfcResult> result = Attempt(width, height, rng); result.IsSome()) {
			WfcResult out = result.Unwrap();
			out.attempts = attempt + 1;
			return Ok(std::move(out));
		}
	}
	return Err(String::Format("WFC : contradiction à chacune des %d tentatives (règles trop strictes ?)", maxAttempts));
}

Option<WfcResult> WaveFunctionCollapse::Attempt(int width, int height, Rng &rng) {
	const size_t n = m_tiles.size(), cells = size_t(width) * size_t(height);
	std::vector<std::vector<bool>> possible(cells, std::vector<bool>(n, true));
	std::vector<int> count(cells, int(n));
	std::deque<size_t> queue;
	auto restrict = [&](size_t cell, int tile) {
		for (size_t t = 0; t < n; ++t)
			if (int(t) != tile && possible[cell][t]) {
				possible[cell][t] = false;
				--count[cell];
			}
		queue.push_back(cell);
	};
	auto propagate = [&]() -> bool {
		while (!queue.empty()) {
			const size_t cell = queue.front();
			queue.pop_front();
			const int x = int(cell % size_t(width)), y = int(cell / size_t(width));
			static constexpr int DX[4] = {0, 1, 0, -1}, DY[4] = {-1, 0, 1, 0};
			for (int d = 0; d < 4; ++d) {
				const int nx = x + DX[d], ny = y + DY[d];
				if (nx < 0 || ny < 0 || nx >= width || ny >= height)
					continue;
				const size_t neighbor = size_t(ny) * size_t(width) + size_t(nx);
				bool changed = false;
				for (size_t b = 0; b < n; ++b) {
					if (!possible[neighbor][b])
						continue;
					bool supported = false;
					for (size_t a = 0; a < n && !supported; ++a)
						supported = possible[cell][a] && m_compatible[d][a][b];
					if (!supported) {
						possible[neighbor][b] = false;
						--count[neighbor];
						changed = true;
					}
				}
				if (count[neighbor] == 0)
					return false;
				if (changed)
					queue.push_back(neighbor);
			}
		}
		return true;
	};
	for (const Fixed &f : m_fixed)
		if (f.x >= 0 && f.y >= 0 && f.x < width && f.y < height && f.tile >= 0 && size_t(f.tile) < n)
			restrict(size_t(f.y) * size_t(width) + size_t(f.x), f.tile);
	// Cohérence initiale : une tuile sans voisine compatible disparaît
	// partout (sinon une contradiction passerait inaperçue).
	for (size_t cell = 0; cell < cells; ++cell)
		queue.push_back(cell);
	if (!propagate())
		return NONE;
	for (;;) {
		// Case d'entropie minimale (au moins 2 possibilités), bruitée.
		size_t best = cells;
		double bestEntropy = 1e300;
		for (size_t cell = 0; cell < cells; ++cell) {
			if (count[cell] <= 1)
				continue;
			double sum = 0.0, sumLog = 0.0;
			for (size_t t = 0; t < n; ++t)
				if (possible[cell][t]) {
					const double w = std::max(1e-6f, m_tiles[t].weight);
					sum += w;
					sumLog += w * std::log(w);
				}
			const double entropy = std::log(sum) - sumLog / sum + rng.Float() * 1e-6;
			if (entropy < bestEntropy) {
				bestEntropy = entropy;
				best = cell;
			}
		}
		if (best == cells)
			break; // tout est fixé
		std::vector<double> weights(n, 0.0);
		for (size_t t = 0; t < n; ++t)
			if (possible[best][t])
				weights[t] = std::max(1e-6f, m_tiles[t].weight);
		restrict(best, int(rng.Weighted(weights)));
		if (!propagate())
			return NONE;
	}
	WfcResult out;
	out.width = width;
	out.height = height;
	out.tiles.resize(cells, 0);
	for (size_t cell = 0; cell < cells; ++cell)
		for (size_t t = 0; t < n; ++t)
			if (possible[cell][t]) {
				out.tiles[cell] = int(t);
				break;
			}
	return Some(std::move(out));
}

} // namespace generators
