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

/// En-tête de commentaires d'un fichier de définitions (gardé à l'écriture).
String LeadingComments(const String &path) {
	std::ifstream file(path.CStr());
	String out;
	std::string line;
	while (std::getline(file, line)) {
		size_t i = line.find_first_not_of(" \t");
		if (i == std::string::npos || line.compare(i, 2, "//") != 0)
			break;
		out += String(line.c_str()) + String("\n");
	}
	return out;
}

bool SameNode(const NodePtr &a, const NodePtr &b) {
	if (!a || !b)
		return a == b;
	return json::Encode(a) == json::Encode(b);
}

} // namespace

void SetJsonPath(const NodePtr &root, const char *path, NodePtr value) {
	if (!root || !root->IsObject())
		return;
	const String full(path);
	NodePtr node = root;
	size_t start = 0;
	while (true) {
		const size_t dot = full.Find(".", start);
		const String key = full.Substring(start, dot == String::NPOS ? full.GetSize() : dot);
		if (dot == String::NPOS) {
			if (value)
				node->Set(key, std::move(value));
			else if (node->Has(key))
				node->Remove(key);
			return;
		}
		NodePtr next = node->Has(key) ? node->Get(key) : nullptr;
		if (!next || !next->IsObject()) {
			if (!value)
				return; // rien à retirer
			next = Node::MakeObject();
			node->Set(key, next);
		}
		node = next;
		start = dot + 1;
	}
}

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

voxel::ContentRegistry::LoadContext BlockDocument::Context() const {
	voxel::ContentRegistry::LoadContext ctx;
	ctx.mod = m_mod;
	ctx.baseDir = m_baseDir;
	ctx.source = m_file;
	return ctx;
}

// ── Lecture / écriture ───────────────────────────────────────────────────────

String BlockDocument::Text() const { return m_json ? json::Encode(m_json) : String("{}"); }

Result<bool, String> BlockDocument::SetJson(const String &text) {
	Result<NodePtr, String> parsed = json::Parse(text);
	if (parsed.IsError())
		return Err(parsed.Error());
	if (!parsed.Value() || !parsed.Value()->IsObject())
		return Err(String("la définition d'un bloc est un objet JSON { … }"));
	const NodePtr before = m_json ? m_json->Clone() : nullptr;
	m_json = parsed.Value();
	if (m_editDepth == 0)
		Commit(before);
	return Ok(true);
}

NodePtr BlockDocument::Field(const char *path) const { return json::At(m_json, path); }

String BlockDocument::FieldText(const char *path, const String &fallback) const { return json::Str(m_json, path, fallback); }

double BlockDocument::FieldNumber(const char *path, double fallback) const { return json::Num(m_json, path, fallback); }

bool BlockDocument::FieldBool(const char *path, bool fallback) const { return json::Bool(m_json, path, fallback); }

void BlockDocument::SetField(const char *path, NodePtr value) {
	if (!m_json)
		m_json = Node::MakeObject();
	const NodePtr before = m_editDepth == 0 ? m_json->Clone() : nullptr;
	SetJsonPath(m_json, path, std::move(value));
	if (m_editDepth == 0)
		Commit(before);
}

// ── Annulation ───────────────────────────────────────────────────────────────

void BlockDocument::Commit(const NodePtr &before) {
	if (SameNode(before, m_json))
		return;
	m_undo.push_back(before);
	if (m_undo.size() > MAX_UNDO)
		m_undo.erase(m_undo.begin());
	m_redo.clear();
	m_dirty = true;
}

void BlockDocument::BeginEdit() {
	if (m_editDepth++ == 0)
		m_editStart = m_json ? m_json->Clone() : nullptr;
}

void BlockDocument::EndEdit() {
	if (m_editDepth == 0)
		return;
	if (--m_editDepth == 0) {
		Commit(m_editStart);
		m_editStart = nullptr;
	}
}

bool BlockDocument::Undo() {
	if (m_undo.empty() || m_editDepth > 0)
		return false;
	m_redo.push_back(m_json);
	m_json = m_undo.back();
	m_undo.pop_back();
	m_dirty = true;
	return true;
}

bool BlockDocument::Redo() {
	if (m_redo.empty() || m_editDepth > 0)
		return false;
	m_undo.push_back(m_json);
	m_json = m_redo.back();
	m_redo.pop_back();
	m_dirty = true;
	return true;
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

Result<bool, String> BlockDocument::Save() {
	if (m_file.IsEmpty())
		return Err(String("aucun fichier associé au bloc"));
	const String header = LeadingComments(m_file);
	NodePtr root;
	if (std::filesystem::exists(m_file.CStr())) {
		Result<NodePtr, String> read = json::ReadFile(m_file);
		if (read.IsError())
			return Err(read.Error());
		root = read.Value();
	}
	auto sameId = [&](const NodePtr &def) {
		return def && def->IsObject() && voxel::ContentRegistry::Qualify(json::Str(def, "id"), m_mod) == m_id;
	};
	if (!root) {
		root = Node::MakeArray();
		root->Push(m_json->Clone());
	} else if (root->IsArray()) {
		bool replaced = false;
		NodePtr rebuilt = Node::MakeArray();
		for (size_t i = 0; i < root->GetSize(); ++i) {
			NodePtr def = root->At(i);
			if (!replaced && sameId(def)) {
				rebuilt->Push(m_json->Clone());
				replaced = true;
			} else {
				rebuilt->Push(def);
			}
		}
		if (!replaced)
			rebuilt->Push(m_json->Clone());
		root = rebuilt;
	} else if (sameId(root)) {
		root = m_json->Clone();
	} else {
		NodePtr both = Node::MakeArray();
		both->Push(root);
		both->Push(m_json->Clone());
		root = both;
	}
	std::error_code ec;
	const std::filesystem::path parent = std::filesystem::path(m_file.CStr()).parent_path();
	if (!parent.empty())
		std::filesystem::create_directories(parent, ec);
	std::ofstream file(m_file.CStr(), std::ios::binary | std::ios::trunc);
	if (!file)
		return Err(String::Format("%s : écriture impossible", m_file.CStr()));
	const String text = header + json::Encode(root);
	file.write(text.CStr(), std::streamsize(text.GetSize()));
	if (!file)
		return Err(String::Format("%s : écriture incomplète", m_file.CStr()));
	m_dirty = false;
	return Ok(true);
}

} // namespace voxelia::studio
