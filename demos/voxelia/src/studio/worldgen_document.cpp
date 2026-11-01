// Définitions de voxelia/studio/worldgen_document.hpp
#include <voxelia/studio/worldgen_document.hpp>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <functional>
#include <limits>

#include <voxel/json.hpp>

namespace voxelia::studio {

namespace json = voxel::json;
using dataio::Node;
using dataio::NodePtr;
using voxel::BlockPos;
using voxel::ChunkPos;

const char *WorldgenKindId(WorldgenKind kind) noexcept {
	switch (kind) {
	case WorldgenKind::Generator:
		return "generator";
	case WorldgenKind::Dimension:
		return "dimension";
	case WorldgenKind::Biome:
	default:
		return "biome";
	}
}

Option<WorldgenDocument> WorldgenDocument::FromContent(const voxel::ContentRegistry &content, WorldgenKind kind, const String &id) {
	WorldgenDocument doc;
	doc.m_kind = kind;
	auto take = [&](const auto *def) -> bool {
		if (!def || !def->source)
			return false;
		doc.m_json = def->source->Clone();
		doc.m_id = def->id;
		doc.m_mod = def->mod;
		doc.m_baseDir = def->baseDir;
		doc.m_file = def->sourceFile;
		return true;
	};
	const bool found = kind == WorldgenKind::Biome	   ? take(content.FindBiome(id))
					   : kind == WorldgenKind::Generator ? take(content.FindGenerator(id))
														 : take(content.FindDimension(id));
	return found ? Option<WorldgenDocument>(Some(std::move(doc))) : NONE;
}

WorldgenDocument WorldgenDocument::New(WorldgenKind kind, const String &id, const String &mod, const String &baseDir, const String &file) {
	WorldgenDocument doc;
	doc.m_kind = kind;
	doc.m_mod = mod;
	doc.m_id = voxel::ContentRegistry::Qualify(id, mod);
	doc.m_baseDir = baseDir;
	doc.m_file = file;
	const size_t colon = id.Find(":");
	const String bare = colon == String::NPOS ? id : id.Substring(colon + 1);
	doc.m_json = Node::MakeObject();
	doc.m_json->Set("type", json::MakeStr(String(WorldgenKindId(kind))));
	doc.m_json->Set("id", json::MakeStr(bare));
	switch (kind) {
	case WorldgenKind::Biome:
		doc.m_json->Set("displayName", json::MakeStr(bare));
		SetJsonPath(doc.m_json, "terrain.height", json::MakeNum(64.0));
		SetJsonPath(doc.m_json, "terrain.variation", json::MakeNum(8.0));
		SetJsonPath(doc.m_json, "blocks.surface", json::MakeStr(String("core:grass")));
		SetJsonPath(doc.m_json, "blocks.subsurface", json::MakeStr(String("core:dirt")));
		SetJsonPath(doc.m_json, "blocks.filler", json::MakeStr(String("core:stone")));
		break;
	case WorldgenKind::Generator:
		doc.m_json->Set("kind", json::MakeStr(String("noise")));
		doc.m_json->Set("seaLevel", json::MakeInt(62));
		break;
	case WorldgenKind::Dimension:
		doc.m_json->Set("name", json::MakeStr(bare));
		break;
	}
	doc.m_dirty = true;
	return doc;
}

Result<voxel::ContentRegistry, String> WorldgenDocument::Scratch(const voxel::ContentRegistry &live) const {
	voxel::ContentRegistry scratch = live; // le contenu vivant n'est jamais touché
	scratch.ForgetLoadWarnings(m_id);
	Result<String, String> loaded = scratch.Load(m_json, Context());
	if (loaded.IsError())
		return Err(loaded.Error());
	scratch.Finalize();
	return Ok(std::move(scratch));
}

std::vector<WorldgenDiagnostic> WorldgenDocument::Validate(const voxel::ContentRegistry &live) const {
	std::vector<WorldgenDiagnostic> out;
	auto warn = [&](const String &m) { out.push_back(WorldgenDiagnostic{WorldgenDiagnostic::Severity::Warning, m}); };
	Result<voxel::ContentRegistry, String> scratch = Scratch(live);
	if (scratch.IsError()) {
		out.push_back(WorldgenDiagnostic{WorldgenDiagnostic::Severity::Error, scratch.Error()});
		return out;
	}
	const voxel::ContentRegistry &c = scratch.Value();
	for (const String &warning : c.Validate())
		if (warning.StartsWith(m_id + String(" :")))
			warn(warning);
	auto block = [&](const String &id, const char *what) {
		if (!id.IsEmpty() && !c.FindBlock(id))
			warn(String::Format("%s : bloc inconnu « %s »", what, id.CStr()));
	};
	switch (m_kind) {
	case WorldgenKind::Biome: {
		const voxel::BiomeDef *b = c.FindBiome(m_id);
		if (!b) {
			out.push_back(WorldgenDiagnostic{WorldgenDiagnostic::Severity::Error, String("ce n'est pas un biome (« type »)")});
			break;
		}
		block(b->surface, "surface");
		block(b->subsurface, "sous-sol");
		block(b->filler, "remplissage");
		block(b->underwater, "sous l'eau");
		if (!b->dimension.IsEmpty() && !c.FindDimension(b->dimension))
			warn(String("dimension inconnue : ") + b->dimension);
		if (b->temperature < 0.f || b->temperature > 1.f || b->humidity < 0.f || b->humidity > 1.f)
			warn(String("température et humidité : de 0 à 1"));
		for (const voxel::Decoration &d : b->decorations) {
			block(d.block, "décoration");
			if (!d.structure.IsEmpty() && !c.FindStructure(d.structure))
				warn(String("décoration : structure inconnue « ") + d.structure + String(" »"));
			if (d.chance < 0.f || d.chance > 1.f)
				warn(String::Format("décoration : chance %.4f hors de 0..1", double(d.chance)));
		}
		break;
	}
	case WorldgenKind::Generator: {
		const voxel::GeneratorDef *g = c.FindGenerator(m_id);
		if (!g) {
			out.push_back(WorldgenDiagnostic{WorldgenDiagnostic::Severity::Error, String("ce n'est pas un générateur (« type »)")});
			break;
		}
		static const char *KINDS[] = {"noise", "flat", "islands", "caverns", "maze", "script"};
		if (std::none_of(std::begin(KINDS), std::end(KINDS), [&](const char *k) { return g->kind == k; }))
			warn(String("genre inconnu « ") + g->kind + String(" » (noise, flat, islands, caverns, maze, script)"));
		block(g->fluid, "liquide");
		block(g->bedrock, "socle");
		for (const String &biome : g->biomes)
			if (!c.FindBiome(biome))
				warn(String("biome inconnu : ") + biome);
		for (const auto &[layer, thickness] : g->layers)
			block(layer, "couche");
		break;
	}
	case WorldgenKind::Dimension: {
		const voxel::DimensionDef *d = c.FindDimension(m_id);
		if (!d) {
			out.push_back(WorldgenDiagnostic{WorldgenDiagnostic::Severity::Error, String("ce n'est pas une dimension (« type »)")});
			break;
		}
		if (!d->generator.IsEmpty() && !c.FindGenerator(d->generator))
			warn(String("générateur inconnu : ") + d->generator);
		if (d->spawnTop <= d->spawnBottom)
			warn(String("bornes d'apparition : le haut doit dépasser le bas"));
		block(d->arrivalPortal, "portail d'arrivée");
		for (const String &tex : {d->cubemap, d->nightCubemap})
			if (!tex.IsEmpty() && !c.FindTexture(tex))
				warn(String("ciel : texture inconnue « ") + tex + String(" »"));
		if (d->gravity == 0.f)
			warn(String("gravité nulle : rien ne retombe"));
		break;
	}
	}
	return out;
}

std::vector<SettingRef> WorldgenDocument::Settings() const {
	std::vector<SettingRef> out;
	std::function<void(const NodePtr &, const String &)> walk = [&](const NodePtr &n, const String &path) {
		if (!n)
			return;
		if (n->IsObject()) {
			if (const String id = json::Str(n, "setting"); !id.IsEmpty()) {
				const NodePtr fallback = n->Get("fallback");
				out.push_back(SettingRef{path, id, fallback ? CompactJson(json::Encode(fallback)) : String()});
				return;
			}
			for (const String &key : n->Keys())
				walk(n->Get(key), path.IsEmpty() ? key : path + String(".") + key);
		} else if (n->IsArray()) {
			for (size_t i = 0; i < n->GetSize(); ++i)
				walk(n->At(i), path + String::Format("[%zu]", i));
		}
	};
	walk(m_json, String());
	return out;
}

String WorldgenDocument::PreviewDimension(const voxel::ContentRegistry &content) const {
	const String overworld = content.DefaultDimension();
	switch (m_kind) {
	case WorldgenKind::Dimension:
		return m_id;
	case WorldgenKind::Biome: {
		if (const String dim = json::Str(m_json, "dimension"); !dim.IsEmpty())
			return voxel::ContentRegistry::Qualify(dim, m_mod);
		for (const voxel::DimensionDef *d : content.Dimensions())
			if (const voxel::GeneratorDef *g = content.FindGenerator(d->generator))
				if (std::find(g->biomes.begin(), g->biomes.end(), m_id) != g->biomes.end())
					return d->id;
		return overworld;
	}
	case WorldgenKind::Generator:
		for (const voxel::DimensionDef *d : content.Dimensions())
			if (d->generator == m_id)
				return d->id;
		return overworld;
	}
	return overworld;
}

// ── Aperçu ───────────────────────────────────────────────────────────────────

Result<WorldgenPreview, String> BuildPreview(voxel::Game &game, const voxel::ContentRegistry &content, const String &dimension,
											 const BlockPos &centre, int side, Option<int> cutY) {
	const auto start = std::chrono::steady_clock::now();
	if (side < 1 || side > 8)
		return Err(String("de 1 à 8 chunks de côté"));
	WorldgenPreview p;
	p.content = std::make_unique<voxel::ContentRegistry>(content);
	p.dimension = dimension;
	p.generator = game.DimensionGenerator(*p.content, dimension);
	if (!p.generator)
		return Err(String("générateur introuvable"));
	voxel::WorldSettings settings;
	settings.name = String("aperçu");
	settings.seed = game.DimensionSeed(dimension);
	settings.generator = p.generator->Definition().id;
	p.world = std::make_unique<voxel::World>(*p.content, settings);
	p.world->SetGenerator(p.generator);
	// Tranche : les étages d'un labyrinthe, sinon autour de la surface.
	const voxel::MazeLayout maze = p.generator->Maze();
	int defaultCut = std::numeric_limits<int>::max();
	if (!maze.floors.empty()) {
		p.minY = maze.floors.front().floorY - 3;
		p.maxY = maze.floors.back().ceilingY + 2;
		defaultCut = maze.floors.front().ceilingY - 1; // le premier étage, vu d'en haut
	} else {
		const int h = p.generator->SurfaceHeight(centre.x, centre.z);
		p.minY = h - 24;
		p.maxY = h + 24;
	}
	p.cutY = cutY.IsSome() ? cutY.Value() : defaultCut;
	p.centre = BlockPos{centre.x, std::min(p.cutY, p.maxY), centre.z};
	const int c0x = (centre.x >> voxel::CHUNK_SHIFT) - side / 2, c0z = (centre.z >> voxel::CHUNK_SHIFT) - side / 2;
	const int cy0 = p.minY >> voxel::CHUNK_SHIFT, cy1 = std::min(p.maxY, p.cutY) >> voxel::CHUNK_SHIFT;
	for (int cz = c0z; cz < c0z + side; ++cz)
		for (int cx = c0x; cx < c0x + side; ++cx)
			for (int cy = cy0; cy <= cy1; ++cy) {
				(void)p.world->EnsureChunk(ChunkPos{cx, cy, cz});
				p.chunks.push_back(ChunkPos{cx, cy, cz});
			}
	// Coupe : tout ce qui dépasse est retiré (on voit l'intérieur d'en haut).
	if (p.cutY != std::numeric_limits<int>::max())
		for (const ChunkPos &cp : p.chunks) {
			const BlockPos o = cp.Origin();
			if (o.y + voxel::CHUNK_SIZE - 1 <= p.cutY)
				continue;
			for (int y = std::max(o.y, p.cutY + 1); y < o.y + voxel::CHUNK_SIZE; ++y)
				for (int z = o.z; z < o.z + voxel::CHUNK_SIZE; ++z)
					for (int x = o.x; x < o.x + voxel::CHUNK_SIZE; ++x)
						(void)p.world->SetBlock(BlockPos{x, y, z}, voxel::AIR, 0, voxel::ChangeCause::SCRIPT);
		}
	p.milliseconds = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
	return Ok(std::move(p));
}

// ── Graphe des dimensions ────────────────────────────────────────────────────

DimensionGraph BuildDimensionGraph(const voxel::ContentRegistry &content) {
	DimensionGraph g;
	const String overworld = content.DefaultDimension();
	g.nodes.push_back(overworld);
	for (const voxel::DimensionDef *d : content.Dimensions())
		if (d->id != overworld)
			g.nodes.push_back(d->id);
	for (const voxel::DimensionDef *d : content.Dimensions())
		if (!d->enterOnDeath.empty()) {
			String causes;
			for (const String &cause : d->enterOnDeath)
				causes += (causes.IsEmpty() ? String() : String(", ")) + cause;
			g.links.push_back(DimensionLink{String("*"), d->id, String("death"), String("mort : ") + causes});
		}
	// Ascenseurs (mod « elevators ») : destination ← étages « from ».
	if (NodePtr destinations = json::At(content.Config(), "elevators.destinations"); destinations && destinations->IsObject())
		for (const String &key : destinations->Keys()) {
			const NodePtr d = destinations->Get(key);
			const String to = json::Str(d, "dimension");
			if (to.IsEmpty())
				continue;
			const String label = json::Str(d, "label", key);
			const std::vector<String> from = json::StrList(d, "from");
			if (from.empty())
				g.links.push_back(DimensionLink{String("*"), to, String("elevator"), label});
			for (const String &f : from)
				g.links.push_back(DimensionLink{f, to, String("elevator"), label});
		}
	for (const voxel::BlockDef &b : content.Blocks())
		if (const String to = json::Str(b.raw, "portal.dimension"); !to.IsEmpty() && b.stateGroup.IsEmpty())
			g.links.push_back(DimensionLink{String("*"), to == "@return" ? String("@return") : voxel::ContentRegistry::Qualify(to, b.mod),
											String("portal"), String("portail ") + b.id});
	return g;
}

} // namespace voxelia::studio
