// RenderSystem — dessin 2D de l'arbre (Painter / sdl3::Renderer)
#include <ui/geometry.hpp>
#include <ui/systems.hpp>

#include <algorithm>
#include <cmath>

namespace ui {

namespace {

constexpr float DEG = math::PI / 180.f;

class Renderer {
public:
	Renderer(ecs::ArchetypeRegistry &w, Context &c) : world(w), ctx(c), painter(c.painter) {}

	ecs::ArchetypeRegistry &world;
	Context &ctx;
	Painter &painter;

	const ComputedStyle &Style(ecs::Entity e) const {
		if (auto cs = world.GetComponent<ComputedStyle>(e); cs.IsSome())
			return *cs.Unwrap().Get();
		return ComputedStyle::Initial();
	}

	sdl3::Font *FontFor(const ComputedStyle &cs, bool monospace = false) {
		return ctx.fonts.Resolve(monospace ? StringView("monospace") : cs.fontFamily.View(), cs.fontSize, cs.fontWeight,
								 cs.fontStyle);
	}

	// ── Boîte ───────────────────────────────────────────────────────────────

	void DrawBox(const ComputedStyle &cs, const sdl3::FRect &box) {
		const math::Corners radius = ClampRadius(box, math::Corners(cs.borderTopLeftRadius * cs.scale,
																	 cs.borderTopRightRadius * cs.scale,
																	 cs.borderBottomLeftRadius * cs.scale,
																	 cs.borderBottomRightRadius * cs.scale));
		if (cs.boxShadow.IsVisible())
			painter.DrawShadow(box, radius, cs.boxShadow);
		painter.FillRoundedRect(box, radius, cs.backgroundColor);
		if (cs.backgroundImage.kind != PaintKind::None)
			painter.FillPaint(box, cs.backgroundImage, radius, cs.objectFit);
		if (cs.gloss > 0.f)
			painter.DrawGloss(box, radius.tl, cs.gloss);
		if (cs.HasBorder()) {
			const math::Sides bw = cs.BorderWidth();
			painter.StrokeRoundedRect(box, radius,
									  math::Sides(bw.left * cs.scale, bw.top * cs.scale, bw.right * cs.scale,
												  bw.bottom * cs.scale),
									  cs.borderColor);
		}
		if (cs.glow > 0.f)
			painter.DrawGlow(box, radius.tl, cs.glow, cs.glowColor);
	}

	void DrawOutline(const ComputedStyle &cs, const sdl3::FRect &box) {
		if (cs.outlineWidth <= 0.f || cs.outlineColor.a <= 0.f)
			return;
		const float o = cs.outlineOffset + cs.outlineWidth;
		const sdl3::FRect r{box.x - o, box.y - o, box.w + 2.f * o, box.h + 2.f * o};
		const float rad = cs.borderTopLeftRadius * cs.scale + o;
		painter.StrokeRoundedRect(r, math::Corners(rad), math::Sides(cs.outlineWidth), cs.outlineColor);
	}

	void DrawScrollbars(ecs::Entity e, const ComputedStyle &cs, const Rect &rect) {
		auto sc = world.GetComponent<Scroll>(e);
		if (sc.IsNone() || (sc.Unwrap()->gutter.x <= 0.f && sc.Unwrap()->gutter.y <= 0.f))
			return;
		const ScrollbarGeometry g = ScrollbarsOf(rect.box, cs.BorderBox(), *sc.Unwrap(), cs.scrollbarWidth);
		const float r = cs.scrollbarWidth * 0.5f;
		if (g.vertical) {
			painter.FillRoundedRect(g.vTrack, math::Corners(r), cs.scrollbarTrackColor);
			painter.FillRoundedRect(g.vThumb, math::Corners(r), cs.scrollbarThumbColor);
		}
		if (g.horizontal) {
			painter.FillRoundedRect(g.hTrack, math::Corners(r), cs.scrollbarTrackColor);
			painter.FillRoundedRect(g.hThumb, math::Corners(r), cs.scrollbarThumbColor);
		}
	}

	// ── Texte ───────────────────────────────────────────────────────────────

