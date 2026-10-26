// LayoutSystem — mise en page (flexbox, grille, pile, positionnement)
#include <ui/systems.hpp>

#include <algorithm>
#include <cmath>
#include <unordered_map>

namespace ui {

namespace {

constexpr float INF = 1.0e30f;

bool Definite(const Length &l, bool parentKnown) noexcept {
	if (l.IsAuto() || l.IsFr())
		return false;
	if (l.IsRelative() && !parentKnown)
		return false;
	return true;
}

struct Pass {
	ecs::ArchetypeRegistry &world;
	Context &ctx;
	std::unordered_map<uint64_t, sdl3::FPoint> memo;
	std::vector<ecs::Entity> fixed; ///< sous-arbres Fixed, placés après le flux

	Pass(ecs::ArchetypeRegistry &w, Context &c) : world(w), ctx(c) {}

	const ComputedStyle &Style(ecs::Entity e) const {
		if (auto cs = world.GetComponent<ComputedStyle>(e); cs.IsSome())
			return *cs.Unwrap().Get();
		return ComputedStyle::Initial();
	}

	LengthBasis Basis(const ComputedStyle &cs, float parent) const {
		LengthBasis b;
		b.parent = parent;
		b.fontSize = cs.fontSize;
		b.rootFontSize = ctx.rootFontSize;
		b.viewportW = ctx.viewport.x;
		b.viewportH = ctx.viewport.y;
		b.scale = cs.scale;
		return b;
	}

	static math::Sides Scaled(const math::Sides &s, float k) {
		return math::Sides(s.left * k, s.top * k, s.right * k, s.bottom * k);
	}
	math::Sides Padding(const ComputedStyle &cs) const { return Scaled(cs.Padding(), cs.scale); }
	math::Sides Border(const ComputedStyle &cs) const { return Scaled(cs.BorderBox(), cs.scale); }
	math::Sides Margin(const ComputedStyle &cs) const { return Scaled(cs.Margin(), cs.scale); }
	math::Sides Insets(const ComputedStyle &cs) const {
		const math::Sides p = Padding(cs), b = Border(cs);
		return math::Sides(p.left + b.left, p.top + b.top, p.right + b.right, p.bottom + b.bottom);
	}

	// ── Visibilité ──────────────────────────────────────────────────────────
	bool ConditionHolds(ecs::Entity e) const {
		auto sw = world.GetComponent<ShowWhen>(e);
		if (sw.IsNone())
			return true;
		const ShowWhen cond = *sw.Unwrap();
		switch (cond.condition) {
		case VisibleIf::Checked:
		case VisibleIf::Unchecked: {
			const ecs::Entity src = AncestorWith<Check>(world, e);
			const bool on = src.Valid() && world.GetComponent<Check>(src).Unwrap()->on;
			return cond.condition == VisibleIf::Checked ? on : !on;
		}
		case VisibleIf::Selected: {
			const ecs::Entity src = AncestorWith<Selection>(world, e);
			return src.Valid() && world.GetComponent<Selection>(src).Unwrap()->IsSelected(cond.index);
		}
		case VisibleIf::Editing:
		case VisibleIf::NotEditing: {
			const ecs::Entity src = AncestorWith<EditOnDoubleClick>(world, e);
			const bool editing = src.Valid() && world.GetComponent<EditOnDoubleClick>(src).Unwrap()->editing;
			return cond.condition == VisibleIf::Editing ? editing : !editing;
		}
		}
		return true;
	}

	bool Shown(ecs::Entity e) const {
		if (world.HasComponent<Hidden>(e))
			return false;
		if (Style(e).display == Display::None)
			return false;
		if (auto p = world.GetComponent<PopupState>(e); p.IsSome() && !p.Unwrap()->open)
			return false;
		return ConditionHolds(e);
	}

	bool OutOfFlow(ecs::Entity e) const {
		const ComputedStyle &cs = Style(e);
		return cs.position != Position::Relative || world.HasComponent<RangeView>(e) || world.HasComponent<PopupState>(e);
	}

	std::vector<ecs::Entity> FlowChildren(ecs::Entity e) const {
		std::vector<ecs::Entity> out;
		for (ecs::Entity c : ChildList(world, e))
			if (Shown(c) && !OutOfFlow(c))
				out.push_back(c);
		return out;
	}

	// ── Texte ───────────────────────────────────────────────────────────────
	LineMeasure Measurer(ecs::Entity e, const ComputedStyle &cs) {
		StringView family = cs.fontFamily.View();
		if (auto ed = world.GetComponent<TextEditor>(e); ed.IsSome() && ed.Unwrap()->monospace)
			family = "monospace";
		return ctx.fonts.Measurer(family, cs.fontSize, cs.fontWeight, cs.fontStyle);
	}

	/// Texte affiché (placeholder d'un champ vide, masque d'un mot de passe).
	String DisplayText(ecs::Entity e) const {
		String text;
		if (auto t = world.GetComponent<TextContent>(e); t.IsSome())
			text = t.Unwrap()->value;
		if (auto ed = world.GetComponent<TextEditor>(e); ed.IsSome()) {
			if (text.IsEmpty())
				return ed.Unwrap()->placeholder;
			if (ed.Unwrap()->password)
				return String(CharCount(text), '*');
		}
		return text;
	}

	/// Taille de contenu d'un texte, `maxW` < 0 : sans contrainte.
	sdl3::FPoint TextSize(ecs::Entity e, const ComputedStyle &cs, float maxW) {
		const String text = DisplayText(e);
		const LineMeasure measure = Measurer(e, cs);
		if (cs.textOverflow == TextOverflow::Wrap && maxW > 0.f && !world.HasComponent<TextEditor>(e)) {
			const std::vector<WrappedLine> lines = WrapTextToWidth(text, maxW, measure, cs.tabSize);
			const float lh = cs.LineHeightFor(measure(String("Ag")).y);
			float w = 0.f;
			for (const WrappedLine &l : lines)
				w = std::max(w, l.width);
			return {w, lh * float(std::max<size_t>(1, lines.size()))};
		}
		TextMetrics m = MeasureTextBlock(text.IsEmpty() ? String(" ") : text, cs.fontSize, measure, cs.tabSize);
		if (text.IsEmpty())
			m.width = 0.f;
		const float lh = cs.LineHeightFor(m.lineHeight);
		sdl3::FPoint size{m.width, lh * float(m.lineCount)};
		if (auto ed = world.GetComponent<TextEditor>(e); ed.IsSome()) {
			size.x += 2.f; // curseur en fin de ligne
			if (ed.Unwrap()->lineNumbers) {
				const size_t lines = std::max<size_t>(1, m.lineCount);
				size_t digits = 1;
				for (size_t n = lines; n >= 10; n /= 10)
					++digits;
				size.x += float(std::max<size_t>(digits, 3)) * ctx.fonts.CellWidth("monospace", cs.fontSize) + 16.f;
			}
		}
		return size;
	}

