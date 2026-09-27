// Définitions de generators/scatter.hpp — fichier généré par splitter.py : le code
// vient tel quel de l'en-tête (seules les signatures sont réécrites).

#include "generators/scatter.hpp"

namespace generators {

std::vector<Point2> PoissonDisk(float width, float height, float radius, uint64_t seed,
		const std::function<float(float, float)> &density,
		int attempts, size_t maxPoints) {
	std::vector<Point2> points;
	if (width <= 0.f || height <= 0.f || radius <= 0.f)
		return points;
	Rng rng(seed);
	const float cell = radius / std::sqrt(2.f);
	const int cols = int(std::ceil(width / cell)), rows = int(std::ceil(height / cell));
	if (double(cols) * double(rows) > 5e7)
		return points; // rayon absurde face à la surface : on refuse plutôt que d'épuiser la mémoire
	std::vector<int> grid(size_t(cols) * size_t(rows), -1);
	std::vector<Point2> accepted;
	std::vector<size_t> active;
	auto fits = [&](float x, float y) {
		if (x < 0.f || y < 0.f || x >= width || y >= height)
			return false;
		const int gx = int(x / cell), gy = int(y / cell);
		for (int j = std::max(0, gy - 2); j <= std::min(rows - 1, gy + 2); ++j)
			for (int i = std::max(0, gx - 2); i <= std::min(cols - 1, gx + 2); ++i) {
				const int k = grid[size_t(j) * size_t(cols) + size_t(i)];
				if (k >= 0) {
					const float dx = accepted[size_t(k)].x - x, dy = accepted[size_t(k)].y - y;
					if (dx * dx + dy * dy < radius * radius)
						return false;
				}
			}
		return true;
	};
	auto add = [&](float x, float y) {
		grid[size_t(int(y / cell)) * size_t(cols) + size_t(int(x / cell))] = int(accepted.size());
		accepted.push_back({x, y});
		active.push_back(accepted.size() - 1);
	};
	add(float(rng.Range(0.0, width)), float(rng.Range(0.0, height)));
	while (!active.empty() && accepted.size() < maxPoints) {
		const size_t pick = size_t(rng.Int(0, int64_t(active.size()) - 1));
		const Point2 base = accepted[active[pick]];
		bool found = false;
		for (int k = 0; k < attempts; ++k) {
			const float angle = float(rng.Range(0.0, 6.283185307179586));
			const float dist = float(rng.Range(radius, 2.0 * radius));
			const float x = base.x + std::cos(angle) * dist, y = base.y + std::sin(angle) * dist;
			if (fits(x, y)) {
				add(x, y);
				found = true;
				break;
			}
		}
		if (!found) {
			active[pick] = active.back();
			active.pop_back();
		}
	}
	// Densité appliquée après coup : l'espacement minimal reste garanti.
	for (const Point2 &p : accepted)
		if (!density || rng.Float() < double(std::clamp(density(p.x, p.y), 0.f, 1.f)))
			points.push_back(p);
	return points;
}

std::vector<Point2> JitteredGrid(float width, float height, float spacing, float jitter, uint64_t seed) {
	std::vector<Point2> points;
	if (spacing <= 0.f)
		return points;
	Rng rng(seed);
	for (float y = spacing * 0.5f; y < height; y += spacing)
		for (float x = spacing * 0.5f; x < width; x += spacing)
			points.push_back({x + float(rng.Range(-0.5, 0.5)) * jitter * spacing,
							  y + float(rng.Range(-0.5, 0.5)) * jitter * spacing});
	return points;
}

} // namespace generators