	void DrawDecorations(const ComputedStyle &cs, float x, float y, float w, float lineH, Color color) {
		if (cs.textDecoration == TextDecoration::None || w <= 0.f)
			return;
		const Color c = cs.textDecorationColor.a > 0.f ? cs.textDecorationColor : color;
		const float t = std::max(1.f, cs.fontSize / 14.f);
		if (HasDecoration(cs.textDecoration, TextDecoration::Underline))
			painter.FillRect({x, y + lineH * 0.88f, w, t}, c);
		if (HasDecoration(cs.textDecoration, TextDecoration::LineThrough))
			painter.FillRect({x, y + lineH * 0.52f, w, t}, c);
		if (HasDecoration(cs.textDecoration, TextDecoration::Overline))
			painter.FillRect({x, y + lineH * 0.08f, w, t}, c);
	}

	float AlignX(const sdl3::FRect &box, float w, TextAlign align) const {
		float x = box.x;
		if (align == TextAlign::Center)
			x = box.x + (box.w - w) * 0.5f;
		else if (align == TextAlign::Right)
			x = box.x + box.w - w;
		// Jamais à gauche de la boîte : un texte trop large commence au bord.
		return std::max(box.x, x);
	}

	void DrawLine(const ComputedStyle &cs, sdl3::Font *font, const String &line, float x, float y, float lineH, Color c) {
		painter.Text(line, font, c, x, y);
		DrawDecorations(cs, x, y, painter.MeasureText(line, font, cs.fontSize).x, lineH, c);
	}

	void DrawText(ecs::Entity e, const ComputedStyle &cs, const sdl3::FRect &content, const String &raw) {
		if (raw.IsEmpty())
			return;
		sdl3::Font *font = FontFor(cs);
		const LineMeasure measure = [&](const String &s) { return painter.MeasureText(s, font, cs.fontSize); };
		const float lineH = cs.LineHeightFor(measure(String("Ag")).y);
		sdl3::FPoint scroll{};
		if (auto sc = world.GetComponent<Scroll>(e); sc.IsSome())
			scroll = sc.Unwrap()->offset;
		painter.PushClip(content);
		if (cs.textOverflow == TextOverflow::Wrap) {
			const std::vector<WrappedLine> lines = WrapTextToWidth(raw, content.w, measure, cs.tabSize);
			const float total = lineH * float(lines.size());
			float y = content.y - scroll.y;
			if (cs.verticalAlign == VerticalAlign::Middle && total < content.h)
				y += (content.h - total) * 0.5f;
			else if (cs.verticalAlign == VerticalAlign::Bottom && total < content.h)
				y += content.h - total;
			for (const WrappedLine &l : lines) {
				if (y + lineH >= content.y && y <= content.y + content.h)
					DrawWrapped(cs, font, l, content, y, lineH, measure);
				y += lineH;
			}
			painter.PopClip();
			return;
		}
		const String normalized = NormalizeDisplayText(raw, cs.tabSize);
		const std::vector<String> lines = normalized.Split('\n');
		const float total = lineH * float(lines.size());
		float y = content.y;
		if (cs.verticalAlign == VerticalAlign::Middle)
			y += (content.h - total) * 0.5f;
		else if (cs.verticalAlign == VerticalAlign::Bottom)
			y += content.h - total;
		for (const String &line : lines) {
			float w = measure(line).x;
			String shown = line;
			float x = AlignX(content, w, cs.textAlign);
			switch (cs.textOverflow) {
			case TextOverflow::Ellipsis:
				if (w > content.w + 0.5f) {
					shown = TruncateWithEllipsis(line, content.w, measure);
					w = measure(shown).x;
					x = AlignX(content, w, cs.textAlign);
				}
				break;
			case TextOverflow::Scroll:
				x = content.x - scroll.x;
				break;
			case TextOverflow::Marquee:
				if (auto mq = world.GetComponent<Marquee>(e); mq.IsSome() && mq.Unwrap()->overflow > 0.f)
					x = content.x - mq.Unwrap()->offset;
				break;
			case TextOverflow::Clip:
			case TextOverflow::Wrap:
				break;
			}
			DrawLine(cs, font, shown, x, y, lineH, cs.color);
			y += lineH;
		}
		painter.PopClip();
	}

