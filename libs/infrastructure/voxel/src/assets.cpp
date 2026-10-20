// Définitions de voxel/assets.hpp
#include <voxel/assets.hpp>
#include <voxel/rig.hpp>
#include <voxel/json.hpp>
#include <voxel/worldgen.hpp>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <array>
#include <cctype>
#include <cstdlib>
#include <fstream>
#include <sstream>

#include <sdl3/image.hpp>

namespace voxel {

// ── Images ───────────────────────────────────────────────────────────────────

Result<Image, String> LoadImageFile(const String &path) {
	auto surface = sdl3::ImgLoad(path);
	if (surface.IsError())
		return Err(String::Format("%s : %s", path.CStr(), String(surface.Error()).CStr()));
	auto rgba = surface.Value().Convert(sdl3::PixelFormat::RGBA32);
	if (rgba.IsError())
		return Err(String::Format("%s : conversion RGBA impossible", path.CStr()));
	SDL_Surface *s = rgba.Value().Get();
	Image image;
	image.width = s->w;
	image.height = s->h;
	image.rgba.resize(size_t(s->w * s->h * 4));
	for (int y = 0; y < s->h; ++y)
		std::memcpy(&image.rgba[size_t(y * s->w * 4)], static_cast<const uint8_t *>(s->pixels) + size_t(y * s->pitch), size_t(s->w * 4));
	return Ok(std::move(image));
}

namespace {

struct Rgb {
	float r, g, b, a;
};

Rgb ColorOf(const NodePtr &node, const char *path, Rgb fallback) {
	NodePtr n = json::At(node, path);
	if (!n || !n->IsArray() || n->GetSize() < 3)
		return fallback;
	auto c = [&](size_t i, float def) {
		if (i >= n->GetSize())
			return def;
		const float v = float(json::Num(n->At(i), "", def));
		return n->At(i)->IsFloat() && v <= 1.f ? v : v / 255.f;
	};
	return {c(0, 1.f), c(1, 1.f), c(2, 1.f), c(3, 1.f)};
}

} // namespace

Image GenerateTexture(const NodePtr &p, uint32_t seed) {
	Image image;
	const int size = std::clamp(json::Int(p, "size", 16), 2, 256);
	image.width = image.height = size;
	image.rgba.resize(size_t(size * size * 4));
	const Rgb base = ColorOf(p, "color", {0.6f, 0.6f, 0.6f, 1.f});
	const Rgb second = ColorOf(p, "color2", {base.r * 0.7f, base.g * 0.7f, base.b * 0.7f, 1.f});
	const float noise = float(json::Num(p, "noise", 0.15));
	const float alpha = float(json::Num(p, "alpha", base.a));
	const String pattern = json::Str(p, "pattern", "noise");
	const float density = float(json::Num(p, "density", 0.12));
	for (int y = 0; y < size; ++y)
		for (int x = 0; x < size; ++x) {
			const float n = (RandomAt(seed, x, y, 0, 1) - 0.5f) * 2.f * noise;
			float t = 0.f; // 0 : couleur de base, 1 : seconde couleur
			float a = alpha;
			if (pattern == "bricks") {
				const int row = y / (size / 4), offset = (row % 2) * (size / 4);
				const bool mortar = y % (size / 4) == 0 || (x + offset) % (size / 2) == 0;
				t = mortar ? 1.f : 0.f;
			} else if (pattern == "planks") {
				t = (y % (size / 4) == 0) ? 1.f : (RandomAt(seed, x / 4, y / (size / 4), 0, 2) < 0.15f ? 0.5f : 0.f);
			} else if (pattern == "spots" || pattern == "ore") {
				t = RandomAt(seed, x / 2, y / 2, 0, 3) < density ? 1.f : 0.f;
			} else if (pattern == "leaves") {
				t = RandomAt(seed, x, y, 0, 4) < 0.3f ? 1.f : 0.f;
				a = RandomAt(seed, x, y, 0, 5) < 0.18f ? 0.f : alpha;
			} else if (pattern == "stripes") {
				t = (x / std::max(1, size / 8)) % 2 ? 1.f : 0.f;
			} else if (pattern == "checker") {
				t = ((x / (size / 2)) + (y / (size / 2))) % 2 ? 1.f : 0.f;
			} else if (pattern == "grass_side") {
				const int band = size / 4 + int(RandomAt(seed, x, 0, 0, 6) * float(size / 6));
				t = y < band ? 1.f : 0.f; // couleur2 : bande d'herbe en haut
			} else if (pattern == "log_top") {
				const float dx = float(x) - float(size) / 2.f + 0.5f, dy = float(y) - float(size) / 2.f + 0.5f;
				const float r = std::sqrt(dx * dx + dy * dy);
				t = r > float(size) * 0.45f ? 1.f : (int(r) % 3 == 0 ? 0.5f : 0.f);
			} else if (pattern == "glass") {
				const bool edge = x == 0 || y == 0 || x == size - 1 || y == size - 1;
				t = edge ? 1.f : 0.f;
				a = edge ? 1.f : alpha;
			} else if (pattern == "water") {
				t = 0.5f + 0.5f * std::sin(float(x + y) * 0.8f + RandomAt(seed, x, y, 0, 7));
			} else if (pattern == "cross") { // fleur / herbe haute : silhouette sur fond transparent
				const int c = size / 2;
				const bool stem = std::abs(x - c) <= 0 && y > size / 3;
				const bool blade = std::abs(x - c) <= (size - y) / 4 && y > size / 3 && RandomAt(seed, x, y, 0, 8) < 0.6f;
				const bool petals = pattern == "cross" && y <= size / 3 && std::abs(x - c) + std::abs(y - size / 5) <= size / 6;
				t = petals ? 1.f : 0.f;
				a = (stem || blade || petals) ? 1.f : 0.f;
			}
			const float k = std::clamp(t, 0.f, 1.f);
			const float r = std::clamp(base.r * (1.f - k) + second.r * k + n, 0.f, 1.f);
			const float g = std::clamp(base.g * (1.f - k) + second.g * k + n, 0.f, 1.f);
			const float b = std::clamp(base.b * (1.f - k) + second.b * k + n, 0.f, 1.f);
			uint8_t *px = &image.rgba[size_t((y * size + x) * 4)];
			px[0] = uint8_t(r * 255.f + 0.5f);
			px[1] = uint8_t(g * 255.f + 0.5f);
			px[2] = uint8_t(b * 255.f + 0.5f);
			px[3] = uint8_t(std::clamp(a, 0.f, 1.f) * 255.f + 0.5f);
		}
	return image;
}

// ── Composition ──────────────────────────────────────────────────────────────

namespace {

Image Resized(const Image &src, int width, int height) {
	if (src.width == width && src.height == height)
		return src;
	Image out;
	out.width = width;
	out.height = height;
	out.rgba.resize(size_t(width * height * 4));
	for (int y = 0; y < height; ++y)
		for (int x = 0; x < width; ++x)
			std::memcpy(&out.rgba[size_t((y * width + x) * 4)], src.At(x * src.width / width, y * src.height / height), 4);
	return out;
}

/// b par-dessus a (« over »), à la plus grande des deux tailles.
Image Overlay(const Image &a, const Image &b) {
	const int w = std::max(a.width, b.width), h = std::max(a.height, b.height);
	Image out = Resized(a, w, h);
	const Image top = Resized(b, w, h);
	for (size_t i = 0; i < out.rgba.size(); i += 4) {
		const float sa = top.rgba[i + 3] / 255.f, da = out.rgba[i + 3] / 255.f;
		const float oa = sa + da * (1.f - sa);
		for (size_t c = 0; c < 3; ++c) {
			const float v = oa <= 0.f ? 0.f : (top.rgba[i + c] * sa + out.rgba[i + c] * da * (1.f - sa)) / oa;
			out.rgba[i + c] = uint8_t(std::clamp(v, 0.f, 255.f) + 0.5f);
		}
		out.rgba[i + 3] = uint8_t(oa * 255.f + 0.5f);
	}
	return out;
}

/// Couleur r, g, b [, a] en 0..1 (arguments en 0..255, ou 0..1 si tous ≤ 1).
std::array<float, 4> ColorArgs(const std::vector<double> &args, size_t count) {
	std::array<float, 4> c{1.f, 1.f, 1.f, 1.f};
	bool unit = true;
	for (size_t i = 0; i < std::min(args.size(), count); ++i)
		unit = unit && args[i] <= 1.0;
	for (size_t i = 0; i < std::min(args.size(), count); ++i)
		c[i] = std::clamp(float(unit ? args[i] : args[i] / 255.0), 0.f, 1.f);
	return c;
}

float Fraction(double v) { return float(v > 1.0 ? v / 255.0 : v); }

using PixelFn = std::function<void(uint8_t *px)>;

void ForEachPixel(Image &image, const PixelFn &fn) {
	for (size_t i = 0; i < image.rgba.size(); i += 4)
		fn(&image.rgba[i]);
}

uint8_t Byte(float v) { return uint8_t(std::clamp(v, 0.f, 255.f) + 0.5f); }

Result<Image, String> ApplyFilter(const String &name, const std::vector<double> &args, Image image) {
	auto need = [&](size_t n) -> Result<bool, String> {
		if (args.size() < n)
			return Err(String::Format("%s : %d argument(s) attendu(s)", name.CStr(), int(n)));
		return Ok(true);
	};
	const String f = name.ToLower();
	if (f == "lighting" || f == "ligthing" || f == "light" || f == "brightness" || f == "darken") {
		if (auto r = need(1); r.IsError())
			return Err(r.Error());
		const float k = f == "darken" ? 1.f - float(args[0]) : float(args[0]);
		ForEachPixel(image, [k](uint8_t *px) {
			for (int c = 0; c < 3; ++c)
				px[c] = Byte(px[c] * k);
		});
	} else if (f == "tint" || f == "multiply") {
		if (auto r = need(3); r.IsError())
			return Err(r.Error());
		const auto col = ColorArgs(args, 3);
		ForEachPixel(image, [&col](uint8_t *px) {
			for (int c = 0; c < 3; ++c)
				px[c] = Byte(px[c] * col[size_t(c)]);
		});
	} else if (f == "colorize") {
		if (auto r = need(3); r.IsError())
			return Err(r.Error());
		const auto col = ColorArgs(args, 3);
		const float part = args.size() > 3 ? std::clamp(Fraction(args[3]), 0.f, 1.f) : 0.5f;
		ForEachPixel(image, [&col, part](uint8_t *px) {
			for (int c = 0; c < 3; ++c)
				px[c] = Byte(px[c] * (1.f - part) + col[size_t(c)] * 255.f * part);
		});
	} else if (f == "opacity") {
		if (auto r = need(1); r.IsError())
			return Err(r.Error());
		const float a = std::clamp(Fraction(args[0]), 0.f, 1.f);
		ForEachPixel(image, [a](uint8_t *px) { px[3] = Byte(px[3] * a); });
	} else if (f == "grayscale" || f == "greyscale") {
		ForEachPixel(image, [](uint8_t *px) {
			const uint8_t l = Byte(px[0] * 0.299f + px[1] * 0.587f + px[2] * 0.114f);
			px[0] = px[1] = px[2] = l;
		});
	} else if (f == "invert") {
		ForEachPixel(image, [](uint8_t *px) {
			for (int c = 0; c < 3; ++c)
				px[c] = uint8_t(255 - px[c]);
		});
	} else if (f == "noalpha") {
		ForEachPixel(image, [](uint8_t *px) { px[3] = 255; });
	} else if (f == "flipx" || f == "flipy" || f == "rotate") {
		int turns = 0; // quarts de tour horaires
		if (f == "rotate") {
			if (auto r = need(1); r.IsError())
				return Err(r.Error());
			turns = ((int(std::lround(args[0] / 90.0)) % 4) + 4) % 4;
		}
		const bool swap = turns % 2 == 1;
		Image out;
		out.width = swap ? image.height : image.width;
		out.height = swap ? image.width : image.height;
		out.rgba.resize(image.rgba.size());
		for (int y = 0; y < out.height; ++y)
			for (int x = 0; x < out.width; ++x) {
				int sx = x, sy = y;
				if (f == "flipx")
					sx = image.width - 1 - x;
				else if (f == "flipy")
					sy = image.height - 1 - y;
				else if (turns == 1)
					sx = y, sy = image.height - 1 - x;
				else if (turns == 2)
					sx = image.width - 1 - x, sy = image.height - 1 - y;
				else if (turns == 3)
					sx = image.width - 1 - y, sy = x;
				std::memcpy(&out.rgba[size_t((y * out.width + x) * 4)], image.At(sx, sy), 4);
			}
		image = std::move(out);
	} else if (f == "resize") {
		if (auto r = need(2); r.IsError())
			return Err(r.Error());
		image = Resized(image, std::clamp(int(args[0]), 1, 1024), std::clamp(int(args[1]), 1, 1024));
	} else if (f == "crop" || f == "frame") {
		int x0 = 0, y0 = 0, w = image.width, h = image.height;
		if (f == "crop") {
			if (auto r = need(4); r.IsError())
				return Err(r.Error());
			x0 = int(args[0]), y0 = int(args[1]), w = int(args[2]), h = int(args[3]);
		} else {
			if (auto r = need(2); r.IsError())
				return Err(r.Error());
			const int total = std::max(1, int(args[1]));
			h = image.height / total;
			y0 = std::clamp(int(args[0]), 0, total - 1) * h;
		}
		if (w <= 0 || h <= 0 || x0 < 0 || y0 < 0 || x0 + w > image.width || y0 + h > image.height)
			return Err(String::Format("%s : zone hors de l'image %d×%d", name.CStr(), image.width, image.height));
		Image out;
		out.width = w;
		out.height = h;
		out.rgba.resize(size_t(w * h * 4));
		for (int y = 0; y < h; ++y)
			std::memcpy(&out.rgba[size_t(y * w * 4)], image.At(x0, y0 + y), size_t(w * 4));
		image = std::move(out);
	} else {
		return Err(String::Format("filtre de texture inconnu « %s »", name.CStr()));
	}
	return Ok(std::move(image));
}

/// Analyseur descendant : expr := terme ('^' terme)* ; terme := '(' expr ')'
/// | nom '(' args ')' | nom.
class Composer {
public:
	Composer(const String &text, const ImageLoader &load) : m_text(text), m_load(load) {}

