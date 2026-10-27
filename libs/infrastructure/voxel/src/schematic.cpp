#include <voxel/schematic.hpp>

#include <voxel/json.hpp>
#include <voxel/physics.hpp>

#include <archive/archive_deflate.hpp>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <unordered_set>

namespace voxel {

namespace {

constexpr const char *NATIVE_FORMAT = "voxel.structure";
constexpr int NATIVE_VERSION = 1;

uint8_t RotateParam(uint8_t param, int quarters) { return uint8_t((param & ~3u) | ((param + unsigned(quarters)) & 3u)); }

/// Orientation après un miroir : x inverse est et ouest, z nord et sud.
uint8_t MirrorParam(uint8_t param, int axis) {
	const unsigned p = param & 3u;
	const unsigned m = axis == 0 ? (4u - p) & 3u : axis == 2 ? (6u - p) & 3u : p;
	return uint8_t((param & ~3u) | m);
}

/// Minetest (+z au nord, façade « facedir » 0 au sud) ↔ ce moteur (+z au sud,
/// orientation 0 : façade au nord).
uint8_t ParamFromMinetest(uint8_t param2) { return uint8_t(((param2 & 3u) + 2u) & 3u); }
uint8_t ParamToMinetest(uint8_t param) { return uint8_t(((param & 3u) + 2u) & 3u); }

BlockPos Min(const BlockPos &a, const BlockPos &b) { return {std::min(a.x, b.x), std::min(a.y, b.y), std::min(a.z, b.z)}; }
BlockPos Max(const BlockPos &a, const BlockPos &b) { return {std::max(a.x, b.x), std::max(a.y, b.y), std::max(a.z, b.z)}; }

NodePtr Triple(const BlockPos &p) {
	NodePtr a = dataio::Node::MakeArray();
	a->Push(json::MakeInt(p.x));
	a->Push(json::MakeInt(p.y));
	a->Push(json::MakeInt(p.z));
	return a;
}

BlockPos TripleOf(const NodePtr &n, BlockPos fallback = {0, 0, 0}) {
	if (!n || !n->IsArray() || n->GetSize() < 3)
		return fallback;
	return {json::Int(n->At(0), "", 0), json::Int(n->At(1), "", 0), json::Int(n->At(2), "", 0)};
}

Result<std::vector<uint8_t>, String> ReadFile(const String &path) {
	std::ifstream in(path.CStr(), std::ios::binary);
	if (!in)
		return Err(String::Format("fichier illisible : %s", path.CStr()));
	std::vector<uint8_t> bytes((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
	return Ok(std::move(bytes));
}

uint32_t Adler32(const uint8_t *data, size_t size) {
	uint32_t a = 1, b = 0;
	for (size_t i = 0; i < size; ++i) {
		a = (a + data[i]) % 65521u;
		b = (b + a) % 65521u;
	}
	return (b << 16) | a;
}

} // namespace

// ── Cellules ─────────────────────────────────────────────────────────────────

Schematic::Schematic(const BlockPos &size) : m_size(Max(size, {0, 0, 0})) {
	m_cells.assign(size_t(Volume()), VOID);
}

uint16_t Schematic::PaletteIndex(const String &block) {
	for (size_t i = 0; i < m_palette.size(); ++i)
		if (m_palette[i] == block)
			return uint16_t(i);
	if (m_palette.size() >= VOID - 1)
		return VOID;
	m_palette.push_back(block);
	return uint16_t(m_palette.size() - 1);
}

String Schematic::BlockAt(const BlockPos &p) const {
	if (!Inside(p))
		return String();
	const uint16_t i = m_cells[Index(p)];
	return i == VOID ? String() : m_palette[i];
}

uint8_t Schematic::ParamAt(const BlockPos &p) const { return Inside(p) && !m_params.empty() ? m_params[Index(p)] : 0; }

void Schematic::Set(const BlockPos &p, const String &block, uint8_t param) {
	if (!Inside(p))
		return;
	if (block.IsEmpty()) {
		Clear(p);
		return;
	}
	m_cells[Index(p)] = PaletteIndex(block);
	if (param != 0 && m_params.empty())
		m_params.assign(m_cells.size(), 0);
	if (!m_params.empty())
		m_params[Index(p)] = param;
}

void Schematic::Clear(const BlockPos &p) {
	if (!Inside(p))
		return;
	m_cells[Index(p)] = VOID;
	if (!m_params.empty())
		m_params[Index(p)] = 0;
	containers.erase(p);
}

int64_t Schematic::Filled() const {
	return std::count_if(m_cells.begin(), m_cells.end(), [](uint16_t c) { return c != VOID; });
}

int64_t Schematic::Solid() const {
	int64_t n = 0;
	for (uint16_t c : m_cells)
		n += c != VOID && m_palette[c] != AIR_ID;
	return n;
}

std::map<String, int64_t> Schematic::Counts() const {
	std::vector<int64_t> counts(m_palette.size(), 0);
	for (uint16_t c : m_cells)
		if (c != VOID)
			++counts[c];
	std::map<String, int64_t> out;
	for (size_t i = 0; i < m_palette.size(); ++i)
		if (counts[i] > 0)
			out[m_palette[i]] += counts[i];
	return out;
}

// ── Monde ────────────────────────────────────────────────────────────────────

Schematic Schematic::Capture(World &world, const BlockPos &a, const BlockPos &b, Option<BlockPos> anchor) {
	const BlockPos lo = Min(a, b), hi = Max(a, b);
	Schematic s(hi - lo + BlockPos{1, 1, 1});
	for (int y = lo.y; y <= hi.y; ++y)
		for (int z = lo.z; z <= hi.z; ++z)
			for (int x = lo.x; x <= hi.x; ++x) {
				const BlockPos p{x, y, z};
				if (!world.IsLoaded(p))
					(void)world.EnsureChunk(ChunkOf(p));
				s.Set(p - lo, world.BlockAt(p).id, world.GetParam(p));
			}
	s.anchor = anchor.IsSome() ? anchor.Unwrap() - lo : BlockPos{0, 0, 0};
	return s;
}

std::pair<BlockPos, BlockPos> Schematic::Bounds(const BlockPos &at, int rotation) const {
	BlockPos lo{0, 0, 0}, hi{0, 0, 0};
	bool first = true;
	for (int cx : {0, m_size.x - 1})
		for (int cy : {0, m_size.y - 1})
			for (int cz : {0, m_size.z - 1}) {
				const BlockPos p = at + RotateOffset(BlockPos{cx, cy, cz} - anchor, uint8_t(((rotation % 4) + 4) % 4));
				lo = first ? p : Min(lo, p);
				hi = first ? p : Max(hi, p);
				first = false;
			}
	return {lo, hi};
}

int64_t Schematic::Paste(World &world, const BlockPos &at, const PasteOptions &options,
						 const std::function<void(const BlockPos &, const NodePtr &)> &containerFill) const {
	const ContentRegistry &content = world.Content();
	const int rotation = ((options.rotation % 4) + 4) % 4;
	std::vector<const BlockDef *> defs;
	for (const String &id : m_palette)
		defs.push_back(content.FindBlock(id));
	int64_t changed = 0;
	for (int y = 0; y < m_size.y; ++y)
		for (int z = 0; z < m_size.z; ++z)
			for (int x = 0; x < m_size.x; ++x) {
				const BlockPos local{x, y, z};
				const size_t i = Index(local);
				if (m_cells[i] == VOID)
					continue;
				const BlockDef *def = defs[m_cells[i]];
				if (!def || (!options.keepAir && def->IsAir()))
					continue;
				const BlockPos p = at + RotateOffset(local - anchor, uint8_t(rotation));
				if (options.onlyAir && !world.BlockAt(p).IsAir() && world.IsLoaded(p))
					continue;
				const uint8_t param = RotateParam(m_params.empty() ? 0 : m_params[i], rotation);
				changed += world.SetBlock(p, def->numeric, param, ChangeCause::COMMAND);
			}
	if (containerFill)
		for (const auto &[local, contents] : containers)
			containerFill(at + RotateOffset(local - anchor, uint8_t(rotation)), contents);
	return changed;
}

// ── Transformations ──────────────────────────────────────────────────────────

Schematic Schematic::Rotated(int quarters) const {
	const uint8_t r = uint8_t(((quarters % 4) + 4) % 4);
	if (r == 0)
		return *this;
	const auto [lo, hi] = Bounds(anchor, r); // anchor posé en anchor : boîte tournée autour de lui
	Schematic out(hi - lo + BlockPos{1, 1, 1});
	out.name = name;
	out.author = author;
	out.description = description;
	out.m_palette = m_palette;
	if (!m_params.empty())
		out.m_params.assign(out.m_cells.size(), 0);
	auto map = [&](const BlockPos &p) { return anchor + RotateOffset(p - anchor, r) - lo; };
	for (int y = 0; y < m_size.y; ++y)
		for (int z = 0; z < m_size.z; ++z)
			for (int x = 0; x < m_size.x; ++x) {
				const size_t from = Index({x, y, z});
				const size_t to = out.Index(map({x, y, z}));
				out.m_cells[to] = m_cells[from];
				if (!m_params.empty())
					out.m_params[to] = RotateParam(m_params[from], r);
			}
	out.anchor = anchor - lo;
	for (const auto &[p, contents] : containers)
		out.containers[map(p)] = contents;
	return out;
}

Schematic Schematic::Mirrored(int axis) const {
	Schematic out = *this;
	auto flip = [&](BlockPos p) {
		if (axis == 0)
			p.x = m_size.x - 1 - p.x;
		else if (axis == 1)
			p.y = m_size.y - 1 - p.y;
		else
			p.z = m_size.z - 1 - p.z;
		return p;
	};
	for (int y = 0; y < m_size.y; ++y)
		for (int z = 0; z < m_size.z; ++z)
			for (int x = 0; x < m_size.x; ++x) {
				const size_t from = Index({x, y, z}), to = Index(flip({x, y, z}));
				out.m_cells[to] = m_cells[from];
				if (!m_params.empty())
					out.m_params[to] = MirrorParam(m_params[from], axis);
			}
	out.anchor = flip(anchor);
	out.containers.clear();
	for (const auto &[p, contents] : containers)
		out.containers[flip(p)] = contents;
	return out;
}

Schematic Schematic::Hollow() const {
	Schematic out = *this;
	auto full = [&](const BlockPos &p) {
		if (!Inside(p))
			return false;
		const uint16_t c = m_cells[Index(p)];
		return c != VOID && m_palette[c] != AIR_ID;
	};
	for (int y = 0; y < m_size.y; ++y)
		for (int z = 0; z < m_size.z; ++z)
			for (int x = 0; x < m_size.x; ++x) {
				const BlockPos p{x, y, z};
				if (full(p) && full(p + BlockPos{1, 0, 0}) && full(p + BlockPos{-1, 0, 0}) && full(p + BlockPos{0, 1, 0}) &&
					full(p + BlockPos{0, -1, 0}) && full(p + BlockPos{0, 0, 1}) && full(p + BlockPos{0, 0, -1}))
					out.Clear(p);
			}
	return out;
}

int64_t Schematic::Replace(const String &from, const String &to) {
	const uint16_t target = to.IsEmpty() ? VOID : PaletteIndex(to);
	int64_t n = 0;
	for (uint16_t &c : m_cells)
		if (c != VOID && c != target && (from.IsEmpty() ? m_palette[c] != AIR_ID : m_palette[c] == from)) {
			c = target;
			++n;
		}
	return n;
}

std::vector<String> Schematic::ResolveNames(const ContentRegistry &content, const NodePtr &aliases) {
	std::vector<String> unknown;
	std::vector<uint16_t> remap(m_palette.size());
	std::vector<String> mods;
	for (const BlockDef &b : content.Blocks())
		if (!b.mod.IsEmpty() && std::find(mods.begin(), mods.end(), b.mod) == mods.end())
			mods.push_back(b.mod);
	std::vector<String> palette;
	for (size_t i = 0; i < m_palette.size(); ++i) {
		const String &name = m_palette[i];
		String resolved;
		if (content.FindBlock(name))
			resolved = name;
		else if (name == "air")
			resolved = AIR_ID;
		else if (name == "ignore")
			resolved = String();
		else {
			if (aliases && aliases->IsObject())
				if (NodePtr a = aliases->Get(name); a && a->IsString() && content.FindBlock(a->stringValue))
					resolved = a->stringValue;
			if (resolved.IsEmpty()) {
				const size_t colon = name.Find(":");
				const String bare = colon == String::NPOS ? name : name.Substring(colon + 1);
				for (const String &mod : mods)
					if (resolved.IsEmpty() && content.FindBlock(mod + String(":") + bare))
						resolved = mod + String(":") + bare;
			}
			if (resolved.IsEmpty())
				unknown.push_back(name);
		}
		if (resolved.IsEmpty()) {
			remap[i] = VOID;
			continue;
		}
		const auto it = std::find(palette.begin(), palette.end(), resolved);
		remap[i] = uint16_t(it == palette.end() ? palette.size() : size_t(it - palette.begin()));
		if (it == palette.end())
			palette.push_back(resolved);
	}
	for (uint16_t &c : m_cells)
		if (c != VOID)
			c = remap[c];
	m_palette = std::move(palette);
	return unknown;
}

// ── Format natif (.vxs.json) ─────────────────────────────────────────────────

NodePtr Schematic::ToJson() const {
	NodePtr n = dataio::Node::MakeObject();
	n->Set("format", json::MakeStr(NATIVE_FORMAT));
	n->Set("version", json::MakeInt(NATIVE_VERSION));
	if (!name.IsEmpty())
		n->Set("name", json::MakeStr(name));
	if (!author.IsEmpty())
		n->Set("author", json::MakeStr(author));
	if (!description.IsEmpty())
		n->Set("description", json::MakeStr(description));
	n->Set("size", Triple(m_size));
	n->Set("anchor", Triple(anchor));
	NodePtr palette = dataio::Node::MakeArray();
	for (const String &id : m_palette)
		palette->Push(json::MakeStr(id));
	n->Set("palette", palette);
	// Plages [index, nombre] (index -1 : cellule vide), ordre y, z, x.
	auto runs = [](const auto &values, auto valueOf) {
		NodePtr list = dataio::Node::MakeArray();
		size_t i = 0;
		while (i < values.size()) {
			size_t j = i;
			while (j < values.size() && values[j] == values[i])
				++j;
			NodePtr run = dataio::Node::MakeArray();
			run->Push(json::MakeInt(valueOf(values[i])));
			run->Push(json::MakeInt(int64_t(j - i)));
			list->Push(run);
			i = j;
		}
		return list;
	};
	n->Set("cells", runs(m_cells, [](uint16_t c) { return c == VOID ? int64_t(-1) : int64_t(c); }));
	if (!m_params.empty() && std::any_of(m_params.begin(), m_params.end(), [](uint8_t p) { return p != 0; }))
		n->Set("params", runs(m_params, [](uint8_t p) { return int64_t(p); }));
	if (!containers.empty()) {
		NodePtr list = dataio::Node::MakeArray();
		for (const auto &[p, contents] : containers) {
			NodePtr c = dataio::Node::MakeObject();
			c->Set("pos", Triple(p));
			c->Set("items", contents ? contents->Clone() : dataio::Node::MakeArray());
			list->Push(c);
		}
		n->Set("containers", list);
	}
	return n;
}

namespace {

/// Définition de contenu « "type": "structure" » (blocs ou couches) → schéma.
Result<Schematic, String> FromContentJson(const NodePtr &def) {
	struct Cell {
		BlockPos pos;
		String block;
		uint8_t param;
	};
	std::vector<Cell> cells;
	if (NodePtr list = json::At(def, "blocks"); list && list->IsArray())
		for (size_t i = 0; i < list->GetSize(); ++i) {
			NodePtr b = list->At(i);
			if (b && b->IsArray() && b->GetSize() >= 4)
				cells.push_back({TripleOf(b), json::Str(b->At(3), ""), uint8_t(b->GetSize() > 4 ? json::Int(b->At(4), "", 0) : 0)});
			else if (b && b->IsObject()) {
				const math::FVector3 p = json::Vec3(b, "pos");
				cells.push_back({BlockPos{int(p.x), int(p.y), int(p.z)}, json::Str(b, "block"), uint8_t(json::Int(b, "param", 0))});
			}
		}
	if (NodePtr layers = json::At(def, "layers"); layers && layers->IsArray()) {
		NodePtr palette = json::At(def, "palette");
		for (size_t y = 0; y < layers->GetSize(); ++y) {
			NodePtr rows = layers->At(y);
			for (size_t z = 0; rows && z < rows->GetSize(); ++z) {
				const String row = json::Str(rows->At(z), "");
				for (size_t x = 0; x < row.GetSize(); ++x)
					if (palette && palette->Has(String(row.CharAt(x))))
						if (NodePtr e = palette->Get(String(row.CharAt(x))); e && e->IsString())
							cells.push_back({BlockPos{int(x), int(y), int(z)}, e->stringValue, 0});
			}
		}
	}
	if (cells.empty())
		return Err(String("structure vide (« blocks » ou « layers » attendu)"));
	BlockPos lo = cells[0].pos, hi = cells[0].pos;
	for (const Cell &c : cells) {
		lo = Min(lo, c.pos);
		hi = Max(hi, c.pos);
	}
	Schematic s(hi - lo + BlockPos{1, 1, 1});
	for (const Cell &c : cells)
		s.Set(c.pos - lo, c.block, c.param);
	s.anchor = TripleOf(json::At(def, "anchor")) - lo;
	s.name = json::Str(def, "displayName", json::Str(def, "id"));
	return Ok(std::move(s));
}

} // namespace

Result<Schematic, String> Schematic::FromJson(const NodePtr &n) {
	if (!n || !n->IsObject())
		return Err(String("objet JSON attendu"));
	if (json::Str(n, "type") == "structure" || (!json::Has(n, "format") && (json::Has(n, "blocks") || json::Has(n, "layers"))))
		return FromContentJson(n);
	if (json::Str(n, "format") != NATIVE_FORMAT)
		return Err(String::Format("format inconnu « %s »", json::Str(n, "format").CStr()));
	if (json::Int(n, "version", 0) > NATIVE_VERSION)
		return Err(String("version de structure plus récente que le jeu"));
	const BlockPos size = TripleOf(json::At(n, "size"));
	if (size.x <= 0 || size.y <= 0 || size.z <= 0 || int64_t(size.x) * size.y * size.z > 64ll * 1024 * 1024)
		return Err(String("taille de structure invalide"));
	Schematic s(size);
	s.name = json::Str(n, "name");
	s.author = json::Str(n, "author");
	s.description = json::Str(n, "description");
	s.anchor = TripleOf(json::At(n, "anchor"));
	s.m_palette = json::StrList(n, "palette");
	auto read = [&](const char *key, auto apply) -> Result<bool, String> {
		NodePtr list = json::At(n, key);
		if (!list)
			return Ok(true);
		size_t at = 0;
		for (size_t i = 0; list->IsArray() && i < list->GetSize(); ++i) {
			const int64_t value = json::Int(list->At(i)->At(0), "", -1);
			const int64_t count = json::Int(list->At(i)->At(1), "", 0);
			if (count < 0 || at + size_t(count) > s.m_cells.size())
				return Err(String::Format("« %s » : plages incohérentes", key));
			for (int64_t k = 0; k < count; ++k)
				apply(at++, value);
		}
		return Ok(true);
	};
	const int64_t paletteSize = int64_t(s.m_palette.size());
	if (Result<bool, String> r = read("cells", [&](size_t i, int64_t v) { s.m_cells[i] = v < 0 || v >= paletteSize ? VOID : uint16_t(v); });
		r.IsError())
		return Err(r.Error());
	if (json::Has(n, "params")) {
		s.m_params.assign(s.m_cells.size(), 0);
		if (Result<bool, String> r = read("params", [&](size_t i, int64_t v) { s.m_params[i] = uint8_t(v); }); r.IsError())
			return Err(r.Error());
	}
	if (NodePtr list = json::At(n, "containers"); list && list->IsArray())
		for (size_t i = 0; i < list->GetSize(); ++i)
			s.containers[TripleOf(json::At(list->At(i), "pos"))] = json::At(list->At(i), "items");
	return Ok(std::move(s));
}

NodePtr Schematic::ToContentJson(const String &id) const {
	NodePtr n = dataio::Node::MakeObject();
	n->Set("type", json::MakeStr("structure"));
	n->Set("id", json::MakeStr(id));
	if (!name.IsEmpty())
		n->Set("displayName", json::MakeStr(name));
	n->Set("anchor", Triple(anchor));
	NodePtr blocks = dataio::Node::MakeArray();
	for (int y = 0; y < m_size.y; ++y)
		for (int z = 0; z < m_size.z; ++z)
			for (int x = 0; x < m_size.x; ++x) {
				const size_t i = Index({x, y, z});
				if (m_cells[i] == VOID)
					continue;
				NodePtr b = Triple({x, y, z});
				b->Push(json::MakeStr(m_palette[m_cells[i]]));
				if (!m_params.empty() && m_params[i] != 0)
					b->Push(json::MakeInt(m_params[i]));
				blocks->Push(b);
			}
	n->Set("blocks", blocks);
	return n;
}

// ── WorldEdit (.we) ──────────────────────────────────────────────────────────

String Schematic::ToWorldEdit() const {
	// Coordonnées Minetest : z inversé (+z au nord).
	std::ostringstream out;
	out << "5:return {";
	bool first = true;
	for (int y = 0; y < m_size.y; ++y)
		for (int z = 0; z < m_size.z; ++z)
			for (int x = 0; x < m_size.x; ++x) {
				const size_t i = Index({x, y, z});
				if (m_cells[i] == VOID || m_palette[m_cells[i]] == AIR_ID)
					continue; // WorldEdit n'enregistre pas l'air
				out << (first ? "" : ", ") << "{[\"x\"] = " << x << ", [\"y\"] = " << y << ", [\"z\"] = " << (m_size.z - 1 - z)
					<< ", [\"name\"] = \"" << m_palette[m_cells[i]].CStr() << "\"";
				if (!m_params.empty() && m_params[i] != 0)
					out << ", [\"param2\"] = " << int(ParamToMinetest(m_params[i]));
				out << "}";
				first = false;
			}
	out << "}";
	return String(out.str().c_str());
}

Result<Schematic, String> Schematic::FromWorldEdit(const String &textIn) {
	const std::string text(textIn.CStr(), textIn.GetSize());
	struct Node {
		BlockPos pos{0, 0, 0};
		std::string name;
		int param2 = 0;
	};
	std::vector<Node> nodes;
	if (text.find('{') == std::string::npos) {
		// Versions 1 à 3 : une ligne « x y z nom param1 param2 » par bloc.
		std::istringstream in(text);
		std::string line;
		while (std::getline(in, line)) {
			std::istringstream l(line);
			Node n;
			int p1 = 0;
			if (l >> n.pos.x >> n.pos.y >> n.pos.z >> n.name) {
				l >> p1 >> n.param2;
				nodes.push_back(n);
			}
		}
	} else {
		// Version 4 ou 5 : table Lua « {{x = 0, …}, …} » (clés nues ou ["x"]).
		size_t i = text.find('{');
		if (i == std::string::npos)
			return Err(String("fichier WorldEdit illisible"));
		++i;
		auto skipSpace = [&] {
			while (i < text.size() && (std::isspace(uint8_t(text[i])) || text[i] == ','))
				++i;
		};
		auto readString = [&]() -> std::string {
			const char quote = text[i++];
			std::string s;
			while (i < text.size() && text[i] != quote) {
				if (text[i] == '\\' && i + 1 < text.size())
					++i;
				s.push_back(text[i++]);
			}
			++i;
			return s;
		};
		auto skipValue = [&] {
			if (text[i] == '"' || text[i] == '\'') {
				(void)readString();
				return;
			}
			if (text[i] == '{') {
				int depth = 0;
				while (i < text.size()) {
					if (text[i] == '"' || text[i] == '\'') {
						(void)readString();
						continue;
					}
					depth += text[i] == '{' ? 1 : text[i] == '}' ? -1 : 0;
					++i;
					if (depth == 0)
						return;
				}
				return;
			}
			while (i < text.size() && text[i] != ',' && text[i] != '}')
				++i;
		};
		for (;;) {
			skipSpace();
			if (i >= text.size() || text[i] == '}')
				break;
			if (text[i] != '{')
				return Err(String::Format("WorldEdit : « { » attendu (octet %d)", int(i)));
			++i;
			Node n;
			for (;;) {
				skipSpace();
				if (i >= text.size())
					return Err(String("WorldEdit : fin de fichier inattendue"));
				if (text[i] == '}') {
					++i;
					break;
				}
				std::string key;
				if (text[i] == '[') {
					++i;
					key = readString();
					i = text.find(']', i) + 1;
				} else {
					while (i < text.size() && (std::isalnum(uint8_t(text[i])) || text[i] == '_'))
						key.push_back(text[i++]);
				}
				while (i < text.size() && (std::isspace(uint8_t(text[i])) || text[i] == '='))
					++i;
				if (key == "name" && (text[i] == '"' || text[i] == '\'')) {
					n.name = readString();
				} else if (key == "x" || key == "y" || key == "z" || key == "param2") {
					char *end = nullptr;
					const double v = std::strtod(text.c_str() + i, &end);
					i = size_t(end - text.c_str());
					const int iv = int(std::lround(v));
					(key == "x" ? n.pos.x : key == "y" ? n.pos.y : key == "z" ? n.pos.z : n.param2) = iv;
				} else {
					skipValue();
				}
			}
			if (!n.name.empty())
				nodes.push_back(n);
		}
	}
	if (nodes.empty())
		return Err(String("fichier WorldEdit vide"));
	BlockPos lo = nodes[0].pos, hi = nodes[0].pos;
	for (const Node &n : nodes) {
		lo = Min(lo, n.pos);
		hi = Max(hi, n.pos);
	}
	const BlockPos size = hi - lo + BlockPos{1, 1, 1};
	if (int64_t(size.x) * size.y * size.z > 64ll * 1024 * 1024)
		return Err(String("structure WorldEdit trop grande"));
	Schematic s(size);
	for (const Node &n : nodes) {
		const BlockPos p = n.pos - lo;
		s.Set({p.x, p.y, size.z - 1 - p.z}, String(n.name.c_str()), ParamFromMinetest(uint8_t(n.param2)));
	}
	return Ok(std::move(s));
}

// ── Schéma Minetest (.mts) ───────────────────────────────────────────────────

std::vector<uint8_t> Schematic::ToMts() const {
	std::vector<uint8_t> out;
	auto u16 = [&](unsigned v) {
		out.push_back(uint8_t(v >> 8));
		out.push_back(uint8_t(v));
	};
	out.insert(out.end(), {'M', 'T', 'S', 'M'});
	u16(4);
	u16(unsigned(m_size.x));
	u16(unsigned(m_size.y));
	u16(unsigned(m_size.z));
	for (int y = 0; y < m_size.y; ++y)
		out.push_back(0x7F); // probabilité des tranches : toujours
	// Noms : la palette, puis « air » pour les cellules vides (probabilité 0).
	std::vector<String> names = m_palette;
	uint16_t voidIndex = uint16_t(names.size());
	if (const auto it = std::find(names.begin(), names.end(), String(AIR_ID)); it != names.end())
		voidIndex = uint16_t(it - names.begin());
	else
		names.push_back(String(AIR_ID));
	u16(unsigned(names.size()));
	for (const String &n : names) {
		const String name = n == AIR_ID ? String("air") : n;
		u16(unsigned(name.GetSize()));
		out.insert(out.end(), name.CStr(), name.CStr() + name.GetSize());
	}
	// Données : index (z * sy + y) * sx + x, z inversé.
	const size_t volume = size_t(Volume());
	std::vector<uint8_t> data(volume * 4);
	size_t k = 0;
	for (int mz = 0; mz < m_size.z; ++mz)
		for (int y = 0; y < m_size.y; ++y)
			for (int x = 0; x < m_size.x; ++x, ++k) {
				const size_t i = Index({x, y, m_size.z - 1 - mz});
				const uint16_t c = m_cells[i] == VOID ? voidIndex : m_cells[i];
				data[k * 2] = uint8_t(c >> 8);
				data[k * 2 + 1] = uint8_t(c);
				data[volume * 2 + k] = m_cells[i] == VOID ? 0x00 : 0x7F;
				data[volume * 3 + k] = m_params.empty() ? ParamToMinetest(0) : ParamToMinetest(m_params[i]);
			}
	// zlib : en-tête, DEFLATE, Adler-32.
	out.push_back(0x78);
	out.push_back(0x9C);
	const archive::Bytes deflated = archive::Deflate(std::span<const uint8_t>(data.data(), data.size()), 6);
	out.insert(out.end(), deflated.begin(), deflated.end());
	const uint32_t adler = Adler32(data.data(), data.size());
	for (int s = 24; s >= 0; s -= 8)
		out.push_back(uint8_t(adler >> s));
	return out;
}

Result<Schematic, String> Schematic::FromMts(const std::vector<uint8_t> &bytes) {
	size_t at = 0;
	auto need = [&](size_t n) { return at + n <= bytes.size(); };
	auto u16 = [&]() -> unsigned {
		const unsigned v = (unsigned(bytes[at]) << 8) | bytes[at + 1];
		at += 2;
		return v;
	};
	if (!need(12) || std::memcmp(bytes.data(), "MTSM", 4) != 0)
		return Err(String("pas un schéma Minetest (MTSM)"));
	at = 4;
	const unsigned version = u16();
	if (version < 1 || version > 4)
		return Err(String::Format("version de schéma Minetest %u non prise en charge", version));
	const BlockPos size{int(u16()), int(u16()), int(u16())};
	const size_t volume = size_t(size.x) * size_t(size.y) * size_t(size.z);
	if (volume == 0 || volume > 64ull * 1024 * 1024)
		return Err(String("taille de schéma invalide"));
	if (version >= 3) {
		if (!need(size_t(size.y)))
			return Err(String("schéma tronqué"));
		at += size_t(size.y); // probabilités des tranches
	}
	if (!need(2))
		return Err(String("schéma tronqué"));
	const unsigned count = u16();
	std::vector<String> names;
	for (unsigned i = 0; i < count; ++i) {
		if (!need(2))
			return Err(String("schéma tronqué (noms)"));
		const unsigned len = u16();
		if (!need(len))
			return Err(String("schéma tronqué (noms)"));
		names.push_back(String(std::string(reinterpret_cast<const char *>(bytes.data() + at), len).c_str()));
		at += len;
	}
	if (!need(2))
		return Err(String("schéma sans données"));
	at += 2; // en-tête zlib
	Result<archive::InflateResult, String> inflated =
		archive::Inflate(std::span<const uint8_t>(bytes.data() + at, bytes.size() - at), volume * 4);
	if (inflated.IsError())
		return Err(String("données du schéma illisibles : ") + inflated.Error());
	const std::vector<uint8_t> &data = inflated.Value().bytes;
	if (data.size() < volume * 4)
		return Err(String("données du schéma incomplètes"));
	Schematic s(size);
	size_t k = 0;
	for (int mz = 0; mz < size.z; ++mz)
		for (int y = 0; y < size.y; ++y)
			for (int x = 0; x < size.x; ++x, ++k) {
				const unsigned c = (unsigned(data[k * 2]) << 8) | data[k * 2 + 1];
				const uint8_t probability = data[volume * 2 + k];
				if (c >= names.size() || probability == 0 || names[c] == "ignore")
					continue;
				s.Set({x, y, size.z - 1 - mz}, names[c], ParamFromMinetest(data[volume * 3 + k]));
			}
	return Ok(std::move(s));
}

// ── Fichiers ─────────────────────────────────────────────────────────────────

String Schematic::FormatOf(const String &file) {
	const String lower = file.ToLower();
	if (lower.EndsWith(".vxs.json") || lower.EndsWith(".vxs"))
		return String("vxs");
	if (lower.EndsWith(".json"))
		return String("json");
	if (lower.EndsWith(".we"))
		return String("we");
	if (lower.EndsWith(".mts"))
		return String("mts");
	return String();
}

Result<Schematic, String> Schematic::Load(const String &path) {
	const String format = FormatOf(path);
	if (format.IsEmpty())
		return Err(String::Format("format inconnu : %s (.vxs.json, .json, .we, .mts)", path.CStr()));
	Result<std::vector<uint8_t>, String> bytes = ReadFile(path);
	if (bytes.IsError())
		return Err(bytes.Error());
	Result<Schematic, String> s = Err(String());
	if (format == "mts") {
		s = FromMts(bytes.Value());
	} else {
		const String text(std::string(bytes.Value().begin(), bytes.Value().end()).c_str());
		if (format == "we") {
			s = FromWorldEdit(text);
		} else {
			Result<NodePtr, String> parsed = json::Parse(text);
			if (parsed.IsError())
				return Err(String::Format("%s : %s", path.CStr(), parsed.Error().CStr()));
			s = FromJson(parsed.Value());
		}
	}
	if (s.IsOk() && s.Value().name.IsEmpty()) {
		std::string stem = std::filesystem::path(path.CStr()).filename().string();
		stem = stem.substr(0, stem.find('.'));
		s.Value().name = String(stem.c_str());
	}
	return s;
}

Result<bool, String> Schematic::Save(const String &path) const {
	const String format = FormatOf(path);
	if (format.IsEmpty())
		return Err(String::Format("format inconnu : %s (.vxs.json, .json, .we, .mts)", path.CStr()));
	std::error_code ec;
	std::filesystem::create_directories(std::filesystem::path(path.CStr()).parent_path(), ec);
	const String tmp = path + String(".tmp");
	{
		std::ofstream out(tmp.CStr(), std::ios::binary | std::ios::trunc);
		if (!out)
			return Err(String::Format("écriture impossible : %s", path.CStr()));
		if (format == "mts") {
			const std::vector<uint8_t> bytes = ToMts();
			out.write(reinterpret_cast<const char *>(bytes.data()), std::streamsize(bytes.size()));
		} else if (format == "we") {
			const String text = ToWorldEdit();
			out.write(text.CStr(), std::streamsize(text.GetSize()));
		} else {
			std::string id = std::filesystem::path(path.CStr()).filename().string();
			id = id.substr(0, id.find('.'));
			const String text = json::Encode(format == "json" ? ToContentJson(String(id.c_str())) : ToJson());
			out.write(text.CStr(), std::streamsize(text.GetSize()));
		}
		if (!out)
			return Err(String::Format("écriture impossible : %s", path.CStr()));
	}
	std::filesystem::rename(tmp.CStr(), path.CStr(), ec);
	if (ec)
		return Err(String::Format("écriture impossible : %s (%s)", path.CStr(), ec.message().c_str()));
	return Ok(true);
}

// ── Formules ─────────────────────────────────────────────────────────────────

namespace {

struct Cells {
	std::unordered_set<int64_t> set;
	BlockPos lo{0, 0, 0}, hi{0, 0, 0};
	static int64_t Key(const BlockPos &p) {
		return (int64_t(p.x + (1 << 20)) << 42) | (int64_t(p.y + (1 << 20)) << 21) | int64_t(p.z + (1 << 20));
	}
	static BlockPos Pos(int64_t k) {
		return {int((k >> 42) & 0x1FFFFF) - (1 << 20), int((k >> 21) & 0x1FFFFF) - (1 << 20), int(k & 0x1FFFFF) - (1 << 20)};
	}
	void Add(const BlockPos &p) {
		if (set.empty()) {
			lo = hi = p;
		} else {
			lo = Min(lo, p);
			hi = Max(hi, p);
		}
		set.insert(Key(p));
	}
};

class Clock {
public:
	explicit Clock(double limit) : m_limit(limit), m_start(std::chrono::steady_clock::now()) {}
	bool Expired() {
		if (m_limit <= 0.0 || ++m_count % 4096 != 0)
			return false;
		return std::chrono::duration<double>(std::chrono::steady_clock::now() - m_start).count() > m_limit;
	}

private:
	double m_limit;
	std::chrono::steady_clock::time_point m_start;
	uint64_t m_count = 0;
};

String SizeText(const BlockPos &s) { return String::Format("%d×%d×%d", s.x, s.y, s.z); }

bool Fits(const BlockPos &size, const BlockPos &max) { return size.x <= max.x && size.y <= max.y && size.z <= max.z; }

} // namespace

Result<Schematic, String> Plot(const PlotSpec &spec) {
	if (spec.block.IsEmpty())
		return Err(String("bloc à poser non précisé"));
	if (!(spec.scale > 0.0) || !std::isfinite(spec.scale))
		return Err(String("échelle invalide"));
	const double k = spec.scale;
	// Formule (x, y, z) ↔ jeu (gx, gy, gz) : en convention mathématique, z
	// monte et y va vers le nord (−z du jeu).
	auto toGame = [&](double x, double y, double z) {
		return spec.zUp ? std::array<double, 3>{x * k, z * k, -y * k} : std::array<double, 3>{x * k, y * k, z * k};
	};
	auto toFormula = [&](double gx, double gy, double gz) {
		return spec.zUp ? std::array<double, 3>{gx / k, -gz / k, gy / k} : std::array<double, 3>{gx / k, gy / k, gz / k};
	};
	Clock clock(spec.timeLimit);
	Cells cells;
	if (spec.kind == PlotSpec::Kind::IMPLICIT) {
		Result<Formula, String> f = Formula::Compile(spec.expression, {"x", "y", "z"});
		if (f.IsError())
			return Err(f.Error());
		// Boîte du jeu couverte par la boîte de la formule.
		BlockPos lo{0, 0, 0}, hi{0, 0, 0};
		bool first = true;
		for (int cx : {spec.min.x, spec.max.x})
			for (int cy : {spec.min.y, spec.max.y})
				for (int cz : {spec.min.z, spec.max.z}) {
					const auto g = toGame(cx, cy, cz);
					const BlockPos p{int(std::lround(g[0])), int(std::lround(g[1])), int(std::lround(g[2]))};
					lo = first ? p : Min(lo, p);
					hi = first ? p : Max(hi, p);
					first = false;
				}
		const BlockPos box = hi - lo + BlockPos{1, 1, 1};
		if (!Fits(box, spec.maxSize))
			return Err(String::Format("zone de calcul trop grande : %s (au plus %s)", SizeText(box).CStr(), SizeText(spec.maxSize).CStr()));
		const Formula &formula = f.Value();
		auto eval = [&](double gx, double gy, double gz) {
			const auto v = toFormula(gx, gy, gz);
			return formula.Eval(v.data());
		};
		if (formula.IsCondition()) {
			// Condition : région pleine, testée au centre des cellules.
			for (int y = lo.y; y <= hi.y; ++y)
				for (int z = lo.z; z <= hi.z; ++z)
					for (int x = lo.x; x <= hi.x; ++x) {
						if (clock.Expired())
							return Err(String("calcul trop long (temps maximal dépassé)"));
						const double v = eval(x, y, z);
						if (v != 0.0 && !std::isnan(v))
							cells.Add({x, y, z});
					}
		} else {
			// Expression : surface où elle s'annule — changement de signe entre
			// les coins d'une cellule (ou zéro au centre). Coins calculés par
			// tranches (deux plans en mémoire).
			const int nx = box.x + 1, nz = box.z + 1;
			std::vector<double> below(size_t(nx * nz)), above(size_t(nx * nz));
			auto plane = [&](std::vector<double> &out, double gy) {
				for (int z = 0; z < nz; ++z)
					for (int x = 0; x < nx; ++x)
						out[size_t(z * nx + x)] = eval(lo.x + x - 0.5, gy, lo.z + z - 0.5);
			};
			plane(below, lo.y - 0.5);
			for (int y = lo.y; y <= hi.y; ++y) {
				plane(above, y + 0.5);
				for (int z = lo.z; z <= hi.z; ++z)
					for (int x = lo.x; x <= hi.x; ++x) {
						if (clock.Expired())
							return Err(String("calcul trop long (temps maximal dépassé)"));
						const int ix = x - lo.x, iz = z - lo.z;
						bool negative = false, positive = false, zero = false;
						for (const std::vector<double> *pl : {&below, &above})
							for (int dz = 0; dz <= 1; ++dz)
								for (int dx = 0; dx <= 1; ++dx) {
									const double v = (*pl)[size_t((iz + dz) * nx + ix + dx)];
									negative |= v < 0.0;
									positive |= v > 0.0;
									zero |= v == 0.0;
								}
						if (zero || (negative && positive) || eval(x, y, z) == 0.0)
							cells.Add({x, y, z});
					}
				std::swap(below, above);
			}
		}
	} else {
		const int params = spec.kind == PlotSpec::Kind::CURVE ? 1 : spec.kind == PlotSpec::Kind::SURFACE ? 2 : 3;
		std::vector<String> names{"u", "v", "w", "t"};
		std::array<Formula, 3> f;
		const String *exprs[3] = {&spec.fx, &spec.fy, &spec.fz};
		for (int i = 0; i < 3; ++i) {
			Result<Formula, String> c = Formula::Compile(*exprs[i], names);
			if (c.IsError())
				return Err(String::Format("%c(…) : %s", "xyz"[i], c.Error().CStr()));
			f[size_t(i)] = std::move(c.Value());
		}
		int n[3] = {1, 1, 1};
		for (int i = 0; i < params; ++i)
			n[i] = std::clamp(spec.samples[i], 2, 4096);
		if (int64_t(n[0]) * n[1] * n[2] > 4'000'000)
			return Err(String("trop d'échantillons (4 millions au plus)"));
		// Points de la grille des paramètres, en coordonnées du jeu.
		std::vector<std::array<double, 3>> points(size_t(n[0] * n[1] * n[2]));
		std::vector<uint8_t> valid(points.size(), 0);
		auto at = [&](int a, int b, int c) { return size_t((c * n[1] + b) * n[0] + a); };
		for (int c = 0; c < n[2]; ++c)
			for (int b = 0; b < n[1]; ++b)
				for (int a = 0; a < n[0]; ++a) {
					if (clock.Expired())
						return Err(String("calcul trop long (temps maximal dépassé)"));
					double values[4] = {0.0, 0.0, 0.0, 0.0};
					const int idx[3] = {a, b, c};
					for (int i = 0; i < params; ++i)
						values[i] = spec.range[i][0] + (spec.range[i][1] - spec.range[i][0]) * double(idx[i]) / double(n[i] - 1);
					values[3] = values[0];
					const double x = f[0].Eval(values), y = f[1].Eval(values), z = f[2].Eval(values);
					if (!std::isfinite(x) || !std::isfinite(y) || !std::isfinite(z))
						continue;
					points[at(a, b, c)] = toGame(x, y, z);
					valid[at(a, b, c)] = 1;
				}
		auto cellOf = [](const std::array<double, 3> &g) {
			return BlockPos{int(std::floor(g[0] + 0.5)), int(std::floor(g[1] + 0.5)), int(std::floor(g[2] + 0.5))};
		};
		const BlockPos limit = spec.maxSize;
		bool tooBig = false;
		auto add = [&](const std::array<double, 3> &g) {
			cells.Add(cellOf(g));
			tooBig = tooBig || !Fits(cells.hi - cells.lo + BlockPos{1, 1, 1}, limit);
		};
		// Segment : des pas d'au plus 0,3 bloc, sans trou.
		auto segment = [&](const std::array<double, 3> &p, const std::array<double, 3> &q) {
			const double d = std::sqrt((q[0] - p[0]) * (q[0] - p[0]) + (q[1] - p[1]) * (q[1] - p[1]) + (q[2] - p[2]) * (q[2] - p[2]));
			const int steps = std::min(100000, int(std::ceil(d / 0.3)));
			for (int s = 0; s <= steps && !tooBig; ++s) {
				const double t = steps == 0 ? 0.0 : double(s) / double(steps);
				add({p[0] + (q[0] - p[0]) * t, p[1] + (q[1] - p[1]) * t, p[2] + (q[2] - p[2]) * t});
			}
		};
		// Triangle (surfaces) : balayage de lignes parallèles à un côté.
		auto triangle = [&](const std::array<double, 3> &p, const std::array<double, 3> &q, const std::array<double, 3> &r) {
			auto dist = [](const std::array<double, 3> &a, const std::array<double, 3> &b) {
				return std::sqrt((a[0] - b[0]) * (a[0] - b[0]) + (a[1] - b[1]) * (a[1] - b[1]) + (a[2] - b[2]) * (a[2] - b[2]));
			};
			const int steps = std::min(2000, int(std::ceil(std::max(dist(p, q), dist(p, r)) / 0.3)));
			for (int s = 0; s <= steps && !tooBig; ++s) {
				const double t = steps == 0 ? 0.0 : double(s) / double(steps);
				segment({p[0] + (q[0] - p[0]) * t, p[1] + (q[1] - p[1]) * t, p[2] + (q[2] - p[2]) * t},
						{p[0] + (r[0] - p[0]) * t, p[1] + (r[1] - p[1]) * t, p[2] + (r[2] - p[2]) * t});
			}
		};
		for (int c = 0; c < n[2] && !tooBig; ++c)
			for (int b = 0; b < n[1] && !tooBig; ++b)
				for (int a = 0; a < n[0] && !tooBig; ++a) {
					if (clock.Expired())
						return Err(String("calcul trop long (temps maximal dépassé)"));
					const size_t i = at(a, b, c);
					if (!valid[i])
						continue;
					add(points[i]);
					if (params == 2) {
						if (a + 1 < n[0] && b + 1 < n[1] && valid[at(a + 1, b, c)] && valid[at(a, b + 1, c)] && valid[at(a + 1, b + 1, c)]) {
							triangle(points[i], points[at(a + 1, b, c)], points[at(a, b + 1, c)]);
							triangle(points[at(a + 1, b + 1, c)], points[at(a + 1, b, c)], points[at(a, b + 1, c)]);
						}
						continue;
					}
					// Courbe ou solide : segments le long de chaque paramètre.
					if (a + 1 < n[0] && valid[at(a + 1, b, c)])
						segment(points[i], points[at(a + 1, b, c)]);
					if (b + 1 < n[1] && valid[at(a, b + 1, c)])
						segment(points[i], points[at(a, b + 1, c)]);
					if (c + 1 < n[2] && valid[at(a, b, c + 1)])
						segment(points[i], points[at(a, b, c + 1)]);
				}
		if (tooBig)
			return Err(String::Format("forme trop grande : plus de %s", SizeText(spec.maxSize).CStr()));
	}
	if (cells.set.empty())
		return Err(String("la formule ne donne aucun bloc dans la zone"));
	const BlockPos size = cells.hi - cells.lo + BlockPos{1, 1, 1};
	if (!Fits(size, spec.maxSize))
		return Err(String::Format("forme trop grande : %s (au plus %s)", SizeText(size).CStr(), SizeText(spec.maxSize).CStr()));
	Schematic s(size);
	for (int64_t key : cells.set)
		s.Set(Cells::Pos(key) - cells.lo, spec.block);
	s.anchor = BlockPos{0, 0, 0} - cells.lo; // l'origine de la formule
	return Ok(spec.hollow ? s.Hollow() : std::move(s));
}

} // namespace voxel
