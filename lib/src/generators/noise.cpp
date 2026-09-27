// Définitions de generators/noise.hpp — fichier généré par splitter.py : le code
// vient tel quel de l'en-tête (seules les signatures sont réécrites).

#include "generators/noise.hpp"

namespace generators {

// ── Noise ────────────────────────────────────────────────────────────────────

void Noise::SetSettings(const NoiseSettings &settings) {
	const bool reseed = settings.seed != m_s.seed;
	m_s = settings;
	if (reseed)
		BuildPermutation();
}

float Noise::Sample(float x, float y) const {
	x += m_s.offsetX;
	y += m_s.offsetY;
	if (m_s.warpAmplitude != 0.f)
		Warp(x, y);
	return Fractal2(x * m_s.frequency, y * m_s.frequency);
}

float Noise::Sample(float x, float y, float z) const {
	x += m_s.offsetX;
	y += m_s.offsetY;
	z += m_s.offsetZ;
	if (m_s.warpAmplitude != 0.f)
		Warp(x, y, z);
	return Fractal3(x * m_s.frequency, y * m_s.frequency, z * m_s.frequency);
}

float Noise::Sample01(float x, float y) const {
	const float v = Sample(x, y);
	return m_s.type == NoiseType::WORLEY ? std::clamp(v, 0.f, 1.f) : std::clamp(v * 0.5f + 0.5f, 0.f, 1.f);
}

float Noise::Base2(float x, float y, uint32_t octave) const {
	switch (m_s.type) {
		case NoiseType::PERLIN:
			return Perlin2(x, y, octave);
		case NoiseType::SIMPLEX:
			return Simplex2(x, y, octave);
		case NoiseType::VALUE:
			return Value2(x, y, octave);
		case NoiseType::WORLEY:
			return Worley2(x, y, octave);
	}
	return 0.f;
}

float Noise::Base3(float x, float y, float z, uint32_t octave) const {
	switch (m_s.type) {
		case NoiseType::PERLIN:
			return Perlin3(x, y, z, octave);
		case NoiseType::SIMPLEX:
			return Simplex3(x, y, z, octave);
		case NoiseType::VALUE:
			return Value3(x, y, z, octave);
		case NoiseType::WORLEY:
			return Worley3(x, y, z, octave);
	}
	return 0.f;
}

float Noise::Perlin2(float x, float y, uint32_t octave) const {
	const int xi = FastFloor(x), yi = FastFloor(y);
	const float xf = x - float(xi), yf = y - float(yi);
	const float u = Fade(xf), v = Fade(yf);
	const float n00 = Grad2(Perm(xi, yi, octave), xf, yf);
	const float n10 = Grad2(Perm(xi + 1, yi, octave), xf - 1.f, yf);
	const float n01 = Grad2(Perm(xi, yi + 1, octave), xf, yf - 1.f);
	const float n11 = Grad2(Perm(xi + 1, yi + 1, octave), xf - 1.f, yf - 1.f);
	return Lerp(Lerp(n00, n10, u), Lerp(n01, n11, u), v) * 1.4142f;
}

float Noise::Perlin3(float x, float y, float z, uint32_t octave) const {
	const int xi = FastFloor(x), yi = FastFloor(y), zi = FastFloor(z);
	const float xf = x - float(xi), yf = y - float(yi), zf = z - float(zi);
	const float u = Fade(xf), v = Fade(yf), w = Fade(zf);
	auto g = [&](int dx, int dy, int dz) {
		return Grad3(Perm(xi + dx, yi + dy, zi + dz, octave), xf - float(dx), yf - float(dy), zf - float(dz));
	};
	const float x00 = Lerp(g(0, 0, 0), g(1, 0, 0), u), x10 = Lerp(g(0, 1, 0), g(1, 1, 0), u);
	const float x01 = Lerp(g(0, 0, 1), g(1, 0, 1), u), x11 = Lerp(g(0, 1, 1), g(1, 1, 1), u);
	return Lerp(Lerp(x00, x10, v), Lerp(x01, x11, v), w);
}

float Noise::Simplex2(float x, float y, uint32_t octave) const {
	constexpr float F2 = 0.36602540378f, G2 = 0.2113248654f;
	const float s = (x + y) * F2;
	const int i = FastFloor(x + s), j = FastFloor(y + s);
	const float t = float(i + j) * G2;
	const float x0 = x - (float(i) - t), y0 = y - (float(j) - t);
	const int i1 = x0 > y0 ? 1 : 0, j1 = x0 > y0 ? 0 : 1;
	const float x1 = x0 - float(i1) + G2, y1 = y0 - float(j1) + G2;
	const float x2 = x0 - 1.f + 2.f * G2, y2 = y0 - 1.f + 2.f * G2;
	auto corner = [&](float cx, float cy, int gi, int gj) {
		float t0 = 0.5f - cx * cx - cy * cy;
		if (t0 < 0.f)
			return 0.f;
		t0 *= t0;
		return t0 * t0 * Grad2(Perm(gi, gj, octave), cx, cy);
	};
	return 70.f * (corner(x0, y0, i, j) + corner(x1, y1, i + i1, j + j1) + corner(x2, y2, i + 1, j + 1));
}

float Noise::Simplex3(float x, float y, float z, uint32_t octave) const {
	constexpr float F3 = 1.f / 3.f, G3 = 1.f / 6.f;
	const float s = (x + y + z) * F3;
	const int i = FastFloor(x + s), j = FastFloor(y + s), k = FastFloor(z + s);
	const float t = float(i + j + k) * G3;
	const float x0 = x - (float(i) - t), y0 = y - (float(j) - t), z0 = z - (float(k) - t);
	int i1, j1, k1, i2, j2, k2;
	if (x0 >= y0) {
		if (y0 >= z0) {
			i1 = 1, j1 = 0, k1 = 0, i2 = 1, j2 = 1, k2 = 0;
		} else if (x0 >= z0) {
			i1 = 1, j1 = 0, k1 = 0, i2 = 1, j2 = 0, k2 = 1;
		} else {
			i1 = 0, j1 = 0, k1 = 1, i2 = 1, j2 = 0, k2 = 1;
		}
	} else {
		if (y0 < z0) {
			i1 = 0, j1 = 0, k1 = 1, i2 = 0, j2 = 1, k2 = 1;
		} else if (x0 < z0) {
			i1 = 0, j1 = 1, k1 = 0, i2 = 0, j2 = 1, k2 = 1;
		} else {
			i1 = 0, j1 = 1, k1 = 0, i2 = 1, j2 = 1, k2 = 0;
		}
	}
	auto corner = [&](float cx, float cy, float cz, int gi, int gj, int gk) {
		float t0 = 0.6f - cx * cx - cy * cy - cz * cz;
		if (t0 < 0.f)
			return 0.f;
		t0 *= t0;
		return t0 * t0 * Grad3(Perm(gi, gj, gk, octave), cx, cy, cz);
	};
	const float n0 = corner(x0, y0, z0, i, j, k);
	const float n1 = corner(x0 - float(i1) + G3, y0 - float(j1) + G3, z0 - float(k1) + G3, i + i1, j + j1, k + k1);
	const float n2 =
		corner(x0 - float(i2) + 2.f * G3, y0 - float(j2) + 2.f * G3, z0 - float(k2) + 2.f * G3, i + i2, j + j2, k + k2);
	const float n3 = corner(x0 - 1.f + 3.f * G3, y0 - 1.f + 3.f * G3, z0 - 1.f + 3.f * G3, i + 1, j + 1, k + 1);
	return 32.f * (n0 + n1 + n2 + n3);
}

float Noise::Value2(float x, float y, uint32_t octave) const {
	const int xi = FastFloor(x), yi = FastFloor(y);
	const float u = Fade(x - float(xi)), v = Fade(y - float(yi));
	const uint32_t seed = m_s.seed + octave * 0x9E37u;
	auto at = [&](int dx, int dy) { return HashFloat2(xi + dx, yi + dy, seed) * 2.f - 1.f; };
	return Lerp(Lerp(at(0, 0), at(1, 0), u), Lerp(at(0, 1), at(1, 1), u), v);
}

float Noise::Value3(float x, float y, float z, uint32_t octave) const {
	const int xi = FastFloor(x), yi = FastFloor(y), zi = FastFloor(z);
	const float u = Fade(x - float(xi)), v = Fade(y - float(yi)), w = Fade(z - float(zi));
	const uint32_t seed = m_s.seed + octave * 0x9E37u;
	auto at = [&](int dx, int dy, int dz) { return HashFloat3(xi + dx, yi + dy, zi + dz, seed) * 2.f - 1.f; };
	const float x00 = Lerp(at(0, 0, 0), at(1, 0, 0), u), x10 = Lerp(at(0, 1, 0), at(1, 1, 0), u);
	const float x01 = Lerp(at(0, 0, 1), at(1, 0, 1), u), x11 = Lerp(at(0, 1, 1), at(1, 1, 1), u);
	return Lerp(Lerp(x00, x10, v), Lerp(x01, x11, v), w);
}

float Noise::Worley2(float x, float y, uint32_t octave) const {
	const int xi = FastFloor(x), yi = FastFloor(y);
	const uint32_t seed = m_s.seed + octave * 0x9E37u;
	float f1 = 1e9f, f2 = 1e9f, cellValue = 0.f;
	for (int dy = -1; dy <= 1; ++dy)
		for (int dx = -1; dx <= 1; ++dx) {
			const int cx = xi + dx, cy = yi + dy;
			const float px = float(cx) + 0.5f + (HashFloat2(cx, cy, seed) - 0.5f) * m_s.jitter;
			const float py = float(cy) + 0.5f + (HashFloat2(cx, cy, seed ^ 0xA5A5u) - 0.5f) * m_s.jitter;
			const float d = Distance(px - x, py - y, 0.f);
			if (d < f1) {
				f2 = f1;
				f1 = d;
				cellValue = HashFloat2(cx, cy, seed ^ 0x5151u);
			} else if (d < f2) {
				f2 = d;
			}
		}
	return CellularResult(f1, f2, cellValue);
}

float Noise::Worley3(float x, float y, float z, uint32_t octave) const {
	const int xi = FastFloor(x), yi = FastFloor(y), zi = FastFloor(z);
	const uint32_t seed = m_s.seed + octave * 0x9E37u;
	float f1 = 1e9f, f2 = 1e9f, cellValue = 0.f;
	for (int dz = -1; dz <= 1; ++dz)
		for (int dy = -1; dy <= 1; ++dy)
			for (int dx = -1; dx <= 1; ++dx) {
				const int cx = xi + dx, cy = yi + dy, cz = zi + dz;
				const float px = float(cx) + 0.5f + (HashFloat3(cx, cy, cz, seed) - 0.5f) * m_s.jitter;
				const float py = float(cy) + 0.5f + (HashFloat3(cx, cy, cz, seed ^ 0xA5A5u) - 0.5f) * m_s.jitter;
				const float pz = float(cz) + 0.5f + (HashFloat3(cx, cy, cz, seed ^ 0x3C3Cu) - 0.5f) * m_s.jitter;
				const float d = Distance(px - x, py - y, pz - z);
				if (d < f1) {
					f2 = f1;
					f1 = d;
					cellValue = HashFloat3(cx, cy, cz, seed ^ 0x5151u);
				} else if (d < f2) {
					f2 = d;
				}
			}
	return CellularResult(f1, f2, cellValue);
}

int Noise::FastFloor(float v) noexcept {
	const int i = int(v);
	return v < float(i) ? i - 1 : i;
}

void Noise::BuildPermutation() {
	Rng rng(m_s.seed);
	for (int i = 0; i < 256; ++i)
		m_perm[size_t(i)] = uint8_t(i);
	for (int i = 255; i > 0; --i)
		std::swap(m_perm[size_t(i)], m_perm[size_t(rng.Int(0, i))]);
}

uint32_t Noise::Perm(int x, int y, uint32_t octave) const noexcept {
	const uint32_t o = octave * 31u;
	return m_perm[(m_perm[(uint32_t(x) + o) & 255u] + uint32_t(y)) & 255u];
}

uint32_t Noise::Perm(int x, int y, int z, uint32_t octave) const noexcept {
	const uint32_t o = octave * 31u;
	return m_perm[(m_perm[(m_perm[(uint32_t(x) + o) & 255u] + uint32_t(y)) & 255u] + uint32_t(z)) & 255u];
}

float Noise::Grad2(uint32_t hash, float x, float y) noexcept {
	// 8 directions régulières.
	switch (hash & 7u) {
		case 0:
			return x + y;
		case 1:
			return -x + y;
		case 2:
			return x - y;
		case 3:
			return -x - y;
		case 4:
			return x;
		case 5:
			return -x;
		case 6:
			return y;
		default:
			return -y;
	}
}

float Noise::Grad3(uint32_t hash, float x, float y, float z) noexcept {
	const uint32_t h = hash & 15u;
	const float u = h < 8 ? x : y;
	const float v = h < 4 ? y : (h == 12 || h == 14 ? x : z);
	return ((h & 1u) ? -u : u) + ((h & 2u) ? -v : v);
}

float Noise::Distance(float dx, float dy, float dz) const noexcept {
	switch (m_s.metric) {
		case DistanceMetric::MANHATTAN:
			return std::fabs(dx) + std::fabs(dy) + std::fabs(dz);
		case DistanceMetric::CHEBYSHEV:
			return std::max({std::fabs(dx), std::fabs(dy), std::fabs(dz)});
		default:
			return std::sqrt(dx * dx + dy * dy + dz * dz);
	}
}

float Noise::CellularResult(float f1, float f2, float cellValue) const noexcept {
	switch (m_s.cellular) {
		case CellularReturn::F1:
			return std::clamp(f1, 0.f, 1.f);
		case CellularReturn::F2:
			return std::clamp(f2 * 0.75f, 0.f, 1.f);
		case CellularReturn::F2_MINUS_F1:
			return std::clamp(f2 - f1, 0.f, 1.f);
		case CellularReturn::CELL_VALUE:
			return cellValue;
	}
	return f1;
}

float Noise::FractalBounding() const noexcept {
	float amp = 1.f, total = 0.f;
	for (int i = 0; i < std::max(1, m_s.octaves); ++i) {
		total += amp;
		amp *= m_s.gain;
	}
	return total > 0.f ? 1.f / total : 1.f;
}

float Noise::Fractal2(float x, float y) const {
	return Fractal([&](float f, uint32_t o) { return Base2(x * f, y * f, o); });
}

float Noise::Fractal3(float x, float y, float z) const {
	return Fractal([&](float f, uint32_t o) { return Base3(x * f, y * f, z * f, o); });
}

void Noise::Warp(float &x, float &y) const {
	const float f = m_s.warpFrequency;
	x += m_s.warpAmplitude * Simplex2(x * f + 17.3f, y * f - 4.1f, 101u);
	y += m_s.warpAmplitude * Simplex2(x * f - 31.7f, y * f + 12.9f, 202u);
}

void Noise::Warp(float &x, float &y, float &z) const {
	const float f = m_s.warpFrequency;
	x += m_s.warpAmplitude * Simplex3(x * f + 17.3f, y * f, z * f, 101u);
	y += m_s.warpAmplitude * Simplex3(x * f, y * f - 31.7f, z * f, 202u);
	z += m_s.warpAmplitude * Simplex3(x * f, y * f, z * f + 8.9f, 303u);
}

} // namespace generators
