// Définitions de ui/css.hpp
#include <ui/css.hpp>

#include <cctype>
#include <filesystem>
#include <unordered_set>

#include <dataio/documents/css.hpp>
#include <sdl3/iostream.hpp>

namespace ui {

namespace {

bool IsSpace(char c) noexcept { return c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\f'; }

bool IsNameChar(char c) noexcept {
	return std::isalnum(static_cast<unsigned char>(c)) || c == '-' || c == '_' || (static_cast<unsigned char>(c) >= 0x80);
}

StringView Trim(StringView s) noexcept {
	size_t b = 0, e = s.GetSize();
	while (b < e && IsSpace(s[b]))
		++b;
	while (e > b && IsSpace(s[e - 1]))
		--e;
	return s.Substr(b, e - b);
}

String Lower(StringView s) {
	String out;
	for (size_t i = 0; i < s.GetSize(); ++i)
		out.PushBack(char(std::tolower(static_cast<unsigned char>(s[i]))));
	return out;
}

String Unquote(StringView s) {
	s = Trim(s);
	if (s.GetSize() >= 2 && (s[0] == '"' || s[0] == '\'') && s.Back() == s[0])
		return String(s.Substr(1, s.GetSize() - 2));
	return String(s);
}

/// `url(x)` ou `"x"` -> x.
String UrlValue(StringView s) {
	s = Trim(s);
	if (s.StartsWith("url(") && s.EndsWith(")"))
		return Unquote(s.Substr(4, s.GetSize() - 5));
	return Unquote(s);
}

/// Chemin joint puis normalisé (« a/sub/../b.css » → « a/b.css ») : la
/// détection des imports cycliques compare des chemins.
String JoinPath(const String &dir, const String &path) {
	const String joined = (dir.IsEmpty() || path.StartsWith("/") || path.Contains(':')) ? path : dir + "/" + path;
	if (joined.Contains(':'))
		return joined; // URL ou lecteur : laissé tel quel
	return String(std::filesystem::path(joined.CStr()).lexically_normal().generic_string());
}

Result<CompoundSelector, String> ParseCompound(StringView t) {
	CompoundSelector c;
	size_t i = 0;
	auto readName = [&]() {
		const size_t start = i;
		while (i < t.GetSize() && IsNameChar(t[i]))
			++i;
		return String(t.Substr(start, i - start));
	};
	if (i < t.GetSize() && t[i] == '*') {
		++i;
	} else if (i < t.GetSize() && IsNameChar(t[i])) {
		String name = readName();
		const String lower = Lower(name.View());
		if (lower == "this")
			c.isThis = true;
		else if (lower == "root")
			c.isRoot = true;
		else
			c.type = lower;
	}
	while (i < t.GetSize()) {
		const char ch = t[i];
		if (ch == '#') {
			++i;
			c.id = readName();
			if (c.id.IsEmpty())
				return Err(String("identifiant vide après '#'"));
		} else if (ch == '.') {
			++i;
			String cls = readName();
			if (cls.IsEmpty())
				return Err(String("classe vide après '.'"));
			c.classes.push_back(std::move(cls));
		} else if (ch == ':') {
			++i;
			if (i < t.GetSize() && t[i] == ':')
				return Err(String("pseudo-éléments non pris en charge"));
			const String pseudo = Lower(readName().View());
			if (pseudo == "root") {
				c.isRoot = true;
			} else if (pseudo == "first-child") {
				c.nth = NthChild::First;
			} else if (pseudo == "last-child") {
				c.nth = NthChild::Last;
			} else if (pseudo == "nth-child") {
				if (i >= t.GetSize() || t[i] != '(')
					return Err(String(":nth-child sans argument"));
				const size_t close = t.Find(')', i);
				if (close == StringView::NPOS)
					return Err(String(":nth-child non fermé"));
				const String arg = Lower(Trim(t.Substr(i + 1, close - i - 1)));
				i = close + 1;
				if (arg == "odd")
					c.nth = NthChild::Odd;
				else if (arg == "even")
					c.nth = NthChild::Even;
				else if (auto n = arg.TryParseInt(); n.IsSome()) {
					c.nth = NthChild::Index;
					c.nthIndex = int(n.Unwrap());
				} else
					return Err(String(":nth-child(") + arg + ") non pris en charge");
			} else if (auto st = StateFromName(pseudo.View()); st.IsSome()) {
				c.states = c.states | st.Unwrap();
			} else {
				return Err(String("pseudo-classe inconnue :") + pseudo);
			}
		} else {
			return Err(String("caractère inattendu '") + String(ch) + "' dans le sélecteur");
		}
	}
	return Ok(std::move(c));
}

uint32_t SpecificityOf(const Selector &s) noexcept {
	uint32_t a = 0, b = 0, c = 0;
	for (const CompoundSelector &p : s.parts) {
		if (!p.id.IsEmpty() || p.isThis)
			++a;
		b += uint32_t(p.classes.size());
		for (uint16_t m = uint16_t(p.states); m; m &= uint16_t(m - 1))
			++b;
		if (p.nth != NthChild::None || p.isRoot)
			++b;
		if (!p.type.IsEmpty())
			++c;
	}
	return a * 10000u + b * 100u + c;
}

} // namespace

Result<Selector, String> ParseSelector(StringView text) {
	text = Trim(text);
	if (text.IsEmpty())
		return Err(String("sélecteur vide"));
	Selector sel;
	size_t i = 0;
	bool pendingChild = false;
	while (i < text.GetSize()) {
		while (i < text.GetSize() && IsSpace(text[i]))
			++i;
		if (i >= text.GetSize())
			break;
		if (text[i] == '>') {
			if (sel.parts.empty() || pendingChild)
				return Err(String("combinateur '>' mal placé"));
			pendingChild = true;
			++i;
			continue;
		}
		if (text[i] == '+' || text[i] == '~')
			return Err(String("combinateurs de frères non pris en charge"));
		size_t start = i;
		int paren = 0;
		while (i < text.GetSize() && (paren > 0 || (!IsSpace(text[i]) && text[i] != '>'))) {
			if (text[i] == '(')
				++paren;
			else if (text[i] == ')')
				--paren;
			++i;
		}
		auto compound = ParseCompound(text.Substr(start, i - start));
		if (compound.IsError())
			return Err(compound.Error());
		if (!sel.parts.empty())
			sel.combinators.push_back(pendingChild ? Combinator::Child : Combinator::Descendant);
		pendingChild = false;
		sel.parts.push_back(std::move(compound.Value()));
	}
	if (sel.parts.empty() || pendingChild)
		return Err(String("sélecteur incomplet"));
	sel.specificity = SpecificityOf(sel);
	return Ok(std::move(sel));
}

std::vector<Selector> ParseSelectorList(StringView text) {
	std::vector<Selector> out;
	size_t start = 0;
	int paren = 0;
	for (size_t i = 0; i <= text.GetSize(); ++i) {
		if (i < text.GetSize()) {
			if (text[i] == '(')
				++paren;
			else if (text[i] == ')')
				--paren;
			if (text[i] != ',' || paren > 0)
				continue;
		}
		auto s = ParseSelector(text.Substr(start, i - start));
		if (s.IsOk())
			out.push_back(std::move(s.Value()));
		start = i + 1;
	}
	return out;
}

bool MatchCompound(const CompoundSelector &c, const ElementData &e) noexcept {
	if (c.isThis && !e.isScopeOwner)
		return false;
	if (c.isRoot && !e.isRoot)
		return false;
	if (!c.type.IsEmpty() && c.type.View() != e.type)
		return false;
	if (!c.id.IsEmpty() && c.id.View() != e.id)
		return false;
	for (const String &cls : c.classes) {
		bool found = false;
		if (e.classes)
			for (const String &have : *e.classes)
				if (have == cls) {
					found = true;
					break;
				}
		if (!found)
			return false;
	}
	if (!HasState(e.state, c.states))
		return false;
	switch (c.nth) {
	case NthChild::None:
		break;
	case NthChild::First:
		if (e.index != 0)
			return false;
		break;
	case NthChild::Last:
		if (e.index != e.siblingCount - 1)
			return false;
		break;
	case NthChild::Odd:
		if ((e.index + 1) % 2 != 1)
			return false;
		break;
	case NthChild::Even:
		if ((e.index + 1) % 2 != 0)
			return false;
		break;
	case NthChild::Index:
		if (e.index + 1 != c.nthIndex)
			return false;
		break;
	}
	return true;
}

// ============================================================================
// StyleSheet
// ============================================================================

struct StyleSheet::ParseState {
	std::unordered_set<String> visited; ///< fichiers déjà importés (cycles)
	int depth = 0;
};

String DirectoryOf(const String &path) {
	const size_t slash = path.Rfind('/');
	return slash == String::npos ? String() : path.Substr(0, slash);
}

Result<std::shared_ptr<StyleSheet>, String> StyleSheet::Load(const String &path) {
	auto bytes = sdl3::ReadFile(path);
	if (bytes.IsError())
		return Err(String("feuille de style « ") + path + " » illisible : " + String(bytes.Error()));
	const std::vector<uint8_t> &data = bytes.Value();
	auto sheet = std::make_shared<StyleSheet>();
	sheet->m_name = path;
	ParseState state;
	state.visited.insert(JoinPath(String(), path));
	sheet->ParseInto(StringView(reinterpret_cast<const char *>(data.data()), data.size()), DirectoryOf(path), state);
	return Ok(std::move(sheet));
}

std::shared_ptr<StyleSheet> StyleSheet::Parse(StringView css, const String &baseDir, const String &name) {
	auto sheet = std::make_shared<StyleSheet>();
	sheet->m_name = name;
	ParseState state;
	sheet->ParseInto(css, baseDir, state);
	return sheet;
}

void StyleSheet::Append(StringView css, const String &baseDir) {
	ParseState state;
	ParseInto(css, baseDir, state);
}

void StyleSheet::AddRule(std::vector<Selector> selectors, StyleProps props) {
	StyleRule rule;
	rule.selectors = std::move(selectors);
	rule.props = std::move(props);
	rule.order = m_rules.size();
	m_rules.push_back(std::move(rule));
	++m_revision;
}

void StyleSheet::ParseInto(StringView source, const String &baseDir, ParseState &state) {
	const String label = m_name.IsEmpty() ? String("<css>") : m_name;
	auto warn = [&](const String &msg) { m_warnings.push_back(label + ": " + msg); };

	// Syntaxe (règles, règles-@, blocs de déclarations, commentaires) :
	// dataio::CssDocument. Ce module n'ajoute que ce que dataio ne connaît
	// pas : sélecteurs, valeurs typées des propriétés, imports et polices.
	dataio::CssDocument doc;
	if (auto error = doc.DecodeStr(String(source)); error.IsSome()) {
		warn(error.Value().Format());
		++m_revision;
		return;
	}
	const dataio::NodePtr root = doc.GetRoot();
	if (!root)
		return;
	const ValueContext ctx{baseDir};

	auto valueOf = [](const dataio::NodePtr &node) { return dataio::scalar::ToString(node); };

	for (size_t r = 0; r < root->GetSize(); ++r) {
		const dataio::NodePtr rule = root->At(r);
		if (!rule || !rule->IsObject())
			continue;
		if (rule->Has("at")) {
			const String at = Lower(rule->Get("at")->stringValue.View());
			const String params = rule->Has("params") ? rule->Get("params")->stringValue : String();
			if (at == "@import") {
				// Paramètres facultatifs après l'URL (media) : ignorés.
				StringView target = params.View();
				if (const size_t sp = target.Find(' '); sp != StringView::NPOS && !target.StartsWith("url("))
					target = target.Substr(0, sp);
				const String path = JoinPath(baseDir, UrlValue(target));
				if (state.depth > 16 || state.visited.count(path)) {
					warn(String("import cyclique ignoré : ") + path);
					continue;
				}
				state.visited.insert(path);
				auto bytes = sdl3::ReadFile(path);
				if (bytes.IsError()) {
					warn(String("import introuvable : ") + path);
					continue;
				}
				++state.depth;
				const std::vector<uint8_t> &data = bytes.Value();
				ParseInto(StringView(reinterpret_cast<const char *>(data.data()), data.size()), DirectoryOf(path), state);
				--state.depth;
			} else if (at == "@font-face" && rule->Has("declarations")) {
				const dataio::NodePtr decls = rule->Get("declarations");
				FontFace face;
				for (const String &key : decls->Keys()) {
					const String name = Lower(key.View());
					const String value = valueOf(decls->Get(key));
					if (name == "font-family") {
						face.family = Unquote(value.View());
					} else if (name == "src") {
						// Première source url(...) ; format(...) ignoré.
						StringView v = value.View();
						if (const size_t comma = v.Find(','); comma != StringView::NPOS)
							v = v.Substr(0, comma);
						if (const size_t close = v.Find(')'); v.StartsWith("url(") && close != StringView::NPOS)
							v = v.Substr(0, close + 1);
						face.source = JoinPath(baseDir, UrlValue(v));
					} else if (name == "font-weight") {
						StyleProps w;
						if (SetDeclaration(w, "font-weight", value.View()) && w.fontWeight.IsSome())
							face.weight = w.fontWeight.Unwrap();
					} else if (name == "font-style") {
						face.style = (Lower(value.View()).Contains("italic") || Lower(value.View()).Contains("oblique"))
										 ? FontStyle::Italic
										 : FontStyle::Normal;
					}
				}
				if (face.family.IsEmpty() || face.source.IsEmpty())
					warn(String("@font-face sans font-family ou src"));
				else
					m_fonts.push_back(std::move(face));
			}
			// @media, @supports, @keyframes… : ignorés (aucune règle appliquée).
			continue;
		}
		if (!rule->Has("selector"))
			continue;
		const String selectorText = rule->Get("selector")->stringValue;
		std::vector<Selector> selectors;
		{
			size_t start = 0;
			int paren = 0;
			const StringView sv = selectorText.View();
			for (size_t k = 0; k <= sv.GetSize(); ++k) {
				if (k < sv.GetSize()) {
					if (sv[k] == '(')
						++paren;
					else if (sv[k] == ')')
						--paren;
					if (sv[k] != ',' || paren > 0)
						continue;
				}
				auto parsed = ParseSelector(sv.Substr(start, k - start));
				if (parsed.IsOk())
					selectors.push_back(std::move(parsed.Value()));
				else
					warn(String("sélecteur ignoré « ") + String(Trim(sv.Substr(start, k - start))) + " » : " +
						 parsed.Error());
				start = k + 1;
			}
		}
		if (selectors.empty())
			continue;
		StyleRule styleRule;
		styleRule.selectors = std::move(selectors);
		if (const dataio::NodePtr decls = rule->Get("declarations"); decls)
			for (const String &key : decls->Keys()) {
				const String value = valueOf(decls->Get(key));
				if (!SetDeclaration(styleRule.props, key.View(), value.View(), ctx))
					warn(String("déclaration ignorée dans « ") + selectorText + " » : " + key + ": " + value);
			}
		styleRule.order = m_rules.size();
		m_rules.push_back(std::move(styleRule));
	}
	++m_revision;
}

} // namespace ui
