// Définitions de data/document.hpp
#include "data/document.hpp"

namespace data {

// ── ParseError ───────────────────────────────────────────────────────────────

String ParseError::Format() const {
	if (line >= 0)
		return message + " (line " + String::From(line) + ")";
	return message;
}

// ── Reader ───────────────────────────────────────────────────────────────────

Option<char> Reader::Peek() const noexcept {
	if (Eof())
		return NONE;
	return Some(m_data[m_pos]);
}

Option<char> Reader::PeekAt(size_t offset) const noexcept {
	size_t p = m_pos + offset;
	if (p >= m_data.GetSize())
		return NONE;
	return Some(m_data[p]);
}

bool Reader::PeekIs(char c) const noexcept {
	auto p = Peek();
	return p.IsSome() && *p == c;
}

bool Reader::PeekIs(std::initializer_list<char> lst) const noexcept {
	auto p = Peek();
	return p.IsSome() &&
		std::find(lst.begin(), lst.end(), *p) != lst.end();
}

Option<char> Reader::Get() noexcept {
	if (Eof())
		return NONE;
	char c = m_data[m_pos++];
	if (c == '\n')
		++m_line;
	return Some(c);
}

void Reader::Putback() noexcept {
	if (m_pos > 0) {
		--m_pos;
		if (m_data[m_pos] == '\n')
			--m_line;
	}
}

bool Reader::Eat(char c) noexcept {
	if (PeekIs(c)) {
		Get();
		return true;
	}
	return false;
}

void Reader::SkipWs() noexcept {
	while (!Eof() && std::isspace(static_cast<unsigned char>(m_data[m_pos])))
		Get();
}

namespace scalar {

Option<bool> ParseBool(StringView text) {
	String up = String(text).ToUpper();
	if (up == "TRUE")
		return Some(true);
	if (up == "FALSE")
		return Some(false);
	return NONE;
}

NodePtr ParseNode(StringView text) {
	if (auto b = ParseBool(text))
		return Node::MakeBool(*b);
	if (auto i = ParseInt(text))
		return Node::MakeInt(*i);
	if (auto f = ParseFloat(text))
		return Node::MakeFloat(*f);
	return Node::MakeString(String(text));
}

String ToString(const NodePtr &node) {
	if (!node)
		return "";
	switch (node->type) {
		case NodeType::NONE:
			return "";
		case NodeType::BOOL:
			return node->boolValue ? "true" : "false";
		case NodeType::STRING:
			return node->stringValue;
		case NodeType::INT:
			return String::From(node->intValue);
		case NodeType::FLOAT:
			return String::FromStream(node->floatValue);
		default:
			return ""; // Object/Array n'ont pas de forme scalaire
	}
}

} // namespace scalar

// ── Document ─────────────────────────────────────────────────────────────────

Option<ParseError> Document::Decode(sdl3::IOStream &io) {
	auto bytes = io.ReadAll();
	String content(reinterpret_cast<const char *>(bytes.data()), bytes.size());
	return DecodeStr(content);
}

bool Document::Encode(sdl3::IOStream &io) const {
	String text = EncodeStr();
	return io.Write(text.CStr(), text.GetSize()) == text.GetSize();
}

// ── DocumentFactory ──────────────────────────────────────────────────────────

DocumentFactory & DocumentFactory::instance() {
	static DocumentFactory inst;
	return inst;
}

bool DocumentFactory::registerFormat(const String &name, std::vector<String> extensions, Creator creator) {
	FormatInfo info{name, extensions, creator};
	m_byName[name] = info;
	for (auto &ext : info.extensions)
		m_byExtension[ext] = info;
	return true;
}

DocumentPtr DocumentFactory::CreateByName(const String &name) const {
	auto it = m_byName.find(name);
	return (it != m_byName.end()) ? it->second.creator() : nullptr;
}

DocumentPtr DocumentFactory::CreateByFilename(const String &filename) const {
	auto ext = ExtractExtension(filename);
	auto it = m_byExtension.find(ext);
	return (it != m_byExtension.end()) ? it->second.creator() : nullptr;
}

std::vector<String> DocumentFactory::RegisteredFormats() const {
	std::vector<String> v;
	v.reserve(m_byName.size());
	for (auto &p : m_byName)
		v.push_back(p.first);
	return v;
}

String DocumentFactory::ExtractExtension(const String &filename) {
	auto pos = filename.Rfind('.');
	if (pos == String::NPOS)
		return {};
	return filename.Substr(pos).ToLower();
}

} // namespace data