	/// Contenu intrinsèque d'une feuille (hors enfants).
	sdl3::FPoint LeafSize(ecs::Entity e, const ComputedStyle &cs, float maxW) {
		sdl3::FPoint size{0.f, 0.f};
		for (const Extension &ext : ctx.extensions)
			if (ext.measure)
				if (auto s = ext.measure(world, ctx, e, cs); s.IsSome())
					return s.Unwrap();
		if (world.HasComponent<TextContent>(e) || world.HasComponent<TextEditor>(e)) {
			const sdl3::FPoint t = TextSize(e, cs, maxW);
			size.x = std::max(size.x, t.x);
			size.y = std::max(size.y, t.y);
		}
		if (auto icon = world.GetComponent<IconContent>(e); icon.IsSome()) {
			const String glyph = icon.Unwrap()->glyph;
			const String family = icon.Unwrap()->family;
			sdl3::Font *font = ctx.fonts.Resolve(family.View(), cs.fontSize);
			sdl3::FPoint g{cs.fontSize, cs.fontSize};
			if (font && ctx.fonts.HasFamily(family.View()))
				g = ctx.fonts.Measure(font, glyph, cs.fontSize);
			size.x = std::max(size.x, std::max(g.x, cs.fontSize));
			size.y = std::max(size.y, std::max(g.y, cs.fontSize));
		}
		if (auto img = world.GetComponent<ImageContent>(e); img.IsSome()) {
			const sdl3::FPoint t = ctx.textures.SizeOf(img.Unwrap()->source);
			size.x = std::max(size.x, t.x * cs.scale);
			size.y = std::max(size.y, t.y * cs.scale);
		}
		if (world.HasComponent<Shape>(e) && size.x <= 0.f && size.y <= 0.f)
			size = {cs.fontSize, cs.fontSize};
		return size;
	}

	// ── Mesure ──────────────────────────────────────────────────────────────
	/// Taille préférée (boîte de bordure) ; `availW`/`availH` : place offerte
	/// pour la boîte de bordure (INF : illimitée), `parentW/H` : contenu du
	/// parent pour les pourcentages (< 0 : inconnu).
	/// Taille minimale de contenu sur un axe (`min-width/min-height: auto` de
	/// CSS) : sous elle, un élément flex ne rétrécit pas. Un conteneur défilant
	/// ne compte que pour son `min-*` (son contenu défile) ; un texte tronqué,
	/// défilant ou replié peut descendre à zéro ; une rangée compte la somme
	/// de ses enfants sur l'axe horizontal, une colonne sur l'axe vertical.
	float MinContent(ecs::Entity e, bool horizontal, float availW, float parentW, float parentH, int depth = 0) {
		const ComputedStyle &cs = Style(e);
		const float parent = horizontal ? parentW : parentH;
		const bool known = parent >= 0.f;
		const LengthBasis basis = Basis(cs, parent);
		const Length &minLen = horizontal ? cs.minWidth : cs.minHeight;
		const Length &len = horizontal ? cs.width : cs.height;
		const float floor = Definite(minLen, known) ? minLen.Resolve(basis) : 0.f;
		if (Definite(len, known))
			return std::max(floor, len.Resolve(basis));
		if ((horizontal ? cs.overflowX : cs.overflowY) != Overflow::Visible)
			return floor;
		const std::vector<ecs::Entity> kids = FlowChildren(e);
		if (kids.empty()) {
			if (horizontal && cs.textOverflow != TextOverflow::Clip && world.HasComponent<TextContent>(e))
				return floor;
			const sdl3::FPoint s = Measure(e, availW, parentW, parentH);
			return std::max(floor, horizontal ? s.x : s.y);
		}
		if (depth > 24 || cs.display != Display::Flex || cs.flexWrap == FlexWrap::Wrap) {
			const sdl3::FPoint s = Measure(e, availW, parentW, parentH);
			return std::max(floor, horizontal ? s.x : s.y);
		}
		const math::Sides in = Insets(cs);
		const float innerW = availW >= INF * 0.5f ? INF : std::max(0.f, availW - in.H());
		const bool along = cs.IsRow() == horizontal;
		float total = 0.f;
		for (ecs::Entity k : kids) {
			const math::Sides m = Margin(Style(k));
			const float v = MinContent(k, horizontal, innerW, innerW >= INF * 0.5f ? -1.f : innerW, -1.f, depth + 1) +
							(horizontal ? m.H() : m.V());
			total = along ? total + v : std::max(total, v);
		}
		if (along)
			total += cs.gap * cs.scale * float(kids.size() - 1);
		return std::max(floor, total + (horizontal ? in.H() : in.V()));
	}

	sdl3::FPoint Measure(ecs::Entity e, float availW, float parentW, float parentH) {
		const uint64_t key = (uint64_t(e.id) << 32) ^ uint64_t(uint32_t(std::lround(availW * 4.f))) ^
							 (uint64_t(uint32_t(std::lround(parentW * 2.f))) << 16);
		if (auto it = memo.find(key); it != memo.end())
			return it->second;
		const ComputedStyle &cs = Style(e);
		const math::Sides in = Insets(cs);
		const bool pwKnown = parentW >= 0.f, phKnown = parentH >= 0.f;
		const LengthBasis bw = Basis(cs, parentW), bh = Basis(cs, parentH);

		Option<float> fixedW = NONE, fixedH = NONE;
		if (Definite(cs.width, pwKnown))
			fixedW = Some(cs.width.Resolve(bw));
		if (Definite(cs.height, phKnown))
			fixedH = Some(cs.height.Resolve(bh));

		float maxBorderW = fixedW.IsSome() ? fixedW.Unwrap() : availW;
		if (Definite(cs.maxWidth, pwKnown))
			maxBorderW = std::min(maxBorderW, cs.maxWidth.Resolve(bw));
		const float contentAvail = maxBorderW >= INF * 0.5f ? -1.f : std::max(0.f, maxBorderW - in.H());

		sdl3::FPoint content = LeafSize(e, cs, contentAvail);
		const std::vector<ecs::Entity> kids = FlowChildren(e);
		if (!kids.empty()) {
			const sdl3::FPoint c = MeasureChildren(e, cs, kids, contentAvail, fixedW.IsSome() ? fixedW.Unwrap() - in.H() : -1.f,
												   fixedH.IsSome() ? fixedH.Unwrap() - in.V() : -1.f);
			content.x = std::max(content.x, c.x);
			content.y = std::max(content.y, c.y);
		}
		sdl3::FPoint size{content.x + in.H(), content.y + in.V()};
		if (fixedW.IsSome())
			size.x = fixedW.Unwrap();
		if (fixedH.IsSome())
			size.y = fixedH.Unwrap();
		size = ClampSize(cs, size, parentW, parentH);
		memo.emplace(key, size);
		return size;
	}

