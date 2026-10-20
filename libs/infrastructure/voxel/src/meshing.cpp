// Définitions de voxel/meshing.hpp
#include <voxel/meshing.hpp>
#include <voxel/physics.hpp>

#include <algorithm>
#include <cmath>
#include <set>

namespace voxel {

// ── Atlas ────────────────────────────────────────────────────────────────────

void TextureAtlas::Build(AssetManager &assets, const ContentRegistry &content, int tileSize) {
	m_tile = std::max(2, tileSize);
	m_rects.clear();
	std::set<std::string> ids;
	ids.insert(MISSING);
	for (const TextureDef *t : content.Textures())
		ids.insert(t->id.CStr());
	auto add = [&](const String &id) {
		if (!id.IsEmpty())
			ids.insert(id.CStr());
	};
	for (const BlockDef &b : content.Blocks())
		for (const String &f : b.look.faces)
			add(f);
	for (const ItemDef *i : content.Items())
		add(i->icon);
	for (const EntityDef *e : content.Entities())
		for (const String &f : e->look.faces)
			add(f);

	// Images d'abord : une texture introuvable ne prend pas de tuile, elle
	// renverra le damier commun.
	std::vector<std::pair<std::string, Image>> tiles;
	tiles.push_back({MISSING, Image{}});
	for (const std::string &id : ids)
		if (id != MISSING)
			if (std::shared_ptr<TextureImage> tex = assets.Texture(String(id.c_str())); tex && !tex->Pixels().IsEmpty())
				tiles.push_back({id, tex->Pixels()});

	const int count = int(tiles.size());
	const int columns = std::max(1, int(std::ceil(std::sqrt(double(count)))));
	int width = 1;
	while (width < columns * m_tile)
		width <<= 1;
	const int rows = (count + (width / m_tile) - 1) / (width / m_tile);
	int height = 1;
	while (height < rows * m_tile)
		height <<= 1;
	m_image.width = width;
	m_image.height = height;
	m_image.rgba.assign(size_t(width * height * 4), 0);

	const int perRow = width / m_tile;
	for (int index = 0; index < count; ++index) {
		const auto &[id, source] = tiles[size_t(index)];
		const int tx = (index % perRow) * m_tile, ty = (index / perRow) * m_tile;
		const bool missing = source.IsEmpty();
		// Image animée (bande verticale) : première image seulement.
		const int sw = missing ? 2 : source.width, sh = missing ? 2 : std::min(source.height, source.width);
		for (int y = 0; y < m_tile; ++y)
			for (int x = 0; x < m_tile; ++x) {
				uint8_t *dst = &m_image.rgba[size_t(((ty + y) * width + tx + x) * 4)];
				if (missing) {
					const bool check = ((x * 2 / m_tile) + (y * 2 / m_tile)) % 2 == 0;
					dst[0] = check ? 255 : 0;
					dst[1] = 0;
					dst[2] = check ? 255 : 0;
					dst[3] = 255;
				} else {
					const uint8_t *src = source.At(std::min(sw - 1, x * sw / m_tile), std::min(sh - 1, y * sh / m_tile));
					std::copy(src, src + 4, dst);
				}
			}
		// Demi-texel de marge : pas de débordement sur la tuile voisine.
		const float hu = 0.5f / float(width), hv = 0.5f / float(height);
		m_rects[id] = Rect{float(tx) / float(width) + hu, float(ty) / float(height) + hv, float(tx + m_tile) / float(width) - hu,
						   float(ty + m_tile) / float(height) - hv};
	}
}

TextureAtlas::Rect TextureAtlas::Lookup(const String &id) const {
	auto it = m_rects.find(id.CStr());
	if (it != m_rects.end())
		return it->second;
	auto missing = m_rects.find(MISSING);
	return missing != m_rects.end() ? missing->second : Rect{};
}

bool TextureAtlas::Contains(const String &id) const { return m_rects.contains(id.CStr()); }

// ── Maillage ─────────────────────────────────────────────────────────────────

namespace {

constexpr float AO_LEVELS[4] = {0.45f, 0.65f, 0.83f, 1.f};

int NormalAxis(Face f) { return f == Face::EAST || f == Face::WEST ? 0 : f == Face::TOP || f == Face::BOTTOM ? 1 : 2; }

float Get(const math::FVector3 &v, int a) { return a == 0 ? v.x : a == 1 ? v.y : v.z; }

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
		v.color = {uint8_t(float(tint[0]) * ao[size_t(k)]), uint8_t(float(tint[1]) * ao[size_t(k)]),
				   uint8_t(float(tint[2]) * ao[size_t(k)]), tint[3]};
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
		r.opaqueCube = b.IsOpaqueCube();
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
		for (int f = 0; f < FACE_COUNT; ++f)
			r.faces[size_t(f)] = atlas.Lookup(b.look.faces[size_t(f)]);
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
						  const std::function<std::array<float, 4>(Face, const math::FVector3 *)> &ao) const {
	for (int fi = 0; fi < FACE_COUNT; ++fi) {
		const Face f = Face(fi);
		if (cull && cull(f))
			continue;
		math::FVector3 corners[4];
		FaceCorners(f, box, corners);
		const TextureAtlas::Rect &rect = render.faces[size_t(fi)];
		math::FVector2 uvs[4];
		for (int k = 0; k < 4; ++k) {
			const math::FVector2 t = FaceUv(f, corners[k]);
			uvs[k] = {rect.u0 + (rect.u1 - rect.u0) * t.x, rect.v0 + (rect.v1 - rect.v0) * t.y};
		}
		const std::array<float, 4> occlusion = ao ? ao(f, corners) : std::array<float, 4>{1.f, 1.f, 1.f, 1.f};
		math::FVector3 placed[4];
		for (int k = 0; k < 4; ++k)
			placed[k] = corners[k] + offset;
		PushQuad(out, placed, uvs, NormalOf(f), (render.tintFaces >> fi) & 1 ? render.tinted : render.tint, occlusion);
	}
}

void ChunkMesher::EmitCross(MeshBuffer &out, const BlockRender &render, const math::FVector3 &o) const {
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
		PushQuad(out, front, uvs, n, tint, {1.f, 1.f, 1.f, 1.f});
		PushQuad(out, back, backUv, n * -1.f, tint, {1.f, 1.f, 1.f, 1.f});
	}
}