	void DrawWrapped(const ComputedStyle &cs, sdl3::Font *font, const WrappedLine &line, const sdl3::FRect &box, float y,
					 float lineH, const LineMeasure &measure) {
		if (cs.textAlign == TextAlign::Justify && !line.lastOfParagraph) {
			std::vector<String> words;
			for (const String &w : line.text.Split(' '))
				if (!w.IsEmpty())
					words.push_back(w);
			if (words.size() > 1) {
				float wordsWidth = 0.f;
				for (const String &w : words)
					wordsWidth += measure(w).x;
				const float gap = (box.w - wordsWidth) / float(words.size() - 1);
				float x = box.x;
				for (const String &w : words) {
					DrawLine(cs, font, w, x, y, lineH, cs.color);
					x += measure(w).x + gap;
				}
				return;
			}
		}
		const String text = line.text.TrimRight();
		DrawLine(cs, font, text, AlignX(box, measure(text).x, cs.textAlign), y, lineH, cs.color);
	}

	void DrawEditor(ecs::Entity e, const ComputedStyle &cs, const Rect &rect) {
		auto edRef = world.GetComponent<TextEditor>(e);
		if (edRef.IsNone())
			return;
		const TextEditor ed = *edRef.Unwrap();
		String text;
		if (auto tc = world.GetComponent<TextContent>(e); tc.IsSome())
			text = tc.Unwrap()->value;
		const TextGeometry g = TextGeometryOf(world, ctx, e);
		const bool focused = ctx.input.focus == e;
		sdl3::Font *font = FontFor(cs, ed.monospace);
		painter.PushClip(rect.content);
		// Gouttière des numéros de ligne
		if (g.gutter > 0.f) {
			const sdl3::FRect gutter{rect.content.x, rect.content.y, g.gutter - 8.f, rect.content.h};
			painter.FillRect(gutter, WithAlpha(cs.color, 0.04f));
			sdl3::Font *mono = FontFor(cs, true);
			for (size_t i = 0; i < g.lines.size(); ++i) {
				const float y = g.YOf(i);
				if (y + g.lineHeight < rect.content.y || y > rect.content.y + rect.content.h)
					continue;
				const String n = String::From(int(i + 1));
				const float w = painter.MeasureText(n, mono, cs.fontSize).x;
				painter.Text(n, mono, cs.placeholderColor, gutter.x + gutter.w - w - 4.f, y);
			}
		}
		painter.PushClip(g.box);
		if (text.IsEmpty()) {
			if (!ed.placeholder.IsEmpty())
				painter.Text(NormalizeDisplayText(ed.placeholder, cs.tabSize), font, cs.placeholderColor, g.box.x - g.scroll.x,
							 g.YOf(0));
		} else {
			const size_t lo = std::min(ed.cursor, ed.anchor), hi = std::max(ed.cursor, ed.anchor);
			const size_t caretLine = LineIndexOf(g.lines, ed.cursor);
			if (focused && ed.highlightLine)
				painter.FillRect({g.box.x, g.YOf(caretLine), g.box.w, g.lineHeight}, WithAlpha(cs.color, 0.06f));
			std::vector<TextSpan> spans;
			for (size_t i = 0; i < g.lines.size(); ++i) {
				const float y = g.YOf(i);
				if (y + g.lineHeight < g.box.y || y > g.box.y + g.box.h)
					continue;
				const TextLine &line = g.lines[i];
				// Sélection
				if (hi > lo && lo <= line.offset + line.text.size() && hi >= line.offset) {
					const size_t a = lo > line.offset ? lo - line.offset : 0;
					const size_t b = std::min(hi - line.offset, line.text.size());
					const float x0 = g.XOf(i, a), x1 = g.XOf(i, b);
					const float extra = hi > line.offset + line.text.size() ? 4.f : 0.f;
					painter.FillRect({x0, y, std::max(1.f, x1 - x0 + extra), g.lineHeight}, cs.selectionColor);
				}
				const String display = g.Display(i);
				const float x = g.box.x - g.scroll.x;
				if (ed.highlighter && !ed.password) {
					spans.clear();
					ed.highlighter(display, spans);
					size_t at = 0;
					auto segment = [&](size_t from, size_t to, Color c) {
						if (to <= from)
							return;
						const float sx = from == 0 ? x : x + g.measure(display.Substr(0, from)).x;
						painter.Text(display.Substr(from, to - from), font, c, sx, y);
					};
					for (const TextSpan &s : spans) {
						const size_t b = std::min(s.begin, display.size()), en = std::min(s.end, display.size());
						segment(at, b, cs.color);
						segment(b, en, s.color);
						at = std::max(at, en);
					}
					segment(at, display.size(), cs.color);
				} else {
					painter.Text(display, font, cs.color, x, y);
				}
			}
		}
		if (focused && IoCanWrite(ed.mode) && ed.blink < 0.55f) {
			const size_t li = LineIndexOf(g.lines, ed.cursor);
			const float x = g.lines.empty() ? g.box.x : g.XOf(li, ed.cursor - g.lines[li].offset);
			painter.FillRect({std::round(x), g.YOf(li), std::max(1.f, 1.5f * cs.scale), g.lineHeight}, cs.caretColor);
		}
		painter.PopClip();
		painter.PopClip();
	}