	sdl3::FPoint ClampSize(const ComputedStyle &cs, sdl3::FPoint size, float parentW, float parentH) const {
		const bool pwKnown = parentW >= 0.f, phKnown = parentH >= 0.f;
		const LengthBasis bw = Basis(cs, parentW), bh = Basis(cs, parentH);
		if (Definite(cs.maxWidth, pwKnown))
			size.x = std::min(size.x, cs.maxWidth.Resolve(bw));
		if (Definite(cs.maxHeight, phKnown))
			size.y = std::min(size.y, cs.maxHeight.Resolve(bh));
		if (Definite(cs.minWidth, pwKnown))
			size.x = std::max(size.x, cs.minWidth.Resolve(bw));
		if (Definite(cs.minHeight, phKnown))
			size.y = std::max(size.y, cs.minHeight.Resolve(bh));
		return {std::max(0.f, size.x), std::max(0.f, size.y)};
	}

	float ColumnWidth(ecs::Entity cell) const {
		auto tc = world.GetComponent<TableCell>(cell);
		if (tc.IsNone())
			return -1.f;
		const ecs::Entity table = AncestorWith<TableModel>(world, cell);
		if (!table.Valid())
			return -1.f;
		auto model = world.GetComponent<TableModel>(table);
		const int col = tc.Unwrap()->column;
		if (col < 0 || size_t(col) >= model.Unwrap()->columns.size())
			return -1.f;
		return model.Unwrap()->columns[size_t(col)].width * Style(cell).scale;
	}

	/// Contenu des enfants en flux d'un conteneur.
	sdl3::FPoint MeasureChildren(ecs::Entity, const ComputedStyle &cs, const std::vector<ecs::Entity> &kids,
								 float contentAvail, float knownW, float knownH) {
		const float gap = cs.gap * cs.scale;
		const float pw = knownW >= 0.f ? knownW : -1.f;
		const float ph = knownH >= 0.f ? knownH : -1.f;
		auto outer = [&](ecs::Entity c, float avail) {
			sdl3::FPoint s = Measure(c, avail, pw, ph);
			if (const float w = ColumnWidth(c); w >= 0.f)
				s.x = w;
			const math::Sides m = Margin(Style(c));
			return sdl3::FPoint{s.x + m.H(), s.y + m.V()};
		};
		const float availChild = contentAvail >= 0.f ? contentAvail : INF;
		if (cs.display == Display::Stack) {
			sdl3::FPoint out{0.f, 0.f};
			for (ecs::Entity c : kids) {
				const sdl3::FPoint s = outer(c, availChild);
				out.x = std::max(out.x, s.x);
				out.y = std::max(out.y, s.y);
			}
			return out;
		}
		if (cs.display == Display::Grid) {
			const int cols = std::max(1, cs.gridColumns);
			const float colAvail =
				contentAvail >= 0.f ? std::max(0.f, (contentAvail - gap * float(cols - 1)) / float(cols)) : INF;
			float colW = 0.f, total = 0.f, rowH = 0.f;
			for (size_t i = 0; i < kids.size(); ++i) {
				const sdl3::FPoint s = outer(kids[i], colAvail);
				colW = std::max(colW, s.x);
				rowH = std::max(rowH, s.y);
				if ((i + 1) % size_t(cols) == 0 || i + 1 == kids.size()) {
					total += rowH + (total > 0.f ? gap : 0.f);
					rowH = 0.f;
				}
			}
			return {colW * float(cols) + gap * float(cols - 1), total};
		}
		const bool row = cs.IsRow();
		if (cs.flexWrap == FlexWrap::Wrap && row && contentAvail >= 0.f) {
			float lineMain = 0.f, lineCross = 0.f, maxMain = 0.f, totalCross = 0.f;
			bool first = true;
			for (ecs::Entity c : kids) {
				const sdl3::FPoint s = outer(c, availChild);
				if (!first && lineMain + gap + s.x > contentAvail) {
					maxMain = std::max(maxMain, lineMain);
					totalCross += lineCross + gap;
					lineMain = 0.f;
					lineCross = 0.f;
					first = true;
				}
				lineMain += (first ? 0.f : gap) + s.x;
				lineCross = std::max(lineCross, s.y);
				first = false;
			}
			maxMain = std::max(maxMain, lineMain);
			totalCross += lineCross;
			return {maxMain, totalCross};
		}
		float main = 0.f, cross = 0.f;
		std::vector<sdl3::FPoint> sizes(kids.size());
		for (size_t i = 0; i < kids.size(); ++i) {
			sizes[i] = outer(kids[i], availChild);
			main += (row ? sizes[i].x : sizes[i].y) + (i > 0 ? gap : 0.f);
			cross = std::max(cross, row ? sizes[i].y : sizes[i].x);
		}
		// Rangée de largeur connue : la hauteur d'un enfant dépend de la largeur
		// qu'il recevra (texte replié) — répartition flex, puis remesure.
		if (row && contentAvail >= 0.f && !kids.empty()) {
			const float free = contentAvail - main;
			float grow = 0.f, shrink = 0.f;
			for (size_t i = 0; i < kids.size(); ++i) {
				const ComputedStyle &k = Style(kids[i]);
				grow += k.width.IsFr() ? k.width.value : k.flexGrow;
				if (!Definite(k.width, true))
					shrink += k.flexShrink * sizes[i].x;
			}
			if ((free > 0.f && grow > 0.f) || (free < 0.f && shrink > 0.f)) {
				cross = 0.f;
				for (size_t i = 0; i < kids.size(); ++i) {
					const ComputedStyle &k = Style(kids[i]);
					const math::Sides m = Margin(k);
					float w = sizes[i].x;
					if (free > 0.f)
						w += free * (k.width.IsFr() ? k.width.value : k.flexGrow) / grow;
					else if (!Definite(k.width, true))
						w = std::max(0.f, w + free * k.flexShrink * sizes[i].x / shrink);
					const sdl3::FPoint s = Measure(kids[i], std::max(0.f, w - m.H()), pw, ph);
					cross = std::max(cross, s.y + m.V());
				}
			}
		}
		return row ? sdl3::FPoint{main, cross} : sdl3::FPoint{cross, main};
	}

