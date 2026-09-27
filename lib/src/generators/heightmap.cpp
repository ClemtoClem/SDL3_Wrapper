// Définitions de generators/heightmap.hpp — fichier généré par splitter.py : le code
// vient tel quel de l'en-tête (seules les signatures sont réécrites).

#include "generators/heightmap.hpp"

namespace generators {

float Blend(BlendMode mode, float a, float b) noexcept {
	switch (mode) {
		case BlendMode::SET:
			return b;
		case BlendMode::ADD:
			return a + b;
		case BlendMode::SUBTRACT:
			return a - b;
		case BlendMode::MULTIPLY:
			return a * b;
		case BlendMode::MIN:
			return std::min(a, b);
		case BlendMode::MAX:
			return std::max(a, b);
		case BlendMode::AVERAGE:
			return (a + b) * 0.5f;
		case BlendMode::SCREEN:
			return 1.f - (1.f - a) * (1.f - b);
		case BlendMode::DIFFERENCE:
			return std::fabs(a - b);
		case BlendMode::OVERLAY:
			return a < 0.5f ? 2.f * a * b : 1.f - 2.f * (1.f - a) * (1.f - b);
	}
	return b;
}

// ── HeightMap ────────────────────────────────────────────────────────────────

float HeightMap::At(int x, int y) const noexcept {
	x = std::clamp(x, 0, m_width - 1);
	y = std::clamp(y, 0, m_height - 1);
	return m_data[size_t(y) * size_t(m_width) + size_t(x)];
}

float & HeightMap::Ref(int x, int y) noexcept {
	x = std::clamp(x, 0, m_width - 1);
	y = std::clamp(y, 0, m_height - 1);
	return m_data[size_t(y) * size_t(m_width) + size_t(x)];
}

float HeightMap::Sample(float x, float y) const noexcept {
	const int x0 = int(std::floor(x)), y0 = int(std::floor(y));
	const float tx = x - float(x0), ty = y - float(y0);
	const float a = At(x0, y0) + (At(x0 + 1, y0) - At(x0, y0)) * tx;
	const float b = At(x0, y0 + 1) + (At(x0 + 1, y0 + 1) - At(x0, y0 + 1)) * tx;
	return a + (b - a) * ty;
}

float HeightMap::SampleUV(float u, float v) const noexcept {
	return Sample(u * float(m_width - 1), v * float(m_height - 1));
}

HeightMap & HeightMap::Fill(const Noise &noise, float scale) {
	return Generate([&](int x, int y) { return noise.Sample01(float(x) * scale, float(y) * scale); });
}

HeightMap & HeightMap::Scale(float factor, float offset) {
	return Apply([=](float v) { return v * factor + offset; });
}

HeightMap & HeightMap::Clamp(float lo, float hi) {
	return Apply([=](float v) { return std::clamp(v, lo, hi); });
}

HeightMap & HeightMap::Invert() {
	const float lo = Min(), hi = Max();
	return Apply([=](float v) { return hi + lo - v; });
}

HeightMap & HeightMap::Abs() {
	return Apply([](float v) { return std::fabs(v); });
}

HeightMap & HeightMap::Power(float exponent) {
	return Apply([=](float v) { return v >= 0.f ? std::pow(v, exponent) : -std::pow(-v, exponent); });
}

HeightMap & HeightMap::Normalize(float lo, float hi) {
	const float a = Min(), b = Max();
	const float span = b - a;
	return Apply([=](float v) { return span > 1e-12f ? lo + (v - a) / span * (hi - lo) : lo; });
}

HeightMap & HeightMap::Remap(float fromLo, float fromHi, float toLo, float toHi) {
	const float span = fromHi - fromLo;
	return Apply([=](float v) { return span != 0.f ? toLo + (v - fromLo) / span * (toHi - toLo) : toLo; });
}

HeightMap & HeightMap::Curve(const std::vector<std::pair<float, float>> &points) {
	if (points.empty())
		return *this;
	return Apply([&](float v) {
		if (v <= points.front().first)
			return points.front().second;
		for (size_t i = 1; i < points.size(); ++i)
			if (v <= points[i].first) {
				const auto &a = points[i - 1], &b = points[i];
				const float t = (v - a.first) / std::max(1e-9f, b.first - a.first);
				return a.second + (b.second - a.second) * t;
			}
		return points.back().second;
	});
}

HeightMap & HeightMap::Terrace(int levels, float sharpness) {
	const float n = float(std::max(1, levels));
	const float k = std::clamp(sharpness, 0.f, 1.f);
	return Apply([=](float v) {
		const float scaled = v * n, step = std::floor(scaled), t = scaled - step;
		float rise = k >= 1.f ? 0.f : std::clamp((t - k) / (1.f - k), 0.f, 1.f);
		rise = rise * rise * (3.f - 2.f * rise);
		return (step + rise) / n;
	});
}

HeightMap & HeightMap::SeaLevel(float level) {
	return Apply([=](float v) { return std::max(v, level); });
}

HeightMap & HeightMap::BoxBlur(int radius, int passes) {
	if (radius <= 0)
		return *this;
	std::vector<float> tmp(m_data.size());
	for (int pass = 0; pass < passes; ++pass) {
		for (int y = 0; y < m_height; ++y)
			for (int x = 0; x < m_width; ++x) {
				float sum = 0.f;
				for (int k = -radius; k <= radius; ++k)
					sum += At(x + k, y);
				tmp[size_t(y) * size_t(m_width) + size_t(x)] = sum / float(2 * radius + 1);
			}
		for (int y = 0; y < m_height; ++y)
			for (int x = 0; x < m_width; ++x) {
				float sum = 0.f;
				for (int k = -radius; k <= radius; ++k)
					sum += tmp[size_t(std::clamp(y + k, 0, m_height - 1)) * size_t(m_width) + size_t(x)];
				m_data[size_t(y) * size_t(m_width) + size_t(x)] = sum / float(2 * radius + 1);
			}
	}
	return *this;
}

HeightMap & HeightMap::Smooth(float sigma) {
	const int radius = std::max(1, int(std::round(sigma * 0.8f)));
	return BoxBlur(radius, 3);
}

HeightMap & HeightMap::Sharpen(float amount, int radius) {
	HeightMap blurred = *this;
	blurred.BoxBlur(radius);
	for (size_t i = 0; i < m_data.size(); ++i)
		m_data[i] += amount * (m_data[i] - blurred.m_data[i]);
	return *this;
}

HeightMap & HeightMap::IslandFalloff(float exponent, float strength) {
	for (int y = 0; y < m_height; ++y)
		for (int x = 0; x < m_width; ++x) {
			const float nx = float(x) / float(std::max(1, m_width - 1)) * 2.f - 1.f;
			const float ny = float(y) / float(std::max(1, m_height - 1)) * 2.f - 1.f;
			const float d = std::min(1.f, std::sqrt(nx * nx + ny * ny));
			float &v = Ref(x, y);
			v *= 1.f - strength * std::pow(d, exponent);
		}
	return *this;
}

HeightMap HeightMap::Resized(int width, int height) const {
	HeightMap out(width, height);
	out.Generate([&](int x, int y) {
		return SampleUV(float(x) / float(std::max(1, width - 1)), float(y) / float(std::max(1, height - 1)));
	});
	return out;
}

HeightMap & HeightMap::ErodeHydraulic(const HydraulicErosion &p) {
	if (m_width < 3 || m_height < 3)
		return *this;
	// Brosse : poids des cases dans le rayon, normalisés.
	struct BrushCell {
		int dx, dy;
		float weight;
	};
	std::vector<BrushCell> brush;
	float weightSum = 0.f;
	for (int dy = -p.radius; dy <= p.radius; ++dy)
		for (int dx = -p.radius; dx <= p.radius; ++dx) {
			const float d = std::sqrt(float(dx * dx + dy * dy));
			if (d <= float(p.radius)) {
				const float w = 1.f - d / float(std::max(1, p.radius) + 1);
				brush.push_back({dx, dy, w});
				weightSum += w;
			}
		}
	for (BrushCell &cell : brush)
		cell.weight /= weightSum;

	Rng rng(p.seed);
	auto gradient = [&](float x, float y, float &gx, float &gy) {
		const int cx = int(x), cy = int(y);
		const float u = x - float(cx), v = y - float(cy);
		const float nw = At(cx, cy), ne = At(cx + 1, cy), sw = At(cx, cy + 1), se = At(cx + 1, cy + 1);
		gx = (ne - nw) * (1.f - v) + (se - sw) * v;
		gy = (sw - nw) * (1.f - u) + (se - ne) * u;
		return nw * (1.f - u) * (1.f - v) + ne * u * (1.f - v) + sw * (1.f - u) * v + se * u * v;
	};
	for (int drop = 0; drop < p.droplets; ++drop) {
		float x = float(rng.Range(0.0, double(m_width - 1))), y = float(rng.Range(0.0, double(m_height - 1)));
		float dirX = 0.f, dirY = 0.f, speed = p.initialSpeed, water = p.initialWater, sediment = 0.f;
		for (int life = 0; life < p.maxLifetime; ++life) {
			const int cx = int(x), cy = int(y);
			const float u = x - float(cx), v = y - float(cy);
			float gx, gy;
			const float height = gradient(x, y, gx, gy);
			dirX = dirX * p.inertia - gx * (1.f - p.inertia);
			dirY = dirY * p.inertia - gy * (1.f - p.inertia);
			const float len = std::sqrt(dirX * dirX + dirY * dirY);
			if (len < 1e-9f)
				break;
			dirX /= len;
			dirY /= len;
			x += dirX;
			y += dirY;
			if (x < 0.f || y < 0.f || x >= float(m_width - 1) || y >= float(m_height - 1))
				break;
			float ngx, ngy;
			const float newHeight = gradient(x, y, ngx, ngy);
			const float delta = newHeight - height;
			const float capacity = std::max(-delta * speed * water * p.sedimentCapacity, p.minCapacity);
			if (sediment > capacity || delta > 0.f) {
				// Dépôt (bilinéaire sur les 4 cases d'où l'on vient).
				const float amount = delta > 0.f ? std::min(delta, sediment) : (sediment - capacity) * p.depositSpeed;
				sediment -= amount;
				Ref(cx, cy) += amount * (1.f - u) * (1.f - v);
				Ref(cx + 1, cy) += amount * u * (1.f - v);
				Ref(cx, cy + 1) += amount * (1.f - u) * v;
				Ref(cx + 1, cy + 1) += amount * u * v;
			} else {
				// Érosion (brosse), jamais plus que la pente descendue.
				const float amount = std::min((capacity - sediment) * p.erodeSpeed, -delta);
				for (const BrushCell &cell : brush) {
					const int bx = cx + cell.dx, by = cy + cell.dy;
					if (bx < 0 || by < 0 || bx >= m_width || by >= m_height)
						continue;
					float &h = Ref(bx, by);
					const float removed = std::min(h, amount * cell.weight);
					h -= removed;
					sediment += removed;
				}
			}
			speed = std::sqrt(std::max(0.f, speed * speed + delta * p.gravity));
			water *= 1.f - p.evaporateSpeed;
		}
	}
	return *this;
}

HeightMap & HeightMap::ErodeThermal(const ThermalErosion &p) {
	static constexpr int DX[] = {1, -1, 0, 0, 1, -1, 1, -1};
	static constexpr int DY[] = {0, 0, 1, -1, 1, -1, -1, 1};
	for (int it = 0; it < p.iterations; ++it) {
		std::vector<float> delta(m_data.size(), 0.f);
		for (int y = 0; y < m_height; ++y)
			for (int x = 0; x < m_width; ++x) {
				const float h = At(x, y);
				float total = 0.f, diffs[8];
				for (int k = 0; k < 8; ++k) {
					const int nx = x + DX[k], ny = y + DY[k];
					const bool inside = nx >= 0 && ny >= 0 && nx < m_width && ny < m_height;
					const float d = inside ? h - At(nx, ny) : 0.f;
					diffs[k] = d > p.talus ? d : 0.f;
					total += diffs[k];
				}
				if (total <= 0.f)
					continue;
				const float moved = p.amount * (total - p.talus) * 0.5f;
				if (moved <= 0.f)
					continue;
				delta[size_t(y) * size_t(m_width) + size_t(x)] -= moved;
				for (int k = 0; k < 8; ++k)
					if (diffs[k] > 0.f)
						delta[size_t(y + DY[k]) * size_t(m_width) + size_t(x + DX[k])] += moved * diffs[k] / total;
			}
		for (size_t i = 0; i < m_data.size(); ++i)
			m_data[i] += delta[i];
	}
	return *this;
}

HeightMap HeightMap::SlopeMap(float cellSize, float heightScale) const {
	HeightMap out(m_width, m_height);
	out.Generate([&](int x, int y) {
		const float dx = (At(x + 1, y) - At(x - 1, y)) * heightScale / (2.f * cellSize);
		const float dy = (At(x, y + 1) - At(x, y - 1)) * heightScale / (2.f * cellSize);
		return std::atan(std::sqrt(dx * dx + dy * dy)) * 57.29577951f;
	});
	return out;
}

void HeightMap::Normal(int x, int y, float cellSize, float heightScale, float &nx, float &ny, float &nz) const noexcept {
	const float dx = (At(x + 1, y) - At(x - 1, y)) * heightScale;
	const float dz = (At(x, y + 1) - At(x, y - 1)) * heightScale;
	nx = -dx;
	ny = 2.f * cellSize;
	nz = -dz;
	const float len = std::sqrt(nx * nx + ny * ny + nz * nz);
	nx /= len;
	ny /= len;
	nz /= len;
}

HeightMap HeightMap::HeightMask(float lo, float hi, float feather) const {
	HeightMap out(m_width, m_height);
	out.Generate([&](int x, int y) { return Band(At(x, y), lo, hi, feather); });
	return out;
}

HeightMap HeightMap::SlopeMask(float minDeg, float maxDeg, float feather, float cellSize, float heightScale) const {
	HeightMap slope = SlopeMap(cellSize, heightScale);
	return slope.HeightMask(minDeg, maxDeg, feather);
}

float HeightMap::Band(float v, float lo, float hi, float feather) noexcept {
	if (feather <= 0.f)
		return v >= lo && v <= hi ? 1.f : 0.f;
	const float a = std::clamp((v - (lo - feather)) / feather, 0.f, 1.f);
	const float b = std::clamp(((hi + feather) - v) / feather, 0.f, 1.f);
	return std::min(a, b);
}

float HeightMap::Mean() const noexcept {
	double sum = 0.0;
	for (float v : m_data)
		sum += v;
	return m_data.empty() ? 0.f : float(sum / double(m_data.size()));
}

Option<String> HeightMap::SavePgm(const String &path, bool sixteenBits) const {
	std::FILE *file = std::fopen(path.CStr(), "wb");
	if (!file)
		return Some(String::Format("impossible d'écrire %s", path.CStr()));
	std::fprintf(file, "P5\n%d %d\n%d\n", m_width, m_height, sixteenBits ? 65535 : 255);
	for (float v : m_data) {
		const float c = std::clamp(v, 0.f, 1.f);
		if (sixteenBits) {
			const uint16_t s = uint16_t(std::lround(c * 65535.f));
			const unsigned char bytes[2] = {uint8_t(s >> 8), uint8_t(s & 0xFF)}; // PGM : gros-boutiste
			std::fwrite(bytes, 1, 2, file);
		} else {
			const unsigned char b = uint8_t(std::lround(c * 255.f));
			std::fwrite(&b, 1, 1, file);
		}
	}
	const bool ok = std::fclose(file) == 0;
	return ok ? Option<String>(NONE) : Some(String::Format("écriture incomplète de %s", path.CStr()));
}

Option<String> HeightMap::SaveRaw16(const String &path) const {
	std::FILE *file = std::fopen(path.CStr(), "wb");
	if (!file)
		return Some(String::Format("impossible d'écrire %s", path.CStr()));
	for (float v : m_data) {
		const uint16_t s = uint16_t(std::lround(std::clamp(v, 0.f, 1.f) * 65535.f));
		const unsigned char bytes[2] = {uint8_t(s & 0xFF), uint8_t(s >> 8)};
		std::fwrite(bytes, 1, 2, file);
	}
	return std::fclose(file) == 0 ? Option<String>(NONE) : Some(String::Format("écriture incomplète de %s", path.CStr()));
}

} // namespace generators
