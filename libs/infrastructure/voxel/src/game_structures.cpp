// Définitions de voxel/game.hpp : structures (copier, coller, remplir,
// fichiers du monde et d'échange), surlignages et formulaires des mods.
#include <voxel/game.hpp>
#include <voxel/json.hpp>
#include <voxel/scripting.hpp>

#include <algorithm>
#include <filesystem>

namespace voxel {

namespace fs = std::filesystem;

namespace {

BlockPos Lo(const BlockPos &a, const BlockPos &b) { return {std::min(a.x, b.x), std::min(a.y, b.y), std::min(a.z, b.z)}; }
BlockPos Hi(const BlockPos &a, const BlockPos &b) { return {std::max(a.x, b.x), std::max(a.y, b.y), std::max(a.z, b.z)}; }

constexpr int64_t MAX_REGION = 16ll * 1024 * 1024; ///< garde-fou du moteur (les mods fixent leurs propres limites)

int64_t VolumeOf(const BlockPos &lo, const BlockPos &hi) { return int64_t(hi.x - lo.x + 1) * (hi.y - lo.y + 1) * (hi.z - lo.z + 1); }

/// Nom de fichier simple (pas de chemin) ; extension « .vxs.json » par défaut.
Result<String, String> FileName(const String &raw) {
	const String name = raw.Trim();
	if (name.IsEmpty())
		return Err(String("nom de structure vide"));
	if (name.GetSize() > 120)
		return Err(String("nom de structure trop long"));
	for (size_t i = 0; i < name.GetSize(); ++i) {
		const char c = name.CStr()[i];
		if (c == '/' || c == '\\' || c == ':' || c == '*' || c == '?' || c == '"' || c == '<' || c == '>' || c == '|' || uint8_t(c) < 32)
			return Err(String::Format("nom invalide « %s » (pas de chemin ni de caractère spécial)", name.CStr()));
	}
	if (name.StartsWith(".") || name.Contains(".."))
		return Err(String::Format("nom invalide « %s »", name.CStr()));
	return Ok(Schematic::FormatOf(name).IsEmpty() ? name + String(".vxs.json") : name);
}

} // namespace

String Game::StructuresDirectory(bool exchange) const {
	if (exchange)
		return m_options.structureDirectory;
	return m_save ? m_save->Directory() + String("/structures") : String();
}

Schematic Game::CaptureRegion(const BlockPos &a, const BlockPos &b, Option<BlockPos> anchor) {
	Schematic s = Schematic::Capture(*m_world, a, b, anchor);
	const BlockPos lo = Lo(a, b), hi = Hi(a, b);
	for (int y = lo.y; y <= hi.y; ++y)
		for (int z = lo.z; z <= hi.z; ++z)
			for (int x = lo.x; x <= hi.x; ++x)
				if (m_world->BlockAt({x, y, z}).IsContainer())
					if (Inventory *inventory = BlockInventory({x, y, z}))
						s.containers[BlockPos{x, y, z} - lo] = inventory->ToJson();
	return s;
}

int64_t Game::PasteSchematic(const Schematic &schematic, const BlockPos &at, const Schematic::PasteOptions &options) {
	if (!m_world)
		return 0;
	// Les rangements écrasés sont vidés (leur contenu ne passe pas au nouveau bloc).
	const BlockPos &size = schematic.Size();
	const int rotation = ((options.rotation % 4) + 4) % 4;
	for (int y = 0; y < size.y; ++y)
		for (int z = 0; z < size.z; ++z)
			for (int x = 0; x < size.x; ++x) {
				const String id = schematic.BlockAt({x, y, z});
				if (id.IsEmpty() || (!options.keepAir && id == AIR_ID))
					continue;
				const BlockPos p = at + RotateOffset(BlockPos{x, y, z} - schematic.anchor, uint8_t(rotation));
				if (m_world->IsLoaded(p) && m_world->BlockAt(p).IsContainer() && !options.onlyAir)
					(void)TakeContainer(p);
			}
	return schematic.Paste(*m_world, at, options, [this](const BlockPos &p, const NodePtr &contents) {
		if (Inventory *inventory = BlockInventory(p)) {
			const Inventory saved = Inventory::FromJson(contents, inventory->Size());
			for (int i = 0; i < inventory->Size(); ++i)
				inventory->Slot(i) = i < saved.Size() ? saved.Slot(i) : ItemStack{};
		}
	});
}

Result<int64_t, String> Game::FillRegion(const BlockPos &a, const BlockPos &b, const String &block, const String &mode) {
	if (!m_world)
		return Err(String("aucun monde ouvert"));
	const BlockDef *def = m_content.FindBlock(block);
	if (!def)
		return Err(String::Format("bloc inconnu « %s »", block.CStr()));
	const BlockPos lo = Lo(a, b), hi = Hi(a, b);
	if (VolumeOf(lo, hi) > MAX_REGION)
		return Err(String("zone trop grande"));
	if (mode != "all" && mode != "hollow" && mode != "walls" && mode != "outline" && mode != "keep")
		return Err(String::Format("mode inconnu « %s » (all, hollow, walls, outline, keep)", mode.CStr()));
	int64_t changed = 0;
	for (int y = lo.y; y <= hi.y; ++y)
		for (int z = lo.z; z <= hi.z; ++z)
			for (int x = lo.x; x <= hi.x; ++x) {
				const BlockPos p{x, y, z};
				const int faces = (x == lo.x || x == hi.x) + (y == lo.y || y == hi.y) + (z == lo.z || z == hi.z);
				const bool side = x == lo.x || x == hi.x || z == lo.z || z == hi.z;
				BlockId id = def->numeric;
				if (mode == "hollow" && faces == 0)
					id = AIR;
				else if ((mode == "walls" && !side) || (mode == "outline" && faces < 2))
					continue;
				if (mode == "keep" && !m_world->BlockAt(p).IsAir())
					continue;
				if (m_world->IsLoaded(p) && m_world->BlockAt(p).IsContainer())
					(void)TakeContainer(p);
				changed += m_world->SetBlock(p, id, 0, ChangeCause::COMMAND);
			}
	return Ok(changed);
}

Result<int64_t, String> Game::ReplaceRegion(const BlockPos &a, const BlockPos &b, const String &from, const String &to) {
	if (!m_world)
		return Err(String("aucun monde ouvert"));
	const BlockDef *target = m_content.FindBlock(to);
	if (!target)
		return Err(String::Format("bloc inconnu « %s »", to.CStr()));
	const BlockDef *source = from.IsEmpty() ? nullptr : m_content.FindBlock(from);
	if (!from.IsEmpty() && !source)
		return Err(String::Format("bloc inconnu « %s »", from.CStr()));
	const BlockPos lo = Lo(a, b), hi = Hi(a, b);
	if (VolumeOf(lo, hi) > MAX_REGION)
		return Err(String("zone trop grande"));
	int64_t changed = 0;
	for (int y = lo.y; y <= hi.y; ++y)
		for (int z = lo.z; z <= hi.z; ++z)
			for (int x = lo.x; x <= hi.x; ++x) {
				const BlockPos p{x, y, z};
				if (!m_world->IsLoaded(p))
					(void)m_world->EnsureChunk(ChunkOf(p));
				const BlockDef &current = m_world->BlockAt(p);
				if (source ? current.numeric != source->numeric : current.IsAir())
					continue;
				if (current.IsContainer())
					(void)TakeContainer(p);
				changed += m_world->SetBlock(p, target->numeric, m_world->GetParam(p), ChangeCause::COMMAND);
			}
	return Ok(changed);
}

Schematic *Game::Clipboard(const String &name) {
	auto it = m_clipboards.find(name.CStr());
	return it == m_clipboards.end() ? nullptr : &it->second;
}

void Game::SetClipboard(const String &name, Schematic schematic) { m_clipboards[name.CStr()] = std::move(schematic); }

bool Game::ForgetClipboard(const String &name) { return m_clipboards.erase(name.CStr()) > 0; }

std::vector<String> Game::ClipboardNames() const {
	std::vector<String> out;
	for (const auto &[name, s] : m_clipboards)
		out.push_back(String(name.c_str()));
	return out;
}

std::vector<String> Game::StructureFiles(bool exchange) const {
	std::vector<String> out;
	const String dir = StructuresDirectory(exchange);
	std::error_code ec;
	if (dir.IsEmpty() || !fs::is_directory(dir.CStr(), ec))
		return out;
	for (const fs::directory_entry &e : fs::directory_iterator(dir.CStr(), ec))
		if (e.is_regular_file(ec)) {
			const String name(e.path().filename().string().c_str());
			if (!Schematic::FormatOf(name).IsEmpty())
				out.push_back(name);
		}
	std::sort(out.begin(), out.end(), [](const String &a, const String &b) { return std::strcmp(a.CStr(), b.CStr()) < 0; });
	return out;
}

Result<Schematic, String> Game::LoadStructureFile(const String &name, bool exchange, std::vector<String> *unknown) {
	Result<String, String> file = FileName(name);
	if (file.IsError())
		return Err(file.Error());
	const String dir = StructuresDirectory(exchange);
	if (dir.IsEmpty())
		return Err(String("aucun monde ouvert"));
	String path = dir + String("/") + file.Value();
	// Sans extension donnée : le premier fichier de ce nom, quel que soit son format.
	if (Schematic::FormatOf(name.Trim()).IsEmpty() && !fs::exists(path.CStr()))
		for (const char *ext : {".vxs.json", ".json", ".we", ".mts"})
			if (fs::exists((dir + String("/") + name.Trim() + String(ext)).CStr())) {
				path = dir + String("/") + name.Trim() + String(ext);
				break;
			}
	Result<Schematic, String> s = Schematic::Load(path);
	if (s.IsError())
		return s;
	const std::vector<String> missing = s.Value().ResolveNames(m_content, json::At(m_content.Config(), "structures.aliases"));
	if (unknown)
		*unknown = missing;
	if (s.Value().Empty() || s.Value().Filled() == 0)
		return Err(String("structure vide (aucun bloc connu)"));
	return s;
}

Result<String, String> Game::SaveStructureFile(const Schematic &schematic, const String &name, bool exchange) {
	Result<String, String> file = FileName(name);
	if (file.IsError())
		return Err(file.Error());
	const String dir = StructuresDirectory(exchange);
	if (dir.IsEmpty())
		return Err(String("aucun monde ouvert"));
	const String path = dir + String("/") + file.Value();
	if (Result<bool, String> r = schematic.Save(path); r.IsError())
		return Err(r.Error());
	return Ok(path);
}

Result<bool, String> Game::DeleteStructureFile(const String &name, bool exchange) {
	Result<String, String> file = FileName(name);
	if (file.IsError())
		return Err(file.Error());
	const String path = StructuresDirectory(exchange) + String("/") + file.Value();
	std::error_code ec;
	if (!fs::remove(path.CStr(), ec))
		return Err(String::Format("structure introuvable : %s", file.Value().CStr()));
	return Ok(true);
}

void Game::SetHighlight(const String &id, const BlockPos &a, const BlockPos &b, std::array<uint8_t, 4> color) {
	ClearHighlight(id);
	m_highlights.push_back(Highlight{id, Lo(a, b), Hi(a, b), color});
}

void Game::ClearHighlight(const String &id) {
	std::erase_if(m_highlights, [&](const Highlight &h) { return h.id == id; });
}

void Game::SubmitForm(ecs::Entity player, const String &form, const NodePtr &values) {
	(void)m_scripts->Emit("form_submit", {m_scripts->EntityValue(player), script::Value::Str(form),
										  values ? script::ValueFromNode(values) : script::Value::EmptyMap()});
}

} // namespace voxel