	// ── Placement ───────────────────────────────────────────────────────────
	void Place(ecs::Entity e, sdl3::FRect box, sdl3::FRect parentClip) {
		const ComputedStyle &cs = Style(e);
		const math::Sides in = Insets(cs);
		sdl3::FRect content{box.x + in.left, box.y + in.top, std::max(0.f, box.w - in.H()), std::max(0.f, box.h - in.V())};

		// Débordement des effets (ombre, lueur, contour de focus) : seule
		// marge de dessin accordée hors de la boîte.
		float outset = 0.f;
		if (cs.glow > 0.f)
			outset = std::max(outset, 6.f);
		if (cs.outlineWidth > 0.f)
			outset = std::max(outset, cs.outlineWidth + cs.outlineOffset + 1.f);
		if (cs.boxShadow.IsVisible())
			outset = std::max(outset, std::fabs(cs.boxShadow.x) + std::fabs(cs.boxShadow.y) + cs.boxShadow.blur +
										  cs.boxShadow.spread);
		const sdl3::FRect inflated{box.x - outset, box.y - outset, box.w + 2.f * outset, box.h + 2.f * outset};
		const sdl3::FRect clip = parentClip.Intersection(inflated);

		// Défilement : place des barres, décalage.
		const bool scrolls = cs.overflowX == Overflow::Auto || cs.overflowX == Overflow::Scroll ||
							 cs.overflowY == Overflow::Auto || cs.overflowY == Overflow::Scroll;
		sdl3::FPoint offset{};
		sdl3::FPoint gutter{};
		if (auto sc = world.GetComponent<Scroll>(e); sc.IsSome()) {
			offset = sc.Unwrap()->offset;
			gutter = sc.Unwrap()->gutter;
		}
		if (scrolls) {
			content.w = std::max(0.f, content.w - gutter.x);
			content.h = std::max(0.f, content.h - gutter.y);
		}

		if (auto r = world.GetComponent<Rect>(e); r.IsSome()) {
			r.Unwrap()->box = box;
			r.Unwrap()->content = content;
			r.Unwrap()->clip = clip;
			r.Unwrap()->visible = clip.w > 0.f && clip.h > 0.f;
		}
		const size_t entryIndex = ctx.paintOrder.size();
		ctx.paintOrder.push_back({e, box, clip, false, 0});

		const sdl3::FRect childClip = cs.Clips() ? clip.Intersection(content) : clip;
		const sdl3::FPoint extent = PlaceChildren(e, cs, content, offset, childClip);
		ctx.paintOrder[entryIndex].subtreeEnd = ctx.paintOrder.size();

		// Texte qui défile (scroll / wrap borné) : son contenu est le texte.
		sdl3::FPoint textExtent{0.f, 0.f};
		if (world.HasComponent<TextContent>(e) || world.HasComponent<TextEditor>(e)) {
			const bool editor = world.HasComponent<TextEditor>(e);
			const bool wrap = cs.textOverflow == TextOverflow::Wrap;
			const sdl3::FPoint t = TextSize(e, cs, wrap ? content.w : -1.f);
			textExtent = t;
			if (auto mq = world.GetComponent<Marquee>(e); mq.IsSome())
				mq.Unwrap()->overflow = std::max(0.f, t.x - content.w);
			if (editor || cs.textOverflow == TextOverflow::Scroll || wrap) {
				if (auto sc = world.GetComponent<Scroll>(e); sc.IsSome()) {
					Scroll &s = *sc.Unwrap();
					s.view = {content.w, content.h};
					s.content = {cs.textOverflow == TextOverflow::Scroll || editor ? t.x : content.w, t.y};
					s.Clamp();
				}
			}
		}
		if (scrolls) {
			if (auto sc = world.GetComponent<Scroll>(e); sc.IsSome()) {
				Scroll &s = *sc.Unwrap();
				const float bar = cs.scrollbarWidth;
				const sdl3::FPoint total{std::max(extent.x, textExtent.x), std::max(extent.y, textExtent.y)};
				// Barres nécessaires (l'une peut rendre l'autre nécessaire).
				const float boxW = box.w - in.H(), boxH = box.h - in.V();
				bool needY = cs.overflowY == Overflow::Scroll || (cs.overflowY == Overflow::Auto && total.y > boxH + 1.f);
				bool needX = cs.overflowX == Overflow::Scroll || (cs.overflowX == Overflow::Auto && total.x > boxW + 1.f);
				if (needY && !needX && cs.overflowX == Overflow::Auto && total.x > boxW - bar + 1.f)
					needX = true;
				if (needX && !needY && cs.overflowY == Overflow::Auto && total.y > boxH - bar + 1.f)
					needY = true;
				const sdl3::FPoint newGutter{needY ? bar : 0.f, needX ? bar : 0.f};
				s.view = {std::max(0.f, boxW - newGutter.x), std::max(0.f, boxH - newGutter.y)};
				s.content = total;
				const bool gutterChanged = newGutter.x != s.gutter.x || newGutter.y != s.gutter.y;
				s.gutter = newGutter;
				const sdl3::FPoint before = s.offset;
				s.Clamp();
				if (gutterChanged || before.x != s.offset.x || before.y != s.offset.y)
					ctx.layoutDirty = true; // une passe de plus avec la bonne géométrie
			}
		}
	}

