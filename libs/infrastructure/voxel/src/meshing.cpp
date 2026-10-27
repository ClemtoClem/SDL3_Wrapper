// Définitions de voxel/meshing.hpp
#include <voxel/meshing.hpp>
#include <voxel/fluids.hpp>
#include <voxel/json.hpp>
#include <voxel/lighting.hpp>
#include <voxel/worldgen.hpp>
#include <voxel/physics.hpp>

#include <algorithm>
#include <cmath>
#include <set>
#include <unordered_map>

namespace voxel {

// ── Atlas ────────────────────────────────────────────────────────────────────

namespace {

/// Plus grande puissance de deux ≤ v (au moins 1).
int FloorPow2(int v) {
	int p = 1;
	while (p * 2 <= v)
		p *= 2;
	return p;
}

/// Coordonnées (en tuiles de base) du rang `i` dans l'ordre de Morton.
void MortonCell(uint32_t i, int &x, int &y) {
	x = y = 0;
	for (int b = 0; b < 16; ++b) {
		x |= int((i >> (2 * b)) & 1u) << b;
		y |= int((i >> (2 * b + 1)) & 1u) << b;
	}
}

/// Moyenne des pixels [x0, x1[ × [y0, y1[ pondérée par l'alpha (pas de
/// franges sombres autour des parties transparentes).
void Average(const Image &src, int x0, int y0, int x1, int y1, uint8_t *dst) {
	double rgb[3] = {}, plain[3] = {}, alpha = 0.0;
	int count = 0;
	for (int y = y0; y < y1; ++y)
		for (int x = x0; x < x1; ++x) {
			const uint8_t *p = src.At(x, y);
			for (int k = 0; k < 3; ++k) {
				rgb[k] += double(p[k]) * double(p[3]);
				plain[k] += double(p[k]);
			}
			alpha += double(p[3]);
			++count;
		}
	for (int k = 0; k < 3; ++k)
		dst[k] = uint8_t(std::lround(alpha > 0.0 ? rgb[k] / alpha : plain[k] / double(std::max(1, count))));
	dst[3] = uint8_t(std::lround(alpha / double(std::max(1, count))));
}

/// Image moitié moins grande (niveau de mipmap suivant).
Image Halve(const Image &src) {
	Image out;
	out.width = std::max(1, src.width / 2);
	out.height = std::max(1, src.height / 2);
	out.rgba.assign(size_t(out.width * out.height * 4), 0);
	for (int y = 0; y < out.height; ++y)
		for (int x = 0; x < out.width; ++x)
			Average(src, std::min(x * 2, src.width - 1), std::min(y * 2, src.height - 1), std::min(x * 2 + 2, src.width),
					std::min(y * 2 + 2, src.height), &out.rgba[size_t((y * out.width + x) * 4)]);
	return out;
}

} // namespace

Image TextureAtlas::Resample(const Image &source, int size) {
	Image out;
	out.width = out.height = size;
	out.rgba.assign(size_t(size * size * 4), 0);
	// Image animée (bande verticale) : première image seulement.
	const int sw = source.width, sh = std::min(source.height, source.width);
	for (int y = 0; y < size; ++y)
		for (int x = 0; x < size; ++x) {
			const int x0 = x * sw / size, y0 = y * sh / size;
			const int x1 = std::max(x0 + 1, (x + 1) * sw / size), y1 = std::max(y0 + 1, (y + 1) * sh / size);
			Average(source, x0, y0, std::min(x1, sw), std::min(y1, sh), &out.rgba[size_t((y * size + x) * 4)]);
		}
	return out;
}

void TextureAtlas::Build(AssetManager &assets, const ContentRegistry &content, int tileSize, int maxTileSize) {
	m_tile = FloorPow2(std::max(2, tileSize));
	const int maxTile = std::max(m_tile, FloorPow2(std::max(maxTileSize, m_tile)));
	m_rects.clear();
	m_variants.clear();
	m_mipmaps.clear();
	// Seules les textures réellement posées sur des faces (blocs, objets,
	// entités, groupes des modèles) prennent une tuile : les mods déclarent
	// aussi skins, interfaces, peintures… que l'atlas n'a pas à décoder.
	std::set<std::string> ids;
	ids.insert(MISSING);
	auto add = [&](const String &id) {
		if (!id.IsEmpty())
			ids.insert(id.CStr());
	};
	auto addModel = [&](const ShapeDesc &shape, const String &mod) {
		if (shape.kind != ShapeKind::MODEL || shape.model.IsEmpty())
			return;
		if (std::shared_ptr<ModelResource> model = assets.Model(shape.model))
			for (const ModelMesh::Group &group : model->Mesh().groups)
				if (!group.texture.IsEmpty())
					add(ContentRegistry::Qualify(group.texture, mod));
	};
	std::set<std::string> glowing; // textures des blocs lumineux : variante « lueur »
	for (const BlockDef &b : content.Blocks()) {
		for (const String &f : b.look.faces)
			add(f);
		add(b.liquid.flowingTexture); // liquide qui coule
		addModel(b.shape, b.mod);
		if (b.look.lightEmission > 0 && json::Bool(b.raw, "graphics.glow", true)) {
			for (const String &f : b.look.faces)
				if (!f.IsEmpty())
					glowing.insert(f.CStr());
			if (!b.liquid.flowingTexture.IsEmpty())
				glowing.insert(b.liquid.flowingTexture.CStr());
		}
	}
	for (const ItemDef *i : content.Items())
		add(i->icon);
	for (const EntityDef *e : content.Entities()) {
		for (const String &f : e->look.faces)
			add(f);
		addModel(e->shape, e->mod);
	}
	// Texture à variantes : ses variantes prennent les tuiles, elle renvoie à la première.
	for (const std::string &id : std::vector<std::string>(ids.begin(), ids.end()))
		if (const TextureDef *def = content.FindTexture(String(id.c_str())); def && !def->variants.empty()) {
			std::vector<std::string> &list = m_variants[id];
			for (const String &v : def->variants) {
				list.push_back(v.CStr());
				ids.insert(v.CStr());
				if (glowing.contains(id))
					glowing.insert(v.CStr());
			}
		}

	// Images d'abord : une texture introuvable ne prend pas de tuile, elle
	// renverra le damier commun.
	std::vector<std::pair<std::string, Image>> tiles;
	{
		Image checker;
		checker.width = checker.height = m_tile;
		checker.rgba.resize(size_t(m_tile * m_tile * 4));
		for (int y = 0; y < m_tile; ++y)
			for (int x = 0; x < m_tile; ++x) {
				const bool check = ((x * 2 / m_tile) + (y * 2 / m_tile)) % 2 == 0;
				uint8_t *dst = &checker.rgba[size_t((y * m_tile + x) * 4)];
				dst[0] = dst[2] = check ? 255 : 0;
				dst[1] = 0;
				dst[3] = 255;
			}
		tiles.push_back({MISSING, std::move(checker)});
	}
	for (const std::string &id : ids)
		if (id != MISSING && !m_variants.contains(id))
			if (std::shared_ptr<TextureImage> tex = assets.Texture(String(id.c_str())); tex && !tex->Pixels().IsEmpty()) {
				const Image &source = tex->Pixels();
				Image tile = Resample(source, std::clamp(FloorPow2(source.width), m_tile, maxTile));
				if (glowing.contains(id))
					tiles.push_back({GlowId(String(id.c_str())).CStr(), Glow(tile)});
				tiles.push_back({id, std::move(tile)});
			}

	// Rangement : des plus grandes tuiles aux plus petites, en ordre de
	// Morton (chaque tuile tombe alignée sur sa taille).
	std::stable_sort(tiles.begin(), tiles.end(), [](const auto &a, const auto &b) { return a.second.width > b.second.width; });
	uint64_t units = 0;
	for (const auto &tile : tiles)
		units += uint64_t(tile.second.width / m_tile) * uint64_t(tile.second.width / m_tile);
	int side = 1; // en tuiles de base
	while (uint64_t(side) * uint64_t(side) < units || side * m_tile < tiles.front().second.width)
		side <<= 1;
	const bool half = units <= uint64_t(side) * uint64_t(side) / 2; // la moitié haute suffit
	const int width = side * m_tile, height = half ? width / 2 : width;
	m_image.width = width;
	m_image.height = height;
	m_image.rgba.assign(size_t(width) * size_t(height) * 4, 0);

	uint32_t cursor = 0;
	for (const auto &[id, pixels] : tiles) {
		const int size = pixels.width, cells = size / m_tile;
		int cx = 0, cy = 0;
		MortonCell(cursor, cx, cy);
		cursor += uint32_t(cells * cells);
		const int tx = cx * m_tile, ty = cy * m_tile;
		for (int y = 0; y < size; ++y)
			std::copy_n(&pixels.rgba[size_t(y * size * 4)], size_t(size * 4), &m_image.rgba[(size_t(ty + y) * size_t(width) + size_t(tx)) * 4]);
		// Demi-texel de marge : pas de débordement sur la tuile voisine.
		const float hu = 0.5f / float(width), hv = 0.5f / float(height);
		m_rects[id] = Rect{float(tx) / float(width) + hu, float(ty) / float(height) + hv, float(tx + size) / float(width) - hu,
						   float(ty + size) / float(height) - hv};
	}
	for (const auto &[id, list] : m_variants)
		if (auto first = m_rects.find(list.front()); first != m_rects.end()) {
			m_rects[id] = first->second;
			if (auto glow = m_rects.find(GlowId(String(list.front().c_str())).CStr()); glow != m_rects.end())
				m_rects[GlowId(String(id.c_str())).CStr()] = glow->second;
		}
	// Mipmaps : jusqu'à la tuile de base réduite à un pixel.
	for (int size = m_tile; size > 1; size /= 2)
		m_mipmaps.push_back(Halve(m_mipmaps.empty() ? m_image : m_mipmaps.back()));
}

TextureAtlas::Rect TextureAtlas::Lookup(const String &id) const {
	auto it = m_rects.find(id.CStr());
	if (it != m_rects.end())
		return it->second;
	auto missing = m_rects.find(MISSING);
	return missing != m_rects.end() ? missing->second : Rect{};
}

bool TextureAtlas::Contains(const String &id) const { return m_rects.contains(id.CStr()); }

TextureAtlas::Rect TextureAtlas::LookupGlow(const String &id) const {
	auto it = m_rects.find(GlowId(id).CStr());
	return it != m_rects.end() ? it->second : Lookup(id);
}

std::vector<TextureAtlas::Rect> TextureAtlas::LookupVariants(const String &id, bool glow) const {
	std::vector<Rect> out;
	if (auto it = m_variants.find(id.CStr()); it != m_variants.end())
		for (const std::string &v : it->second)
			out.push_back(glow ? LookupGlow(String(v.c_str())) : Lookup(String(v.c_str())));
	return out;
}

Image TextureAtlas::Glow(const Image &source) {
	// Première image d'une bande animée seulement (comme l'atlas).
	Image out;
	out.width = source.width;
	out.height = std::min(source.height, source.width);
	out.rgba.assign(source.rgba.begin(), source.rgba.begin() + size_t(out.width * out.height * 4));
	auto luminance = [](const uint8_t *p) { return 0.299f * float(p[0]) + 0.587f * float(p[1]) + 0.114f * float(p[2]); };
	// Bornes du contraste : 4e et 98e centiles de la luminance des pixels visibles.
	int histogram[256] = {};
	int visible = 0;
	for (size_t i = 0; i + 3 < out.rgba.size(); i += 4)
		if (out.rgba[i + 3] > 16) {
			++histogram[std::clamp(int(luminance(&out.rgba[i])), 0, 255)];
			++visible;
		}
	if (visible == 0)
		return out;
	auto percentile = [&](float q) {
		const int target = int(float(visible) * q);
		int sum = 0;
		for (int v = 0; v < 256; ++v)
			if ((sum += histogram[v]) > target)
				return v;
		return 255;
	};
	const float lo = float(percentile(0.04f)), hi = float(percentile(0.98f));
	const float range = std::max(24.f, hi - lo); // une texture presque unie n'est pas étirée à l'excès
	for (size_t i = 0; i + 3 < out.rgba.size(); i += 4) {
		uint8_t *p = &out.rgba[i];
		// Contraste étiré sur la LUMINANCE seule (les teintes ne bougent
		// pas), puis couleurs un peu ravivées et éclaircies.
		const float l = luminance(p) / 255.f;
		const float stretched = std::clamp((luminance(p) - lo) / range, 0.f, 1.f);
		const float gain = l > 1e-3f ? stretched / l : 0.f;
		float c[3];
		for (int k = 0; k < 3; ++k)
			c[k] = float(p[k]) / 255.f * gain;
		for (int k = 0; k < 3; ++k) {
			const float saturated = std::clamp(stretched + (c[k] - stretched) * 1.1f, 0.f, 1.f);
			p[k] = uint8_t(std::lround(255.f * std::pow(saturated, 0.75f)));
		}
	}
	return out;
}

// ── Maillage ─────────────────────────────────────────────────────────────────

namespace {

constexpr float AO_LEVELS[4] = {0.45f, 0.65f, 0.83f, 1.f};

int NormalAxis(Face f) { return f == Face::EAST || f == Face::WEST ? 0 : f == Face::TOP || f == Face::BOTTOM ? 1 : 2; }

float Get(const math::FVector3 &v, int a) { return a == 0 ? v.x : a == 1 ? v.y : v.z; }
void Set(math::FVector3 &v, int a, float value) { (a == 0 ? v.x : a == 1 ? v.y : v.z) = value; }

/// Coins d'une face (sens direct vu de l'extérieur) dans une boîte.
void FaceCorners(Face f, const Box &b, math::FVector3 out[4]) {
	const math::FVector3 lo = b.min, hi = b.max;
	switch (f) {
	case Face::EAST:
		out[0] = {hi.x, lo.y, hi.z}, out[1] = {hi.x, lo.y, lo.z}, out[2] = {hi.x, hi.y, lo.z}, out[3] = {hi.x, hi.y, hi.z};
		break;
	case Face::WEST:
		out[0] = {lo.x, lo.y, lo.z}, out[1] = {lo.x, lo.y, hi.z}, out[2] = {lo.x, hi.y, hi.z}, out[3] = {lo.x, hi.y, lo.z};
		break;
	case Face::SOUTH:
		out[0] = {lo.x, lo.y, hi.z}, out[1] = {hi.x, lo.y, hi.z}, out[2] = {hi.x, hi.y, hi.z}, out[3] = {lo.x, hi.y, hi.z};
		break;
	case Face::NORTH:
		out[0] = {hi.x, lo.y, lo.z}, out[1] = {lo.x, lo.y, lo.z}, out[2] = {lo.x, hi.y, lo.z}, out[3] = {hi.x, hi.y, lo.z};
		break;
	case Face::TOP:
		out[0] = {lo.x, hi.y, hi.z}, out[1] = {hi.x, hi.y, hi.z}, out[2] = {hi.x, hi.y, lo.z}, out[3] = {lo.x, hi.y, lo.z};
		break;
	case Face::BOTTOM:
		out[0] = {lo.x, lo.y, lo.z}, out[1] = {hi.x, lo.y, lo.z}, out[2] = {hi.x, lo.y, hi.z}, out[3] = {lo.x, lo.y, hi.z};
		break;
	}
}

/// UV dans la tuile d'après la position (une sous-boîte prend la partie
/// correspondante de la texture, comme dans Minecraft).
math::FVector2 FaceUv(Face f, const math::FVector3 &p) {
	auto c = [](float v) { return std::clamp(v, 0.f, 1.f); };
	switch (f) {
	case Face::EAST: return {c(1.f - p.z), c(1.f - p.y)};
	case Face::WEST: return {c(p.z), c(1.f - p.y)};
	case Face::SOUTH: return {c(p.x), c(1.f - p.y)};
	case Face::NORTH: return {c(1.f - p.x), c(1.f - p.y)};
	case Face::TOP: return {c(p.x), c(p.z)};
	case Face::BOTTOM: return {c(p.x), c(1.f - p.z)};
	}
	return {0.f, 0.f};
}

/// Face d'origine d'une face d'un bloc tourné d'un quart de tour (inverse de
/// RotateBlockBox : sud → ouest → nord → est → sud).
Face UnrotateFace(Face f) {
	switch (f) {
	case Face::WEST: return Face::SOUTH;
	case Face::NORTH: return Face::WEST;
	case Face::EAST: return Face::NORTH;
	case Face::SOUTH: return Face::EAST;
	default: return f;
	}
}

math::FVector3 NormalOf(Face f) {
	const BlockPos n = FaceNormal(f);
	return {float(n.x), float(n.y), float(n.z)};
}

void PushQuad(MeshBuffer &out, const math::FVector3 corners[4], const math::FVector2 uvs[4], const math::FVector3 &normal,
			  const std::array<uint8_t, 4> &tint, const std::array<float, 4> &ao) {
	const uint32_t base = uint32_t(out.vertices.size());
	for (int k = 0; k < 4; ++k) {
		MeshVertex v;
		v.position = corners[k];
		v.normal = normal;
		v.uv = uvs[k];
		v.color = tint;
		v.light = {0.f, 0.f, 0.f, ao[size_t(k)]}; // occlusion ambiante ; le reste : ChunkMesher::Build
		out.vertices.push_back(v);
	}
	// Diagonale choisie selon l'occlusion : pas de dégradé en « Z ».
	if (ao[0] + ao[2] < ao[1] + ao[3])
		for (uint32_t i : {1u, 2u, 3u, 1u, 3u, 0u})
			out.indices.push_back(base + i);
	else
		for (uint32_t i : {0u, 1u, 2u, 0u, 2u, 3u})
			out.indices.push_back(base + i);
}

/// Lecture rapide des blocs d'un chunk et de sa bordure (18³).
struct Padded {
	static constexpr int N = CHUNK_SIZE + 2;
	std::vector<BlockId> ids = std::vector<BlockId>(size_t(N * N * N), AIR);
	std::vector<uint8_t> params = std::vector<uint8_t>(size_t(N * N * N), 0);
	std::vector<uint8_t> loaded = std::vector<uint8_t>(size_t(N * N * N), 1);
	[[nodiscard]] static int Index(int x, int y, int z) { return ((y + 1) * N + (z + 1)) * N + (x + 1); }
	[[nodiscard]] BlockId At(int x, int y, int z) const { return ids[size_t(Index(x, y, z))]; }
};

} // namespace

ChunkMesher::ChunkMesher(const ContentRegistry &content, const TextureAtlas &atlas, AssetManager *assets)
	: m_content(content), m_atlas(atlas), m_assets(assets) {
	for (const BlockDef &b : content.Blocks()) {
		BlockRender r;
		r.id = b.numeric;
		r.visible = !b.IsAir() && b.shape.kind != ShapeKind::NONE && b.look.material != RenderMaterial::INVISIBLE;
		r.opaqueCube = b.IsOpaqueCube() && !b.fluid; // un liquide qui coule laisse voir ses voisins
		r.translucent = b.look.material == RenderMaterial::TRANSLUCENT || b.look.material == RenderMaterial::TRANSPARENT;
		r.fluid = b.fluid;
		r.shape = b.shape.kind;
		r.tint = r.tinted = b.look.tint;
		auto kindOf = [](const String &t) -> uint8_t { return t == "grass" ? 1 : t == "foliage" ? 2 : t == "water" ? 3 : 0; };
		r.biomeTint = kindOf(b.look.biomeTint);
		r.tintFaces = r.biomeTint ? b.look.tintFaces : 0;
		if (!r.biomeTint) // teinte portée par la texture d'une face
			for (int f = 0; f < FACE_COUNT; ++f)
				if (const TextureDef *tex = content.FindTexture(b.look.faces[size_t(f)]); tex && kindOf(tex->biomeTint)) {
					r.biomeTint = kindOf(tex->biomeTint);
					r.tintFaces |= uint8_t(1u << f);
				}
		// Bloc lumineux : pleine luminosité et textures « lueur ».
		r.emissive = b.look.lightEmission > 0 && !r.translucent && json::Bool(b.raw, "graphics.glow", true);
		for (int f = 0; f < FACE_COUNT; ++f) {
			r.faces[size_t(f)] = r.emissive ? atlas.LookupGlow(b.look.faces[size_t(f)]) : atlas.Lookup(b.look.faces[size_t(f)]);
			r.variants[size_t(f)] = atlas.LookupVariants(b.look.faces[size_t(f)], r.emissive);
			r.varied = r.varied || r.variants[size_t(f)].size() > 1;
		}
		m_blocks.push_back(r);
	}
	// Palettes de couleurs (herbe, feuillage, eau) : « colormaps » de la configuration.
	if (m_assets)
		for (int i = 0; i < 3; ++i) {
			const char *keys[3] = {"colormaps.grass", "colormaps.foliage", "colormaps.water"};
			if (const String id = content.ConfigStr(keys[i]); !id.IsEmpty())
				if (std::shared_ptr<TextureImage> tex = m_assets->Texture(id); tex && !tex->Pixels().IsEmpty())
					m_colormaps[i] = tex->Pixels();
		}
	// Hors d'un chunk (objet au sol, bloc qui tombe) : teinte d'un biome tempéré.
	const auto defaults = BiomeColors(nullptr);
	for (BlockRender &r : m_blocks)
		if (r.biomeTint)
			for (int k = 0; k < 3; ++k)
				r.tinted[size_t(k)] = uint8_t(int(r.tint[size_t(k)]) * int(defaults[size_t(r.biomeTint - 1)][size_t(k)]) / 255);
}

std::array<std::array<uint8_t, 3>, 3> ChunkMesher::BiomeColors(const BiomeDef *biome) const {
	std::array<std::array<uint8_t, 3>, 3> out{{{120, 190, 80}, {90, 170, 60}, {70, 120, 220}}};
	const float t = biome ? std::clamp(biome->temperature, 0.f, 1.f) : 0.6f;
	const float h = biome ? std::clamp(biome->humidity, 0.f, 1.f) * t : 0.3f;
	for (int i = 0; i < 3; ++i) {
		const Image &map = m_colormaps[i];
		if (!map.IsEmpty()) {
			const int x = std::clamp(int((1.f - t) * float(map.width - 1)), 0, map.width - 1);
			const int y = std::clamp(int((1.f - h) * float(map.height - 1)), 0, map.height - 1);
			const uint8_t *p = map.At(x, y);
			out[size_t(i)] = {p[0], p[1], p[2]};
		}
	}
	if (biome) {
		if (biome->grassColor)
			out[0] = *biome->grassColor;
		if (biome->foliageColor)
			out[1] = *biome->foliageColor;
		if (biome->waterColor)
			out[2] = *biome->waterColor;
	}
	return out;
}

void ChunkMesher::EmitBox(MeshBuffer &out, const Box &box, const BlockRender &render, const math::FVector3 &offset,
						  const std::function<bool(Face)> &cull,
						  const std::function<std::array<float, 4>(Face, const math::FVector3 *)> &ao, uint8_t rotation) const {
	rotation &= 3;
	// Boîte qui dépasse de sa cellule (volet ouvert contre le mur) : UV pris
	// dans la cellule voisine qu'elle occupe, plutôt qu'un bord étiré.
	const math::FVector3 shift{std::floor((box.min.x + box.max.x) * 0.5f), std::floor((box.min.y + box.max.y) * 0.5f),
							   std::floor((box.min.z + box.max.z) * 0.5f)};
	for (int fi = 0; fi < FACE_COUNT; ++fi) {
		const Face f = Face(fi);
		if (cull && cull(f))
			continue;
		math::FVector3 corners[4];
		FaceCorners(f, box, corners);
		// Bloc tourné : la face et les coins vus dans le repère du bloc d'origine.
		Face local = f;
		math::FVector3 at[4] = {corners[0] - shift, corners[1] - shift, corners[2] - shift, corners[3] - shift};
		for (int r = 0; r < rotation; ++r) {
			local = UnrotateFace(local);
			for (math::FVector3 &c : at)
				c = math::FVector3{c.z, c.y, 1.f - c.x};
		}
		const TextureAtlas::Rect &rect = render.faces[size_t(local)];
		math::FVector2 uvs[4];
		for (int k = 0; k < 4; ++k) {
			const math::FVector2 t = FaceUv(local, at[k]);
			uvs[k] = {rect.u0 + (rect.u1 - rect.u0) * t.x, rect.v0 + (rect.v1 - rect.v0) * t.y};
		}
		const std::array<float, 4> occlusion = ao ? ao(f, corners) : std::array<float, 4>{1.f, 1.f, 1.f, 1.f};
		math::FVector3 placed[4];
		for (int k = 0; k < 4; ++k)
			placed[k] = corners[k] + offset;
		PushQuad(out, placed, uvs, NormalOf(f), (render.tintFaces >> int(local)) & 1 ? render.tinted : render.tint, occlusion);
	}
}

void ChunkMesher::EmitCross(MeshBuffer &out, const BlockRender &render, const math::FVector3 &o, float light) const {
	const TextureAtlas::Rect &rect = render.faces[size_t(Face::SOUTH)];
	const math::FVector2 uvs[4] = {{rect.u0, rect.v1}, {rect.u1, rect.v1}, {rect.u1, rect.v0}, {rect.u0, rect.v0}};
	const float a = 0.15f, b = 0.85f;
	const math::FVector3 quads[2][4] = {
		{{a, 0.f, a}, {b, 0.f, b}, {b, 1.f, b}, {a, 1.f, a}},
		{{a, 0.f, b}, {b, 0.f, a}, {b, 1.f, a}, {a, 1.f, b}},
	};
	for (const auto &q : quads) {
		math::FVector3 front[4], back[4];
		math::FVector2 backUv[4];
		for (int k = 0; k < 4; ++k) {
			front[k] = q[k] + o;
			back[3 - k] = q[k] + o;
			backUv[3 - k] = uvs[k];
		}
		const math::FVector3 n = (q[1] - q[0]).Cross(q[3] - q[0]).Normalize();
		const std::array<uint8_t, 4> &tint = render.tintFaces ? render.tinted : render.tint;
		PushQuad(out, front, uvs, n, tint, {light, light, light, light});
		PushQuad(out, back, backUv, n * -1.f, tint, {light, light, light, light});
	}
}

void ChunkMesher::EmitModel(MeshBuffer &out, const String &model, const BlockRender &render, const math::FVector3 &offset,
							uint8_t param, const String &mod, bool normalize, float light) const {
	if (!m_assets) {
		EmitBox(out, Box{}, render, offset, {}, {});
		return;
	}
	std::shared_ptr<ModelResource> resource = m_assets->Model(model);
	if (!resource) {
		EmitBox(out, Box{}, render, offset, {}, {}); // repli visible : un cube
		return;
	}
	ModelMesh mesh = resource->Mesh();
	mesh.Normalize(normalize);
	const TextureAtlas::Rect fallback = render.faces[size_t(Face::SOUTH)];
	for (const ModelMesh::Group &group : mesh.groups) {
		TextureAtlas::Rect rect = fallback;
		if (!group.texture.IsEmpty()) {
			const String qualified = ContentRegistry::Qualify(group.texture, mod);
			if (m_atlas.Contains(qualified))
				rect = m_atlas.Lookup(qualified);
		}
		const uint32_t base = uint32_t(out.vertices.size());
		std::vector<uint32_t> remap;
		for (uint32_t t = group.first; t < group.first + group.count; ++t)
			for (int k = 0; k < 3; ++k) {
				const uint32_t src = mesh.indices[size_t(t) * 3 + size_t(k)];
				MeshVertex v;
				math::FVector3 p = mesh.positions[src];
				math::FVector3 n = src < mesh.normals.size() ? mesh.normals[src] : math::FVector3{0.f, 1.f, 0.f};
				for (int r = 0; r < (param & 3); ++r) { // quart de tour autour du centre
					p = {1.f - p.z, p.y, p.x};
					n = {-n.z, n.y, n.x};
				}
				v.position = p + offset;
				v.normal = n;
				const math::FVector2 uv = src < mesh.uvs.size() ? mesh.uvs[src] : math::FVector2{0.f, 0.f};
				v.uv = {rect.u0 + (rect.u1 - rect.u0) * std::clamp(uv.x, 0.f, 1.f), rect.v0 + (rect.v1 - rect.v0) * std::clamp(uv.y, 0.f, 1.f)};
				v.color = render.tintFaces ? render.tinted : render.tint;
				for (int c = 0; c < 3; ++c)
					v.color[size_t(c)] = uint8_t(float(v.color[size_t(c)]) * light);
				out.vertices.push_back(v);
				out.indices.push_back(base + uint32_t(remap.size()));
				remap.push_back(src);
			}
		// Les modèles importés n'ont pas toujours un sens d'enroulement
		// fiable : chaque triangle est aussi émis à l'envers (normale
		// opposée), visible quel que soit l'élimination des faces arrière.
		const uint32_t front = uint32_t(out.vertices.size()) - base;
		for (uint32_t i = 0; i < front; ++i) {
			MeshVertex back = out.vertices[base + i];
			back.normal = back.normal * -1.f;
			out.vertices.push_back(back);
		}
		for (uint32_t t = 0; t < front; t += 3)
			for (uint32_t k : {0u, 2u, 1u})
				out.indices.push_back(base + front + t + k);
	}
}

ChunkMesh ChunkMesher::Build(const World &world, const ChunkPos &cp) const {
	ChunkMesh mesh;
	const Chunk *chunk = world.GetChunk(cp);
	if (!chunk)
		return mesh;
	mesh.revision = chunk->Revision();
	if (chunk->IsEmpty())
		return mesh;
	// Copie bordée : les voisins ne coûtent qu'une recherche par chunk.
	Padded pad;
	const BlockPos origin = cp.Origin();
	for (int dy = -1; dy <= 1; ++dy)
		for (int dz = -1; dz <= 1; ++dz)
			for (int dx = -1; dx <= 1; ++dx) {
				const Chunk *n = world.GetChunk({cp.x + dx, cp.y + dy, cp.z + dz});
				const int x0 = dx < 0 ? -1 : dx == 0 ? 0 : CHUNK_SIZE, x1 = dx < 0 ? -1 : dx == 0 ? CHUNK_SIZE - 1 : CHUNK_SIZE;
				const int y0 = dy < 0 ? -1 : dy == 0 ? 0 : CHUNK_SIZE, y1 = dy < 0 ? -1 : dy == 0 ? CHUNK_SIZE - 1 : CHUNK_SIZE;
				const int z0 = dz < 0 ? -1 : dz == 0 ? 0 : CHUNK_SIZE, z1 = dz < 0 ? -1 : dz == 0 ? CHUNK_SIZE - 1 : CHUNK_SIZE;
				for (int y = y0; y <= y1; ++y)
					for (int z = z0; z <= z1; ++z)
						for (int x = x0; x <= x1; ++x) {
							const int i = Padded::Index(x, y, z);
							if (!n) {
								pad.loaded[size_t(i)] = 0;
								continue;
							}
							const int lx = FloorMod(x, CHUNK_SIZE), ly = FloorMod(y, CHUNK_SIZE), lz = FloorMod(z, CHUNK_SIZE);
							pad.ids[size_t(i)] = n->Get(lx, ly, lz);
							pad.params[size_t(i)] = n->Param(lx, ly, lz);
						}
			}
	auto render = [&](BlockId id) -> const BlockRender & { return m_blocks[id < m_blocks.size() ? id : 0]; };
	// Teintes de biome par colonne (calculées à la demande).
	std::array<std::array<std::array<uint8_t, 3>, 3>, CHUNK_SIZE * CHUNK_SIZE> columnColors{};
	std::array<bool, CHUNK_SIZE * CHUNK_SIZE> columnKnown{};
	ChunkGenerator *generator = world.Generator();
	auto biomeColor = [&](int x, int z, int kind) -> const std::array<uint8_t, 3> & {
		const size_t i = size_t(z * CHUNK_SIZE + x);
		if (!columnKnown[i]) {
			columnColors[i] = BiomeColors(generator ? generator->BiomeAt(origin.x + x, origin.z + z) : nullptr);
			columnKnown[i] = true;
		}
		return columnColors[i][size_t(kind - 1)];
	};
	// Un voisin NON chargé cache les faces (pas de murs visibles au bord du monde chargé)…
	auto occludes = [&](int x, int y, int z) {
		const int i = Padded::Index(x, y, z);
		return !pad.loaded[size_t(i)] || render(pad.ids[size_t(i)]).opaqueCube;
	};
	// …mais n'assombrit pas les coins : l'occlusion ne compte que les blocs
	// réels (le chunk sera refait quand son voisin arrivera).
	auto shades = [&](int x, int y, int z) {
		const int i = Padded::Index(x, y, z);
		return pad.loaded[size_t(i)] && render(pad.ids[size_t(i)]).opaqueCube;
	};
	// Éclairage (lighting.hpp) : sources qui atteignent le chunk, puis, par
	// cellule devant une face, éclairement direct et part de ciel (en cache).
	std::vector<lighting::Source> sources;
	if (options.blockLight)
		lighting::GatherSources(world, origin.ToVector() - math::FVector3{1.f, 1.f, 1.f},
								origin.ToVector() + math::FVector3{float(CHUNK_SIZE + 1), float(CHUNK_SIZE + 1), float(CHUNK_SIZE + 1)},
								sources);
	std::unordered_map<uint64_t, lighting::Rgb> direct;
	std::unordered_map<int, float> skyAccess;
	auto skyAt = [&](const BlockPos &cell) {
		if (!options.skyLight)
			return 1.f;
		const int key = Padded::Index(cell.x - origin.x, cell.y - origin.y, cell.z - origin.z);
		auto it = skyAccess.find(key);
		if (it == skyAccess.end())
			it = skyAccess.emplace(key, lighting::SkyAccess(world, cell.Center())).first;
		return it->second;
	};
	// Lumière des sommets [first, fin[ : moyenne des cellules libres autour du
	// sommet, du côté éclairé (la normale) — lissée et sans raccord entre chunks.
	auto lightVertices = [&](MeshBuffer &out, size_t first, const BlockPos &cell) {
		for (size_t i = first; i < out.vertices.size(); ++i) {
			MeshVertex &v = out.vertices[i];
			const math::FVector3 n = v.normal;
			const bool axial = std::fabs(n.x) + std::fabs(n.y) + std::fabs(n.z) > 0.999f &&
							   (std::fabs(n.x) > 0.999f || std::fabs(n.y) > 0.999f || std::fabs(n.z) > 0.999f);
			const math::FVector3 at = origin.ToVector() + v.position;
			lighting::Rgb e{0.f, 0.f, 0.f};
			float sky = 0.f;
			int samples = 0;
			auto sample = [&](const BlockPos &c, const math::FVector3 &point, const math::FVector3 &normal, uint64_t key) {
				auto it = direct.find(key);
				if (it == direct.end())
					it = direct.emplace(key, sources.empty() ? lighting::Rgb{0.f, 0.f, 0.f} : lighting::DirectIrradiance(world, point, normal, sources)).first;
				for (int k = 0; k < 3; ++k)
					e[size_t(k)] += it->second[size_t(k)];
				sky += skyAt(c);
				++samples;
			};
			if (axial) {
				const int axis = std::fabs(n.x) > 0.5f ? 0 : std::fabs(n.y) > 0.5f ? 1 : 2;
				const int dir = Get(n, axis) > 0.f ? 1 : -1;
				const int t1 = (axis + 1) % 3, t2 = (axis + 2) % 3;
				const float plane = Get(at, axis);
				const int face = axis * 2 + (dir > 0 ? 0 : 1);
				const int quantized = int(std::lround((plane - std::floor(plane)) * 16.f));
				for (int a = -1; a <= 1; a += 2)
					for (int b = -1; b <= 1; b += 2) {
						math::FVector3 probe = at + n * 0.5f;
						Set(probe, t1, Get(probe, t1) + 0.49f * float(a));
						Set(probe, t2, Get(probe, t2) + 0.49f * float(b));
						const BlockPos c = BlockPos::FromWorld(probe);
						const int lx = c.x - origin.x, ly = c.y - origin.y, lz = c.z - origin.z;
						if (lx < -1 || ly < -1 || lz < -1 || lx > CHUNK_SIZE || ly > CHUNK_SIZE || lz > CHUNK_SIZE || shades(lx, ly, lz))
							continue;
						// Point de mesure : sur le plan de la face, au droit du centre de la cellule.
						math::FVector3 point = c.Center();
						Set(point, axis, plane + 0.04f * float(dir));
						const uint64_t key = (uint64_t(uint32_t(Padded::Index(lx, ly, lz))) << 16) | uint64_t(face << 8) | uint64_t(quantized);
						sample(c, point, n, key);
					}
			}
			if (samples == 0) { // plantes, modèles, faces enfouies : la cellule du bloc, sans Lambert
				const uint64_t key = (uint64_t(uint32_t(Padded::Index(cell.x - origin.x, cell.y - origin.y, cell.z - origin.z))) << 16) | 0xFFFFu;
				sample(cell, cell.Center(), math::FVector3{0.f, 0.f, 0.f}, key);
			}
			const float inv = 1.f / float(samples);
			v.light = {e[0] * inv, e[1] * inv, e[2] * inv, v.light[3] * sky * inv};
		}
	};
	for (int y = 0; y < CHUNK_SIZE; ++y)
		for (int z = 0; z < CHUNK_SIZE; ++z)
			for (int x = 0; x < CHUNK_SIZE; ++x) {
				const BlockId id = pad.At(x, y, z);
				const BlockRender &base = render(id);
				if (!base.visible)
					continue;
				BlockRender tintedRender;
				const BlockRender *chosen = &base;
				if (base.varied) { // variante de texture tirée d'après la position
					tintedRender = base;
					const uint64_t pick = HashPosition(0x5eedu, origin.x + x, origin.y + y, origin.z + z, 11);
					for (int f = 0; f < FACE_COUNT; ++f)
						if (const auto &list = base.variants[size_t(f)]; !list.empty())
							tintedRender.faces[size_t(f)] = list[size_t(pick % list.size())];
					chosen = &tintedRender;
				}
				if (base.biomeTint) {
					if (chosen == &base)
						tintedRender = base;
					const std::array<uint8_t, 3> &c = biomeColor(x, z, base.biomeTint);
					for (int k = 0; k < 3; ++k)
						tintedRender.tinted[size_t(k)] = uint8_t(int(base.tint[size_t(k)]) * int(c[size_t(k)]) / 255);
					chosen = &tintedRender;
				}
				const BlockRender &r = *chosen;
				const BlockDef &def = m_content.Block(id);
				if (def.look.lightEmission > 0)
					mesh.lights.push_back({origin.ToVector() + math::FVector3{float(x) + 0.5f, float(y) + 0.5f, float(z) + 0.5f},
										   def.look.lightEmission});
				MeshBuffer &out = r.translucent ? mesh.translucent : r.emissive ? mesh.emissive : mesh.opaque;
				const size_t firstVertex = out.vertices.size();
				const math::FVector3 offset{float(x), float(y), float(z)};
				const uint8_t param = pad.params[size_t(Padded::Index(x, y, z))];
				if (r.fluid && r.shape == ShapeKind::CUBE) {
					// Liquide : surface en pente d'après les niveaux voisins
					// (hauteur de chaque coin : moyenne des cellules du même
					// liquide qui le partagent ; pleine sous le même liquide).
					auto same = [&](int ax, int ay, int az) { return pad.At(ax, ay, az) == id; };
					auto height = [&](int ax, int ay, int az) {
						return LiquidHeight(def, pad.params[size_t(Padded::Index(ax, ay, az))], same(ax, ay + 1, az));
					};
					float corner[2][2]; // [x][z]
					for (int cx = 0; cx <= 1; ++cx)
						for (int cz = 0; cz <= 1; ++cz) {
							float sum = 0.f;
							int count = 0;
							bool full = false;
							for (int dx = cx - 1; dx <= cx; ++dx)
								for (int dz = cz - 1; dz <= cz; ++dz)
									if (same(x + dx, y, z + dz)) {
										const float h = height(x + dx, y, z + dz);
										full = full || h >= 1.f;
										sum += h;
										++count;
									}
							corner[cx][cz] = full ? 1.f : count > 0 ? sum / float(count) : height(x, y, z);
						}
					BlockRender liquid = r;
					if (!LiquidSource(param) && !def.liquid.flowingTexture.IsEmpty())
						liquid.faces.fill(r.emissive ? m_atlas.LookupGlow(def.liquid.flowingTexture) : m_atlas.Lookup(def.liquid.flowingTexture));
					const float light = 1.f; // la lumière vient de lightVertices
					for (int fi = 0; fi < FACE_COUNT; ++fi) {
						const Face f = Face(fi);
						const BlockPos n = BlockPos{x, y, z}.Neighbor(f);
						if (same(n.x, n.y, n.z) || (f != Face::TOP && occludes(n.x, n.y, n.z)) ||
							(f == Face::TOP && occludes(n.x, n.y, n.z) && corner[0][0] >= 1.f && corner[1][1] >= 1.f))
							continue;
						math::FVector3 corners[4];
						FaceCorners(f, Box{{0.f, 0.f, 0.f}, {1.f, 1.f, 1.f}}, corners);
						for (math::FVector3 &c : corners)
							if (c.y > 0.5f)
								c.y = corner[c.x > 0.5f ? 1 : 0][c.z > 0.5f ? 1 : 0];
						const TextureAtlas::Rect &rect = liquid.faces[size_t(fi)];
						math::FVector2 uvs[4];
						math::FVector3 placed[4];
						for (int k = 0; k < 4; ++k) {
							const math::FVector2 t = FaceUv(f, corners[k]);
							uvs[k] = {rect.u0 + (rect.u1 - rect.u0) * t.x, rect.v0 + (rect.v1 - rect.v0) * t.y};
							placed[k] = corners[k] + offset;
						}
						PushQuad(out, placed, uvs, NormalOf(f), (liquid.tintFaces >> fi) & 1 ? liquid.tinted : liquid.tint,
								 {light, light, light, light});
					}
				} else if (r.shape == ShapeKind::CUBE) {
					Box box{{0.f, 0.f, 0.f}, {1.f, 1.f, 1.f}};
					if (def.shape.size.x != 1.f || def.shape.size.y != 1.f || def.shape.size.z != 1.f) {
						const math::FVector3 s = def.shape.size;
						box = Box{{0.5f - s.x * 0.5f, 0.f, 0.5f - s.z * 0.5f}, {0.5f + s.x * 0.5f, s.y, 0.5f + s.z * 0.5f}};
					}
					auto cull = [&](Face f) {
						const BlockPos n = BlockPos{x, y, z}.Neighbor(f);
						if (occludes(n.x, n.y, n.z))
							return !(r.fluid && f == Face::TOP && box.max.y < 1.f);
						// Deux blocs translucides identiques : pas de face entre eux.
						return r.translucent && pad.At(n.x, n.y, n.z) == id;
					};
					auto ao = [&](Face f, const math::FVector3 *corners) {
						std::array<float, 4> levels{1.f, 1.f, 1.f, 1.f};
						if (r.translucent || !options.ambientOcclusion)
							return levels;
						const BlockPos n = FaceNormal(f);
						const int axis = NormalAxis(f);
						const int t1 = (axis + 1) % 3, t2 = (axis + 2) % 3;
						for (int k = 0; k < 4; ++k) {
							int s[3] = {0, 0, 0};
							s[t1] = Get(corners[k], t1) > 0.5f ? 1 : -1;
							s[t2] = Get(corners[k], t2) > 0.5f ? 1 : -1;
							const int qx = x + n.x, qy = y + n.y, qz = z + n.z;
							const int ax = qx + (t1 == 0 ? s[0] : 0), ay = qy + (t1 == 1 ? s[1] : 0), az = qz + (t1 == 2 ? s[2] : 0);
							const int bx = qx + (t2 == 0 ? s[0] : 0), by = qy + (t2 == 1 ? s[1] : 0), bz = qz + (t2 == 2 ? s[2] : 0);
							const bool side1 = shades(ax, ay, az), side2 = shades(bx, by, bz);
							const bool corner = shades(qx + s[0], qy + s[1], qz + s[2]);
							const int level = side1 && side2 ? 0 : 3 - int(side1) - int(side2) - int(corner);
							levels[size_t(k)] = AO_LEVELS[std::clamp(level, 0, 3)];
						}
						return levels;
					};
					EmitBox(out, box, r, offset, cull, ao, param);
				} else if (r.shape == ShapeKind::NODEBOX) {
					for (const Box &local : def.shape.boxes) {
						const Box b = RotateBlockBox(local, param);
						// Une face sur le bord de la cellule est cachée par un voisin opaque.
						auto cull = [&](Face f) {
							const float v = Get(f == Face::EAST || f == Face::TOP || f == Face::SOUTH ? b.max : b.min, NormalAxis(f));
							const bool onEdge = (f == Face::EAST || f == Face::TOP || f == Face::SOUTH) ? v >= 1.f : v <= 0.f;
							if (!onEdge)
								return false;
							const BlockPos n = BlockPos{x, y, z}.Neighbor(f);
							return occludes(n.x, n.y, n.z);
						};
						EmitBox(out, b, r, offset, cull, [](Face, const math::FVector3 *) {
							return std::array<float, 4>{1.f, 1.f, 1.f, 1.f};
						}, param);
					}
				} else if (r.shape == ShapeKind::CROSS) {
					// Densité de végétation : une partie des plantes seulement.
					if (options.crossDensity >= 1.f ||
						float(HashPosition(0x9e37u, origin.x + x, origin.y + y, origin.z + z, 3) % 1000u) < options.crossDensity * 1000.f)
						EmitCross(out, r, offset);
				} else if (r.shape == ShapeKind::MODEL) {
					EmitModel(out, def.shape.model, r, offset, param, def.mod, true);
				}
				// Bloc lumineux : sa propre teinte, dessiné sans éclairage (matériau
				// émissif) ; les autres reçoivent la lumière du monde.
				if (r.emissive) {
					for (size_t i = firstVertex; i < out.vertices.size(); ++i) {
						for (int k = 0; k < 3; ++k)
							out.vertices[i].color[size_t(k)] = r.tint[size_t(k)];
						out.vertices[i].light = {0.f, 0.f, 0.f, 1.f};
					}
				} else {
					lightVertices(out, firstVertex, origin + BlockPos{x, y, z});
				}
			}
	(void)origin;
	return mesh;
}

MeshBuffer ChunkMesher::BuildShape(const ShapeDesc &shape, const Appearance &look, uint8_t param, const String &mod) const {
	MeshBuffer out;
	BlockRender r;
	r.visible = true;
	r.shape = shape.kind;
	r.tint = look.tint;
	r.emissive = look.lightEmission > 0;
	for (int f = 0; f < FACE_COUNT; ++f)
		r.faces[size_t(f)] = r.emissive ? m_atlas.LookupGlow(look.faces[size_t(f)]) : m_atlas.Lookup(look.faces[size_t(f)]);
	const math::FVector3 center{-0.5f, 0.f, -0.5f};
	switch (shape.kind) {
	case ShapeKind::NONE: break;
	case ShapeKind::CUBE: {
		const math::FVector3 s = shape.size;
		EmitBox(out, Box{{-s.x * 0.5f, 0.f, -s.z * 0.5f}, {s.x * 0.5f, s.y, s.z * 0.5f}}, r, {0.f, 0.f, 0.f}, {}, {});
		break;
	}
	case ShapeKind::NODEBOX:
		for (const Box &b : shape.boxes)
			EmitBox(out, RotateBlockBox(b, param), r, center, {}, {}, param);
		break;
	case ShapeKind::CROSS: EmitCross(out, r, center); break;
	case ShapeKind::MODEL: EmitModel(out, shape.model, r, {0.f, 0.f, 0.f}, param, mod, false); break;
	}
	return out;
}

MeshBuffer ChunkMesher::BuildBlock(BlockId block) const {
	const BlockDef &def = m_content.Block(block);
	ShapeDesc shape = def.shape;
	if (shape.kind == ShapeKind::CUBE)
		shape.size = {1.f, 1.f, 1.f};
	MeshBuffer out;
	if (shape.kind == ShapeKind::MODEL) {
		BlockRender r = m_blocks[block < m_blocks.size() ? block : 0];
		EmitModel(out, shape.model, r, {-0.5f, 0.f, -0.5f}, 0, def.mod, true);
		return out;
	}
	return BuildShape(shape, def.look, 0, def.mod);
}

} // namespace voxel