	// ── Formes, icônes, images ──────────────────────────────────────────────

	void DrawShape(ecs::Entity e, const ComputedStyle &cs, const sdl3::FRect &r) {
		const Shape shape = *world.GetComponent<Shape>(e).Unwrap();
		const Color c = cs.color;
		const float t = std::max(1.f, shape.thickness * cs.scale);
		const sdl3::FPoint center{r.x + r.w * 0.5f, r.y + r.h * 0.5f};
		const float radius = std::max(0.f, std::min(r.w, r.h) * 0.5f);
		float start = shape.start, sweep = shape.sweep;
		if (auto spin = world.GetComponent<Spin>(e); spin.IsSome())
			start += spin.Unwrap()->angle;
		// Aiguille / arc d'un potentiomètre : angle lié au Range.
		if (auto view = world.GetComponent<RangeView>(e); view.IsSome() && view.Unwrap()->part == RangePart::Angle) {
			const ecs::Entity src = NearestWith<Range>(world, e);
			const float k = src.Valid() ? world.GetComponent<Range>(src).Unwrap()->Normalized() : 0.f;
			if (shape.kind == ShapeKind::Arc)
				sweep = shape.sweep * k;
			else
				start = shape.start + shape.sweep * k;
		}
		switch (shape.kind) {
		case ShapeKind::Circle:
			painter.FillCircle(center, radius, c);
			break;
		case ShapeKind::Ring:
			painter.StrokeCircle(center, radius, t, c);
			break;
		case ShapeKind::Dot:
			painter.FillCircle(center, radius * 0.45f, c);
			break;
		case ShapeKind::Arc:
			painter.Arc(center, radius - t * 0.5f, start, sweep, t, c);
			break;
		case ShapeKind::Line: {
			const float a = start * DEG;
			painter.Line({center.x + std::cos(a) * radius * 0.25f, center.y + std::sin(a) * radius * 0.25f},
						 {center.x + std::cos(a) * radius * 0.9f, center.y + std::sin(a) * radius * 0.9f}, t, c);
			break;
		}
		case ShapeKind::Check: {
			const sdl3::FPoint pts[3] = {{r.x + r.w * 0.18f, r.y + r.h * 0.52f},
										 {r.x + r.w * 0.42f, r.y + r.h * 0.76f},
										 {r.x + r.w * 0.84f, r.y + r.h * 0.26f}};
			painter.Polyline(pts, t, c);
			break;
		}
		case ShapeKind::Dash:
		case ShapeKind::Minus:
			painter.Line({r.x + r.w * 0.2f, center.y}, {r.x + r.w * 0.8f, center.y}, t, c);
			break;
		case ShapeKind::Plus:
			painter.Line({r.x + r.w * 0.2f, center.y}, {r.x + r.w * 0.8f, center.y}, t, c);
			painter.Line({center.x, r.y + r.h * 0.2f}, {center.x, r.y + r.h * 0.8f}, t, c);
			break;
		case ShapeKind::Cross:
			painter.Line({r.x + r.w * 0.25f, r.y + r.h * 0.25f}, {r.x + r.w * 0.75f, r.y + r.h * 0.75f}, t, c);
			painter.Line({r.x + r.w * 0.75f, r.y + r.h * 0.25f}, {r.x + r.w * 0.25f, r.y + r.h * 0.75f}, t, c);
			break;
		case ShapeKind::ChevronRight:
		case ShapeKind::ChevronDown:
		case ShapeKind::ChevronLeft:
		case ShapeKind::ChevronUp: {
			const float s = std::min(r.w, r.h) * 0.22f;
			sdl3::FPoint pts[3];
			if (shape.kind == ShapeKind::ChevronRight)
				pts[0] = {center.x - s * 0.5f, center.y - s}, pts[1] = {center.x + s * 0.5f, center.y},
				pts[2] = {center.x - s * 0.5f, center.y + s};
			else if (shape.kind == ShapeKind::ChevronLeft)
				pts[0] = {center.x + s * 0.5f, center.y - s}, pts[1] = {center.x - s * 0.5f, center.y},
				pts[2] = {center.x + s * 0.5f, center.y + s};
			else if (shape.kind == ShapeKind::ChevronDown)
				pts[0] = {center.x - s, center.y - s * 0.5f}, pts[1] = {center.x, center.y + s * 0.5f},
				pts[2] = {center.x + s, center.y - s * 0.5f};
			else
				pts[0] = {center.x - s, center.y + s * 0.5f}, pts[1] = {center.x, center.y - s * 0.5f},
				pts[2] = {center.x + s, center.y + s * 0.5f};
			painter.Polyline(pts, t, c);
			break;
		}
		case ShapeKind::TriangleRight: {
			const float s = std::min(r.w, r.h) * 0.25f;
			const sdl3::FPoint pts[3] = {{center.x - s * 0.6f, center.y - s}, {center.x + s * 0.8f, center.y},
										 {center.x - s * 0.6f, center.y + s}};
			painter.FillPolygon(pts, c);
			break;
		}
		case ShapeKind::TriangleDown: {
			const float s = std::min(r.w, r.h) * 0.25f;
			const sdl3::FPoint pts[3] = {{center.x - s, center.y - s * 0.6f}, {center.x + s, center.y - s * 0.6f},
										 {center.x, center.y + s * 0.8f}};
			painter.FillPolygon(pts, c);
			break;
		}
		case ShapeKind::Grip:
			for (int i = 1; i <= 3; ++i) {
				const float o = float(i) * r.w / 4.f;
				painter.Line({r.x + r.w - o, r.y + r.h - 1.f}, {r.x + r.w - 1.f, r.y + r.h - o}, 1.f, c);
			}
			break;
		}
	}