	/// Place les enfants ; rend l'étendue du contenu (pour le défilement).
	sdl3::FPoint PlaceChildren(ecs::Entity e, const ComputedStyle &cs, const sdl3::FRect &content, sdl3::FPoint offset,
							   const sdl3::FRect &childClip) {
		const std::vector<ecs::Entity> kids = FlowChildren(e);
		const sdl3::FPoint origin{content.x - offset.x, content.y - offset.y};
		sdl3::FPoint extent{0.f, 0.f};
		auto record = [&](const sdl3::FRect &b, const math::Sides &m) {
			extent.x = std::max(extent.x, b.x + b.w + m.right - origin.x);
			extent.y = std::max(extent.y, b.y + b.h + m.bottom - origin.y);
		};
		const float gap = cs.gap * cs.scale;
		const float cw = content.w, ch = content.h;

		if (cs.display == Display::Stack) {
			for (ecs::Entity c : kids) {
				const ComputedStyle &ccs = Style(c);
				const math::Sides m = Margin(ccs);
				sdl3::FPoint s = Measure(c, cw - m.H(), cw, ch);
				const Align align = ccs.alignSelf != Align::Auto ? ccs.alignSelf : cs.alignItems;
				const bool stretch = align == Align::Stretch;
				if (stretch && ccs.width.IsAuto())
					s.x = cw - m.H();
				if (stretch && ccs.height.IsAuto())
					s.y = ch - m.V();
				if (ccs.width.IsFr())
					s.x = cw - m.H();
				if (ccs.height.IsFr())
					s.y = ch - m.V();
				s = ClampSize(ccs, s, cw, ch);
				const sdl3::FPoint f = AnchorFactors(ccs.anchor);
				float x = origin.x + m.left + (cw - m.H() - s.x) * f.x;
				float y = origin.y + m.top + (ch - m.V() - s.y) * f.y;
				if (!stretch && ccs.anchor == Anchor::TopLeft) {
					x = origin.x + m.left + (cw - m.H() - s.x) * JustifyFactor(cs.justifyContent);
					y = origin.y + m.top + (ch - m.V() - s.y) * AlignFactor(align);
				}
				const sdl3::FRect b{x, y, s.x, s.y};
				Place(c, b, childClip);
				record(b, m);
			}
		} else if (cs.display == Display::Grid) {
			const int cols = std::max(1, cs.gridColumns);
			const float colW = std::max(0.f, (cw - gap * float(cols - 1)) / float(cols));
			float y = origin.y;
			for (size_t i = 0; i < kids.size(); i += size_t(cols)) {
				float rowH = 0.f;
				std::vector<sdl3::FPoint> sizes;
				for (size_t k = i; k < std::min(kids.size(), i + size_t(cols)); ++k) {
					const math::Sides m = Margin(Style(kids[k]));
					sdl3::FPoint s = Measure(kids[k], colW - m.H(), colW, -1.f);
					sizes.push_back(s);
					rowH = std::max(rowH, s.y + m.V());
				}
				for (size_t k = i; k < std::min(kids.size(), i + size_t(cols)); ++k) {
					const ComputedStyle &ccs = Style(kids[k]);
					const math::Sides m = Margin(ccs);
					sdl3::FPoint s = sizes[k - i];
					const Align align = ccs.alignSelf != Align::Auto ? ccs.alignSelf : cs.alignItems;
					if (ccs.width.IsAuto() || ccs.width.IsFr())
						s.x = colW - m.H();
					if (align == Align::Stretch && ccs.height.IsAuto())
						s.y = rowH - m.V();
					const float x = origin.x + float(k - i) * (colW + gap) + m.left;
					const float yy = y + m.top + (rowH - m.V() - s.y) * AlignFactor(align);
					const sdl3::FRect b{x, yy, s.x, s.y};
					Place(kids[k], b, childClip);
					record(b, m);
				}
				y += rowH + gap;
			}
		} else {
			PlaceFlex(cs, kids, origin, cw, ch, gap, childClip, record);
		}

		// Hors flux : positionnés, parties liées aux valeurs.
		for (ecs::Entity c : ChildList(world, e)) {
			if (!Shown(c) || !OutOfFlow(c))
				continue;
			const ComputedStyle &ccs = Style(c);
			if (ccs.position == Position::Fixed || world.HasComponent<PopupState>(c)) {
				fixed.push_back(c);
				continue;
			}
			if (world.HasComponent<RangeView>(c)) {
				PlaceRangeView(e, c, content, childClip);
				continue;
			}
			PlacePositioned(c, sdl3::FRect{origin.x, origin.y, cw, ch}, childClip);
		}
		return extent;
	}

	static float JustifyFactor(Justify j) noexcept {
		return j == Justify::Center ? 0.5f : (j == Justify::End ? 1.f : 0.f);
	}
	static float AlignFactor(Align a) noexcept { return a == Align::Center ? 0.5f : (a == Align::End ? 1.f : 0.f); }