	Result<Image, String> Run() {
		auto image = Expression();
		if (image.IsError())
			return image;
		Skip();
		if (m_pos < m_text.GetSize())
			return Fail("caractère inattendu");
		return image;
	}

private:
	Result<Image, String> Fail(const char *what) const {
		return Err(String::Format("composition « %s », position %d : %s", m_text.CStr(), int(m_pos), what));
	}
	void Skip() {
		while (m_pos < m_text.GetSize() && std::isspace(uint8_t(m_text.CharAt(m_pos))))
			++m_pos;
	}
	bool Peek(char c) {
		Skip();
		return m_pos < m_text.GetSize() && m_text.CharAt(m_pos) == c;
	}

	Result<Image, String> Expression() {
		Option<Image> image;
		do {
			Skip();
			const size_t start = m_pos;
			String name;
			std::vector<double> args;
			bool call = false;
			if (!Peek('(')) {
				name = Name();
				if (name.IsEmpty())
					return Fail("nom de texture attendu");
				call = Peek('(');
				if (call) {
					if (auto r = Arguments(args); r.IsError())
						return Err(r.Error());
				}
			}
			Result<Image, String> next = Err(String());
			if (name.IsEmpty()) { // ( … )
				++m_pos;
				next = Expression();
				if (next.IsError())
					return next;
				if (!Peek(')'))
					return Fail("« ) » attendue");
				++m_pos;
			} else if (call && name.ToLower() != "color" && name.ToLower() != "fill") {
				if (image.IsNone()) {
					m_pos = start;
					return Fail("filtre sans image à sa gauche");
				}
				auto filtered = ApplyFilter(name, args, std::move(image).Unwrap());
				if (filtered.IsError())
					return Err(String::Format("composition « %s » : %s", m_text.CStr(), filtered.Error().CStr()));
				image = Some(std::move(filtered).Unwrap());
				continue;
			} else if (call) { // aplat
				if (args.size() < 3)
					return Fail("color(r, g, b [, a]) attendu");
				const auto c = ColorArgs(args, 4);
				Image flat;
				flat.width = flat.height = 16;
				flat.rgba.resize(16 * 16 * 4);
				ForEachPixel(flat, [&c](uint8_t *px) {
					for (int k = 0; k < 4; ++k)
						px[k] = Byte(c[size_t(k)] * 255.f);
				});
				next = Ok(std::move(flat));
			} else {
				next = m_load(name);
				if (next.IsError())
					return next;
			}
			image = image.IsNone() ? Some(std::move(next).Unwrap()) : Some(Overlay(image.Value(), next.Value()));
		} while (Peek('^') && ++m_pos);
		if (image.IsNone())
			return Fail("image attendue");
		return Ok(std::move(image).Unwrap());
	}

