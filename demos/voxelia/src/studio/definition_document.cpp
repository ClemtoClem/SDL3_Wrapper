// Définitions de voxelia/studio/definition_document.hpp
#include <voxelia/studio/definition_document.hpp>

#include <cctype>
#include <cstdio>
#include <filesystem>

#include <voxel/json.hpp>

namespace voxelia::studio {

namespace json = voxel::json;
using dataio::Node;
using dataio::NodePtr;

namespace {

/// En-tête de commentaires d'un fichier de définitions (gardé à l'écriture).
String LeadingComments(const String &path) {
	String out;
	for (const String &line : String::ReadFile(path).UnwrapOr(String()).Split('\n')) {
		if (!line.TrimLeft().StartsWith("//"))
			break;
		out += line + "\n";
	}
	return out;
}

/// Blancs et commentaires (// et /* */) à partir de `i`.
size_t SkipTrivia(const String &s, size_t i) {
	while (i < s.GetSize()) {
		if (std::isspace(static_cast<unsigned char>(s[i]))) {
			++i;
		} else if (s.StartsWith("//", i)) {
			const size_t end = s.Find('\n', i);
			i = end == String::NPOS ? s.GetSize() : end;
		} else if (s.StartsWith("/*", i)) {
			const size_t end = s.Find("*/", i + 2);
			i = end == String::NPOS ? s.GetSize() : end + 2;
		} else {
			break;
		}
	}
	return i;
}

/// Fin (exclue) de la valeur qui commence en `i` : objet, tableau, chaîne
/// ('…' accepté, comme json::Normalize) ou littéral.
size_t SkipValue(const String &s, size_t i) {
	int depth = 0;
	while (i < s.GetSize()) {
		const char c = s[i];
		if (c == '"' || c == '\'') {
			for (++i; i < s.GetSize() && s[i] != c; ++i)
				if (s[i] == '\\')
					++i;
			if (++i, depth == 0)
				return i;
		} else if (s.StartsWith("//", i) || s.StartsWith("/*", i)) {
			if (depth == 0)
				return i;
			i = SkipTrivia(s, i);
		} else if (c == '{' || c == '[') {
			++depth;
			++i;
		} else if (c == '}' || c == ']') {
			if (depth == 0)
				return i;
			++i;
			if (--depth == 0)
				return i;
		} else if (depth == 0 && (c == ',' || std::isspace(static_cast<unsigned char>(c)))) {
			return i;
		} else {
			++i;
		}
	}
	return i;
}

/// Colonne de `i` (blancs de début de ligne) : indentation d'un élément.
String IndentAt(const String &s, size_t i) {
	const size_t newline = i == 0 ? String::NPOS : s.Rfind('\n', i - 1);
	String indent;
	for (size_t k = newline == String::NPOS ? 0 : newline + 1; k < i && (s[k] == ' ' || s[k] == '\t'); ++k)
		indent.PushBack(s[k]);
	return indent;
}

/// `text` encodé, chaque ligne après la première décalée de `indent` (la
/// première prend la place de la valeur remplacée).
String Indented(const String &text, const String &indent) {
	return text.TrimRight().IndentLines(indent.View()).Substr(indent.GetSize());
}

bool SameNode(const NodePtr &a, const NodePtr &b) {
	if (!a || !b)
		return a == b;
	return json::Encode(a) == json::Encode(b);
}

} // namespace

String CompactJson(const String &text) {
	String out;
	bool quoted = false, newline = false;
	for (size_t i = 0; i < text.GetSize(); ++i) {
		const char c = text[i];
		if (c == '"' && (i == 0 || text[i - 1] != '\\'))
			quoted = !quoted;
		if (!quoted && (c == '\n' || c == '\r')) {
			newline = true;
			continue;
		}
		if (!quoted && newline && (c == ' ' || c == '\t'))
			continue;
		if (newline && !out.IsEmpty() && out[out.GetSize() - 1] == ',')
			out += ' ';
		newline = false;
		out += c;
	}
	return out;
}

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

voxel::ContentRegistry::LoadContext DefinitionDocument::Context() const {
	voxel::ContentRegistry::LoadContext ctx;
	ctx.mod = m_mod;
	ctx.baseDir = m_baseDir;
	ctx.source = m_file;
	return ctx;
}

// ── Lecture / écriture ───────────────────────────────────────────────────────

String DefinitionDocument::Text() const { return m_json ? json::Encode(m_json) : String("{}"); }

Result<bool, String> DefinitionDocument::SetJson(const String &text) {
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

NodePtr DefinitionDocument::Field(const char *path) const { return json::At(m_json, path); }

String DefinitionDocument::FieldText(const char *path, const String &fallback) const { return json::Str(m_json, path, fallback); }

double DefinitionDocument::FieldNumber(const char *path, double fallback) const { return json::Num(m_json, path, fallback); }

bool DefinitionDocument::FieldBool(const char *path, bool fallback) const { return json::Bool(m_json, path, fallback); }

void DefinitionDocument::SetField(const char *path, NodePtr value) {
	if (!m_json)
		m_json = Node::MakeObject();
	const NodePtr before = m_editDepth == 0 ? m_json->Clone() : nullptr;
	SetJsonPath(m_json, path, std::move(value));
	if (m_editDepth == 0)
		Commit(before);
}

// ── Annulation ───────────────────────────────────────────────────────────────

void DefinitionDocument::Commit(const NodePtr &before) {
	if (SameNode(before, m_json))
		return;
	m_undo.push_back(before);
	if (m_undo.size() > MAX_UNDO)
		m_undo.erase(m_undo.begin());
	m_redo.clear();
	m_dirty = true;
}

void DefinitionDocument::BeginEdit() {
	if (m_editDepth++ == 0)
		m_editStart = m_json ? m_json->Clone() : nullptr;
}

void DefinitionDocument::EndEdit() {
	if (m_editDepth == 0)
		return;
	if (--m_editDepth == 0) {
		Commit(m_editStart);
		m_editStart = nullptr;
	}
}

bool DefinitionDocument::Undo() {
	if (m_undo.empty() || m_editDepth > 0)
		return false;
	m_redo.push_back(m_json);
	m_json = m_undo.back();
	m_undo.pop_back();
	m_dirty = true;
	return true;
}

bool DefinitionDocument::Redo() {
	if (m_redo.empty() || m_editDepth > 0)
		return false;
	m_undo.push_back(m_json);
	m_json = m_redo.back();
	m_redo.pop_back();
	m_dirty = true;
	return true;
}

// ── Opérations ───────────────────────────────────────────────────────────────

void DefinitionDocument::Mutate(const std::function<void(const NodePtr &)> &change) {
	if (!m_json)
		m_json = Node::MakeObject();
	const NodePtr before = m_editDepth == 0 ? m_json->Clone() : nullptr;
	change(m_json);
	if (m_editDepth == 0)
		Commit(before);
}

Result<bool, String> DefinitionDocument::Save() {
	if (m_file.IsEmpty())
		return Err(String("aucun fichier associé au bloc"));
	auto sameId = [&](const NodePtr &def) {
		return def && def->IsObject() && voxel::ContentRegistry::Qualify(json::Str(def, "id"), m_mod) == m_id;
	};
	// Le fichier existant est modifié EN TEXTE : seule la définition visée est
	// réécrite (ou ajoutée en fin de tableau) ; le reste — commentaires,
	// mise en forme, autres définitions — est gardé octet pour octet.
	String text;
	if (std::filesystem::exists(m_file.CStr())) {
		Result<NodePtr, String> read = json::ReadFile(m_file);
		if (read.IsError())
			return Err(read.Error());
		text = String::ReadFile(m_file).UnwrapOr(String());
		const NodePtr root = read.Value();
		const String encoded = json::Encode(m_json);
		const size_t start = SkipTrivia(text, 0);
		if (root->IsArray() && start < text.GetSize() && text[start] == '[') {
			size_t i = SkipTrivia(text, start + 1), lastEnd = String::NPOS;
			bool replaced = false;
			while (i < text.GetSize() && text[i] != ']') {
				const size_t end = SkipValue(text, i);
				Result<NodePtr, String> def = json::Parse(text.Substr(i, end - i));
				if (def.IsOk() && sameId(def.Value())) {
					text.ReplaceInplace(i, end - i, Indented(encoded, IndentAt(text, i)));
					replaced = true;
					break;
				}
				lastEnd = end;
				i = SkipTrivia(text, end);
				if (i < text.GetSize() && text[i] == ',')
					i = SkipTrivia(text, i + 1);
			}
			if (!replaced) {
				if (i >= text.GetSize())
					return Err(String::Format("%s : tableau de définitions non fermé", m_file.CStr()));
				if (lastEnd == String::NPOS) {
					text.ReplaceInplace(start + 1, 0, String("\n  ") + Indented(encoded, String("  ")) + String("\n"));
				} else {
					const String indent = IndentAt(text, lastEnd);
					text.ReplaceInplace(lastEnd, 0, String(",\n") + indent + Indented(encoded, indent));
				}
			}
		} else if (root->IsObject() && sameId(root) && start < text.GetSize()) {
			text.ReplaceInplace(start, SkipValue(text, start) - start, Indented(encoded, String()));
		} else {
			// Objet seul d'une autre définition : le fichier devient un tableau.
			NodePtr both = Node::MakeArray();
			both->Push(root);
			both->Push(m_json->Clone());
			text = LeadingComments(m_file) + json::Encode(both);
		}
	} else {
		NodePtr root = Node::MakeArray();
		root->Push(m_json->Clone());
		text = json::Encode(root);
	}
	std::error_code ec;
	const std::filesystem::path parent = std::filesystem::path(m_file.CStr()).parent_path();
	if (!parent.empty())
		std::filesystem::create_directories(parent, ec);
	if (!text.WriteFile(m_file))
		return Err(String::Format("%s : écriture impossible", m_file.CStr()));
	m_dirty = false;
	return Ok(true);
}

} // namespace voxelia::studio