	template <typename Record>
	void PlaceFlex(const ComputedStyle &cs, std::vector<ecs::Entity> kids, sdl3::FPoint origin, float cw, float ch,
				   float gap, const sdl3::FRect &childClip, Record &record) {
		const bool row = cs.IsRow();
		if (cs.flexDirection == FlexDirection::RowReverse || cs.flexDirection == FlexDirection::ColumnReverse)
			std::reverse(kids.begin(), kids.end());
		const float mainSize = row ? cw : ch;
		const float crossSize = row ? ch : cw;

		struct Item {
			ecs::Entity e;
			float base = 0.f;
			float grow = 0.f;
			float shrink = 1.f;
			float main = 0.f;
			float minMain = 0.f;
			float cross = 0.f;
			math::Sides margin;
			bool fixedMain = false;
		};
		// Conteneur défilant sur l'axe principal : les éléments débordent.
		const Overflow mainOverflow = row ? cs.overflowX : cs.overflowY;
		const bool scrollsMain = mainOverflow == Overflow::Auto || mainOverflow == Overflow::Scroll;
		std::vector<Item> items;
		items.reserve(kids.size());
		for (ecs::Entity c : kids) {
			const ComputedStyle &ccs = Style(c);
			Item it;
			it.e = c;
			it.margin = Margin(ccs);
			const Length &mainLen = row ? ccs.width : ccs.height;
			const LengthBasis b = Basis(ccs, mainSize);
			const float mMain = row ? it.margin.H() : it.margin.V();
			const float mCross = row ? it.margin.V() : it.margin.H();
			if (const float colW = ColumnWidth(c); colW >= 0.f && row) {
				it.base = colW;
				it.fixedMain = true;
				it.shrink = 0.f;
			} else if (mainLen.IsFr()) {
				it.base = 0.f;
				it.grow = mainLen.value;
			} else if (Definite(mainLen, true)) {
				it.base = mainLen.Resolve(b);
				it.fixedMain = true;
			} else {
				// Colonne : la hauteur dépend de la largeur reçue (retour à la ligne).
				float availW = row ? INF : crossSize - mCross;
				if (!row) {
					const Align align = ccs.alignSelf != Align::Auto ? ccs.alignSelf : cs.alignItems;
					if (Definite(ccs.width, true))
						availW = ccs.width.Resolve(Basis(ccs, cw));
					else if (align != Align::Stretch)
						availW = std::min(availW, crossSize - mCross);
				}
				const sdl3::FPoint s = Measure(c, availW, cw, ch);
				it.base = row ? s.x : s.y;
			}
			if (ccs.flexGrow > 0.f)
				it.grow = std::max(it.grow, ccs.flexGrow);
			// Taille minimale automatique (CSS) : un élément de taille explicite ne
			// rétrécit pas en deçà ; un élément à taille de contenu peut rétrécir
			// (texte tronqué, barre de défilement).
			it.shrink = it.fixedMain || scrollsMain ? 0.f : ccs.flexShrink;
			// `min-width/min-height: auto` : un élément ne rétrécit pas sous la
			// taille minimale de son contenu (texte, bouton, rangée de boutons…) ;
			// un conteneur défilant, lui, peut descendre jusqu'à son min-*.
			if (it.shrink > 0.f)
				it.minMain = std::min(it.base, MinContent(c, row, row ? INF : crossSize, cw, ch));
			// Bornes min/max sur l'axe principal.
			const sdl3::FPoint clamped = ClampSize(ccs, row ? sdl3::FPoint{it.base, 0.f} : sdl3::FPoint{0.f, it.base}, cw, ch);
			it.base = row ? clamped.x : clamped.y;
			(void)mMain;
			items.push_back(it);
		}

		// Lignes (flex-wrap)
		std::vector<std::pair<size_t, size_t>> lines;
		if (cs.flexWrap == FlexWrap::Wrap && row) {
			size_t start = 0;
			float used = 0.f;
			for (size_t i = 0; i < items.size(); ++i) {
				const float w = items[i].base + items[i].margin.H();
				if (i > start && used + gap + w > mainSize) {
					lines.push_back({start, i});
					start = i;
					used = 0.f;
				}
				used += (i > start ? gap : 0.f) + w;
			}
			lines.push_back({start, items.size()});
		} else {
			lines.push_back({0, items.size()});
		}

		float crossCursor = 0.f;
		for (auto [lo, hi] : lines) {
			float used = 0.f, growTotal = 0.f, shrinkTotal = 0.f;
			for (size_t i = lo; i < hi; ++i) {
				used += items[i].base + (row ? items[i].margin.H() : items[i].margin.V());
				growTotal += items[i].grow;
				shrinkTotal += items[i].shrink * items[i].base;
			}
			used += gap * float(hi > lo ? hi - lo - 1 : 0);
			float free = mainSize - used;
			// Rétrécissement : un élément qui atteint son minimum y est gelé et
			// le reste du manque se répartit sur les autres (algorithme CSS).
			std::vector<bool> frozen(hi - lo, false);
			if (free < 0.f && shrinkTotal > 0.f) {
				for (bool changed = true; changed;) {
					changed = false;
					float remaining = free, weights = 0.f;
					for (size_t i = lo; i < hi; ++i) {
						if (frozen[i - lo])
							remaining += items[i].base - items[i].minMain;
						else
							weights += items[i].shrink * items[i].base;
					}
					for (size_t i = lo; i < hi; ++i) {
						Item &it = items[i];
						if (frozen[i - lo])
							continue;
						const float target =
							weights > 0.f ? it.base + std::min(0.f, remaining) * it.shrink * it.base / weights : it.base;
						if (target < it.minMain) {
							frozen[i - lo] = true;
							changed = true;
						}
					}
					if (!changed)
						for (size_t i = lo; i < hi; ++i)
							if (!frozen[i - lo])
								items[i].main = weights > 0.f ? items[i].base + std::min(0.f, remaining) * items[i].shrink *
																					   items[i].base / weights
															  : items[i].base;
				}
			}
			for (size_t i = lo; i < hi; ++i) {
				Item &it = items[i];
				if (!(free < 0.f && shrinkTotal > 0.f))
					it.main = it.base;
				else if (frozen[i - lo])
					it.main = it.minMain;
				if (free > 0.f && growTotal > 0.f)
					it.main += free * it.grow / growTotal;
				const ComputedStyle &ccs = Style(it.e);
				const sdl3::FPoint clamped =
					ClampSize(ccs, row ? sdl3::FPoint{it.main, 0.f} : sdl3::FPoint{0.f, it.main}, cw, ch);
				it.main = row ? clamped.x : clamped.y;
			}
			float usedAfter = 0.f;
			for (size_t i = lo; i < hi; ++i)
				usedAfter += items[i].main + (row ? items[i].margin.H() : items[i].margin.V());
			usedAfter += gap * float(hi > lo ? hi - lo - 1 : 0);
			free = std::max(0.f, mainSize - usedAfter);

			// Axe secondaire : taille de chaque élément, hauteur de ligne.
			const bool singleLine = lines.size() == 1;
			float lineCross = singleLine ? crossSize : 0.f;
			for (size_t i = lo; i < hi; ++i) {
				Item &it = items[i];
				const ComputedStyle &ccs = Style(it.e);
				const Length &crossLen = row ? ccs.height : ccs.width;
				const float mCross = row ? it.margin.V() : it.margin.H();
				const Align align = ccs.alignSelf != Align::Auto ? ccs.alignSelf : cs.alignItems;
				if (Definite(crossLen, true) && !crossLen.IsFr()) {
					it.cross = crossLen.Resolve(Basis(ccs, crossSize));
				} else if ((align == Align::Stretch || crossLen.IsFr()) && singleLine) {
					it.cross = crossSize - mCross;
				} else {
					const sdl3::FPoint s = Measure(it.e, row ? it.main : crossSize - mCross, cw, ch);
					it.cross = row ? s.y : s.x;
					if (!row && align == Align::Stretch)
						it.cross = crossSize - mCross;
				}
				const sdl3::FPoint clamped =
					ClampSize(ccs, row ? sdl3::FPoint{0.f, it.cross} : sdl3::FPoint{it.cross, 0.f}, cw, ch);
				it.cross = row ? clamped.y : clamped.x;
				if (!singleLine)
					lineCross = std::max(lineCross, it.cross + mCross);
			}
			if (!singleLine)
				for (size_t i = lo; i < hi; ++i) {
					const ComputedStyle &ccs = Style(items[i].e);
					const Align align = ccs.alignSelf != Align::Auto ? ccs.alignSelf : cs.alignItems;
					const Length &crossLen = row ? ccs.height : ccs.width;
					if (align == Align::Stretch && crossLen.IsAuto())
						items[i].cross = lineCross - (row ? items[i].margin.V() : items[i].margin.H());
				}

			// Répartition sur l'axe principal.
			const size_t n = hi - lo;
			float cursor = 0.f, spacing = gap;
			switch (cs.justifyContent) {
			case Justify::Start:
				break;
			case Justify::Center:
				cursor = free * 0.5f;
				break;
			case Justify::End:
				cursor = free;
				break;
			case Justify::SpaceBetween:
				if (n > 1)
					spacing = gap + free / float(n - 1);
				break;
			case Justify::SpaceAround:
				if (n > 0) {
					spacing = gap + free / float(n);
					cursor = free / float(n) * 0.5f;
				}
				break;
			case Justify::SpaceEvenly:
				spacing = gap + free / float(n + 1);
				cursor = free / float(n + 1);
				break;
			}
			for (size_t i = lo; i < hi; ++i) {
				Item &it = items[i];
				const ComputedStyle &ccs = Style(it.e);
				const Align align = ccs.alignSelf != Align::Auto ? ccs.alignSelf : cs.alignItems;
				const float mBefore = row ? it.margin.left : it.margin.top;
				const float mAfter = row ? it.margin.right : it.margin.bottom;
				const float cBefore = row ? it.margin.top : it.margin.left;
				const float cAfter = row ? it.margin.bottom : it.margin.right;
				const float slot = singleLine ? crossSize : lineCross;
				const float crossPos = crossCursor + cBefore + (slot - cBefore - cAfter - it.cross) * AlignFactor(align);
				const float mainPos = cursor + mBefore;
				sdl3::FRect b = row ? sdl3::FRect{origin.x + mainPos, origin.y + crossPos, it.main, it.cross}
									: sdl3::FRect{origin.x + crossPos, origin.y + mainPos, it.cross, it.main};
				// Positionnement relatif : décalage visuel sans effet sur le flux.
				if (!ccs.left.IsAuto())
					b.x += ccs.left.Resolve(Basis(ccs, cw));
				if (!ccs.top.IsAuto())
					b.y += ccs.top.Resolve(Basis(ccs, ch));
				Place(it.e, b, childClip);
				record(b, it.margin);
				cursor += mBefore + it.main + mAfter + spacing;
			}
			crossCursor += lineCross + gap;
		}
	}