	String Name() {
		Skip();
		const size_t start = m_pos;
		while (m_pos < m_text.GetSize()) {
			const char c = m_text.CharAt(m_pos);
			if (c == '^' || c == '(' || c == ')' || c == ',' || std::isspace(uint8_t(c)))
				break;
			++m_pos;
		}
		return m_text.Substr(start, m_pos - start);
	}

	Result<bool, String> Arguments(std::vector<double> &args) {
		++m_pos; // (
		while (!Peek(')')) {
			Skip();
			const char *begin = m_text.CStr() + m_pos;
			char *end = nullptr;
			const double v = std::strtod(begin, &end);
			if (end == begin)
				return Err(Fail("nombre attendu").Error());
			args.push_back(v);
			m_pos += size_t(end - begin);
			if (Peek(','))
				++m_pos;
			else if (!Peek(')'))
				return Err(Fail("« , » ou « ) » attendue").Error());
		}
		++m_pos; // )
		return Ok(true);
	}

	const String &m_text;
	const ImageLoader &m_load;
	size_t m_pos = 0;
};

} // namespace

Result<Image, String> ComposeTexture(const String &expression, const ImageLoader &load) {
	return Composer(expression, load).Run();
}

Result<bool, String> TextureImage::DoLoad() {
	if (m_builder) {
		Result<Image, String> image = m_builder();
		if (image.IsError())
			return Err(image.Error());
		m_image = std::move(image).Unwrap();
	} else if (!m_path.IsEmpty()) {
		Result<Image, String> image = LoadImageFile(m_path);
		if (image.IsError())
			return Err(image.Error());
		m_image = std::move(image).Unwrap();
	} else if (m_procedural) {
		m_image = GenerateTexture(m_procedural, m_seed);
	} else {
		return Err(String::Format("%s : ni fichier ni description", m_key.CStr()));
	}
	return Ok(true);
}

// ── Modèles ──────────────────────────────────────────────────────────────────

Box ModelMesh::Bounds() const {
	if (positions.empty())
		return Box{{0.f, 0.f, 0.f}, {0.f, 0.f, 0.f}};
	Box b{positions[0], positions[0]};
	for (const math::FVector3 &p : positions) {
		b.min = {std::min(b.min.x, p.x), std::min(b.min.y, p.y), std::min(b.min.z, p.z)};
		b.max = {std::max(b.max.x, p.x), std::max(b.max.y, p.y), std::max(b.max.z, p.z)};
	}
	return b;
}

void ModelMesh::Normalize(bool fit) {
	if (!fit || positions.empty())
		return;
	const Box b = Bounds();
	const math::FVector3 size = b.Size();
	const float scale = 1.f / std::max({size.x, size.y, size.z, 1e-6f});
	const math::FVector3 center{(b.min.x + b.max.x) * 0.5f, b.min.y, (b.min.z + b.max.z) * 0.5f};
	for (math::FVector3 &p : positions)
		p = (p - center) * scale + math::FVector3{0.5f, 0.f, 0.5f};
}

namespace {

/// Normales plates des triangles qui n'en ont pas.
void ComputeMissingNormals(ModelMesh &m) {
	if (m.normals.size() == m.positions.size())
		return;
	m.normals.assign(m.positions.size(), {0.f, 0.f, 0.f});
	for (size_t i = 0; i + 2 < m.indices.size(); i += 3) {
		const math::FVector3 &a = m.positions[m.indices[i]], &b = m.positions[m.indices[i + 1]], &c = m.positions[m.indices[i + 2]];
		const math::FVector3 n = (b - a).Cross(c - a);
		for (int k = 0; k < 3; ++k)
			m.normals[m.indices[i + size_t(k)]] = m.normals[m.indices[i + size_t(k)]] + n;
	}
	for (math::FVector3 &n : m.normals)
		n = n.Length() > 1e-9f ? n.Normalize() : math::FVector3{0.f, 1.f, 0.f};
}

} // namespace

Result<ModelMesh, String> ParseObj(const String &text) {
	ModelMesh mesh;
	std::vector<math::FVector3> v, vn;
	std::vector<math::FVector2> vt;
	std::istringstream in(std::string(text.CStr(), text.GetSize()));
	std::string line;
	String texture;
	auto openGroup = [&]() {
		if (!mesh.groups.empty() && mesh.groups.back().count == 0)
			mesh.groups.back().texture = texture;
		else
			mesh.groups.push_back({uint32_t(mesh.indices.size() / 3), 0, texture});
	};
	openGroup();
	int lineNo = 0;
	while (std::getline(in, line)) {
		++lineNo;
		std::istringstream ls(line);
		std::string tag;
		ls >> tag;
		if (tag == "v") {
			math::FVector3 p;
			ls >> p.x >> p.y >> p.z;
			v.push_back(p);
		} else if (tag == "vt") {
			math::FVector2 t;
			ls >> t.x >> t.y;
			vt.push_back({t.x, 1.f - t.y});
		} else if (tag == "vn") {
			math::FVector3 n;
			ls >> n.x >> n.y >> n.z;
			vn.push_back(n);
		} else if (tag == "usemtl") {
			std::string name;
			ls >> name;
			texture = String(name.c_str());
			openGroup();
		} else if (tag == "f") {
			std::vector<uint32_t> poly;
			std::string corner;
			while (ls >> corner) {
				int iv = 0, it = 0, in2 = 0;
				const int n = std::sscanf(corner.c_str(), "%d/%d/%d", &iv, &it, &in2);
				if (n < 1 || std::sscanf(corner.c_str(), "%d//%d", &iv, &in2) == 2)
					it = 0;
				auto resolve = [](int index, size_t size) { return index < 0 ? int(size) + index : index - 1; };
				const int pv = resolve(iv, v.size()), pt = it ? resolve(it, vt.size()) : -1, pn = in2 ? resolve(in2, vn.size()) : -1;
				if (pv < 0 || size_t(pv) >= v.size())
					return Err(String::Format("obj ligne %d : sommet %d inexistant", lineNo, iv));
				mesh.positions.push_back(v[size_t(pv)]);
				mesh.uvs.push_back(pt >= 0 && size_t(pt) < vt.size() ? vt[size_t(pt)] : math::FVector2{0.f, 0.f});
				if (pn >= 0 && size_t(pn) < vn.size())
					mesh.normals.push_back(vn[size_t(pn)]);
				poly.push_back(uint32_t(mesh.positions.size() - 1));
			}
			for (size_t k = 1; k + 1 < poly.size(); ++k) { // éventail
				mesh.indices.push_back(poly[0]);
				mesh.indices.push_back(poly[k]);
				mesh.indices.push_back(poly[k + 1]);
				++mesh.groups.back().count;
			}
		}
	}
	if (mesh.normals.size() != mesh.positions.size())
		mesh.normals.clear();
	ComputeMissingNormals(mesh);
	std::erase_if(mesh.groups, [](const ModelMesh::Group &g) { return g.count == 0; });
	if (mesh.indices.empty())
		return Err(String("obj sans face"));
	return Ok(std::move(mesh));
}

namespace {

/// Lecteur de B3D (Blitz3D) : blocs « TAG » + longueur 32 bits.
struct B3dReader {
	const std::vector<uint8_t> &data;
	size_t at = 0;
	bool ok = true;
	int32_t I32() {
		if (at + 4 > data.size()) {
			ok = false;
			return 0;
		}
		int32_t v;
		std::memcpy(&v, &data[at], 4);
		at += 4;
		return v;
	}
	float F32() {
		const int32_t i = I32();
		float f;
		std::memcpy(&f, &i, 4);
		return f;
	}
	std::string Str() {
		std::string s;
		while (at < data.size() && data[at] != 0)
			s.push_back(char(data[at++]));
		++at;
		return s;
	}
	std::string Tag() {
		if (at + 4 > data.size()) {
			ok = false;
			return {};
		}
		std::string t(reinterpret_cast<const char *>(&data[at]), 4);
		at += 4;
		return t;
	}
};

void ReadB3dNode(B3dReader &r, size_t end, const math::FMatrix4 &parent, std::vector<std::string> &textures,
				 std::vector<int> &brushTexture, ModelMesh &mesh) {
	(void)r.Str(); // nom
	const math::FVector3 pos{r.F32(), r.F32(), r.F32()};
	const math::FVector3 scale{r.F32(), r.F32(), r.F32()};
	const float qw = r.F32(), qx = r.F32(), qy = r.F32(), qz = r.F32();
	math::FQuaternion q{qx, qy, qz, qw};
	const math::FMatrix4 local = math::FMatrix4::Translate(pos) * q.ToMat4() * math::FMatrix4::Scale(scale);
	const math::FMatrix4 world = parent * local;
	while (r.ok && r.at + 8 <= end) {
		const std::string tag = r.Tag();
		const size_t chunkEnd = r.at + 4 + size_t(r.I32());
		if (tag == "MESH") {
			const int meshBrush = r.I32();
			uint32_t base = uint32_t(mesh.positions.size());
			while (r.ok && r.at + 8 <= chunkEnd) {
				const std::string sub = r.Tag();
				const size_t subEnd = r.at + 4 + size_t(r.I32());
				if (sub == "VRTS") {
					base = uint32_t(mesh.positions.size());
					const int flags = r.I32(), sets = r.I32(), setSize = r.I32();
					while (r.ok && r.at < subEnd) {
						math::FVector3 p{r.F32(), r.F32(), r.F32()};
						math::FVector3 n{0.f, 1.f, 0.f};
						if (flags & 1)
							n = {r.F32(), r.F32(), r.F32()};
						if (flags & 2)
							for (int k = 0; k < 4; ++k)
								(void)r.F32();
						math::FVector2 uv{0.f, 0.f};
						for (int s = 0; s < sets; ++s)
							for (int k = 0; k < setSize; ++k) {
								const float value = r.F32();
								if (s == 0 && k == 0)
									uv.x = value;
								if (s == 0 && k == 1)
									uv.y = value;
							}
						// Blitz3D est en repère INDIRECT : on retourne Z.
						p = world.TransformPoint(p);
						p.z = -p.z;
						n.z = -n.z;
						mesh.positions.push_back(p);
						mesh.normals.push_back(n);
						mesh.uvs.push_back(uv);
					}
				} else if (sub == "TRIS") {
					int brush = r.I32();
					if (brush < 0)
						brush = meshBrush;
					String texture;
					if (brush >= 0 && size_t(brush) < brushTexture.size() && brushTexture[size_t(brush)] >= 0 &&
						size_t(brushTexture[size_t(brush)]) < textures.size())
						texture = String(textures[size_t(brushTexture[size_t(brush)])].c_str());
					mesh.groups.push_back({uint32_t(mesh.indices.size() / 3), 0, texture});
					while (r.ok && r.at + 12 <= subEnd) {
						const uint32_t a = uint32_t(r.I32()), b = uint32_t(r.I32()), c = uint32_t(r.I32());
						// Ordre inversé (repère retourné).
						mesh.indices.push_back(base + a);
						mesh.indices.push_back(base + c);
						mesh.indices.push_back(base + b);
						++mesh.groups.back().count;
					}
				}
				r.at = subEnd;
			}
		} else if (tag == "NODE") {
			ReadB3dNode(r, chunkEnd, world, textures, brushTexture, mesh);
		}
		r.at = chunkEnd; // BONE, KEYS, ANIM… : ignorés (modèle statique)
	}
}

} // namespace

Result<ModelMesh, String> ParseB3D(const std::vector<uint8_t> &bytes) {
	B3dReader r{bytes};
	if (r.Tag() != "BB3D")
		return Err(String("b3d : en-tête BB3D attendu"));
	const size_t end = r.at + 4 + size_t(r.I32());
	(void)r.I32(); // version
	ModelMesh mesh;
	std::vector<std::string> textures;
	std::vector<int> brushTexture;
	while (r.ok && r.at + 8 <= std::min(end, bytes.size())) {
		const std::string tag = r.Tag();
		const size_t chunkEnd = r.at + 4 + size_t(r.I32());
		if (tag == "TEXS") {
			while (r.ok && r.at < chunkEnd) {
				std::string name = r.Str();
				for (int k = 0; k < 7; ++k) // flags, blend, x/y pos, x/y scale, rotation
					(void)r.I32();
				// Nom de fichier → nom de texture (sans dossier ni extension).
				const size_t slash = name.find_last_of("/\\");
				if (slash != std::string::npos)
					name = name.substr(slash + 1);
				const size_t dot = name.find_last_of('.');
				if (dot != std::string::npos)
					name = name.substr(0, dot);
				textures.push_back(name);
			}
		} else if (tag == "BRUS") {
			const int texCount = r.I32();
			while (r.ok && r.at < chunkEnd) {
				(void)r.Str();
				for (int k = 0; k < 4 + 1; ++k) // rgba, shininess
					(void)r.F32();
				(void)r.I32(); // blend
				(void)r.I32(); // fx
				int first = -1;
				for (int k = 0; k < texCount; ++k) {
					const int id = r.I32();
					if (k == 0)
						first = id;
				}
				brushTexture.push_back(first);
			}
		} else if (tag == "NODE") {
			ReadB3dNode(r, chunkEnd, math::FMatrix4::Identity(), textures, brushTexture, mesh);
		}
		r.at = chunkEnd;
	}
	if (mesh.indices.empty())
		return Err(String("b3d sans triangle"));
	std::erase_if(mesh.groups, [](const ModelMesh::Group &g) { return g.count == 0; });
	return Ok(std::move(mesh));
}

Result<ModelMesh, String> ModelFromJson(const NodePtr &data) {
	ModelMesh mesh;
	if (NodePtr boxes = json::At(data, "boxes"); boxes) {
		// Boîtes (convention nodebox) : six faces chacune.
		for (const Box &b : parse::NodeBoxes(boxes)) {
			const math::FVector3 lo = b.min, hi = b.max;
			const math::FVector3 c[8] = {{lo.x, lo.y, lo.z}, {hi.x, lo.y, lo.z}, {hi.x, hi.y, lo.z}, {lo.x, hi.y, lo.z},
										 {lo.x, lo.y, hi.z}, {hi.x, lo.y, hi.z}, {hi.x, hi.y, hi.z}, {lo.x, hi.y, hi.z}};
			const int faces[6][4] = {{1, 2, 6, 5}, {4, 7, 3, 0}, {3, 7, 6, 2}, {0, 1, 5, 4}, {5, 6, 7, 4}, {0, 3, 2, 1}};
			const math::FVector3 normals[6] = {{1, 0, 0}, {-1, 0, 0}, {0, 1, 0}, {0, -1, 0}, {0, 0, 1}, {0, 0, -1}};
			for (int f = 0; f < 6; ++f) {
				const uint32_t base = uint32_t(mesh.positions.size());
				const math::FVector2 uv[4] = {{0, 1}, {0, 0}, {1, 0}, {1, 1}};
				for (int k = 0; k < 4; ++k) {
					mesh.positions.push_back(c[faces[f][k]]);
					mesh.normals.push_back(normals[f]);
					mesh.uvs.push_back(uv[k]);
				}
				for (uint32_t i : {0u, 1u, 2u, 0u, 2u, 3u})
					mesh.indices.push_back(base + i);
			}
		}
	} else {
		const std::vector<std::vector<double>> vertices = json::NumRows(data, "vertices");
		for (const auto &v : vertices) {
			if (v.size() < 3)
				continue;
			mesh.positions.push_back({float(v[0]), float(v[1]), float(v[2])});
			mesh.uvs.push_back(v.size() >= 5 ? math::FVector2{float(v[3]), float(v[4])} : math::FVector2{0.f, 0.f});
		}
		for (const auto &f : json::NumRows(data, "faces"))
			for (size_t k = 1; k + 1 < f.size(); ++k)
				for (size_t i : {size_t(0), k, k + 1})
					if (f[i] >= 0 && size_t(f[i]) < mesh.positions.size())
						mesh.indices.push_back(uint32_t(f[i]));
		ComputeMissingNormals(mesh);
	}
	if (mesh.indices.empty())
		return Err(String("modèle json vide (« boxes » ou « vertices »/« faces » attendus)"));
	mesh.groups.push_back({0, uint32_t(mesh.indices.size() / 3), json::Str(data, "texture")});
	return Ok(std::move(mesh));
}

Result<bool, String> ModelResource::DoLoad() {
	Result<ModelMesh, String> mesh = Err(String());
	if (m_format == "json") {
		NodePtr data = m_data;
		if (!data && !m_path.IsEmpty()) {
			Result<NodePtr, String> read = json::ReadFile(m_path);
			if (read.IsError())
				return Err(read.Error());
			data = read.Unwrap();
		}
		mesh = ModelFromJson(data);
	} else {
		std::ifstream file(m_path.CStr(), std::ios::binary);
		if (!file)
			return Err(String::Format("%s : fichier illisible", m_path.CStr()));
		std::vector<uint8_t> bytes((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
		if (m_format == "b3d")
			mesh = ParseB3D(bytes);
		else
			mesh = ParseObj(String(reinterpret_cast<const char *>(bytes.data()), bytes.size()));
	}
	if (mesh.IsError())
		return Err(String::Format("%s : %s", m_key.CStr(), mesh.Error().CStr()));
	m_mesh = std::move(mesh).Unwrap();
	return Ok(true);
}

// ── Gestionnaire ─────────────────────────────────────────────────────────────

namespace {

String FormatOf(const String &path) {
	const String lower = path.ToLower();
	if (lower.EndsWith(".b3d") || lower.EndsWith(".b3b"))
		return "b3d";
	if (lower.EndsWith(".json"))
		return "json";
	return "obj";
}

uint32_t SeedOf(const String &id) {
	uint32_t h = 2166136261u;
	for (size_t i = 0; i < id.GetSize(); ++i)
		h = (h ^ uint8_t(id.CharAt(i))) * 16777619u;
	return h;
}

} // namespace

AssetManager::AssetManager(size_t threads) : m_cache(String(), threads) {
	m_cache.RegisterFactory(resources::AssetType::TEXTURE, [this](const resources::AssetRecord &record,
																  const resources::ResourceCache &) -> std::shared_ptr<resources::Resource> {
		TextureImage::Builder builder;
		if (json::Bool(record.params, "compose", false)) {
			const String id = record.key;
			builder = [this, id] { return RenderTexture(id); };
		}
		return std::make_shared<TextureImage>(record.key, record.path, builder ? NodePtr() : record.params, SeedOf(record.key),
											  std::move(builder));
	});
	m_cache.RegisterFactory(resources::AssetType::MESH, [](const resources::AssetRecord &record,
														   const resources::ResourceCache &) -> std::shared_ptr<resources::Resource> {
		return std::make_shared<ModelResource>(record.key, json::Str(record.params, "format", FormatOf(record.path)), record.path,
											   json::At(record.params, "data"));
	});
}

resources::AssetId AssetManager::TextureId(const String &id) { return resources::AssetId::FromKey((String("texture:") + id).View()); }
resources::AssetId AssetManager::ModelId(const String &id) { return resources::AssetId::FromKey((String("model:") + id).View()); }

void AssetManager::Declare(const ContentRegistry &content) {
	m_content = &content;
	for (const TextureDef *t : content.Textures()) {
		resources::AssetRecord record;
		record.id = TextureId(t->id);
		record.type = resources::AssetType::TEXTURE;
		record.name = t->id;
		record.key = t->id;
		record.path = t->path;
		record.params = t->procedural;
		if (!t->compose.IsEmpty()) {
			record.params = dataio::Node::MakeObject();
			record.params->Set("compose", json::MakeBool(true));
		}
		(void)m_cache.Declare(record);
	}
	for (const ModelDef *m : content.Models()) {
		resources::AssetRecord record;
		record.id = ModelId(m->id);
		record.type = resources::AssetType::MESH;
		record.name = m->id;
		record.key = m->id;
		record.path = m->path;
		record.params = dataio::Node::MakeObject();
		record.params->Set("format", json::MakeStr(m->format));
		if (m->data)
			record.params->Set("data", m->data);
		(void)m_cache.Declare(record);
	}
	for (const SoundDef *s : content.Sounds())
		for (size_t k = 0; k < std::max<size_t>(1, s->paths.size()); ++k) {
			resources::AssetRecord record;
			record.id = SoundId(s->id, int(k));
			record.type = resources::AssetType::AUDIO_CLIP;
			record.name = s->id;
			record.key = k == 0 ? s->id : s->id + String::Format("#%d", int(k));
			record.path = k < s->paths.size() ? s->paths[k] : s->path;
			(void)m_cache.Declare(record);
		}
}

std::shared_ptr<TextureImage> AssetManager::Texture(const String &id) {
	const resources::AssetId assetId = TextureId(id);
	if (!m_cache.Knows(assetId))
		return nullptr;
	(void)m_cache.LoadNow(assetId);
	return m_cache.GetReady(resources::AssetRef<TextureImage>{assetId}, id.View());
}

Result<Image, String> AssetManager::RenderTexture(const String &id, int depth) const {
	const TextureDef *def = m_content ? m_content->FindTexture(id) : nullptr;
	if (!def)
		return Err(String::Format("texture inconnue « %s »", id.CStr()));
	if (depth > 8)
		return Err(String::Format("%s : composition trop profonde (boucle ?)", id.CStr()));
	if (!def->compose.IsEmpty())
		return ComposeTexture(def->compose, [this, def, depth](const String &name) { return ResolveImage(name, *def, depth + 1); });
	Result<Image, String> image = !def->path.IsEmpty() ? LoadImageFile(def->path)
								  : def->procedural ? Result<Image, String>(Ok(GenerateTexture(def->procedural, SeedOf(id))))
													: Result<Image, String>(Err(String::Format("%s : ni fichier ni description", id.CStr())));
	if (image.IsOk() && def->frames > 1) { // bande animée : première image
		Image &strip = image.Value();
		strip.height = std::max(1, strip.height / def->frames);
		strip.rgba.resize(size_t(strip.width * strip.height * 4));
	}
	return image;
}

Result<Image, String> AssetManager::ResolveImage(const String &name, const TextureDef &owner, int depth) const {
	namespace fs = std::filesystem;
	if (!ContentRegistry::IsTextureExpression(name))
		return RenderTexture(ContentRegistry::Qualify(name, owner.mod), depth);
	// « core:dirt.png », « blocks/dirt.png » : d'abord la texture du même nom.
	const String withoutExt(fs::path(name.CStr()).replace_extension().generic_string().c_str());
	if (const String id = ContentRegistry::Qualify(withoutExt, owner.mod); m_content->FindTexture(id))
		return RenderTexture(id, depth);
	if (name.Contains(':'))
		return Err(String::Format("texture inconnue « %s »", withoutExt.CStr()));
	// Fichier : dossier de la définition, textures/, puis dossiers où le mod
	// range déjà ses textures.
	std::vector<fs::path> dirs;
	if (!owner.baseDir.IsEmpty()) {
		dirs.emplace_back(owner.baseDir.CStr());
		dirs.emplace_back(fs::path(owner.baseDir.CStr()) / "textures");
	}
	for (const TextureDef *t : m_content->Textures())
		if (t->mod == owner.mod && !t->path.IsEmpty())
			if (fs::path dir = fs::path(t->path.CStr()).parent_path();
				std::find(dirs.begin(), dirs.end(), dir) == dirs.end())
				dirs.push_back(dir);
	const fs::path file(name.CStr());
	if (file.is_absolute() && fs::exists(file))
		return LoadImageFile(name);
	for (const fs::path &dir : dirs)
		if (std::error_code ec; fs::exists(dir / file, ec))
			return LoadImageFile(String((dir / file).lexically_normal().string().c_str()));
	// « dirt.png » désigne aussi la texture core:dirt.
	const String stem(file.stem().string().c_str());
	if (m_content->FindTexture(ContentRegistry::Qualify(stem, owner.mod)))
		return RenderTexture(ContentRegistry::Qualify(stem, owner.mod), depth);
	return Err(String::Format("image « %s » introuvable (mod %s)", name.CStr(), owner.mod.CStr()));
}

std::shared_ptr<const RigModel> AssetManager::Rig(const String &id) {
	if (auto it = m_rigs.find(id.CStr()); it != m_rigs.end())
		return it->second;
	std::shared_ptr<const RigModel> rig;
	String error;
	const ModelDef *def = m_content ? m_content->FindModel(id) : nullptr;
	if (!def) {
		error = "modèle inconnu";
	} else if (def->format == "rig") {
		if (Result<RigModel, String> r = RigFromJson(def->data); r.IsOk())
			rig = std::make_shared<RigModel>(std::move(r).Unwrap());
		else
			error = r.Error();
	} else if (std::shared_ptr<ModelResource> model = Model(id); model) {
		// Maillage : sa texture est l'image du même nom à côté du fichier.
		namespace fs = std::filesystem;
		fs::path png = fs::path(def->path.CStr()).replace_extension(".png");
		rig = std::make_shared<RigModel>(RigFromMesh(model->Mesh(), fs::exists(png) ? String(png.string().c_str()) : String()));
	} else {
		error = "maillage illisible";
	}
	if (!rig)
		m_rigErrors.push_back(resources::MissingAsset{resources::AssetId::FromKey((String("rig:") + id).View()),
													  resources::AssetType::MESH, id, error});
	m_rigs[id.CStr()] = rig;
	return rig;
}

std::shared_ptr<ModelResource> AssetManager::Model(const String &idOrPath) {
	const resources::AssetId assetId = ModelId(idOrPath);
	if (!m_cache.Knows(assetId)) {
		// Fichier désigné directement par un bloc (« path »).
		resources::AssetRecord record;
		record.id = assetId;
		record.type = resources::AssetType::MESH;
		record.name = idOrPath;
		record.key = idOrPath;
		record.path = idOrPath;
		record.params = dataio::Node::MakeObject();
		record.params->Set("format", json::MakeStr(FormatOf(idOrPath)));
		(void)m_cache.Declare(record);
	}
	(void)m_cache.LoadNow(assetId);
	return m_cache.GetReady(resources::AssetRef<ModelResource>{assetId}, idOrPath.View());
}

resources::AssetId AssetManager::SoundId(const String &id, int variant) const {
	const String key = variant == 0 ? String("sound:") + id : String::Format("sound:%s#%d", id.CStr(), variant);
	return resources::AssetId::FromKey(key.View());
}

Option<String> AssetManager::SoundPath(const String &id) const {
	if (Option<resources::AssetRecord> record = m_cache.Record(SoundId(id)); record.IsSome())
		return Some(record.Unwrap().path);
	return NONE;
}

size_t AssetManager::ReleaseUnused() { return m_cache.UnloadUnused(); }
size_t AssetManager::ResidentCount() const { return m_cache.ResidentCount(); }
std::vector<resources::MissingAsset> AssetManager::Missing() {
	std::vector<resources::MissingAsset> missing = m_cache.Missing();
	missing.insert(missing.end(), m_rigErrors.begin(), m_rigErrors.end());
	return missing;
}

} // namespace voxel
