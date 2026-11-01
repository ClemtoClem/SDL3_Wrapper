// Définitions de voxelia/studio/texture_document.hpp
#include <voxelia/studio/texture_document.hpp>

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <functional>

#include <voxel/json.hpp>
#include <voxel/meshing.hpp>
#include <voxel/worldgen.hpp>

namespace voxelia::studio {

using voxel::Image;
namespace json = voxel::json;

namespace {

float Channel(uint8_t v) { return float(v) / 255.f; }
uint8_t Byte(float v) { return uint8_t(std::clamp(int(std::lround(v * 255.f)), 0, 255)); }

float BlendChannel(float s, float d, PaintBlend mode) {
	switch (mode) {
	case PaintBlend::MULTIPLY:
		return s * d;
	case PaintBlend::SCREEN:
		return 1.f - (1.f - s) * (1.f - d);
	case PaintBlend::OVERLAY:
		return d < 0.5f ? 2.f * s * d : 1.f - 2.f * (1.f - s) * (1.f - d);
	case PaintBlend::NORMAL:
	default:
		return s;
	}
}

Rgba At(const Image &img, int x, int y) {
	const uint8_t *p = &img.rgba[(size_t(y) * size_t(img.width) + size_t(x)) * 4];
	return {p[0], p[1], p[2], p[3]};
}

void Put(Image &img, int x, int y, const Rgba &c) {
	uint8_t *p = &img.rgba[(size_t(y) * size_t(img.width) + size_t(x)) * 4];
	p[0] = c[0], p[1] = c[1], p[2] = c[2], p[3] = c[3];
}

Image Blank(int w, int h) {
	Image img;
	img.width = w;
	img.height = h;
	img.rgba.assign(size_t(w) * size_t(h) * 4, 0);
	return img;
}

/// Bruit de valeur lissé (0..1) à la cellule `scale`.
float ValueNoise(uint32_t seed, float x, float y, float scale) {
	const float fx = x / scale, fy = y / scale;
	const int x0 = int(std::floor(fx)), y0 = int(std::floor(fy));
	const float tx = fx - float(x0), ty = fy - float(y0);
	auto corner = [&](int cx, int cy) { return float(voxel::HashPosition(seed, cx, 0, cy, 17) % 10000u) / 9999.f; };
	auto smooth = [](float t) { return t * t * (3.f - 2.f * t); };
	const float a = corner(x0, y0), b = corner(x0 + 1, y0), c = corner(x0, y0 + 1), d = corner(x0 + 1, y0 + 1);
	const float sx = smooth(tx), sy = smooth(ty);
	return (a + (b - a) * sx) + ((c + (d - c) * sx) - (a + (b - a) * sx)) * sy;
}

Rgba Mix(const Rgba &a, const Rgba &b, float t) {
	Rgba out;
	for (int k = 0; k < 4; ++k)
		out[size_t(k)] = Byte(Channel(a[size_t(k)]) + (Channel(b[size_t(k)]) - Channel(a[size_t(k)])) * std::clamp(t, 0.f, 1.f));
	return out;
}

} // namespace

const char *PaintBlendId(PaintBlend mode) noexcept {
	switch (mode) {
	case PaintBlend::MULTIPLY:
		return "multiply";
	case PaintBlend::SCREEN:
		return "screen";
	case PaintBlend::OVERLAY:
		return "overlay";
	case PaintBlend::NORMAL:
	default:
		return "normal";
	}
}

Option<PaintBlend> PaintBlendOf(const String &id) noexcept {
	for (PaintBlend mode : {PaintBlend::NORMAL, PaintBlend::MULTIPLY, PaintBlend::SCREEN, PaintBlend::OVERLAY})
		if (id == PaintBlendId(mode))
			return Some(mode);
	return NONE;
}

Rgba BlendPixel(const Rgba &src, const Rgba &dst, PaintBlend mode, float opacity) noexcept {
	const float sa = Channel(src[3]) * std::clamp(opacity, 0.f, 1.f), da = Channel(dst[3]);
	const float outA = sa + da * (1.f - sa);
	if (outA <= 1e-6f)
		return {0, 0, 0, 0};
	Rgba out;
	for (int k = 0; k < 3; ++k) {
		const float s = Channel(src[size_t(k)]), d = Channel(dst[size_t(k)]);
		// Mode appliqué là où les deux existent ; ailleurs, chacun tel quel.
		const float c = (BlendChannel(s, d, mode) * sa * da + s * sa * (1.f - da) + d * da * (1.f - sa)) / outA;
		out[size_t(k)] = Byte(c);
	}
	out[3] = Byte(outA);
	return out;
}

// ── Création ─────────────────────────────────────────────────────────────────

Result<TextureDocument, String> TextureDocument::Create(int width, int height, const String &name) {
	if (width < 1 || height < 1 || width > MAX_SOURCE || height > MAX_SOURCE)
		return Err(String::Format("taille de 1 à %d px par côté", MAX_SOURCE));
	TextureDocument doc;
	doc.m_width = width;
	doc.m_height = height;
	doc.m_name = name;
	doc.m_layers.push_back(PaintLayer{String("Fond"), String(), Blank(width, height)});
	return Ok(std::move(doc));
}

Result<TextureDocument, String> TextureDocument::FromImage(Image image, const String &name) {
	if (image.IsEmpty() || image.width > MAX_SOURCE || image.height > MAX_SOURCE)
		return Err(String::Format("image vide ou plus grande que %d px", MAX_SOURCE));
	TextureDocument doc;
	doc.m_width = image.width;
	doc.m_height = image.height;
	doc.m_name = name;
	doc.m_layers.push_back(PaintLayer{String("Fond"), String(), std::move(image)});
	return Ok(std::move(doc));
}

// ── Annulation ───────────────────────────────────────────────────────────────

size_t TextureDocument::Snapshot::Bytes() const {
	size_t n = 0;
	for (const PaintLayer &l : layers)
		n += l.pixels.rgba.size();
	return n;
}

TextureDocument::Snapshot TextureDocument::Capture() const { return Snapshot{m_layers, m_active, m_selection}; }

void TextureDocument::Restore(Snapshot s) {
	m_layers = std::move(s.layers);
	m_active = std::clamp(s.active, 0, int(m_layers.size()) - 1);
	m_selection = s.selection;
}

void TextureDocument::Commit() {
	if (m_strokeDepth > 0) {
		if (m_strokeCommitted)
			return; // le geste a déjà son étape
		m_strokeCommitted = true;
	}
	m_undo.push_back(Capture());
	m_redo.clear();
	size_t bytes = 0;
	for (const Snapshot &s : m_undo)
		bytes += s.Bytes();
	while (m_undo.size() > MAX_UNDO || (m_undo.size() > 1 && bytes > MAX_UNDO_BYTES)) {
		bytes -= m_undo.front().Bytes();
		m_undo.pop_front();
	}
}

void TextureDocument::Changed() {
	m_dirty = true;
	++m_version;
}

bool TextureDocument::Undo() {
	if (m_undo.empty() || m_strokeDepth > 0)
		return false;
	m_redo.push_back(Capture());
	Restore(std::move(m_undo.back()));
	m_undo.pop_back();
	Changed();
	return true;
}

bool TextureDocument::Redo() {
	if (m_redo.empty() || m_strokeDepth > 0)
		return false;
	m_undo.push_back(Capture());
	Restore(std::move(m_redo.back()));
	m_redo.pop_back();
	Changed();
	return true;
}

void TextureDocument::BeginStroke() {
	if (m_strokeDepth++ == 0) {
		m_strokeCommitted = false;
		m_last = NONE;
	}
}

void TextureDocument::EndStroke() {
	if (m_strokeDepth > 0 && --m_strokeDepth == 0)
		m_last = NONE;
}

// ── Calques ──────────────────────────────────────────────────────────────────

Result<bool, String> TextureDocument::SetActive(int index) {
	if (index < 0 || index >= int(m_layers.size()))
		return Err(String("calque inconnu"));
	m_active = index;
	++m_version;
	return Ok(true);
}

void TextureDocument::AddLayer(const String &name) {
	Commit();
	m_layers.insert(m_layers.begin() + m_active + 1, PaintLayer{name, String(), Blank(m_width, m_height)});
	++m_active;
	Changed();
}

Result<bool, String> TextureDocument::DuplicateLayer(int index) {
	if (index < 0 || index >= int(m_layers.size()))
		return Err(String("calque inconnu"));
	Commit();
	PaintLayer copy = m_layers[size_t(index)];
	copy.name += String(" (copie)");
	m_layers.insert(m_layers.begin() + index + 1, std::move(copy));
	m_active = index + 1;
	Changed();
	return Ok(true);
}

Result<bool, String> TextureDocument::RemoveLayer(int index) {
	if (index < 0 || index >= int(m_layers.size()))
		return Err(String("calque inconnu"));
	if (m_layers.size() == 1)
		return Err(String("il faut garder au moins un calque"));
	Commit();
	m_layers.erase(m_layers.begin() + index);
	m_active = std::clamp(m_active > index ? m_active - 1 : m_active, 0, int(m_layers.size()) - 1);
	Changed();
	return Ok(true);
}

Result<bool, String> TextureDocument::MoveLayer(int from, int to) {
	const int n = int(m_layers.size());
	if (from < 0 || from >= n || to < 0 || to >= n)
		return Err(String("calque inconnu"));
	if (from == to)
		return Ok(true);
	Commit();
	PaintLayer moved = std::move(m_layers[size_t(from)]);
	m_layers.erase(m_layers.begin() + from);
	m_layers.insert(m_layers.begin() + to, std::move(moved));
	if (m_active == from)
		m_active = to;
	else if (from < m_active && to >= m_active)
		--m_active;
	else if (from > m_active && to <= m_active)
		++m_active;
	Changed();
	return Ok(true);
}

Result<bool, String> TextureDocument::SetLayerVisible(int index, bool visible) {
	if (index < 0 || index >= int(m_layers.size()))
		return Err(String("calque inconnu"));
	Commit();
	m_layers[size_t(index)].visible = visible;
	Changed();
	return Ok(true);
}

Result<bool, String> TextureDocument::SetLayerOpacity(int index, float opacity) {
	if (index < 0 || index >= int(m_layers.size()))
		return Err(String("calque inconnu"));
	Commit();
	m_layers[size_t(index)].opacity = std::clamp(opacity, 0.f, 1.f);
	Changed();
	return Ok(true);
}

Result<bool, String> TextureDocument::SetLayerBlend(int index, PaintBlend blend) {
	if (index < 0 || index >= int(m_layers.size()))
		return Err(String("calque inconnu"));
	Commit();
	m_layers[size_t(index)].blend = blend;
	Changed();
	return Ok(true);
}

Result<bool, String> TextureDocument::RenameLayer(int index, const String &name) {
	if (index < 0 || index >= int(m_layers.size()))
		return Err(String("calque inconnu"));
	if (name.Trim().IsEmpty())
		return Err(String("nom vide"));
	Commit();
	m_layers[size_t(index)].name = name.Trim();
	Changed();
	return Ok(true);
}

Result<bool, String> TextureDocument::SetLayerGroup(int index, const String &group) {
	if (index < 0 || index >= int(m_layers.size()))
		return Err(String("calque inconnu"));
	Commit();
	m_layers[size_t(index)].group = group.Trim();
	Changed();
	return Ok(true);
}

Result<bool, String> TextureDocument::MergeDown(int index) {
	if (index <= 0 || index >= int(m_layers.size()))
		return Err(String("pas de calque en dessous"));
	Commit();
	const PaintLayer &top = m_layers[size_t(index)];
	PaintLayer &below = m_layers[size_t(index - 1)];
	if (top.visible)
		for (int y = 0; y < m_height; ++y)
			for (int x = 0; x < m_width; ++x)
				Put(below.pixels, x, y, BlendPixel(At(top.pixels, x, y), At(below.pixels, x, y), top.blend, top.opacity));
	m_layers.erase(m_layers.begin() + index);
	m_active = index - 1;
	Changed();
	return Ok(true);
}

// ── Sélection ────────────────────────────────────────────────────────────────

void TextureDocument::Select(PaintRect rect) {
	if (rect.w < 0) {
		rect.x += rect.w;
		rect.w = -rect.w;
	}
	if (rect.h < 0) {
		rect.y += rect.h;
		rect.h = -rect.h;
	}
	const int x0 = std::clamp(rect.x, 0, m_width), y0 = std::clamp(rect.y, 0, m_height);
	const int x1 = std::clamp(rect.x + rect.w, 0, m_width), y1 = std::clamp(rect.y + rect.h, 0, m_height);
	m_selection = PaintRect{x0, y0, x1 - x0, y1 - y0};
	++m_version;
}

PaintRect TextureDocument::Area() const noexcept { return m_selection.Empty() ? PaintRect{0, 0, m_width, m_height} : m_selection; }

bool TextureDocument::Paintable(int x, int y) const noexcept {
	return x >= 0 && y >= 0 && x < m_width && y < m_height && (m_selection.Empty() || m_selection.Contains(x, y));
}

// ── Peinture ─────────────────────────────────────────────────────────────────

void TextureDocument::Plot(int x, int y, const Rgba &color, float opacity, bool erase) {
	if (!Paintable(x, y))
		return;
	Image &img = Layer().pixels;
	Rgba p = At(img, x, y);
	if (erase)
		p[3] = Byte(Channel(p[3]) * (1.f - std::clamp(opacity * Channel(color[3]), 0.f, 1.f)));
	else
		p = BlendPixel(color, p, brush.blend, opacity);
	Put(img, x, y, p);
}

void TextureDocument::Stamp(int x, int y, bool erase) {
	const int size = std::max(1, brush.size);
	const int r0 = (size - 1) / 2, r1 = size / 2; // carré pair décalé vers le bas / la droite
	const float radius = float(size) * 0.5f - 0.25f; // 3 px : une croix, comme Aseprite
	for (int dy = -r0; dy <= r1; ++dy)
		for (int dx = -r0; dx <= r1; ++dx) {
			if (size > 2) { // disque au-delà de 2 px
				const float cx = float(dx) - (size % 2 == 0 ? 0.5f : 0.f), cy = float(dy) - (size % 2 == 0 ? 0.5f : 0.f);
				if (cx * cx + cy * cy > radius * radius)
					continue;
			}
			Plot(x + dx, y + dy, brush.color, brush.opacity, erase);
		}
}

void TextureDocument::Dab(int x, int y, bool erase) {
	Commit();
	const bool h = brush.symmetry == PaintSymmetry::H || brush.symmetry == PaintSymmetry::HV;
	const bool v = brush.symmetry == PaintSymmetry::V || brush.symmetry == PaintSymmetry::HV;
	const int mx = m_width - 1 - x, my = m_height - 1 - y;
	Stamp(x, y, erase);
	if (h && mx != x)
		Stamp(mx, y, erase);
	if (v && my != y)
		Stamp(x, my, erase);
	if (h && v && (mx != x || my != y))
		Stamp(mx, my, erase);
	if (!erase)
		RememberColor(brush.color);
	m_last = Some(std::array<int, 2>{x, y});
	Changed();
}

void TextureDocument::StrokeTo(int x, int y, bool erase) {
	if (m_last.IsNone()) {
		Dab(x, y, erase);
		return;
	}
	const auto [x0, y0] = m_last.Value();
	if (x0 == x && y0 == y)
		return;
	// Bresenham : chaque pixel du trajet reçoit une touche (sans redoubler le départ).
	int cx = x0, cy = y0;
	const int dx = std::abs(x - x0), dy = -std::abs(y - y0), sx = x0 < x ? 1 : -1, sy = y0 < y ? 1 : -1;
	int err = dx + dy;
	while (cx != x || cy != y) {
		const int e2 = 2 * err;
		if (e2 >= dy) {
			err += dy;
			cx += sx;
		}
		if (e2 <= dx) {
			err += dx;
			cy += sy;
		}
		Dab(cx, cy, erase);
	}
}

void TextureDocument::Line(int x0, int y0, int x1, int y1, bool erase) {
	BeginStroke();
	Dab(x0, y0, erase);
	StrokeTo(x1, y1, erase);
	EndStroke();
}

void TextureDocument::Rectangle(int x0, int y0, int x1, int y1, bool filled) {
	BeginStroke();
	const int ax = std::min(x0, x1), bx = std::max(x0, x1), ay = std::min(y0, y1), by = std::max(y0, y1);
	if (filled) {
		Commit();
		for (int y = ay; y <= by; ++y)
			for (int x = ax; x <= bx; ++x)
				Plot(x, y, brush.color, brush.opacity, false);
		RememberColor(brush.color);
		Changed();
	} else {
		Dab(ax, ay);
		StrokeTo(bx, ay);
		StrokeTo(bx, by);
		StrokeTo(ax, by);
		StrokeTo(ax, ay);
	}
	EndStroke();
}

void TextureDocument::Ellipse(int x0, int y0, int x1, int y1, bool filled) {
	BeginStroke();
	Commit();
	const float cx = (float(x0) + float(x1)) * 0.5f, cy = (float(y0) + float(y1)) * 0.5f;
	const float rx = std::max(0.5f, std::fabs(float(x1 - x0)) * 0.5f + 0.5f), ry = std::max(0.5f, std::fabs(float(y1 - y0)) * 0.5f + 0.5f);
	auto inside = [&](int x, int y) {
		const float dx = (float(x) - cx) / rx, dy = (float(y) - cy) / ry;
		return dx * dx + dy * dy <= 1.f;
	};
	const int ax = std::min(x0, x1), bx = std::max(x0, x1), ay = std::min(y0, y1), by = std::max(y0, y1);
	for (int y = ay; y <= by; ++y)
		for (int x = ax; x <= bx; ++x) {
			if (!inside(x, y))
				continue;
			// Contour : un voisin (4) hors de l'ellipse.
			if (filled || !inside(x - 1, y) || !inside(x + 1, y) || !inside(x, y - 1) || !inside(x, y + 1)) {
				if (brush.size <= 1 || filled)
					Plot(x, y, brush.color, brush.opacity, false);
				else
					Stamp(x, y, false);
			}
		}
	RememberColor(brush.color);
	Changed();
	EndStroke();
}

int TextureDocument::Fill(int x, int y, int tolerance, int alphaThreshold) {
	if (!Paintable(x, y))
		return 0;
	Commit();
	Image &img = Layer().pixels;
	const Rgba seed = At(img, x, y);
	const bool seedVisible = seed[3] >= alphaThreshold;
	auto matches = [&](const Rgba &p) {
		if ((p[3] >= alphaThreshold) != seedVisible)
			return false;
		if (!seedVisible)
			return true; // zone transparente : la couleur n'importe pas
		for (int k = 0; k < 3; ++k)
			if (std::abs(int(p[size_t(k)]) - int(seed[size_t(k)])) > tolerance)
				return false;
		return true;
	};
	std::vector<uint8_t> seen(size_t(m_width) * size_t(m_height), 0);
	std::vector<std::pair<int, int>> stack{{x, y}};
	std::vector<std::pair<int, int>> region;
	while (!stack.empty()) {
		const auto [px, py] = stack.back();
		stack.pop_back();
		if (!Paintable(px, py))
			continue;
		uint8_t &s = seen[size_t(py) * size_t(m_width) + size_t(px)];
		if (s || !matches(At(img, px, py)))
			continue;
		s = 1;
		region.push_back({px, py});
		stack.push_back({px + 1, py});
		stack.push_back({px - 1, py});
		stack.push_back({px, py + 1});
		stack.push_back({px, py - 1});
	}
	for (const auto &[px, py] : region)
		Plot(px, py, brush.color, brush.opacity, false);
	RememberColor(brush.color);
	Changed();
	return int(region.size());
}

Option<Rgba> TextureDocument::Pick(int x, int y, bool composite) {
	if (x < 0 || y < 0 || x >= m_width || y >= m_height)
		return NONE;
	const Rgba c = composite ? At(Composite(), x, y) : At(m_layers[size_t(m_active)].pixels, x, y);
	brush.color = c;
	RememberColor(c);
	++m_version;
	return Some(c);
}

void TextureDocument::RememberColor(const Rgba &color) {
	m_palette.erase(std::remove(m_palette.begin(), m_palette.end(), color), m_palette.end());
	m_palette.insert(m_palette.begin(), color);
	if (m_palette.size() > MAX_PALETTE)
		m_palette.resize(MAX_PALETTE);
}

void TextureDocument::Move(int dx, int dy) {
	if (dx == 0 && dy == 0)
		return;
	Commit();
	Image &img = Layer().pixels;
	const PaintRect area = Area();
	// Contenu de la zone, retiré puis reposé décalé (hors toile : perdu).
	Image piece = Blank(area.w, area.h);
	for (int y = 0; y < area.h; ++y)
		for (int x = 0; x < area.w; ++x) {
			Put(piece, x, y, At(img, area.x + x, area.y + y));
			Put(img, area.x + x, area.y + y, {0, 0, 0, 0});
		}
	for (int y = 0; y < area.h; ++y)
		for (int x = 0; x < area.w; ++x) {
			const int tx = area.x + x + dx, ty = area.y + y + dy;
			if (tx >= 0 && ty >= 0 && tx < m_width && ty < m_height)
				Put(img, tx, ty, At(piece, x, y));
		}
	if (!m_selection.Empty())
		Select(PaintRect{m_selection.x + dx, m_selection.y + dy, m_selection.w, m_selection.h});
	Changed();
}

void TextureDocument::Convolve(const float kernel[9]) {
	Commit();
	Image &img = Layer().pixels;
	const Image source = img;
	const PaintRect area = Area();
	for (int y = area.y; y < area.y + area.h; ++y)
		for (int x = area.x; x < area.x + area.w; ++x) {
			// Couleur pondérée par l'alpha (un voisin transparent ne noircit pas).
			float rgb[3] = {0.f, 0.f, 0.f}, alpha = 0.f, weight = 0.f;
			for (int k = 0; k < 9; ++k) {
				const int sx = std::clamp(x + k % 3 - 1, 0, m_width - 1), sy = std::clamp(y + k / 3 - 1, 0, m_height - 1);
				const Rgba p = At(source, sx, sy);
				const float a = Channel(p[3]);
				for (int c = 0; c < 3; ++c)
					rgb[c] += kernel[k] * Channel(p[size_t(c)]) * a;
				alpha += kernel[k] * a;
				weight += kernel[k];
			}
			Rgba out;
			for (int c = 0; c < 3; ++c)
				out[size_t(c)] = alpha > 1e-4f ? Byte(rgb[c] / alpha) : At(source, x, y)[size_t(c)];
			out[3] = Byte(weight != 0.f ? alpha / weight : alpha);
			Put(img, x, y, out);
		}
	Changed();
}

void TextureDocument::Blur() {
	static constexpr float BOX[9] = {1.f, 2.f, 1.f, 2.f, 4.f, 2.f, 1.f, 2.f, 1.f};
	Convolve(BOX);
}

void TextureDocument::Sharpen() {
	static constexpr float SHARP[9] = {0.f, -1.f, 0.f, -1.f, 5.f, -1.f, 0.f, -1.f, 0.f};
	Convolve(SHARP);
}

void TextureDocument::Clear() {
	Commit();
	const PaintRect area = Area();
	for (int y = area.y; y < area.y + area.h; ++y)
		for (int x = area.x; x < area.x + area.w; ++x)
			Put(Layer().pixels, x, y, {0, 0, 0, 0});
	Changed();
}

Result<bool, String> TextureDocument::Generate(const String &kind, uint32_t seed) {
	const PaintRect area = Area();
	const Rgba a = brush.color, b = brush.secondary;
	std::function<Rgba(int, int)> color;
	if (kind == "noise") {
		color = [&](int x, int y) {
			const float n = ValueNoise(seed, float(x), float(y), 8.f) * 0.55f + ValueNoise(seed + 1, float(x), float(y), 4.f) * 0.3f +
							ValueNoise(seed + 2, float(x), float(y), 2.f) * 0.15f;
			return Mix(a, b, n);
		};
	} else if (kind == "gradient") {
		color = [&](int, int y) { return Mix(a, b, area.h > 1 ? float(y - area.y) / float(area.h - 1) : 0.f); };
	} else if (kind == "bricks") {
		const int bw = std::max(4, area.w / 4), bh = std::max(2, area.h / 8);
		color = [&, bw, bh](int x, int y) {
			const int row = (y - area.y) / bh, ly = (y - area.y) % bh;
			const int lx = ((x - area.x) + (row % 2) * (bw / 2)) % bw;
			const bool mortar = ly == bh - 1 || lx == bw - 1;
			const float shade = 0.85f + 0.15f * float(voxel::HashPosition(seed, (x - area.x + (row % 2) * (bw / 2)) / bw, 0, row, 5) % 100u) / 99.f;
			return mortar ? b : Mix(Rgba{0, 0, 0, a[3]}, a, shade);
		};
	} else if (kind == "checker") {
		const int cell = std::max(1, std::min(area.w, area.h) / 8);
		color = [&, cell](int x, int y) { return (((x - area.x) / cell + (y - area.y) / cell) % 2) == 0 ? a : b; };
	} else {
		return Err(String::Format("motif inconnu « %s » (noise, gradient, bricks, checker)", kind.CStr()));
	}
	Commit();
	for (int y = area.y; y < area.y + area.h; ++y)
		for (int x = area.x; x < area.x + area.w; ++x)
			Put(Layer().pixels, x, y, color(x, y));
	Changed();
	return Ok(true);
}

// ── Résultats ────────────────────────────────────────────────────────────────

Image TextureDocument::Composite() const {
	Image out = Blank(m_width, m_height);
	for (const PaintLayer &layer : m_layers) {
		if (!layer.visible || layer.opacity <= 0.f)
			continue;
		for (int y = 0; y < m_height; ++y)
			for (int x = 0; x < m_width; ++x)
				Put(out, x, y, BlendPixel(At(layer.pixels, x, y), At(out, x, y), layer.blend, layer.opacity));
	}
	return out;
}

Image TextureDocument::Rendered(int size) const {
	return voxel::TextureAtlas::Resample(Composite(), std::clamp(size, 1, MAX_RENDER));
}

std::vector<Image> TextureDocument::MipChain(int size) const {
	std::vector<Image> chain{Rendered(size)};
	while (chain.back().width > 1)
		chain.push_back(voxel::TextureAtlas::Resample(chain.back(), chain.back().width / 2));
	return chain;
}

Image TextureDocument::Glow(int size) const { return voxel::TextureAtlas::Glow(Rendered(size)); }

// ── Fichiers ─────────────────────────────────────────────────────────────────

Result<bool, String> TextureDocument::Export(const String &path, int size) const {
	return voxel::SaveImageFile(size > 0 ? Rendered(size) : Composite(), path);
}

Result<TextureDocument, String> TextureDocument::Import(const String &path) {
	Result<Image, String> image = voxel::LoadImageFile(path);
	if (image.IsError())
		return Err(image.Error());
	String name = path;
	if (const size_t slash = name.Rfind('/'); slash != String::NPOS)
		name = name.Substr(slash + 1);
	if (const size_t dot = name.Find('.'); dot != String::NPOS)
		name = name.Substr(0, dot);
	return FromImage(std::move(image).Unwrap(), name);
}

Result<bool, String> TextureDocument::SaveProject(const String &dir) const {
	std::error_code ec;
	std::filesystem::create_directories(dir.CStr(), ec);
	dataio::NodePtr root = dataio::Node::MakeObject();
	root->Set("format", json::MakeStr("voxelia.texture"));
	root->Set("name", json::MakeStr(m_name));
	root->Set("width", json::MakeInt(m_width));
	root->Set("height", json::MakeInt(m_height));
	dataio::NodePtr layers = dataio::Node::MakeArray();
	for (size_t i = 0; i < m_layers.size(); ++i) {
		const PaintLayer &l = m_layers[i];
		const String file = String::Format("layer_%zu.png", i);
		if (Result<bool, String> saved = voxel::SaveImageFile(l.pixels, dir + String("/") + file); saved.IsError())
			return saved;
		dataio::NodePtr n = dataio::Node::MakeObject();
		n->Set("name", json::MakeStr(l.name));
		n->Set("group", json::MakeStr(l.group));
		n->Set("file", json::MakeStr(file));
		n->Set("visible", json::MakeBool(l.visible));
		n->Set("opacity", json::MakeNum(double(l.opacity)));
		n->Set("blend", json::MakeStr(PaintBlendId(l.blend)));
		layers->Push(n);
	}
	root->Set("layers", layers);
	root->Set("active", json::MakeInt(m_active));
	return json::WriteFile(dir + String("/layers.json"), root);
}

Result<TextureDocument, String> TextureDocument::LoadProject(const String &dir) {
	Result<dataio::NodePtr, String> read = json::ReadFile(dir + String("/layers.json"));
	if (read.IsError())
		return Err(read.Error());
	const dataio::NodePtr root = read.Value();
	Result<TextureDocument, String> created = Create(json::Int(root, "width", 0), json::Int(root, "height", 0), json::Str(root, "name"));
	if (created.IsError())
		return created;
	TextureDocument doc = std::move(created).Unwrap();
	dataio::NodePtr layers = json::At(root, "layers");
	if (!layers || !layers->IsArray() || layers->GetSize() == 0)
		return Err(String("layers.json : « layers » vide"));
	doc.m_layers.clear();
	for (size_t i = 0; i < layers->GetSize(); ++i) {
		const dataio::NodePtr n = layers->At(i);
		Result<Image, String> pixels = voxel::LoadImageFile(dir + String("/") + json::Str(n, "file"));
		if (pixels.IsError())
			return Err(pixels.Error());
		if (pixels.Value().width != doc.m_width || pixels.Value().height != doc.m_height)
			return Err(String::Format("calque %zu : taille différente de la toile", i));
		PaintLayer layer{json::Str(n, "name", String::Format("Calque %zu", i)), json::Str(n, "group"), std::move(pixels).Unwrap()};
		layer.visible = json::Bool(n, "visible", true);
		layer.opacity = std::clamp(float(json::Num(n, "opacity", 1.0)), 0.f, 1.f);
		layer.blend = PaintBlendOf(json::Str(n, "blend", "normal")).UnwrapOr(PaintBlend::NORMAL);
		doc.m_layers.push_back(std::move(layer));
	}
	doc.m_active = std::clamp(json::Int(root, "active", 0), 0, int(doc.m_layers.size()) - 1);
	return Ok(std::move(doc));
}

} // namespace voxelia::studio
