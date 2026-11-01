// Définitions de voxelia/studio/block_document.hpp
#include <voxelia/studio/block_document.hpp>

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <sstream>

#include <voxel/json.hpp>

namespace voxelia::studio {

namespace json = voxel::json;
using dataio::Node;
using dataio::NodePtr;

namespace {

/// Feuilles d'un arbre JSON : chemin pointé → texte (comparaisons d'états).
void Flatten(const NodePtr &node, const String &prefix, std::vector<std::pair<String, String>> &out) {
	if (node && node->IsObject() && !node->Keys().empty()) {
		for (const String &key : node->Keys())
			Flatten(node->Get(key), prefix.IsEmpty() ? key : prefix + String(".") + key, out);
		return;
	}
	out.emplace_back(prefix, node ? json::Encode(node).Trim() : String("null"));
}

/// Chemin d'un champ cité par un avertissement (« id : light.color hors… »).
String FieldOf(const String &message) {
	const size_t colon = message.Find(" : ");
	if (colon == String::NPOS)
		return String();
	const String rest = message.Substring(colon + 3);
	size_t end = 0;
	while (end < rest.GetSize() && (std::isalnum(static_cast<unsigned char>(rest[end])) || rest[end] == '.' || rest[end] == '_'))
		++end;
	const String field = rest.Substring(0, end);
	return field.Contains(".") || field == "light" || field == "optics" || field == "sensor" ? field : String();
}

} // namespace

// ── Création ─────────────────────────────────────────────────────────────────

Option<BlockDocument> BlockDocument::FromBlock(const voxel::ContentRegistry &content, const String &blockId) {
	const voxel::BlockDef *block = content.FindBlock(blockId);
	if (!block)
		return NONE;
	if (!block->stateGroup.IsEmpty())
		if (const voxel::BlockDef *group = content.FindBlock(block->stateGroup))
			block = group;
	if (!block->source)
		return NONE;
	BlockDocument doc;
	doc.m_json = block->source->Clone();
	doc.m_id = block->id;
	doc.m_mod = block->mod;
	doc.m_baseDir = block->baseDir;
	doc.m_file = block->sourceFile;
	return Some(std::move(doc));
}

BlockDocument BlockDocument::New(const String &id, const String &mod, const String &baseDir, const String &file) {
	BlockDocument doc;
	doc.m_mod = mod;
	doc.m_id = voxel::ContentRegistry::Qualify(id, mod);
	doc.m_baseDir = baseDir;
	doc.m_file = file;
	const size_t colon = id.Find(":");
	doc.m_json = Node::MakeObject();
	doc.m_json->Set("id", json::MakeStr(colon == String::NPOS ? id : id.Substring(colon + 1)));
	doc.m_json->Set("displayName", json::MakeStr(colon == String::NPOS ? id : id.Substring(colon + 1)));
	SetJsonPath(doc.m_json, "graphics.texture", json::MakeStr(String("core:stone")));
	SetJsonPath(doc.m_json, "gameplay.hardness", json::MakeNum(1.0));
	doc.m_dirty = true;
	return doc;
}

// ── Contenu ──────────────────────────────────────────────────────────────────

std::vector<BlockDiagnostic> BlockDocument::Validate(const voxel::ContentRegistry &live) const {
	std::vector<BlockDiagnostic> out;
	voxel::ContentRegistry scratch = live; // le contenu vivant n'est jamais touché
	std::vector<String> owners{m_id};
	if (const voxel::BlockDef *old = scratch.FindBlock(m_id))
		for (const auto &[state, id] : old->stateIds)
			owners.push_back(id);
	for (const String &owner : owners)
		scratch.ForgetLoadWarnings(owner);
	Result<String, String> loaded = scratch.Load(m_json, Context());
	if (loaded.IsError()) {
		out.push_back({BlockDiagnostic::Severity::Error, loaded.Error(), String()});
		return out;
	}
	scratch.Finalize();
	owners = {loaded.Value()};
	if (const voxel::BlockDef *block = scratch.FindBlock(loaded.Value()))
		for (const auto &[state, id] : block->stateIds)
			owners.push_back(id);
	for (const String &warning : scratch.Validate())
		for (const String &owner : owners)
			if (warning.StartsWith(owner + String(" :"))) {
				out.push_back({BlockDiagnostic::Severity::Warning, warning, FieldOf(warning)});
				break;
			}
	return out;
}

std::vector<MergedState> BlockDocument::MergedStates(const voxel::ContentRegistry &live) const {
	std::vector<MergedState> out;
	voxel::ContentRegistry scratch = live;
	Result<String, String> loaded = scratch.Load(m_json, Context());
	if (loaded.IsError())
		return out;
	const voxel::BlockDef *block = scratch.FindBlock(loaded.Value());
	if (!block)
		return out;
	std::vector<std::pair<String, String>> base;
	Flatten(block->raw, String(), base);
	auto describe = [&](const voxel::BlockDef &b, const String &name) {
		MergedState s;
		s.name = name;
		s.id = b.id;
		s.isDefault = b.id == block->id;
		s.json = json::Encode(b.raw);
		std::vector<std::pair<String, String>> leaves;
		Flatten(b.raw, String(), leaves);
		for (const auto &[path, value] : leaves) {
			if (path == "id" || path == "stateName" || path == "item" || path == "gameplay.drops")
				continue;
			auto it = std::find_if(base.begin(), base.end(), [&](const auto &p) { return p.first == path; });
			if (it == base.end() || it->second != value)
				s.differences.push_back(path);
		}
		out.push_back(std::move(s));
	};
	if (block->stateIds.empty()) {
		describe(*block, String());
		return out;
	}
	for (const auto &[name, id] : block->stateIds)
		if (const voxel::BlockDef *state = scratch.FindBlock(id))
			describe(*state, name);
	return out;
}

// ── Forme nodebox ────────────────────────────────────────────────────────────

namespace {

String ScopePath(const String &state, const char *field) {
	return state.IsEmpty() ? String(field) : String("states.") + state + String(".") + String(field);
}

/// Liste de boîtes d'un nœud « nodebox » (tableau, ou {"fixed" | "boxes"}).
NodePtr BoxList(const NodePtr &nodebox) {
	if (!nodebox)
		return nullptr;
	if (nodebox->IsArray())
		return nodebox;
	if (NodePtr fixed = json::At(nodebox, "fixed"); fixed && fixed->IsArray())
		return fixed;
	if (NodePtr boxes = json::At(nodebox, "boxes"); boxes && boxes->IsArray())
		return boxes;
	return nullptr;
}

} // namespace

std::vector<BlockDocument::NodeBox> BlockDocument::NodeBoxes(const String &state) const {
	NodePtr list = BoxList(Field(ScopePath(state, "nodebox").CStr()));
	if (!list)
		list = BoxList(Field(ScopePath(state, "shape").CStr()));
	if (!list && !state.IsEmpty())
		return NodeBoxes(String()); // l'état hérite des boîtes du bloc commun
	std::vector<NodeBox> out;
	for (size_t i = 0; list && i < list->GetSize(); ++i) {
		NodePtr row = list->At(i);
		if (!row || !row->IsArray() || row->GetSize() < 6)
			continue;
		NodeBox b{};
		for (size_t k = 0; k < 6; ++k)
			b[k] = (float(json::Num(row->At(k), "", 0.0)) + 0.5f) * 16.f;
		out.push_back(b);
	}
	return out;
}

Result<bool, String> BlockDocument::SetNodeBoxes(const std::vector<NodeBox> &boxes, const String &state) {
	for (size_t i = 0; i < boxes.size(); ++i) {
		const NodeBox &b = boxes[i];
		for (size_t k = 0; k < 6; ++k)
			if (!(b[k] >= -8.f && b[k] <= 24.f)) // jusqu'à un demi-bloc de débord (comme Minetest)
				return Err(String::Format("boîte %zu : coordonnée %.2f px hors de [−8, 24]", i, double(b[k])));
		if (b[0] == b[3] || b[1] == b[4] || b[2] == b[5])
			return Err(String::Format("boîte %zu : épaisseur nulle", i));
	}
	if (!state.IsEmpty() && !Field((String("states.") + state).CStr()))
		return Err(String("état inconnu : ") + state);
	Mutate([&](const NodePtr &root) {
		NodePtr list = Node::MakeArray();
		for (const NodeBox &b : boxes) {
			NodePtr row = Node::MakeArray();
			for (size_t k = 0; k < 6; ++k)
				row->Push(json::MakeNum(double(b[k]) / 16.0 - 0.5));
			list->Push(row);
		}
		SetJsonPath(root, ScopePath(state, "nodebox").CStr(), list);
		if (Field(ScopePath(state, "shape").CStr()) && BoxList(Field(ScopePath(state, "shape").CStr())))
			SetJsonPath(root, ScopePath(state, "shape").CStr(), nullptr); // une seule source de boîtes
		// Le bloc devient une nodebox (le type se lit sur la définition entière).
		const String type = json::Str(root, "type", "block").ToLower();
		if (type == "block" || type == "cube")
			root->Set("type", json::MakeStr(String("nodebox")));
	});
	return Ok(true);
}

Result<bool, String> BlockDocument::AddNodeBox(const NodeBox &box, const String &state) {
	std::vector<NodeBox> boxes = NodeBoxes(state);
	boxes.push_back(box);
	return SetNodeBoxes(boxes, state);
}

Result<bool, String> BlockDocument::SetNodeBox(size_t index, const NodeBox &box, const String &state) {
	std::vector<NodeBox> boxes = NodeBoxes(state);
	if (index >= boxes.size())
		return Err(String::Format("boîte %zu inconnue", index));
	boxes[index] = box;
	return SetNodeBoxes(boxes, state);
}

Result<bool, String> BlockDocument::RemoveNodeBox(size_t index, const String &state) {
	std::vector<NodeBox> boxes = NodeBoxes(state);
	if (index >= boxes.size())
		return Err(String::Format("boîte %zu inconnue", index));
	if (boxes.size() == 1)
		return Err(String("une nodebox garde au moins une boîte"));
	boxes.erase(boxes.begin() + std::ptrdiff_t(index));
	return SetNodeBoxes(boxes, state);
}

Result<bool, String> BlockDocument::MirrorNodeBox(size_t index, int axis, const String &state) {
	std::vector<NodeBox> boxes = NodeBoxes(state);
	if (index >= boxes.size() || axis < 0 || axis > 2)
		return Err(String::Format("boîte %zu ou axe %d inconnu", index, axis));
	NodeBox b = boxes[index];
	const float lo = 16.f - b[size_t(axis) + 3], hi = 16.f - b[size_t(axis)]; // symétrie par le centre du bloc
	b[size_t(axis)] = lo;
	b[size_t(axis) + 3] = hi;
	boxes.push_back(b);
	return SetNodeBoxes(boxes, state);
}

Result<bool, String> BlockDocument::RotateNodeBoxes(const String &state) {
	std::vector<NodeBox> boxes = NodeBoxes(state);
	if (boxes.empty())
		return Err(String("aucune boîte à tourner"));
	for (NodeBox &b : boxes) {
		// Quart de tour autour de y par le centre : (x, z) → (16 − z, x).
		const NodeBox r{16.f - b[5], b[1], b[0], 16.f - b[2], b[4], b[3]};
		b = r;
	}
	return SetNodeBoxes(boxes, state);
}

Option<voxel::BlockDef> BlockDocument::Loaded(const voxel::ContentRegistry &live, const String &state) const {
	voxel::ContentRegistry scratch = live;
	Result<String, String> loaded = scratch.Load(m_json, Context());
	if (loaded.IsError())
		return NONE;
	const voxel::BlockDef *block = scratch.FindBlock(loaded.Value());
	if (block && !state.IsEmpty())
		block = scratch.BlockState(*block, state);
	if (!block)
		return NONE;
	return Some(*block);
}

} // namespace voxelia::studio