	void DrawIcon(ecs::Entity e, const ComputedStyle &cs, const sdl3::FRect &r) {
		const IconContent icon = *world.GetComponent<IconContent>(e).Unwrap();
		if (!ctx.fonts.HasFamily(icon.family.View()))
			return;
		sdl3::Font *font = ctx.fonts.Resolve(icon.family.View(), cs.fontSize);
		const sdl3::FPoint s = painter.MeasureText(icon.glyph, font, cs.fontSize);
		painter.Text(icon.glyph, font, cs.color, r.x + (r.w - s.x) * 0.5f, r.y + (r.h - s.y) * 0.5f);
	}

	// ── Entité ──────────────────────────────────────────────────────────────

	void DrawEntity(ecs::Entity e, const PaintEntry &entry) {
		auto rectRef = world.GetComponent<Rect>(e);
		if (rectRef.IsNone() || !rectRef.Unwrap()->visible)
			return;
		const Rect rect = *rectRef.Unwrap();
		const ComputedStyle &cs = Style(e);
		if (cs.visibility == Visibility::Hidden)
			return;
		painter.SetOpacity(cs.effectiveOpacity);
		painter.ResetClip(entry.clip);
		DrawBox(cs, rect.box);
		if (auto img = world.GetComponent<ImageContent>(e); img.IsSome())
			painter.Image(img.Unwrap()->source, rect.content, cs.objectFit);
		if (world.HasComponent<Shape>(e))
			DrawShape(e, cs, rect.content);
		if (world.HasComponent<IconContent>(e))
			DrawIcon(e, cs, rect.content);
		if (world.HasComponent<TextEditor>(e))
			DrawEditor(e, cs, rect);
		else if (auto t = world.GetComponent<TextContent>(e); t.IsSome()) {
			const String text = t.Unwrap()->value;
			DrawText(e, cs, rect.content, text);
		}
		if (auto canvas = world.GetComponent<CanvasPainter>(e); canvas.IsSome() && canvas.Unwrap()->draw) {
			const auto draw = canvas.Unwrap()->draw;
			painter.PushClip(rect.content);
			draw(painter, rect.content);
			painter.PopClip();
		}
		for (const Extension &ext : ctx.extensions)
			if (ext.draw)
				ext.draw(world, ctx, e, rect, cs);
	}

