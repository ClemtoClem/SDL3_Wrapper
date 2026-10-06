// Définitions de generators/random.hpp
#include "generators/random.hpp"

namespace generators {

// ── Rng ──────────────────────────────────────────────────────────────────────

double Rng::Normal(double mean, double stddev) noexcept {
	double u = Float();
	while (u <= 1e-300)
		u = Float();
	const double v = Float();
	return mean + stddev * std::sqrt(-2.0 * std::log(u)) * std::cos(6.283185307179586 * v);
}

int64_t Rng::Weighted(const std::vector<double> &weights) noexcept {
	double total = 0.0;
	for (double w : weights)
		total += w > 0.0 ? w : 0.0;
	if (total <= 0.0)
		return -1;
	double pick = Float() * total;
	for (size_t i = 0; i < weights.size(); ++i) {
		if (weights[i] <= 0.0)
			continue;
		if (pick < weights[i])
			return int64_t(i);
		pick -= weights[i];
	}
	return int64_t(weights.size()) - 1;
}

std::pair<double, double> Rng::InDisk() noexcept {
	const double r = std::sqrt(Float()), a = Float() * 6.283185307179586;
	return {r * std::cos(a), r * std::sin(a)};
}

} // namespace generators