	/// Widget positionné (absolute) dans la boîte `ref` (ou la fenêtre pour Fixed).
	void PlacePositioned(ecs::Entity c, const sdl3::FRect &ref, const sdl3::FRect &clip) {
		const ComputedStyle &ccs = Style(c);
		const LengthBasis bw = Basis(ccs, ref.w), bh = Basis(ccs, ref.h);
		sdl3::FPoint s{0.f, 0.f};
		const bool hasL = !ccs.left.IsAuto(), hasR = !ccs.right.IsAuto();
		const bool hasT = !ccs.top.IsAuto(), hasB = !ccs.bottom.IsAuto();
		const sdl3::FPoint m = Measure(c, hasL && hasR ? ref.w : INF, ref.w, ref.h);
		s = m;
		if (Definite(ccs.width, true))
			s.x = ccs.width.Resolve(bw);
		else if (hasL && hasR)
			s.x = ref.w - ccs.left.Resolve(bw) - ccs.right.Resolve(bw);
		else if (ccs.width.IsFr())
			s.x = ref.w;
		if (Definite(ccs.height, true))
			s.y = ccs.height.Resolve(bh);
		else if (hasT && hasB)
			s.y = ref.h - ccs.top.Resolve(bh) - ccs.bottom.Resolve(bh);
		else if (ccs.height.IsFr())
			s.y = ref.h;
		s = ClampSize(ccs, s, ref.w, ref.h);
		sdl3::FPoint f = AnchorFactors(ccs.anchor);
		if (!hasL && hasR)
			f.x = 1.f;
		if (!hasT && hasB)
			f.y = 1.f;
		float x = ref.x + (ref.w - s.x) * f.x;
		float y = ref.y + (ref.h - s.y) * f.y;
		if (hasL)
			x += ccs.left.Resolve(bw);
		else if (hasR)
			x -= ccs.right.Resolve(bw);
		if (hasT)
			y += ccs.top.Resolve(bh);
		else if (hasB)
			y -= ccs.bottom.Resolve(bh);
		Place(c, {x, y, s.x, s.y}, clip);
	}

	/// Partie liée à une valeur (piste remplie, pouce, interrupteur…).
	void PlaceRangeView(ecs::Entity parent, ecs::Entity c, const sdl3::FRect &content, const sdl3::FRect &clip) {
		const RangeView view = *world.GetComponent<RangeView>(c).Unwrap();
		const ComputedStyle &ccs = Style(c);
		const bool horizontal = view.axis == Orientation::Horizontal;
		sdl3::FPoint s = Measure(c, content.w, content.w, content.h);
		if (Definite(ccs.width, true))
			s.x = ccs.width.Resolve(Basis(ccs, content.w));
		if (Definite(ccs.height, true))
			s.y = ccs.height.Resolve(Basis(ccs, content.h));
		float t = 0.f;
		sdl3::FPoint t2{0.f, 0.f};
		if (view.part == RangePart::CheckThumb) {
			const ecs::Entity src = NearestWith<Check>(world, parent);
			if (src.Valid())
				t = world.GetComponent<Check>(src).Unwrap()->anim;
		} else if (view.part == RangePart::Thumb2D) {
			const ecs::Entity src = NearestWith<Range2D>(world, parent);
			if (src.Valid())
				t2 = world.GetComponent<Range2D>(src).Unwrap()->value;
		} else {
			const ecs::Entity src = NearestWith<Range>(world, parent);
			if (src.Valid()) {
				const Range r = *world.GetComponent<Range>(src).Unwrap();
				t = r.Normalized();
				if (view.part == RangePart::Thumb && r.page > 0.f && r.Span() > 0.f) {
					// Barre de défilement : pouce proportionnel à la portion visible.
					const float ratio = math::Clamp(r.page / r.Span(), 0.05f, 1.f);
					if (horizontal)
						s.x = std::max(s.x, content.w * ratio);
					else
						s.y = std::max(s.y, content.h * ratio);
				}
			}
		}
		if (view.invert)
			t = 1.f - t;
		sdl3::FRect b{content.x, content.y, s.x, s.y};
		switch (view.part) {
		case RangePart::Fill:
			if (horizontal) {
				b.w = content.w * t;
				b.h = Definite(ccs.height, true) ? s.y : content.h;
				b.y = content.y + (content.h - b.h) * 0.5f;
			} else {
				b.h = content.h * t;
				b.w = Definite(ccs.width, true) ? s.x : content.w;
				b.x = content.x + (content.w - b.w) * 0.5f;
				if (!view.invert)
					b.y = content.y + content.h - b.h;
			}
			break;
		case RangePart::Thumb:
		case RangePart::CheckThumb:
			if (horizontal) {
				b.x = content.x + (content.w - s.x) * t;
				b.y = content.y + (content.h - s.y) * 0.5f;
			} else {
				b.y = content.y + (content.h - s.y) * (view.invert ? t : 1.f - t);
				b.x = content.x + (content.w - s.x) * 0.5f;
			}
			break;
		case RangePart::Thumb2D:
			b.x = content.x + content.w * t2.x - s.x * 0.5f;
			b.y = content.y + content.h * t2.y - s.y * 0.5f;
			break;
		case RangePart::Angle:
			b = content;
			break;
		}
		Place(c, b, clip);
	}