	void DrawAfterChildren(ecs::Entity e, const PaintEntry &entry) {
		auto rectRef = world.GetComponent<Rect>(e);
		if (rectRef.IsNone() || !rectRef.Unwrap()->visible)
			return;
		const Rect rect = *rectRef.Unwrap();
		const ComputedStyle &cs = Style(e);
		painter.SetOpacity(cs.effectiveOpacity);
		painter.ResetClip(entry.clip);
		for (const Extension &ext : ctx.extensions)
			if (ext.drawOver)
				ext.drawOver(world, ctx, e, rect, cs);
		DrawScrollbars(e, cs, rect);
		if (auto ws = world.GetComponent<WidgetState>(e); ws.IsSome()) {
			const State s = ws.Unwrap()->flags;
			if (HasState(s, State::FocusVisible))
				DrawOutline(cs, rect.box);
			if (HasState(s, State::DragOver))
				if (auto dt = world.GetComponent<DropTarget>(e); dt.IsSome() && dt.Unwrap()->highlight)
					painter.StrokeRoundedRect(rect.box, math::Corners(4.f), math::Sides(2.f), cs.accentColor);
		}
	}

	void DrawOverlays() {
		const sdl3::FRect viewport{0.f, 0.f, ctx.viewport.x, ctx.viewport.y};
		painter.ResetClip(viewport);
		painter.SetOpacity(1.f);
		Interaction &in = ctx.input;
		// Fantôme du glisser-déposer
		if (in.drag == Interaction::Drag::Payload && in.dragActive && world.IsAlive(in.dragEntity))
			if (auto src = world.GetComponent<DragSource>(in.dragEntity); src.IsSome() && !src.Unwrap()->label.IsEmpty()) {
				const DragSource ds = *src.Unwrap();
				const ComputedStyle &cs = Style(ctx.root);
				sdl3::Font *font = FontFor(cs);
				const sdl3::FPoint s = painter.MeasureText(ds.label, font, cs.fontSize);
				const sdl3::FRect box{in.pointer.x + 14.f, in.pointer.y + 6.f, s.x + 16.f, s.y + 8.f};
				painter.FillRoundedRect(box, math::Corners(4.f), WithAlpha(Rgba(25, 27, 38), 0.92f));
				painter.StrokeRoundedRect(box, math::Corners(4.f), math::Sides(1.f), cs.accentColor);
				painter.Text(ds.label, font, cs.color, box.x + 8.f, box.y + 4.f);
				if (ds.count > 1) {
					const String n = String::From(ds.count);
					const sdl3::FPoint ns = painter.MeasureText(n, font, cs.fontSize);
					const float d = std::max(ns.x, ns.y) + 4.f;
					painter.FillCircle({box.x + box.w, box.y}, d * 0.5f, cs.accentColor);
					painter.Text(n, font, Rgba(255, 255, 255), box.x + box.w - ns.x * 0.5f, box.y - ns.y * 0.5f);
				}
			}
		// Infobulle
		if (in.tooltipVisible && !in.tooltipText.IsEmpty()) {
			const ComputedStyle &cs = Style(ctx.root);
			const float size = 13.f;
			sdl3::Font *font = ctx.fonts.Resolve(cs.fontFamily.View(), size);
			const String text = NormalizeDisplayText(in.tooltipText, 4);
			const std::vector<String> lines = text.Split('\n');
			float w = 0.f;
			const float lh = painter.MeasureText(String("Ag"), font, size).y;
			for (const String &l : lines)
				w = std::max(w, painter.MeasureText(l, font, size).x);
			sdl3::FRect box{in.tooltipPos.x, in.tooltipPos.y, w + 16.f, lh * float(lines.size()) + 10.f};
			if (box.x + box.w > viewport.w)
				box.x = std::max(0.f, viewport.w - box.w - 4.f);
			if (box.y + box.h > viewport.h)
				box.y = std::max(0.f, in.pointer.y - box.h - 6.f);
			painter.FillRoundedRect(box, math::Corners(4.f), WithAlpha(Rgba(25, 27, 38), 0.96f));
			painter.StrokeRoundedRect(box, math::Corners(4.f), math::Sides(1.f), Rgba(70, 76, 110));
			float y = box.y + 5.f;
			for (const String &l : lines) {
				painter.Text(l, font, Rgba(232, 234, 242), box.x + 8.f, y);
				y += lh;
			}
		}
	}

