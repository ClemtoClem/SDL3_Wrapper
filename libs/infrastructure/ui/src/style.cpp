// Définitions de ui/style.hpp
#include <ui/style.hpp>

#include <cctype>
#include <cmath>
#include <cstdlib>

namespace ui {

// ============================================================================
// Registre des propriétés
// ============================================================================

namespace {

constexpr PropInfo K_PROPS[] = {
#define UI_X(T, member, css, inherited, def) {css, inherited},
	UI_STYLE_PROPERTIES(UI_X)
#undef UI_X
};

static_assert(sizeof(K_PROPS) / sizeof(K_PROPS[0]) == PROP_COUNT);

} // namespace

const PropInfo &InfoOf(Prop p) noexcept { return K_PROPS[size_t(p)]; }

Option<Prop> PropFromName(StringView cssName) noexcept {
	for (size_t i = 0; i < PROP_COUNT; ++i)
		if (StringView(K_PROPS[i].name) == cssName)
			return Some(Prop(i));
	return NONE;
}

// ============================================================================
// StyleProps
// ============================================================================

void StyleProps::Merge(const StyleProps &over) {
#define UI_X(T, member, css, inherited, def)                                                                         \
	if (over.member.IsSome())                                                                                        \
		member = over.member;
	UI_STYLE_PROPERTIES(UI_X)
#undef UI_X
	for (const CustomProperty &c : over.customs)
		SetCustom(c.name, c.value);
	for (const DeferredDeclaration &d : over.deferred)
		deferred.push_back(d);
}

void StyleProps::MergeInherited(const StyleProps &over) {
#define UI_X(T, member, css, inherited, def)                                                                         \
	if (inherited && over.member.IsSome())                                                                           \
		member = over.member;
	UI_STYLE_PROPERTIES(UI_X)
#undef UI_X
	for (const CustomProperty &c : over.customs)
		SetCustom(c.name, c.value);
}

void StyleProps::SetMargin(const math::Sides &s) {
	marginTop = Some(s.top);
	marginRight = Some(s.right);
	marginBottom = Some(s.bottom);
	marginLeft = Some(s.left);
}

void StyleProps::SetPadding(const math::Sides &s) {
	paddingTop = Some(s.top);
	paddingRight = Some(s.right);
	paddingBottom = Some(s.bottom);
	paddingLeft = Some(s.left);
}

void StyleProps::SetBorderWidth(const math::Sides &s) {
	borderTopWidth = Some(s.top);
	borderRightWidth = Some(s.right);
	borderBottomWidth = Some(s.bottom);
	borderLeftWidth = Some(s.left);
}

void StyleProps::SetBorderRadius(const math::Corners &c) {
	borderTopLeftRadius = Some(c.tl);
	borderTopRightRadius = Some(c.tr);
	borderBottomRightRadius = Some(c.br);
	borderBottomLeftRadius = Some(c.bl);
}

bool StyleProps::Has(Prop p) const noexcept {
	switch (p) {
#define UI_X(T, member, css, inherited, def)                                                                         \
	case Prop::member:                                                                                               \
		return member.IsSome();
		UI_STYLE_PROPERTIES(UI_X)
#undef UI_X
	case Prop::Count:
		break;
	}
	return false;
}

void StyleProps::Clear(Prop p) noexcept {
	switch (p) {
#define UI_X(T, member, css, inherited, def)                                                                         \
	case Prop::member:                                                                                               \
		member = NONE;                                                                                               \
		break;
		UI_STYLE_PROPERTIES(UI_X)
#undef UI_X
	case Prop::Count:
		break;
	}
}

bool StyleProps::IsEmpty() const noexcept {
#define UI_X(T, member, css, inherited, def)                                                                         \
	if (member.IsSome())                                                                                             \
		return false;
	UI_STYLE_PROPERTIES(UI_X)
#undef UI_X
	return customs.empty() && deferred.empty();
}

Option<String> StyleProps::Custom(StringView name) const {
	for (const CustomProperty &c : customs)
		if (c.name.View() == name)
			return Some(c.value);
	return NONE;
}

void StyleProps::SetCustom(String name, String value) {
	for (CustomProperty &c : customs)
		if (c.name == name) {
			c.value = std::move(value);
			return;
		}
	customs.push_back({std::move(name), std::move(value)});
}

// ============================================================================
// ComputedStyle
// ============================================================================

const ComputedStyle &ComputedStyle::Initial() noexcept {
	static const ComputedStyle initial{};
	return initial;
}

void ComputedStyle::Apply(const StyleProps &props) {
#define UI_X(T, member, css, inherited, def)                                                                         \
	if (props.member.IsSome())                                                                                       \
		member = props.member.ValueUnchecked();
	UI_STYLE_PROPERTIES(UI_X)
#undef UI_X
}

// ============================================================================
// États
// ============================================================================

Option<State> StateFromName(StringView pseudo) noexcept {
	struct Entry {
		const char *name;
		State state;
	};
	static constexpr Entry K_STATES[] = {
		{"hover", State::Hover},		   {"active", State::Active},	  {"pressed", State::Active},
		{"focus", State::Focus},		   {"focus-visible", State::FocusVisible},
		{"focus-within", State::Focus},	   {"checked", State::Checked},	  {"disabled", State::Disabled},
		{"selected", State::Selected},	   {"open", State::Open},		  {"expanded", State::Checked},
		{"drag-over", State::DragOver},	   {"dragging", State::Dragging},
	};
	for (const Entry &e : K_STATES)
		if (StringView(e.name) == pseudo)
			return Some(e.state);
	return NONE;
}

// ============================================================================
// Analyse des valeurs
// ============================================================================

namespace {

String Lower(StringView s) {
	String out;
	out.Reserve(s.GetSize());
	for (size_t i = 0; i < s.GetSize(); ++i)
		out.PushBack(char(std::tolower(static_cast<unsigned char>(s[i]))));
	return out;
}

bool IsSpace(char c) noexcept { return c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\f'; }

StringView Trim(StringView s) noexcept {
	size_t b = 0, e = s.GetSize();
	while (b < e && IsSpace(s[b]))
		++b;
	while (e > b && IsSpace(s[e - 1]))
		--e;
	return s.Substr(b, e - b);
}

/// Découpe sur les espaces (ou sur `sep`), en respectant parenthèses et
/// guillemets : `linear-gradient(to right, #fff, #000) 4px` = 2 morceaux.
std::vector<StringView> SplitTokens(StringView s, char sep = ' ') {
	std::vector<StringView> out;
	int depth = 0;
	char quote = 0;
	size_t start = 0;
	auto push = [&](size_t end) {
		StringView t = Trim(s.Substr(start, end - start));
		if (!t.IsEmpty())
			out.push_back(t);
	};
	for (size_t i = 0; i < s.GetSize(); ++i) {
		const char c = s[i];
		if (quote) {
			if (c == quote)
				quote = 0;
			continue;
		}
		if (c == '"' || c == '\'') {
			quote = c;
		} else if (c == '(') {
			++depth;
		} else if (c == ')') {
			--depth;
		} else if (depth == 0 && (sep == ' ' ? IsSpace(c) : c == sep)) {
			push(i);
			start = i + 1;
		}
	}
	push(s.GetSize());
	return out;
}

/// Lit un nombre au début de `s` ; `rest` reçoit le suffixe (unité).
Option<double> LeadingNumber(StringView s, StringView &rest) noexcept {
	char buf[64];
	const size_t n = math::Min(s.GetSize(), sizeof(buf) - 1);
	for (size_t i = 0; i < n; ++i)
		buf[i] = s[i];
	buf[n] = '\0';
	char *end = nullptr;
	const double v = std::strtod(buf, &end);
	if (end == buf)
		return NONE;
	rest = s.Substr(size_t(end - buf));
	return Some(v);
}

Option<float> Number(StringView s) noexcept {
	StringView rest;
	auto v = LeadingNumber(Trim(s), rest);
	if (v.IsNone() || !Trim(rest).IsEmpty())
		return NONE;
	return Some(float(v.Unwrap()));
}

/// Contenu d'un appel de fonction `nom(...)`, NONE si `s` n'en est pas un.
Option<StringView> FunctionArgs(StringView s, StringView name) noexcept {
	s = Trim(s);
	if (s.GetSize() < name.GetSize() + 2 || !s.StartsWith(name) || s[name.GetSize()] != '(' || s.Back() != ')')
		return NONE;
	return Some(s.Substr(name.GetSize() + 1, s.GetSize() - name.GetSize() - 2));
}

StringView Unquote(StringView s) noexcept {
	s = Trim(s);
	if (s.GetSize() >= 2 && (s[0] == '"' || s[0] == '\'') && s.Back() == s[0])
		return s.Substr(1, s.GetSize() - 2);
	return s;
}

// ── Énumérations ────────────────────────────────────────────────────────────

template <typename E> struct Keyword {
	const char *name;
	E value;
};

template <typename E, size_t N> Option<E> FindKeyword(const Keyword<E> (&table)[N], StringView text) {
	const String key = Lower(Trim(text));
	for (const Keyword<E> &k : table)
		if (key.View() == StringView(k.name))
			return Some(k.value);
	return NONE;
}

constexpr Keyword<Display> K_DISPLAY[] = {{"flex", Display::Flex},		   {"block", Display::Flex},
										  {"inline-flex", Display::Flex},  {"inline", Display::Flex},
										  {"inline-block", Display::Flex}, {"grid", Display::Grid},
										  {"stack", Display::Stack},	   {"none", Display::None}};
constexpr Keyword<Position> K_POSITION[] = {{"relative", Position::Relative},
											{"static", Position::Relative},
											{"absolute", Position::Absolute},
											{"fixed", Position::Fixed}};
constexpr Keyword<Anchor> K_ANCHOR[] = {{"top-left", Anchor::TopLeft},		 {"top", Anchor::Top},
										{"top-right", Anchor::TopRight},	 {"left", Anchor::Left},
										{"center", Anchor::Center},			 {"right", Anchor::Right},
										{"bottom-left", Anchor::BottomLeft}, {"bottom", Anchor::Bottom},
										{"bottom-right", Anchor::BottomRight}};
constexpr Keyword<FlexDirection> K_DIRECTION[] = {{"row", FlexDirection::Row},
												  {"column", FlexDirection::Column},
												  {"row-reverse", FlexDirection::RowReverse},
												  {"column-reverse", FlexDirection::ColumnReverse}};
constexpr Keyword<FlexWrap> K_WRAP[] = {{"nowrap", FlexWrap::NoWrap}, {"wrap", FlexWrap::Wrap}};
constexpr Keyword<Justify> K_JUSTIFY[] = {
	{"flex-start", Justify::Start},		 {"start", Justify::Start},			  {"left", Justify::Start},
	{"center", Justify::Center},		 {"flex-end", Justify::End},		  {"end", Justify::End},
	{"right", Justify::End},			 {"space-between", Justify::SpaceBetween},
	{"space-around", Justify::SpaceAround}, {"space-evenly", Justify::SpaceEvenly}};
constexpr Keyword<Align> K_ALIGN[] = {{"auto", Align::Auto},	  {"flex-start", Align::Start}, {"start", Align::Start},
									  {"center", Align::Center},  {"flex-end", Align::End},		{"end", Align::End},
									  {"stretch", Align::Stretch}, {"baseline", Align::Start},	{"normal", Align::Stretch}};
constexpr Keyword<Overflow> K_OVERFLOW[] = {{"visible", Overflow::Visible},
											{"hidden", Overflow::Hidden},
											{"clip", Overflow::Hidden},
											{"scroll", Overflow::Scroll},
											{"auto", Overflow::Auto}};
constexpr Keyword<ObjectFit> K_FIT[] = {{"fill", ObjectFit::Fill},
										{"contain", ObjectFit::Contain},
										{"cover", ObjectFit::Cover},
										{"none", ObjectFit::None},
										{"scale-down", ObjectFit::ScaleDown}};
constexpr Keyword<Visibility> K_VISIBILITY[] = {
	{"visible", Visibility::Visible}, {"hidden", Visibility::Hidden}, {"collapse", Visibility::Hidden}};
constexpr Keyword<PointerEvents> K_POINTER[] = {{"auto", PointerEvents::Auto}, {"none", PointerEvents::None}};
constexpr Keyword<Cursor> K_CURSOR[] = {
	{"default", Cursor::DEFAULT},		 {"auto", Cursor::DEFAULT},			  {"pointer", Cursor::POINTER},
	{"text", Cursor::TEXT},				 {"move", Cursor::MOVE},			  {"ew-resize", Cursor::SIZE_EW},
	{"col-resize", Cursor::SIZE_EW},	 {"ns-resize", Cursor::SIZE_NS},	  {"row-resize", Cursor::SIZE_NS},
	{"nwse-resize", Cursor::SIZE_NWSE},	 {"nesw-resize", Cursor::SIZE_NESW},  {"not-allowed", Cursor::NOT_ALLOWED},
	{"wait", Cursor::WAIT},				 {"progress", Cursor::PROGRESS},	  {"crosshair", Cursor::CROSSHAIR},
	{"grab", Cursor::MOVE},				 {"grabbing", Cursor::MOVE},		  {"se-resize", Cursor::SE_RESIZE},
	{"n-resize", Cursor::N_RESIZE},		 {"s-resize", Cursor::S_RESIZE},	  {"e-resize", Cursor::E_RESIZE},
	{"w-resize", Cursor::W_RESIZE}};
constexpr Keyword<TextAlign> K_TEXT_ALIGN[] = {{"left", TextAlign::Left},	  {"start", TextAlign::Left},
											   {"center", TextAlign::Center}, {"right", TextAlign::Right},
											   {"end", TextAlign::Right},	  {"justify", TextAlign::Justify}};
constexpr Keyword<VerticalAlign> K_VALIGN[] = {{"top", VerticalAlign::Top},
											   {"middle", VerticalAlign::Middle},
											   {"center", VerticalAlign::Middle},
											   {"bottom", VerticalAlign::Bottom}};
constexpr Keyword<FontStyle> K_FONT_STYLE[] = {
	{"normal", FontStyle::Normal}, {"italic", FontStyle::Italic}, {"oblique", FontStyle::Italic}};
constexpr Keyword<TextOverflow> K_TEXT_OVERFLOW[] = {{"clip", TextOverflow::Clip},
													 {"ellipsis", TextOverflow::Ellipsis},
													 {"scroll", TextOverflow::Scroll},
													 {"wrap", TextOverflow::Wrap},
													 {"marquee", TextOverflow::Marquee}};

// ── Couleurs nommées ────────────────────────────────────────────────────────

struct NamedColor {
	const char *name;
	uint32_t rgba;
};

constexpr NamedColor K_NAMED_COLORS[] = {
	{"transparent", 0x00000000u}, {"black", 0x000000FFu},	 {"white", 0xFFFFFFFFu},	 {"red", 0xFF0000FFu},
	{"green", 0x008000FFu},		  {"lime", 0x00FF00FFu},	 {"blue", 0x0000FFFFu},		 {"yellow", 0xFFFF00FFu},
	{"cyan", 0x00FFFFFFu},		  {"aqua", 0x00FFFFFFu},	 {"magenta", 0xFF00FFFFu},	 {"fuchsia", 0xFF00FFFFu},
	{"gray", 0x808080FFu},		  {"grey", 0x808080FFu},	 {"silver", 0xC0C0C0FFu},	 {"maroon", 0x800000FFu},
	{"olive", 0x808000FFu},		  {"navy", 0x000080FFu},	 {"purple", 0x800080FFu},	 {"teal", 0x008080FFu},
	{"orange", 0xFFA500FFu},	  {"pink", 0xFFC0CBFFu},	 {"brown", 0xA52A2AFFu},	 {"gold", 0xFFD700FFu},
	{"darkgray", 0xA9A9A9FFu},	  {"darkgrey", 0xA9A9A9FFu}, {"lightgray", 0xD3D3D3FFu}, {"lightgrey", 0xD3D3D3FFu},
	{"dimgray", 0x696969FFu},	  {"crimson", 0xDC143CFFu},	 {"coral", 0xFF7F50FFu},	 {"tomato", 0xFF6347FFu},
	{"skyblue", 0x87CEEBFFu},	  {"steelblue", 0x4682B4FFu}, {"royalblue", 0x4169E1FFu}, {"dodgerblue", 0x1E90FFFFu},
	{"seagreen", 0x2E8B57FFu},	  {"forestgreen", 0x228B22FFu}, {"limegreen", 0x32CD32FFu},
	{"indigo", 0x4B0082FFu},	  {"violet", 0xEE82EEFFu},	 {"orchid", 0xDA70D6FFu},	 {"salmon", 0xFA8072FFu},
	{"khaki", 0xF0E68CFFu},		  {"beige", 0xF5F5DCFFu},	 {"ivory", 0xFFFFF0FFu},	 {"wheat", 0xF5DEB3FFu},
	{"slategray", 0x708090FFu},	  {"darkslategray", 0x2F4F4FFFu}, {"midnightblue", 0x191970FFu},
	{"darkred", 0x8B0000FFu},	  {"darkgreen", 0x006400FFu}, {"darkblue", 0x00008BFFu},  {"darkorange", 0xFF8C00FFu},
};

Option<float> ColorChannel(StringView s, float scale) noexcept {
	s = Trim(s);
	if (!s.IsEmpty() && s.Back() == '%') {
		auto v = Number(s.Substr(0, s.GetSize() - 1));
		if (v.IsNone())
			return NONE;
		return Some(math::Clamp(v.Unwrap() / 100.f, 0.f, 1.f));
	}
	auto v = Number(s);
	if (v.IsNone())
		return NONE;
	return Some(math::Clamp(v.Unwrap() / scale, 0.f, 1.f));
}

/// Arguments d'une fonction de couleur : séparés par des virgules OU par des
/// espaces (`rgb(10 20 30 / 50%)`).
std::vector<StringView> ColorArgs(StringView args) {
	std::vector<StringView> parts = SplitTokens(args, ',');
	if (parts.size() == 1) {
		parts.clear();
		for (StringView t : SplitTokens(args, ' '))
			if (t != StringView("/"))
				parts.push_back(t);
	}
	return parts;
}

} // namespace

Option<Color> ParseColor(StringView text) noexcept {
	text = Trim(text);
	if (text.IsEmpty())
		return NONE;
	if (text[0] == '#')
		return ParseHexColor(text);
	auto rgbArgs = FunctionArgs(text, "rgba");
	if (rgbArgs.IsNone())
		rgbArgs = FunctionArgs(text, "rgb");
	if (rgbArgs.IsSome()) {
		const std::vector<StringView> a = ColorArgs(rgbArgs.Unwrap());
		if (a.size() < 3)
			return NONE;
		auto r = ColorChannel(a[0], 255.f), g = ColorChannel(a[1], 255.f), b = ColorChannel(a[2], 255.f);
		auto al = a.size() > 3 ? ColorChannel(a[3], 1.f) : Option<float>(Some(1.f));
		if (r.IsNone() || g.IsNone() || b.IsNone() || al.IsNone())
			return NONE;
		return Some(Color{r.Unwrap(), g.Unwrap(), b.Unwrap(), al.Unwrap()});
	}
	auto hslArgs = FunctionArgs(text, "hsla");
	if (hslArgs.IsNone())
		hslArgs = FunctionArgs(text, "hsl");
	if (hslArgs.IsSome()) {
		const std::vector<StringView> a = ColorArgs(hslArgs.Unwrap());
		if (a.size() < 3)
			return NONE;
		StringView rest;
		auto h = LeadingNumber(a[0], rest);
		auto s = ColorChannel(a[1], 100.f), l = ColorChannel(a[2], 100.f);
		auto al = a.size() > 3 ? ColorChannel(a[3], 1.f) : Option<float>(Some(1.f));
		if (h.IsNone() || s.IsNone() || l.IsNone() || al.IsNone())
			return NONE;
		// HSL -> HSV
		const float L = l.Unwrap(), S = s.Unwrap();
		const float v = L + S * math::Min(L, 1.f - L);
		const float sv = v > 0.f ? 2.f * (1.f - L / v) : 0.f;
		return Some(HsvToColor(float(h.Unwrap()), sv, v, al.Unwrap()));
	}
	const String key = Lower(text);
	for (const NamedColor &nc : K_NAMED_COLORS)
		if (key.View() == StringView(nc.name))
			return Some(Rgba(int((nc.rgba >> 24) & 0xFF), int((nc.rgba >> 16) & 0xFF), int((nc.rgba >> 8) & 0xFF),
							 int(nc.rgba & 0xFF)));
	return NONE;
}

Option<Length> ParseLength(StringView text) noexcept {
	text = Trim(text);
	if (text.IsEmpty())
		return NONE;
	const String key = Lower(text);
	if (key == "auto" || key == "none" || key == "initial")
		return Some(Length::Auto());
	if (auto calc = FunctionArgs(text, "calc"); calc.IsSome()) {
		// calc(A + B) / calc(A - B) : une longueur relative et un décalage en px.
		std::vector<StringView> parts = SplitTokens(calc.Unwrap(), ' ');
		if (parts.size() == 1)
			return ParseLength(parts[0]);
		if (parts.size() != 3 || (parts[1] != StringView("+") && parts[1] != StringView("-")))
			return NONE;
		auto a = ParseLength(parts[0]);
		auto b = ParseLength(parts[2]);
		if (a.IsNone() || b.IsNone())
			return NONE;
		const float sign = parts[1] == StringView("-") ? -1.f : 1.f;
		Length base = a.Unwrap();
		const Length off = b.Unwrap();
		if (base.unit == Unit::Px && off.unit != Unit::Px) {
			// calc(8px + 50%) : on garde l'unité relative comme base.
			Length rel = off;
			rel.value *= sign;
			return Some(rel.Plus(base.value + base.offset));
		}
		if (off.unit != Unit::Px)
			return NONE;
		return Some(base.Plus(sign * (off.value + off.offset)));
	}
	StringView unit;
	auto v = LeadingNumber(text, unit);
	if (v.IsNone())
		return NONE;
	const float f = float(v.Unwrap());
	const String u = Lower(Trim(unit));
	if (u.IsEmpty() || u == "px")
		return Some(Length::Px(f));
	if (u == "%")
		return Some(Length::Pct(f));
	if (u == "em")
		return Some(Length::Em(f));
	if (u == "rem")
		return Some(Length::Rem(f));
	if (u == "vw")
		return Some(Length::Vw(f));
	if (u == "vh")
		return Some(Length::Vh(f));
	if (u == "fr")
		return Some(Length::Fr(f));
	if (u == "pt")
		return Some(Length::Px(f * 4.f / 3.f));
	return NONE;
}

Option<float> ParsePixels(StringView text) noexcept {
	auto l = ParseLength(text);
	if (l.IsNone())
		return NONE;
	const Length len = l.Unwrap();
	if (len.unit == Unit::Px)
		return Some(len.value + len.offset);
	if (len.unit == Unit::Em || len.unit == Unit::Rem)
		return Some(len.value * 14.f);
	return NONE;
}

namespace {

// ── Analyse par type ────────────────────────────────────────────────────────

Option<Paint> ParsePaint(StringView text, const ValueContext &ctx) {
	text = Trim(text);
	const String key = Lower(text);
	if (key == "none")
		return Some(Paint());
	if (key == "checkerboard" || key == "checker")
		return Some(Paint::Checkerboard());
	if (auto url = FunctionArgs(text, "url"); url.IsSome()) {
		String path(Unquote(url.Unwrap()));
		if (!ctx.baseDir.IsEmpty() && !path.StartsWith("/") && !path.Contains(':'))
			path = ctx.baseDir + "/" + path;
		return Some(Paint::FromImage(std::move(path)));
	}
	auto directionAngle = [](StringView d, float &angle) -> bool {
		const String k = Lower(Trim(d));
		if (k == "to bottom")
			angle = 180.f;
		else if (k == "to top")
			angle = 0.f;
		else if (k == "to right")
			angle = 90.f;
		else if (k == "to left")
			angle = 270.f;
		else if (k.EndsWith("deg")) {
			auto n = Number(k.View().Substr(0, k.GetSize() - 3));
			if (n.IsNone())
				return false;
			angle = n.Unwrap();
		} else
			return false;
		return true;
	};
	if (auto args = FunctionArgs(text, "linear-gradient"); args.IsSome()) {
		std::vector<StringView> parts = SplitTokens(args.Unwrap(), ',');
		Paint p;
		p.kind = PaintKind::Linear;
		size_t first = 0;
		if (!parts.empty() && directionAngle(parts[0], p.angle))
			first = 1;
		std::vector<Color> stops;
		for (size_t i = first; i < parts.size(); ++i) {
			// « #fff 30% » : seule la couleur compte (deux arrêts aux extrémités).
			std::vector<StringView> stop = SplitTokens(parts[i], ' ');
			if (stop.empty())
				continue;
			auto c = ParseColor(stop[0]);
			if (c.IsNone())
				return NONE;
			stops.push_back(c.Unwrap());
		}
		if (stops.size() < 2)
			return NONE;
		p.colors[0] = stops.front();
		p.colors[1] = stops.back();
		return Some(p);
	}
	if (auto args = FunctionArgs(text, "hue-gradient"); args.IsSome()) {
		Paint p = Paint::HueSpectrum();
		if (!Trim(args.Unwrap()).IsEmpty() && !directionAngle(args.Unwrap(), p.angle))
			return NONE;
		return Some(p);
	}
	if (auto args = FunctionArgs(text, "corner-gradient"); args.IsSome()) {
		std::vector<StringView> parts = SplitTokens(args.Unwrap(), ',');
		if (parts.size() != 4)
			return NONE;
		Paint p;
		p.kind = PaintKind::Bilinear;
		for (size_t i = 0; i < 4; ++i) {
			auto c = ParseColor(parts[i]);
			if (c.IsNone())
				return NONE;
			p.colors[i] = c.Unwrap();
		}
		return Some(p);
	}
	return NONE;
}

Option<Shadow> ParseShadow(StringView text) {
	if (Lower(Trim(text)) == "none")
		return Some(Shadow());
	Shadow s;
	std::vector<float> lengths;
	bool hasColor = false;
	for (StringView t : SplitTokens(text, ' ')) {
		if (auto px = ParsePixels(t); px.IsSome()) {
			lengths.push_back(px.Unwrap());
			continue;
		}
		auto c = ParseColor(t);
		if (c.IsNone())
			return NONE;
		s.color = c.Unwrap();
		hasColor = true;
	}
	if (lengths.size() < 2)
		return NONE;
	s.x = lengths[0];
	s.y = lengths[1];
	if (lengths.size() > 2)
		s.blur = lengths[2];
	if (lengths.size() > 3)
		s.spread = lengths[3];
	if (!hasColor)
		s.color = Color{0.f, 0.f, 0.f, 0.5f};
	return Some(s);
}

Option<float> ParseSeconds(StringView text) noexcept {
	StringView unit;
	auto v = LeadingNumber(Trim(text), unit);
	if (v.IsNone())
		return NONE;
	const String u = Lower(Trim(unit));
	if (u == "ms")
		return Some(float(v.Unwrap()) / 1000.f);
	if (u.IsEmpty() || u == "s")
		return Some(float(v.Unwrap()));
	return NONE;
}

Option<float> ParseFraction(StringView text) noexcept {
	text = Trim(text);
	if (!text.IsEmpty() && text.Back() == '%') {
		auto v = Number(text.Substr(0, text.GetSize() - 1));
		if (v.IsNone())
			return NONE;
		return Some(v.Unwrap() / 100.f);
	}
	return Number(text);
}

Option<int> ParseFontWeight(StringView text) noexcept {
	const String k = Lower(Trim(text));
	if (k == "normal")
		return Some(400);
	if (k == "bold" || k == "bolder")
		return Some(700);
	if (k == "lighter")
		return Some(300);
	auto n = Number(k.View());
	if (n.IsNone())
		return NONE;
	return Some(int(n.Unwrap()));
}

Option<TextDecoration> ParseDecoration(StringView text) {
	TextDecoration d = TextDecoration::None;
	for (StringView t : SplitTokens(text, ' ')) {
		const String k = Lower(t);
		if (k == "none")
			d = TextDecoration::None;
		else if (k == "underline")
			d = d | TextDecoration::Underline;
		else if (k == "line-through" || k == "strikethrough")
			d = d | TextDecoration::LineThrough;
		else if (k == "overline")
			d = d | TextDecoration::Overline;
		else if (ParseColor(t).IsSome() || k == "solid" || k == "wavy" || k == "dotted" || k == "dashed")
			continue;
		else
			return NONE;
	}
	return Some(d);
}

/// Valeurs 1 à 4 (haut, droite, bas, gauche — ordre CSS) en pixels.
bool ParseBox(StringView text, float out[4]) {
	std::vector<StringView> parts = SplitTokens(text, ' ');
	if (parts.empty() || parts.size() > 4)
		return false;
	float v[4];
	for (size_t i = 0; i < parts.size(); ++i) {
		auto px = ParsePixels(parts[i]);
		if (px.IsNone()) {
			if (Lower(parts[i]) != "auto")
				return false;
			v[i] = 0.f;
		} else
			v[i] = px.Unwrap();
	}
	switch (parts.size()) {
	case 1:
		out[0] = out[1] = out[2] = out[3] = v[0];
		break;
	case 2:
		out[0] = out[2] = v[0];
		out[1] = out[3] = v[1];
		break;
	case 3:
		out[0] = v[0];
		out[1] = out[3] = v[1];
		out[2] = v[2];
		break;
	default:
		out[0] = v[0];
		out[1] = v[1];
		out[2] = v[2];
		out[3] = v[3];
		break;
	}
	return true;
}

/// Analyse générique d'une valeur pour la propriété `p`.
bool ParseInto(StyleProps &s, Prop p, StringView value, const ValueContext &ctx) {
	switch (p) {
	case Prop::display:
		return (s.display = FindKeyword(K_DISPLAY, value)).IsSome();
	case Prop::position:
		return (s.position = FindKeyword(K_POSITION, value)).IsSome();
	case Prop::anchor:
		return (s.anchor = FindKeyword(K_ANCHOR, value)).IsSome();
	case Prop::top:
		return (s.top = ParseLength(value)).IsSome();
	case Prop::right:
		return (s.right = ParseLength(value)).IsSome();
	case Prop::bottom:
		return (s.bottom = ParseLength(value)).IsSome();
	case Prop::left:
		return (s.left = ParseLength(value)).IsSome();
	case Prop::width:
		return (s.width = ParseLength(value)).IsSome();
	case Prop::height:
		return (s.height = ParseLength(value)).IsSome();
	case Prop::minWidth:
		return (s.minWidth = ParseLength(value)).IsSome();
	case Prop::minHeight:
		return (s.minHeight = ParseLength(value)).IsSome();
	case Prop::maxWidth:
		return (s.maxWidth = ParseLength(value)).IsSome();
	case Prop::maxHeight:
		return (s.maxHeight = ParseLength(value)).IsSome();
	case Prop::marginTop:
		return (s.marginTop = ParsePixels(value)).IsSome();
	case Prop::marginRight:
		return (s.marginRight = ParsePixels(value)).IsSome();
	case Prop::marginBottom:
		return (s.marginBottom = ParsePixels(value)).IsSome();
	case Prop::marginLeft:
		return (s.marginLeft = ParsePixels(value)).IsSome();
	case Prop::paddingTop:
		return (s.paddingTop = ParsePixels(value)).IsSome();
	case Prop::paddingRight:
		return (s.paddingRight = ParsePixels(value)).IsSome();
	case Prop::paddingBottom:
		return (s.paddingBottom = ParsePixels(value)).IsSome();
	case Prop::paddingLeft:
		return (s.paddingLeft = ParsePixels(value)).IsSome();
	case Prop::flexDirection:
		return (s.flexDirection = FindKeyword(K_DIRECTION, value)).IsSome();
	case Prop::flexWrap:
		return (s.flexWrap = FindKeyword(K_WRAP, value)).IsSome();
	case Prop::justifyContent:
		return (s.justifyContent = FindKeyword(K_JUSTIFY, value)).IsSome();
	case Prop::alignItems:
		return (s.alignItems = FindKeyword(K_ALIGN, value)).IsSome();
	case Prop::alignSelf:
		return (s.alignSelf = FindKeyword(K_ALIGN, value)).IsSome();
	case Prop::flexGrow:
		return (s.flexGrow = Number(value)).IsSome();
	case Prop::flexShrink:
		return (s.flexShrink = Number(value)).IsSome();
	case Prop::gap: {
		std::vector<StringView> parts = SplitTokens(value, ' ');
		return !parts.empty() && (s.gap = ParsePixels(parts[0])).IsSome();
	}
	case Prop::gridColumns: {
		auto n = Number(value);
		if (n.IsNone())
			return false;
		s.gridColumns = Some(math::Max(1, int(n.Unwrap())));
		return true;
	}
	case Prop::overflowX:
		return (s.overflowX = FindKeyword(K_OVERFLOW, value)).IsSome();
	case Prop::overflowY:
		return (s.overflowY = FindKeyword(K_OVERFLOW, value)).IsSome();
	case Prop::zIndex: {
		auto n = Number(value);
		if (n.IsNone())
			return Lower(Trim(value)) == "auto" && (s.zIndex = Some(0)).IsSome();
		s.zIndex = Some(int(n.Unwrap()));
		return true;
	}
	case Prop::zoom:
		return (s.zoom = ParseFraction(value)).IsSome();
	case Prop::backgroundColor:
		return (s.backgroundColor = ParseColor(value)).IsSome();
	case Prop::backgroundImage:
		return (s.backgroundImage = ParsePaint(value, ctx)).IsSome();
	case Prop::objectFit:
		return (s.objectFit = FindKeyword(K_FIT, value)).IsSome();
	case Prop::borderColor:
		return (s.borderColor = ParseColor(value)).IsSome();
	case Prop::borderTopWidth:
		return (s.borderTopWidth = ParsePixels(value)).IsSome();
	case Prop::borderRightWidth:
		return (s.borderRightWidth = ParsePixels(value)).IsSome();
	case Prop::borderBottomWidth:
		return (s.borderBottomWidth = ParsePixels(value)).IsSome();
	case Prop::borderLeftWidth:
		return (s.borderLeftWidth = ParsePixels(value)).IsSome();
	case Prop::borderTopLeftRadius:
		return (s.borderTopLeftRadius = ParsePixels(value)).IsSome();
	case Prop::borderTopRightRadius:
		return (s.borderTopRightRadius = ParsePixels(value)).IsSome();
	case Prop::borderBottomRightRadius:
		return (s.borderBottomRightRadius = ParsePixels(value)).IsSome();
	case Prop::borderBottomLeftRadius:
		return (s.borderBottomLeftRadius = ParsePixels(value)).IsSome();
	case Prop::boxShadow:
		return (s.boxShadow = ParseShadow(value)).IsSome();
	case Prop::opacity:
		return (s.opacity = ParseFraction(value)).IsSome();
	case Prop::outlineColor:
		return (s.outlineColor = ParseColor(value)).IsSome();
	case Prop::outlineWidth:
		return (s.outlineWidth = ParsePixels(value)).IsSome();
	case Prop::outlineOffset:
		return (s.outlineOffset = ParsePixels(value)).IsSome();
	case Prop::gloss:
		return (s.gloss = ParseFraction(value)).IsSome();
	case Prop::glow:
		return (s.glow = ParseFraction(value)).IsSome();
	case Prop::glowColor:
		return (s.glowColor = ParseColor(value)).IsSome();
	case Prop::visibility:
		return (s.visibility = FindKeyword(K_VISIBILITY, value)).IsSome();
	case Prop::pointerEvents:
		return (s.pointerEvents = FindKeyword(K_POINTER, value)).IsSome();
	case Prop::cursor:
		return (s.cursor = FindKeyword(K_CURSOR, value)).IsSome();
	case Prop::color:
		return (s.color = ParseColor(value)).IsSome();
	case Prop::fontFamily: {
		std::vector<StringView> families = SplitTokens(value, ',');
		if (families.empty())
			return false;
		s.fontFamily = Some(String(Unquote(families[0])));
		return true;
	}
	case Prop::fontSize: {
		auto l = ParseLength(value);
		if (l.IsNone())
			return false;
		const Length len = l.Unwrap();
		if (len.unit == Unit::Px)
			s.fontSize = Some(len.value + len.offset);
		else if (len.unit == Unit::Em || len.unit == Unit::Rem)
			s.fontSize = Some(len.value * 14.f);
		else
			return false;
		return true;
	}
	case Prop::fontWeight:
		return (s.fontWeight = ParseFontWeight(value)).IsSome();
	case Prop::fontStyle:
		return (s.fontStyle = FindKeyword(K_FONT_STYLE, value)).IsSome();
	case Prop::lineHeight: {
		if (Lower(Trim(value)) == "normal") {
			s.lineHeight = Some(0.f);
			return true;
		}
		return (s.lineHeight = ParseFraction(value)).IsSome();
	}
	case Prop::textAlign:
		return (s.textAlign = FindKeyword(K_TEXT_ALIGN, value)).IsSome();
	case Prop::verticalAlign:
		return (s.verticalAlign = FindKeyword(K_VALIGN, value)).IsSome();
	case Prop::textDecoration:
		return (s.textDecoration = ParseDecoration(value)).IsSome();
	case Prop::textDecorationColor:
		return (s.textDecorationColor = ParseColor(value)).IsSome();
	case Prop::textOverflow:
		return (s.textOverflow = FindKeyword(K_TEXT_OVERFLOW, value)).IsSome();
	case Prop::tabSize: {
		auto n = Number(value);
		if (n.IsNone())
			return false;
		s.tabSize = Some(math::Max(1, int(n.Unwrap())));
		return true;
	}
	case Prop::marqueeSpeed:
		return (s.marqueeSpeed = ParsePixels(value)).IsSome();
	case Prop::marqueePause:
		return (s.marqueePause = ParseSeconds(value)).IsSome();
	case Prop::accentColor:
		return (s.accentColor = ParseColor(value)).IsSome();
	case Prop::caretColor:
		return (s.caretColor = ParseColor(value)).IsSome();
	case Prop::placeholderColor:
		return (s.placeholderColor = ParseColor(value)).IsSome();
	case Prop::selectionColor:
		return (s.selectionColor = ParseColor(value)).IsSome();
	case Prop::scrollbarThumbColor:
		return (s.scrollbarThumbColor = ParseColor(value)).IsSome();
	case Prop::scrollbarTrackColor:
		return (s.scrollbarTrackColor = ParseColor(value)).IsSome();
	case Prop::scrollbarWidth: {
		const String k = Lower(Trim(value));
		if (k == "none") {
			s.scrollbarWidth = Some(0.f);
			return true;
		}
		if (k == "thin") {
			s.scrollbarWidth = Some(5.f);
			return true;
		}
		if (k == "auto") {
			s.scrollbarWidth = Some(8.f);
			return true;
		}
		return (s.scrollbarWidth = ParsePixels(value)).IsSome();
	}
	case Prop::transitionDuration:
		return (s.transitionDuration = ParseSeconds(value)).IsSome();
	case Prop::Count:
		break;
	}
	return false;
}

bool ParseShorthand(StyleProps &s, StringView name, StringView value, const ValueContext &ctx, bool &handled) {
	handled = true;
	float box[4];
	if (name == StringView("margin")) {
		if (!ParseBox(value, box))
			return false;
		s.SetMargin(math::Sides(box[3], box[0], box[1], box[2]));
		return true;
	}
	if (name == StringView("padding")) {
		if (!ParseBox(value, box))
			return false;
		s.SetPadding(math::Sides(box[3], box[0], box[1], box[2]));
		return true;
	}
	if (name == StringView("border-width")) {
		if (!ParseBox(value, box))
			return false;
		s.SetBorderWidth(math::Sides(box[3], box[0], box[1], box[2]));
		return true;
	}
	if (name == StringView("border-radius")) {
		// Ordre CSS : haut-gauche, haut-droit, bas-droit, bas-gauche.
		if (!ParseBox(value, box))
			return false;
		s.SetBorderRadius(math::Corners(box[0], box[1], box[3], box[2]));
		return true;
	}
	auto borderParts = [&](float &width, bool &hasWidth) -> bool {
		hasWidth = false;
		for (StringView t : SplitTokens(value, ' ')) {
			const String k = Lower(t);
			if (k == "none" || k == "hidden") {
				width = 0.f;
				hasWidth = true;
			} else if (k == "solid" || k == "dashed" || k == "dotted" || k == "double" || k == "groove" ||
					   k == "ridge" || k == "inset" || k == "outset") {
				continue;
			} else if (auto px = ParsePixels(t); px.IsSome()) {
				width = px.Unwrap();
				hasWidth = true;
			} else if (auto c = ParseColor(t); c.IsSome()) {
				s.borderColor = c;
			} else if (k == "thin") {
				width = 1.f;
				hasWidth = true;
			} else if (k == "medium") {
				width = 2.f;
				hasWidth = true;
			} else if (k == "thick") {
				width = 4.f;
				hasWidth = true;
			} else {
				return false;
			}
		}
		return true;
	};
	if (name == StringView("border")) {
		float w = 1.f;
		bool hasWidth = false;
		if (!borderParts(w, hasWidth))
			return false;
		s.SetBorderWidth(math::Sides(w));
		return true;
	}
	if (name == StringView("border-top") || name == StringView("border-right") || name == StringView("border-bottom") ||
		name == StringView("border-left")) {
		float w = 1.f;
		bool hasWidth = false;
		if (!borderParts(w, hasWidth))
			return false;
		if (name == StringView("border-top"))
			s.borderTopWidth = Some(w);
		else if (name == StringView("border-right"))
			s.borderRightWidth = Some(w);
		else if (name == StringView("border-bottom"))
			s.borderBottomWidth = Some(w);
		else
			s.borderLeftWidth = Some(w);
		return true;
	}
	if (name == StringView("outline")) {
		for (StringView t : SplitTokens(value, ' ')) {
			const String k = Lower(t);
			if (k == "none") {
				s.outlineWidth = Some(0.f);
			} else if (k == "solid" || k == "dashed" || k == "dotted" || k == "auto") {
				continue;
			} else if (auto px = ParsePixels(t); px.IsSome()) {
				s.outlineWidth = px;
			} else if (auto c = ParseColor(t); c.IsSome()) {
				s.outlineColor = c;
			} else {
				return false;
			}
		}
		return true;
	}
	if (name == StringView("overflow")) {
		std::vector<StringView> parts = SplitTokens(value, ' ');
		if (parts.empty() || parts.size() > 2)
			return false;
		auto x = FindKeyword(K_OVERFLOW, parts[0]);
		auto y = FindKeyword(K_OVERFLOW, parts.size() > 1 ? parts[1] : parts[0]);
		if (x.IsNone() || y.IsNone())
			return false;
		s.overflowX = x;
		s.overflowY = y;
		return true;
	}
	if (name == StringView("background")) {
		bool any = false;
		for (StringView t : SplitTokens(value, ' ')) {
			if (auto c = ParseColor(t); c.IsSome()) {
				s.backgroundColor = c;
				any = true;
			} else if (auto p = ParsePaint(t, ctx); p.IsSome()) {
				s.backgroundImage = p;
				any = true;
			} else if (Lower(t) == "none") {
				s.backgroundColor = Some(Color(0.f, 0.f, 0.f, 0.f));
				s.backgroundImage = Some(Paint());
				any = true;
			} else {
				return false;
			}
		}
		return any;
	}
	if (name == StringView("background-size")) {
		auto fit = FindKeyword(K_FIT, value);
		if (fit.IsNone())
			return false;
		s.objectFit = fit;
		return true;
	}
	if (name == StringView("flex")) {
		std::vector<StringView> parts = SplitTokens(value, ' ');
		if (parts.empty())
			return false;
		const String k = Lower(parts[0]);
		if (k == "none") {
			s.flexGrow = Some(0.f);
			s.flexShrink = Some(0.f);
			return true;
		}
		if (k == "auto") {
			s.flexGrow = Some(1.f);
			s.flexShrink = Some(1.f);
			return true;
		}
		auto g = Number(parts[0]);
		if (g.IsNone())
			return false;
		s.flexGrow = g;
		if (parts.size() > 1)
			if (auto sh = Number(parts[1]); sh.IsSome())
				s.flexShrink = sh;
		return true;
	}
	if (name == StringView("inset")) {
		std::vector<StringView> parts = SplitTokens(value, ' ');
		if (parts.empty() || parts.size() > 4)
			return false;
		Length v[4];
		for (size_t i = 0; i < parts.size(); ++i) {
			auto l = ParseLength(parts[i]);
			if (l.IsNone())
				return false;
			v[i] = l.Unwrap();
		}
		const size_t n = parts.size();
		s.top = Some(v[0]);
		s.right = Some(n > 1 ? v[1] : v[0]);
		s.bottom = Some(n > 2 ? v[2] : v[0]);
		s.left = Some(n > 3 ? v[3] : (n > 1 ? v[1] : v[0]));
		return true;
	}
	if (name == StringView("grid-template-columns")) {
		if (auto rep = FunctionArgs(value, "repeat"); rep.IsSome()) {
			std::vector<StringView> parts = SplitTokens(rep.Unwrap(), ',');
			if (parts.empty())
				return false;
			auto n = Number(parts[0]);
			if (n.IsNone())
				return false;
			s.gridColumns = Some(math::Max(1, int(n.Unwrap())));
			return true;
		}
		s.gridColumns = Some(math::Max(1, int(SplitTokens(value, ' ').size())));
		return true;
	}
	if (name == StringView("font")) {
		// font: [style] [weight] size family
		for (StringView t : SplitTokens(value, ' ')) {
			const String k = Lower(t);
			if (k == "italic" || k == "oblique")
				s.fontStyle = Some(FontStyle::Italic);
			else if (k == "normal")
				continue;
			else if (k == "bold" || k == "bolder" || k == "lighter")
				s.fontWeight = ParseFontWeight(t);
			else if (auto px = ParsePixels(t); px.IsSome())
				s.fontSize = px;
			else if (auto w = Number(t); w.IsSome())
				s.fontWeight = Some(int(w.Unwrap()));
			else
				s.fontFamily = Some(String(Unquote(t)));
		}
		return true;
	}
	if (name == StringView("white-space")) {
		const String k = Lower(Trim(value));
		if (k == "nowrap" || k == "pre")
			s.textOverflow = Some(TextOverflow::Clip);
		else if (k == "normal" || k == "pre-wrap" || k == "pre-line")
			s.textOverflow = Some(TextOverflow::Wrap);
		else
			return false;
		return true;
	}
	if (name == StringView("row-gap") || name == StringView("column-gap")) {
		return (s.gap = ParsePixels(value)).IsSome();
	}
	handled = false;
	return false;
}

} // namespace

bool SetDeclaration(StyleProps &props, StringView name, StringView value, const ValueContext &ctx) {
	name = Trim(name);
	value = Trim(value);
	// « !important » est accepté mais sans effet : la cascade du module a déjà
	// ses propres priorités (privé > partagé).
	if (value.EndsWith("!important"))
		value = Trim(value.Substr(0, value.GetSize() - 10));
	if (name.StartsWith("--")) {
		props.SetCustom(String(name.Substr(2)), String(value));
		return true;
	}
	if (value.Contains(StringView("var("))) {
		props.deferred.push_back({String(name), String(value)});
		return true;
	}
	const String lowerName = Lower(name);
	if (Lower(value) == "inherit" || Lower(value) == "initial" || Lower(value) == "unset")
		return false;
	bool handled = false;
	const bool ok = ParseShorthand(props, lowerName.View(), value, ctx, handled);
	if (handled)
		return ok;
	auto p = PropFromName(lowerName.View());
	if (p.IsNone())
		return false;
	return ParseInto(props, p.Unwrap(), value, ctx);
}

String SubstituteVars(StringView value, const std::vector<CustomProperty> &vars) {
	String out;
	size_t i = 0;
	int guard = 0;
	while (i < value.GetSize()) {
		const size_t at = value.Find(StringView("var("), i);
		if (at == StringView::NPOS) {
			out.Append(value.Substr(i));
			break;
		}
		out.Append(value.Substr(i, at - i));
		// Fin de l'appel (parenthèses imbriquées : var(--a, rgb(1,2,3))).
		size_t j = at + 4;
		int depth = 1;
		while (j < value.GetSize() && depth > 0) {
			if (value[j] == '(')
				++depth;
			else if (value[j] == ')')
				--depth;
			++j;
		}
		const StringView inner = value.Substr(at + 4, (j - 1) - (at + 4));
		const size_t comma = inner.Find(',');
		StringView varName = Trim(comma == StringView::NPOS ? inner : inner.Substr(0, comma));
		const StringView fallback = comma == StringView::NPOS ? StringView() : Trim(inner.Substr(comma + 1));
		if (varName.StartsWith("--"))
			varName = varName.Substr(2);
		bool found = false;
		for (const CustomProperty &c : vars)
			if (c.name.View() == varName) {
				// Une variable peut en référencer une autre (borné contre les cycles).
				out.Append(++guard < 16 ? SubstituteVars(c.value.View(), vars) : c.value);
				found = true;
				break;
			}
		if (!found)
			out.Append(SubstituteVars(fallback, vars));
		i = j;
	}
	return out;
}

String DescribeProperty(const ComputedStyle &style, Prop p) {
	auto color = [](Color c) { return ColorToHex(c); };
	auto length = [](const Length &l) -> String {
		switch (l.unit) {
		case Unit::Px:
			return String::From(l.value + l.offset, 1) + "px";
		case Unit::Pct:
			return String::From(l.value, 1) + "%";
		case Unit::Em:
			return String::From(l.value, 2) + "em";
		case Unit::Rem:
			return String::From(l.value, 2) + "rem";
		case Unit::Vw:
			return String::From(l.value, 1) + "vw";
		case Unit::Vh:
			return String::From(l.value, 1) + "vh";
		case Unit::Fr:
			return String::From(l.value, 1) + "fr";
		case Unit::Auto:
			break;
		}
		return String("auto");
	};
	switch (p) {
	case Prop::color:
		return color(style.color);
	case Prop::backgroundColor:
		return color(style.backgroundColor);
	case Prop::borderColor:
		return color(style.borderColor);
	case Prop::accentColor:
		return color(style.accentColor);
	case Prop::width:
		return length(style.width);
	case Prop::height:
		return length(style.height);
	case Prop::fontSize:
		return String::From(style.fontSize, 1) + "px";
	case Prop::fontFamily:
		return style.fontFamily;
	case Prop::fontWeight:
		return String::From(style.fontWeight);
	case Prop::opacity:
		return String::From(style.opacity, 2);
	case Prop::gap:
		return String::From(style.gap, 1) + "px";
	default:
		break;
	}
	return String();
}

} // namespace ui
