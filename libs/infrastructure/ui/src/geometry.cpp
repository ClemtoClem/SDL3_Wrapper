// Définitions de ui/geometry.hpp
#include <ui/geometry.hpp>

#include <algorithm>

namespace ui {

ScrollbarGeometry ScrollbarsOf(const sdl3::FRect &box, const math::Sides &border, const Scroll &s, float thickness) {
	ScrollbarGeometry g;
	const sdl3::FRect inner{box.x + border.left, box.y + border.top, std::max(0.f, box.w - border.H()),
							std::max(0.f, box.h - border.V())};
	constexpr float MIN_THUMB = 20.f;
	if (s.gutter.x > 0.f) {
		g.vertical = true;
		g.vTrack = {inner.x + inner.w - s.gutter.x, inner.y, s.gutter.x, std::max(0.f, inner.h - s.gutter.y)};
		const float maxY = s.MaxOffset().y;
		float thumb = s.content.y > 0.f ? g.vTrack.h * s.view.y / s.content.y : g.vTrack.h;
		thumb = std::min(g.vTrack.h, std::max(MIN_THUMB, thumb));
		const float t = maxY > 0.f ? s.offset.y / maxY : 0.f;
		g.vThumb = {g.vTrack.x, g.vTrack.y + t * (g.vTrack.h - thumb), thickness, thumb};
		if (maxY <= 0.f)
			g.vThumb = {};
	}
	if (s.gutter.y > 0.f) {
		g.horizontal = true;
		g.hTrack = {inner.x, inner.y + inner.h - s.gutter.y, std::max(0.f, inner.w - s.gutter.x), s.gutter.y};
		const float maxX = s.MaxOffset().x;
		float thumb = s.content.x > 0.f ? g.hTrack.w * s.view.x / s.content.x : g.hTrack.w;
		thumb = std::min(g.hTrack.w, std::max(MIN_THUMB, thumb));
		const float t = maxX > 0.f ? s.offset.x / maxX : 0.f;
		g.hThumb = {g.hTrack.x + t * (g.hTrack.w - thumb), g.hTrack.y, thumb, thickness};
		if (maxX <= 0.f)
			g.hThumb = {};
	}
	return g;
}

String TextGeometry::Display(size_t line, size_t upTo) const {
	if (line >= lines.size())
		return String();
	const String &raw = lines[line].text;
	const String part = upTo == String::npos ? raw : raw.Substr(0, std::min(upTo, raw.size()));
	if (password)
		return String(CharCount(part), '*');
	return NormalizeDisplayText(part, tabSize);
}

float TextGeometry::XOf(size_t line, size_t offset) const {
	if (line >= lines.size() || offset == 0 || !measure)
		return box.x - scroll.x;
	return box.x - scroll.x + measure(Display(line, offset)).x;
}

size_t TextGeometry::OffsetAt(sdl3::FPoint p) const {
	if (lines.empty())
		return 0;
	const float rel = p.y - (box.y - scroll.y);
	size_t li = rel <= 0.f || lineHeight <= 0.f ? 0 : size_t(rel / lineHeight);
	li = std::min(li, lines.size() - 1);
	const String &raw = lines[li].text;
	// Frontières de caractères ; largeur croissante : recherche dichotomique
	// du caractère dont le milieu est le plus proche du point.
	std::vector<size_t> bounds;
	bounds.push_back(0);
	for (size_t i = 1; i < raw.size(); ++i)
		if ((static_cast<unsigned char>(raw[i]) & 0xC0) != 0x80)
			bounds.push_back(i);
	if (!raw.IsEmpty())
		bounds.push_back(raw.size());
	const float x = p.x;
	size_t lo = 0, hi = bounds.size() - 1;
	while (lo < hi) {
		const size_t mid = (lo + hi) / 2;
		const float a = XOf(li, bounds[mid]);
		const float b = XOf(li, bounds[mid + 1]);
		if (x < (a + b) * 0.5f)
			hi = mid;
		else
			lo = mid + 1;
	}
	return lines[li].offset + bounds[lo];
}

TextGeometry TextGeometryOf(ecs::ArchetypeRegistry &world, Context &ctx, ecs::Entity e) {
	TextGeometry g;
	const ComputedStyle *cs = nullptr;
	if (auto c = world.GetComponent<ComputedStyle>(e); c.IsSome())
		cs = c.Unwrap().Get();
	const ComputedStyle &style = cs ? *cs : ComputedStyle::Initial();
	String text;
	if (auto t = world.GetComponent<TextContent>(e); t.IsSome())
		text = t.Unwrap()->value;
	bool monospace = false, lineNumbers = false;
	if (auto ed = world.GetComponent<TextEditor>(e); ed.IsSome()) {
		monospace = ed.Unwrap()->monospace;
		lineNumbers = ed.Unwrap()->lineNumbers;
		g.password = ed.Unwrap()->password;
	}
	g.tabSize = style.tabSize;
	g.measure = ctx.fonts.Measurer(monospace ? StringView("monospace") : style.fontFamily.View(), style.fontSize,
								   style.fontWeight, style.fontStyle);
	g.lineHeight = style.LineHeightFor(g.measure(String("Ag")).y);
	g.lines = SplitLines(text);
	if (auto r = world.GetComponent<Rect>(e); r.IsSome())
		g.box = r.Unwrap()->content;
	if (auto s = world.GetComponent<Scroll>(e); s.IsSome())
		g.scroll = s.Unwrap()->offset;
	if (lineNumbers) {
		size_t digits = 1;
		for (size_t n = std::max<size_t>(1, g.lines.size()); n >= 10; n /= 10)
			++digits;
		g.gutter = float(std::max<size_t>(digits, 3)) * ctx.fonts.CellWidth("monospace", style.fontSize) + 16.f;
		g.box.x += g.gutter;
		g.box.w = std::max(0.f, g.box.w - g.gutter);
	}
	// Une ligne unique d'un champ se centre verticalement dans sa boîte.
	if (g.lines.size() == 1 && style.verticalAlign == VerticalAlign::Middle && g.box.h > g.lineHeight)
		if (auto ed = world.GetComponent<TextEditor>(e); ed.IsSome() && !ed.Unwrap()->multiline)
			g.box.y += (g.box.h - g.lineHeight) * 0.5f;
	return g;
}

void KeepCaretVisible(ecs::ArchetypeRegistry &world, Context &ctx, ecs::Entity e) {
	auto ed = world.GetComponent<TextEditor>(e);
	if (ed.IsNone())
		return;
	const size_t cursor = ed.Unwrap()->cursor;
	const TextGeometry g = TextGeometryOf(world, ctx, e);
	auto sc = world.GetComponent<Scroll>(e);
	if (sc.IsNone())
		return;
	Scroll &s = *sc.Unwrap();
	const size_t li = LineIndexOf(g.lines, cursor);
	const float top = float(li) * g.lineHeight;
	if (top < s.offset.y)
		s.offset.y = top;
	else if (top + g.lineHeight > s.offset.y + g.box.h)
		s.offset.y = top + g.lineHeight - g.box.h;
	const float x = g.XOf(li, cursor - g.lines[li].offset) - (g.box.x - g.scroll.x);
	const float margin = 8.f;
	if (x - margin < s.offset.x)
		s.offset.x = std::max(0.f, x - margin);
	else if (x + margin > s.offset.x + g.box.w)
		s.offset.x = x + margin - g.box.w;
	// Le contenu peut avoir grandi depuis la dernière mise en page.
	s.content.x = std::max(s.content.x, x + margin);
	s.content.y = std::max(s.content.y, float(g.lines.size()) * g.lineHeight);
	s.Clamp();
}

} // namespace ui