	/// Popups et widgets Fixed, après le flux (leur ancre est placée).
	void PlaceFixed() {
		const sdl3::FRect viewport{0.f, 0.f, ctx.viewport.x, ctx.viewport.y};
		// Ordre : z-index, puis ordre d'ouverture des popups, puis ordre de
		// l'arbre. Un Fixed peut en contenir d'autres (sous-menus) : traités
		// par vagues successives.
		while (!fixed.empty()) {
			std::vector<ecs::Entity> batch;
			batch.swap(fixed);
			std::stable_sort(batch.begin(), batch.end(),
							 [&](ecs::Entity a, ecs::Entity b) { return SortKey(a) < SortKey(b); });
			for (ecs::Entity e : batch) {
				const size_t before = ctx.paintOrder.size();
				if (auto p = world.GetComponent<PopupState>(e); p.IsSome()) {
					const PopupState popup = *p.Unwrap();
					PlacePopup(e, popup, viewport);
					if (popup.modal && before < ctx.paintOrder.size())
						ctx.paintOrder[before].blocksBelow = true;
				} else {
					PlacePositioned(e, viewport, viewport);
				}
			}
		}
	}

	std::pair<int, int> SortKey(ecs::Entity e) const {
		int order = -1;
		for (size_t i = 0; i < ctx.input.popups.size(); ++i)
			if (ctx.input.popups[i] == e)
				order = int(i);
		return {Style(e).zIndex, order};
	}

	void PlacePopup(ecs::Entity e, const PopupState &popup, const sdl3::FRect &viewport) {
		const ComputedStyle &cs = Style(e);
		if (popup.modal || popup.placement == Placement::Center) {
			if (popup.modal) {
				// Voile plein écran ; le contenu (enfants) se centre par le style.
				Place(e, viewport, viewport);
				return;
			}
		}
		ecs::Entity anchor = popup.anchor;
		if (!anchor.Valid() || !world.IsAlive(anchor))
			if (auto parent = ecs::ParentOf(world, e); parent.IsSome())
				anchor = parent.Unwrap();
		sdl3::FRect a{popup.at.x, popup.at.y, 0.f, 0.f};
		if (popup.placement != Placement::Point && anchor.Valid())
			if (auto r = world.GetComponent<Rect>(anchor); r.IsSome())
				a = r.Unwrap()->box;
		sdl3::FPoint s = Measure(e, viewport.w, viewport.w, viewport.h);
		if (Definite(cs.width, true))
			s.x = cs.width.Resolve(Basis(cs, viewport.w));
		if (Definite(cs.height, true))
			s.y = cs.height.Resolve(Basis(cs, viewport.h));
		if (popup.matchAnchorWidth)
			s.x = std::max(s.x, a.w);
		s.x = std::min(s.x, viewport.w);
		s.y = std::min(s.y, viewport.h);
		float x = a.x, y = a.y + a.h;
		switch (popup.placement) {
		case Placement::Below:
			break;
		case Placement::BelowRight:
			x = a.x + a.w - s.x;
			break;
		case Placement::Above:
			y = a.y - s.y;
			break;
		case Placement::Right: {
			// La première entrée du sous-menu s'aligne sur celle qui l'ouvre.
			x = a.x + a.w;
			y = a.y - Insets(cs).top;
			break;
		}
		case Placement::Left:
			x = a.x - s.x;
			y = a.y - Insets(cs).top;
			break;
		case Placement::Center:
			x = viewport.x + (viewport.w - s.x) * 0.5f;
			y = viewport.y + (viewport.h - s.y) * 0.5f;
			break;
		case Placement::Point:
			x = popup.at.x;
			y = popup.at.y;
			break;
		}
		// Reste dans la fenêtre : bascule de côté plutôt que de déborder.
		if (y + s.y > viewport.y + viewport.h) {
			if (popup.placement == Placement::Below || popup.placement == Placement::BelowRight)
				y = a.y - s.y;
			if (y + s.y > viewport.y + viewport.h)
				y = viewport.y + viewport.h - s.y;
		}
		if (x + s.x > viewport.x + viewport.w) {
			if (popup.placement == Placement::Right)
				x = a.x - s.x;
			if (x + s.x > viewport.x + viewport.w)
				x = viewport.x + viewport.w - s.x;
		}
		x = std::max(viewport.x, x);
		y = std::max(viewport.y, y);
		Place(e, {x, y, s.x, s.y}, viewport);
	}

	/// Composants d'état que la passe écrit (créés avant, pour qu'aucune
	/// migration d'archétype n'invalide les références pendant la passe).
	void EnsureComponents(ecs::Entity e) {
		if (!world.IsAlive(e))
			return;
		const ComputedStyle cs = Style(e);
		const bool scrolls = cs.Clips() && cs.overflowX != Overflow::Hidden && cs.overflowX != Overflow::Visible;
		const bool scrollsY = cs.overflowY == Overflow::Auto || cs.overflowY == Overflow::Scroll;
		const bool text = world.HasComponent<TextContent>(e) || world.HasComponent<TextEditor>(e);
		if ((scrolls || scrollsY || (text && (cs.textOverflow == TextOverflow::Scroll ||
											   cs.textOverflow == TextOverflow::Wrap || world.HasComponent<TextEditor>(e)))) &&
			!world.HasComponent<Scroll>(e))
			world.AddComponent(e, Scroll{});
		if (text && cs.textOverflow == TextOverflow::Marquee && !world.HasComponent<Marquee>(e))
			world.AddComponent(e, Marquee{});
		if (!world.HasComponent<Rect>(e))
			world.AddComponent(e, Rect{});
		for (ecs::Entity c : std::vector<ecs::Entity>(ChildList(world, e)))
			EnsureComponents(c);
	}

	void ResetHidden(ecs::Entity e, bool hidden) {
		if (hidden)
			if (auto r = world.GetComponent<Rect>(e); r.IsSome())
				r.Unwrap()->visible = false;
		for (ecs::Entity c : ChildList(world, e))
			ResetHidden(c, hidden || !Shown(c));
	}
};

} // namespace

bool LayoutSystem::Run(ecs::ArchetypeRegistry &world, Context &ctx, bool force) {
	if (!force && !ctx.layoutDirty)
		return false;
	if (!world.IsAlive(ctx.root))
		return false;
	// Une passe peut en demander une autre (barres de défilement apparues) :
	// au plus trois pour converger.
	for (int iteration = 0; iteration < 3; ++iteration) {
		ctx.layoutDirty = false;
		Pass pass(world, ctx);
		pass.EnsureComponents(ctx.root);
		pass.ResetHidden(ctx.root, false);
		ctx.paintOrder.clear();
		const sdl3::FRect viewport{0.f, 0.f, ctx.viewport.x, ctx.viewport.y};
		pass.Place(ctx.root, viewport, viewport);
		pass.PlaceFixed();
		++ctx.layoutPass;
		if (!ctx.layoutDirty)
			break;
	}
	ctx.layoutDirty = false;
	return true;
}

} // namespace ui