void ChunkMesher::EmitModel(MeshBuffer &out, const String &model, const BlockRender &render, const math::FVector3 &offset,
							uint8_t param, const String &mod, bool normalize) const {
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
	// Un voisin NON chargé cache les faces (pas de murs visibles au bord du monde chargé).
	auto occludes = [&](int x, int y, int z) {
		const int i = Padded::Index(x, y, z);
		return !pad.loaded[size_t(i)] || render(pad.ids[size_t(i)]).opaqueCube;
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
				if (base.biomeTint) {
					tintedRender = base;
					const std::array<uint8_t, 3> &c = biomeColor(x, z, base.biomeTint);
					for (int k = 0; k < 3; ++k)
						tintedRender.tinted[size_t(k)] = uint8_t(int(base.tint[size_t(k)]) * int(c[size_t(k)]) / 255);
					chosen = &tintedRender;
				}
				const BlockRender &r = *chosen;
				const BlockDef &def = m_content.Block(id);
				MeshBuffer &out = r.translucent ? mesh.translucent : mesh.opaque;
				const math::FVector3 offset{float(x), float(y), float(z)};
				const uint8_t param = pad.params[size_t(Padded::Index(x, y, z))];
				if (r.shape == ShapeKind::CUBE) {
					Box box{{0.f, 0.f, 0.f}, {1.f, 1.f, 1.f}};
					// Fluide sous l'air : surface abaissée.
					if (r.fluid && render(pad.At(x, y + 1, z)).id != id)
						box.max.y = 0.875f;
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
						if (r.translucent)
							return levels;
						const BlockPos n = FaceNormal(f);
						const int axis = NormalAxis(f);
						const int t1 = (axis + 1) % 3, t2 = (axis + 2) % 3;
						for (int k = 0; k < 4; ++k) {
							int s[3] = {0, 0, 0};
							s[t1] = Get(corners[k], t1) > 0.5f ? 1 : -1;
							s[t2] = Get(corners[k], t2) > 0.5f ? 1 : -1;
							const int qx = x + n.x, qy = y + n.y, qz = z + n.z;
							const bool side1 = occludes(qx + (t1 == 0 ? s[0] : 0), qy + (t1 == 1 ? s[1] : 0), qz + (t1 == 2 ? s[2] : 0));
							const bool side2 = occludes(qx + (t2 == 0 ? s[0] : 0), qy + (t2 == 1 ? s[1] : 0), qz + (t2 == 2 ? s[2] : 0));
							const bool corner = occludes(qx + s[0], qy + s[1], qz + s[2]);
							const int level = side1 && side2 ? 0 : 3 - int(side1) - int(side2) - int(corner);
							levels[size_t(k)] = AO_LEVELS[std::clamp(level, 0, 3)];
						}
						return levels;
					};
					EmitBox(out, box, r, offset, cull, ao);
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
						EmitBox(out, b, r, offset, cull, {});
					}
				} else if (r.shape == ShapeKind::CROSS) {
					EmitCross(out, r, offset);
				} else if (r.shape == ShapeKind::MODEL) {
					EmitModel(out, def.shape.model, r, offset, param, def.mod, true);
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
	for (int f = 0; f < FACE_COUNT; ++f)
		r.faces[size_t(f)] = m_atlas.Lookup(look.faces[size_t(f)]);
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
			EmitBox(out, RotateBlockBox(b, param), r, center, {}, {});
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