	bool ReplacedByExtension(const PaintEntry &entry) {
		auto rect = world.GetComponent<Rect>(entry.entity);
		if (rect.IsNone() || !rect.Unwrap()->visible)
			return false;
		const Rect r = *rect.Unwrap();
		for (const Extension &ext : ctx.extensions)
			if (ext.drawSubtree) {
				painter.ResetClip(entry.clip);
				painter.SetOpacity(Style(entry.entity).effectiveOpacity);
				if (ext.drawSubtree(world, ctx, entry.entity, r))
					return true;
			}
		return false;
	}

	void DrawRange(size_t begin, size_t end) {
		struct Open {
			size_t end;
			size_t index;
		};
		std::vector<Open> stack;
		for (size_t i = begin; i < end; ++i) {
			while (!stack.empty() && stack.back().end <= i) {
				const PaintEntry &p = ctx.paintOrder[stack.back().index];
				DrawAfterChildren(p.entity, p);
				stack.pop_back();
			}
			const PaintEntry &entry = ctx.paintOrder[i];
			if (!world.IsAlive(entry.entity))
				continue;
			if (!ctx.capturing && ReplacedByExtension(entry)) {
				i = std::max(entry.subtreeEnd, i + 1) - 1; // sous-arbre déjà dessiné
				continue;
			}
			DrawEntity(entry.entity, entry);
			stack.push_back({std::max(entry.subtreeEnd, i + 1), i});
		}
		while (!stack.empty()) {
			const PaintEntry &p = ctx.paintOrder[stack.back().index];
			DrawAfterChildren(p.entity, p);
			stack.pop_back();
		}
	}
};

} // namespace

void RenderSystem::Run(ecs::ArchetypeRegistry &world, Context &ctx) {
	ctx.painter.BeginFrame(sdl3::FRect{0.f, 0.f, ctx.viewport.x, ctx.viewport.y});
	if (ctx.painter.CanDraw()) {
		Renderer r(world, ctx);
		r.DrawRange(0, ctx.paintOrder.size());
		r.DrawOverlays();
	}
	ctx.painter.EndFrame();
}

void RenderSystem::DrawSubtree(ecs::ArchetypeRegistry &world, Context &ctx, ecs::Entity e) {
	for (size_t i = 0; i < ctx.paintOrder.size(); ++i)
		if (ctx.paintOrder[i].entity == e) {
			const bool was = ctx.capturing;
			ctx.capturing = true;
			Renderer r(world, ctx);
			r.DrawRange(i, std::max(i + 1, ctx.paintOrder[i].subtreeEnd));
			ctx.capturing = was;
			return;
		}
}

} // namespace ui
