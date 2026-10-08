// Définitions de ui/systems.hpp
#include "ui/systems.hpp"

namespace ui {

float ClampOpt(float v, const Option<float> &lo, const Option<float> &hi) noexcept {
	if (lo.IsSome())
		v = sdl3::Max(v, lo.Unwrap());
	if (hi.IsSome())
		v = sdl3::Min(v, hi.Unwrap());
	return v;
}

math::Sides BorderSides(const ResolvedStyle &rs) noexcept {
	const float bw = float(sdl3::Max(1, int(rs.BordersWidth(math::Sides(1.f)).top)));
	return math::Sides(bw);
}

sdl3::FRect VScrollbarTrackRect(const UiRect &r, const sdl3::FRect &screen) noexcept {
	return {screen.x + screen.w - K_SCROLLBAR_THICKNESS, screen.y, K_SCROLLBAR_THICKNESS,
			sdl3::Max(0.f, screen.h - r.gutter.y)};
}

sdl3::FRect HScrollbarTrackRect(const UiRect &r, const sdl3::FRect &screen) noexcept {
	return {screen.x, screen.y + screen.h - K_SCROLLBAR_THICKNESS, sdl3::Max(0.f, screen.w - r.gutter.x),
			K_SCROLLBAR_THICKNESS};
}

sdl3::FRect VScrollbarThumbRect(const UiRect &r, const sdl3::FRect &screen) noexcept {
	float maxY = r.MaxScroll().y;
	if (maxY <= 0.f)
		return {};
	const sdl3::FRect track = VScrollbarTrackRect(r, screen);
	float trackH = track.h;
	float thumbH = sdl3::Max(K_SCROLLBAR_MIN_THUMB, trackH * r.ViewSize().y / r.ContentSize().y);
	thumbH = sdl3::Min(thumbH, trackH);
	float t = maxY > 0.f ? r.scroll.y / maxY : 0.f;
	float thumbY = track.y + t * (trackH - thumbH);
	return {track.x, thumbY, K_SCROLLBAR_THICKNESS, thumbH};
}

sdl3::FRect HScrollbarThumbRect(const UiRect &r, const sdl3::FRect &screen) noexcept {
	float maxX = r.MaxScroll().x;
	if (maxX <= 0.f)
		return {};
	const sdl3::FRect track = HScrollbarTrackRect(r, screen);
	float trackW = track.w;
	float thumbW = sdl3::Max(K_SCROLLBAR_MIN_THUMB, trackW * r.ViewSize().x / r.ContentSize().x);
	thumbW = sdl3::Min(thumbW, trackW);
	float t = maxX > 0.f ? r.scroll.x / maxX : 0.f;
	float thumbX = track.x + t * (trackW - thumbW);
	return {thumbX, track.y, thumbW, K_SCROLLBAR_THICKNESS};
}

sdl3::FRect InsetRect(const sdl3::FRect &r, float inset) noexcept {
	return {r.x + inset, r.y + inset, sdl3::Max(0.f, r.w - 2.f * inset), sdl3::Max(0.f, r.h - 2.f * inset)};
}

float TextOriginX(const sdl3::FRect &box, float textWidth, TextAlign align, float inset) noexcept {
	float x = box.x + inset;
	if (align == TextAlign::Center)
		x = box.x + (box.w - textWidth) * 0.5f;
	else if (align == TextAlign::Right)
		x = box.x + box.w - textWidth - inset;
	return sdl3::Clamp(x, box.x, box.x + sdl3::Max(0.f, box.w - textWidth));
}

sdl3::Rect ToClipRect(const sdl3::FRect &r) noexcept {
	const int x0 = int(sdl3::Floor(r.x));
	const int y0 = int(sdl3::Floor(r.y));
	const int x1 = int(sdl3::Ceil(r.x + r.w));
	const int y1 = int(sdl3::Ceil(r.y + r.h));
	return sdl3::Rect{x0, y0, x1 > x0 ? x1 - x0 : 0, y1 > y0 ? y1 - y0 : 0};
}

size_t CharCount(const String &text) noexcept {
	size_t count = 0;
	for (size_t i = 0; i < text.size(); ++i)
		if ((static_cast<unsigned char>(text[i]) & 0xC0) != 0x80)
			++count;
	return count;
}

size_t DisplayCells(const String &text) noexcept {
	const auto *p = reinterpret_cast<const uint8_t *>(text.c_str());
	const auto *end = p + text.size();
	size_t cells = 0;
	while (p < end)
		cells += size_t(CodepointCells(char32_t(unicode::DecodeNext(p, end))));
	return cells;
}

int CellsAt(const String &text, size_t i) noexcept {
	const auto *p = reinterpret_cast<const uint8_t *>(text.c_str()) + i;
	return CodepointCells(char32_t(unicode::DecodeNext(p, reinterpret_cast<const uint8_t *>(text.c_str()) + text.size())));
}

bool IsNonPrintableControl(unsigned char c) noexcept {
	return (c < 0x20 && c != '\n' && c != '\r' && c != '\t') || c == 0x7F;
}

String NormalizeDisplayText(const String &text, int tabStop) {
	bool hasControl = false;
	for (size_t i = 0; i < text.size(); ++i) {
		const unsigned char c = static_cast<unsigned char>(text[i]);
		if (c < 0x20 || c == 0x7F) {
			hasControl = true;
			break;
		}
	}
	if (!hasControl)
		return text;

	const int stop = tabStop > 0 ? tabStop : 1;
	String out;
	out.Reserve(text.size() + 8);
	int column = 0;
	for (size_t i = 0; i < text.size(); ++i) {
		const unsigned char c = static_cast<unsigned char>(text[i]);
		if (c == '\r') {
			// `\r\n` est UN saut de ligne, pas deux ; un `\r` seul (vieux
			// fichiers Mac, sorties de terminal) en est un aussi.
			if (i + 1 < text.size() && text[i + 1] == '\n')
				++i;
			out.PushBack('\n');
			column = 0;
			continue;
		}
		if (c == '\n') {
			out.PushBack('\n');
			column = 0;
			continue;
		}
		if (c == '\t') {
			const int spaces = stop - (column % stop);
			for (int k = 0; k < spaces; ++k)
				out.PushBack(' ');
			column += spaces;
			continue;
		}
		if (IsNonPrintableControl(c))
			continue;
		out.PushBack(static_cast<char>(c));
		// Octet de continuation UTF-8 (10xxxxxx) : même caractère, donc même
		// colonne.
		if ((c & 0xC0) != 0x80)
			++column;
	}
	return out;
}

size_t DisplayColumn(const String &line, size_t byteOffset, int tabStop) noexcept {
	const int stop = tabStop > 0 ? tabStop : 1;
	size_t column = 0;
	const size_t end = sdl3::Min(byteOffset, line.size());
	for (size_t i = 0; i < end; ++i) {
		const unsigned char c = static_cast<unsigned char>(line[i]);
		if (c == '\t')
			column += size_t(stop) - (column % size_t(stop));
		else if (IsNonPrintableControl(c) || c == '\n' || c == '\r')
			continue; // retiré à l'affichage : n'occupe aucune cellule
		else if ((c & 0xC0) != 0x80)
			column += size_t(CellsAt(line, i)); // 2 pour un idéogramme, 0 pour un accent combinant
	}
	return column;
}

size_t DisplayColumns(const String &line, int tabStop) noexcept {
	return DisplayColumn(line, line.size(), tabStop);
}

size_t ColumnToByteOffset(const String &line, size_t column, int tabStop) noexcept {
	const int stop = tabStop > 0 ? tabStop : 1;
	size_t current = 0;
	size_t i = 0;
	while (i < line.size()) {
		// Fin du caractère commencé en `i` (les octets 10xxxxxx en font partie).
		size_t next = i + 1;
		while (next < line.size() && (static_cast<unsigned char>(line[next]) & 0xC0) == 0x80)
			++next;

		const unsigned char c = static_cast<unsigned char>(line[i]);
		size_t width = size_t(CellsAt(line, i));
		if (c == '\t')
			width = size_t(stop) - (current % size_t(stop));
		else if (IsNonPrintableControl(c) || c == '\n' || c == '\r')
			width = 0;

		if (column <= current)
			return i;
		if (column < current + width)
			return (column - current) * 2 <= width ? i : next;
		current += width;
		i = next;
	}
	return line.size();
}

TextMetrics MeasureTextBlock(const String &text, float fontSize,
		const std::function<sdl3::FPoint(const String &, float)> &measureLine,
		int tabStop) {
	TextMetrics metrics;
	metrics.lineHeight = LineHeightApprox(fontSize);
	if (!measureLine)
		return metrics;

	const String normalized = NormalizeDisplayText(text, tabStop);
	size_t start = 0;
	float hookHeight = 0.f;
	metrics.lineCount = 0;
	while (true) {
		const size_t nl = normalized.Find('\n', start);
		const String line = nl == String::npos ? normalized.Substr(start) : normalized.Substr(start, nl - start);
		const sdl3::FPoint size = measureLine(line, fontSize);
		metrics.width = sdl3::Max(metrics.width, size.x);
		hookHeight = sdl3::Max(hookHeight, size.y);
		++metrics.lineCount;
		if (nl == String::npos)
			break;
		start = nl + 1;
	}
	// Hauteur de ligne : celle que la mesure branchée rapporte, et l'
	// heuristique SEULEMENT si elle n'en rapporte aucune (une ligne vide rend
	// souvent une hauteur nulle). Prendre le maximum des deux paraîtrait plus
	// prudent mais serait faux : l'heuristique (1,3 x la taille de police)
	// dépasse la hauteur réelle d'une police donnée, et la place réservée ne
	// correspondrait plus à la place dessinée — la propriété même que ce
	// fichier existe pour garantir.
	if (hookHeight > 0.f)
		metrics.lineHeight = hookHeight;
	metrics.height = metrics.lineHeight * float(metrics.lineCount);
	return metrics;
}

std::vector<WrappedLine> WrapTextToWidth(const String &text, float fontSize, float maxWidth,
		const std::function<sdl3::FPoint(const String &, float)> &measureLine,
		int tabStop) {
	std::vector<WrappedLine> lines;
	if (!measureLine)
		return lines;
	const String normalized = NormalizeDisplayText(text, tabStop);
	auto widthOf = [&](const String &s) { return s.IsEmpty() ? 0.f : measureLine(s, fontSize).x; };

	size_t paraStart = 0;
	while (true) {
		const size_t nl = normalized.Find('\n', paraStart);
		const String paragraph =
			nl == String::npos ? normalized.Substr(paraStart) : normalized.Substr(paraStart, nl - paraStart);

		String current;
		size_t i = 0;
		while (i <= paragraph.size()) {
			// Mot suivant = jusqu'au prochain espace (inclus : l'espace reste
			// collé au mot, pour que la largeur mesurée soit celle dessinée).
			size_t space = paragraph.Find(' ', i);
			const size_t end = space == String::npos ? paragraph.size() : space + 1;
			if (i >= paragraph.size())
				break;
			const String word = paragraph.Substr(i, end - i);
			const String candidate = current.IsEmpty() ? word : current + word;
			if (maxWidth > 0.f && widthOf(candidate) > maxWidth && !current.IsEmpty()) {
				lines.push_back({current, widthOf(current), false});
				current = word;
			} else if (maxWidth > 0.f && widthOf(candidate) > maxWidth) {
				// Mot seul trop long : coupé au caractère, sur une frontière
				// UTF-8 valide.
				String piece;
				for (size_t k = 0; k < word.size();) {
					size_t next = k + 1;
					while (next < word.size() && (static_cast<unsigned char>(word[next]) & 0xC0) == 0x80)
						++next;
					const String grown = piece + word.Substr(k, next - k);
					if (!piece.IsEmpty() && widthOf(grown) > maxWidth) {
						lines.push_back({piece, widthOf(piece), false});
						piece = word.Substr(k, next - k);
					} else {
						piece = grown;
					}
					k = next;
				}
				current = piece;
			} else {
				current = candidate;
			}
			i = end;
		}
		lines.push_back({current, widthOf(current), true});

		if (nl == String::npos)
			break;
		paraStart = nl + 1;
	}
	return lines;
}

float WrappedHeight(const String &text, float fontSize, float maxWidth,
		const std::function<sdl3::FPoint(const String &, float)> &measureLine,
		int tabStop) {
	const std::vector<WrappedLine> lines = WrapTextToWidth(text, fontSize, maxWidth, measureLine, tabStop);
	float lineHeight = LineHeightApprox(fontSize);
	if (measureLine)
		if (const float measured = measureLine(String("Ag"), fontSize).y; measured > 0.f)
			lineHeight = measured;
	return lineHeight * float(sdl3::Max(size_t(1), lines.size()));
}

std::vector<TextLine> WrapText(const String &text, float fontSize, float maxWidth) {
	std::vector<TextLine> lines;
	float cw = CharWidthApprox(fontSize);
	size_t maxChars = cw > 0.f ? sdl3::Max(1, int(maxWidth / cw)) : text.size() + 1;
	size_t start = 0, n = text.size();
	while (start < n) {
		size_t remaining = n - start;
		if (remaining <= maxChars) {
			lines.push_back({start, text.Substr(start)});
			break;
		}
		size_t breakAt = start + maxChars;
		size_t lastSpace = text.Rfind(' ', breakAt - 1);
		bool useSpace = lastSpace != String::npos && lastSpace >= start && lastSpace < breakAt;
		size_t cut = useSpace ? lastSpace : breakAt;
		lines.push_back({start, text.Substr(start, cut - start)});
		start = useSpace ? cut + 1 : cut; // saute l'espace de coupure
	}
	if (lines.empty())
		lines.push_back({0, String()});
	return lines;
}

String StripTrailingReturn(String line) {
	if (!line.IsEmpty() && line[line.size() - 1] == '\r')
		return line.Substr(0, line.size() - 1);
	return line;
}

std::vector<TextLine> SplitLines(const String &text) {
	std::vector<TextLine> lines;
	size_t start = 0;
	while (true) {
		size_t nl = text.Find('\n', start);
		if (nl == String::npos) {
			lines.push_back({start, StripTrailingReturn(text.Substr(start))});
			break;
		}
		// `\r` de fin de ligne avalé : un texte collé depuis un fichier CRLF
		// afficherait sinon une boîte « glyphe manquant » au bout de CHAQUE
		// ligne. Les décalages restent justes — seul le contenu affiché de la
		// ligne perd son dernier octet.
		lines.push_back({start, StripTrailingReturn(text.Substr(start, nl - start))});
		start = nl + 1;
	}
	return lines;
}

size_t LineIndexOf(const std::vector<TextLine> &lines, size_t pos) noexcept {
	size_t li = 0;
	for (size_t i = 0; i < lines.size(); ++i) {
		if (pos >= lines[i].offset)
			li = i;
		else
			break;
	}
	return li;
}

size_t PrevCodepoint(const String &text, size_t pos) noexcept {
	if (pos == 0)
		return 0;
	size_t n = pos - 1;
	while (n > 0 && (uint8_t(text.c_str()[n]) & 0xC0) == 0x80)
		--n;
	return n;
}

size_t NextCodepoint(const String &text, size_t pos) noexcept {
	size_t n = text.size();
	if (pos >= n)
		return n;
	++pos;
	while (pos < n && (uint8_t(text.c_str()[pos]) & 0xC0) == 0x80)
		++pos;
	return pos;
}

size_t HitTestOffset(const std::vector<TextLine> &lines, const String &text, float fontSize,
		const sdl3::FRect &s, const sdl3::FPoint &scroll, float mx, float my,
		int tabStop, float cellWidth) noexcept {
	if (lines.empty())
		return 0;
	float lh = LineHeightApprox(fontSize);
	float cw = cellWidth > 0.f ? cellWidth : CharWidthApprox(fontSize);
	float relY = my - (s.y + 4.f - scroll.y);
	int li = int(relY < 0.f ? 0.f : relY / lh);
	li = int(sdl3::Clamp(float(li), 0.f, float(lines.size() - 1)));
	const TextLine &line = lines[size_t(li)];
	float relX = mx - (s.x + 8.f - scroll.x);
	int col = cw > 0.f ? int(relX / cw + 0.5f) : 0;
	col = int(sdl3::Clamp(float(col), 0.f, float(DisplayColumns(line.text, tabStop))));
	// Colonne D'AFFICHAGE -> octet : une tabulation occupe plusieurs cellules
	// et un caractère accentué une seule pour deux octets, donc l'un n'est pas
	// l'autre. La conversion retombe toujours sur une frontière de caractère.
	(void)text;
	return line.offset + ColumnToByteOffset(line.text, size_t(col), tabStop);
}

void ScrollIntoView(UiRect &r, const UiComputed &c, size_t lineIndex, float fontSize) noexcept {
	float lh = LineHeightApprox(fontSize);
	float lineTop = float(lineIndex) * lh;
	(void)c;
	float viewH = sdl3::Max(1.f, r.ViewSize().y - 8.f);
	if (lineTop < r.scroll.y)
		r.scroll.y = lineTop;
	else if (lineTop + lh > r.scroll.y + viewH)
		r.scroll.y = lineTop + lh - viewH;
	r.ClampScroll();
}

void ScrollColumnIntoView(UiRect &r, float viewWidth, size_t column, float cellWidth) noexcept {
	const float x = float(column) * cellWidth;
	const float margin = cellWidth * 2.f;
	const float view = sdl3::Max(1.f, viewWidth - 16.f);
	if (x - margin < r.scroll.x)
		r.scroll.x = x - margin;
	else if (x + margin > r.scroll.x + view)
		r.scroll.x = x + margin - view;
	r.ClampScroll();
}

float AreaCellWidth(const UiInputArea &f, float fontSize) noexcept {
	return f.cellWidth > 0.f ? f.cellWidth : CharWidthApprox(fontSize);
}

float AreaGutterWidth(const UiInputArea &f, size_t lineCount, float cellWidth) noexcept {
	if (!f.lineNumbers)
		return 0.f;
	size_t digits = 1;
	for (size_t n = lineCount; n >= 10; n /= 10)
		++digits;
	return float(sdl3::Max<size_t>(digits, 3)) * cellWidth + 16.f;
}

sdl3::FRect AreaTextBox(const sdl3::FRect &screen, float gutter) noexcept {
	return {screen.x + gutter, screen.y, sdl3::Max(0.f, screen.w - gutter), screen.h};
}

// ── LayoutSystem ─────────────────────────────────────────────────────────────

sdl3::FPoint LayoutSystem::MeasureText(const String &text, float fontSize) const {
	const TextMetrics metrics = MeasureTextBlock(text, fontSize, measureText, tabStop);
	return {metrics.width, metrics.height};
}

sdl3::FPoint LayoutSystem::MeasureText(std::u16string_view text, float fontSize) const {
	return MeasureText(String::FromUtf16(text), fontSize);
}

sdl3::FPoint LayoutSystem::MeasureText(std::u32string_view text, float fontSize) const {
	return MeasureText(String::FromUtf32(text), fontSize);
}

TextMetrics LayoutSystem::MeasureTextMetrics(const String &text, float fontSize) const {
	return MeasureTextBlock(text, fontSize, measureText, tabStop);
}

bool LayoutSystem::RunIfNeeded(ecs::ArchetypeRegistry &world, float screenW, float screenH) {
	if (!dirty && screenW == lastW && screenH == lastH)
		return false;
	Run(world, screenW, screenH);
	return true;
}

void LayoutSystem::Run(ecs::ArchetypeRegistry &world, float screenW, float screenH) {
	++passes;
	lastW = screenW;
	lastH = screenH;
	dirty = false;

	Pass pass{*this, world, screenW, screenH};
	pass.Snapshot();

	sdl3::FRect full{0.f, 0.f, screenW, screenH};
	for (ecs::Entity root : pass.roots) {
		auto [w, h] = pass.ResolveSize(root, screenW, screenH);
		sdl3::FRect screen = pass.rects[root.id].ResolveIn({0.f, 0.f}, screenW, screenH);
		screen.w = w;
		screen.h = h;
		// Un popup racine (menu déroulant, menu contextuel ouvert près d'un
		// bord) est RAMENÉ dans la fenêtre : ouvert au clic droit tout en
		// bas de l'écran, il se déploierait sinon hors de vue.
		if (world.HasComponent<UiPopupState>(root)) {
			screen.x = sdl3::Clamp(screen.x, 0.f, sdl3::Max(0.f, screenW - w));
			screen.y = sdl3::Clamp(screen.y, 0.f, sdl3::Max(0.f, screenH - h));
		}
		pass.Place(root, screen, full);
	}

	pass.WriteBack();
}

// ── LayoutSystem::Pass ───────────────────────────────────────────────────────

void LayoutSystem::Pass::Snapshot() {
	world.Query<UiRect>([&](ecs::Entity e, UiRect &r) { rects[e.id] = r; });
	world.Query<UiFlow>([&](ecs::Entity e, UiFlow &f) { flows[e.id] = f; });
	world.Query<UiItem>([&](ecs::Entity e, UiItem &i) { items[e.id] = i; });
	world.Query<UiChildren>([&](ecs::Entity e, UiChildren &c) { children[e.id] = c.list; });
	world.Query<UiHidden>([&](ecs::Entity e, UiHidden &) { hidden.insert(e.id); });
	// Même règle que RenderSystem::DrawWidget (UiPanel) : une bordure
	// n'est dessinée — donc n'occupe de place — que si elle a une couleur.
	world.Query<UiPanel>([&](ecs::Entity e, UiPanel &) {
		ResolvedStyle rs = GetResolved(world, e);
		if (rs.HasBorderColor())
			borders[e.id] = BorderSides(rs);
	});
	world.Query<UiRect>([&](ecs::Entity e, UiRect &) {
		if (!world.HasComponent<UiParent>(e) && !hidden.contains(e.id))
			roots.push_back(e);
	});
	ComputeFonts();
}

void LayoutSystem::Pass::ComputeFonts() {
	// "Propre" = taille/échelle explicitement posée via
	// .FontSize()/.Style(...) sur l'entité (UiStyle inline) —
	// indépendant de la cascade complète (UiComputedStyle), qui n'a
	// pas forcément encore tourné à ce point (StyleSystem::Resolve()
	// est optionnel/découplé du layout — cf. styles.hpp).
	std::unordered_map<uint32_t, float> ownFontSize;
	std::unordered_map<uint32_t, float> ownScale;
	world.Query<UiStyle>([&](ecs::Entity e, UiStyle &s) {
		if (const auto *fs = s.Get<prop::FontSize>())
			ownFontSize[e.id] = fs->value;
		if (const auto *iz = s.Get<prop::InnerZoom>())
			ownScale[e.id] = iz->value;
	});

	const float ROOT_FS = sys.rootFontSize;
	// (entité, police EFFECTIVE DÉJÀ mise à l'échelle du parent, échelle cumulée du parent)
	std::vector<std::tuple<ecs::Entity, float, float>> stack;
	for (ecs::Entity r : roots)
		stack.push_back({r, ROOT_FS, 1.f});
	while (!stack.empty()) {
		auto [e, parentFsScaled, parentScale] = stack.back();
		stack.pop_back();

		// Composition multiplicative (cf. Décision #3 du plan node-graph) :
		// un inner-zoom de 0.5 sous un inner-zoom de 2.0 ambiant donne 1.0.
		float scale = parentScale;
		if (auto it = ownScale.find(e.id); it != ownScale.end())
			scale = parentScale * it->second;

		float eff;
		if (auto it = ownFontSize.find(e.id); it != ownFontSize.end())
			eff = it->second * scale; // valeur d'auteur (design) mise à l'échelle ICI
		else
			eff = parentFsScaled; // héritage : copie la valeur du parent DÉJÀ à l'échelle

		fonts[e.id] = {eff, parentFsScaled, ROOT_FS, scale};
		if (auto it = children.find(e.id); it != children.end())
			for (ecs::Entity c : it->second)
				stack.push_back({c, eff, scale});
	}
}

EmBases LayoutSystem::Pass::FontsOf(ecs::Entity e) const {
	auto it = fonts.find(e.id);
	return it != fonts.end() ? it->second : EmBases{sys.rootFontSize, sys.rootFontSize, sys.rootFontSize};
}

UiItem LayoutSystem::Pass::ItemOf(ecs::Entity e) const {
	auto it = items.find(e.id);
	return it != items.end() ? it->second : UiItem{};
}

std::pair<Dimension, Dimension> LayoutSystem::Pass::SizeVals(ecs::Entity e) const {
	auto it = items.find(e.id);
	if (it != items.end())
		return {it->second.width, it->second.height};
	const auto &r = rects.at(e.id);
	return {r.size.x > 0.f ? Dimension::Px(r.size.x) : Dimension::Auto(),
			r.size.y > 0.f ? Dimension::Px(r.size.y) : Dimension::Auto()};
}

std::vector<ecs::Entity> LayoutSystem::Pass::VisibleChildren(ecs::Entity e) const {
	std::vector<ecs::Entity> outv;
	auto it = children.find(e.id);
	if (it == children.end())
		return outv;
	for (ecs::Entity c : it->second)
		if (!hidden.contains(c.id) && rects.contains(c.id))
			outv.push_back(c);
	return outv;
}

sdl3::FPoint LayoutSystem::Pass::Intrinsic(ecs::Entity e) {
	// Taille de police effective déjà résolue par computeFonts() (sa
	// propre valeur si posée via .FontSize()/.Style(...), sinon
	// héritée du parent) — même donnée que celle qui sert aux unités
	// Em/Rem, pas de nouvelle résolution ici.
	float fs = FontsOf(e).em;
	if (auto l = world.GetComponent<UiLabel>(e); l.IsSome())
		return sys.MeasureText(l.Unwrap()->text, fs);
	if (auto b = world.GetComponent<UiButton>(e); b.IsSome()) {
		auto t = sys.MeasureText(b.Unwrap()->text, fs);
		return {t.x + 24.f, t.y + 14.f};
	}
	if (world.HasComponent<UiToggle>(e))
		return {44.f, 24.f};
	if (world.HasComponent<UiCheckbox>(e))
		return {20.f, 20.f};
	if (auto s = world.GetComponent<UiSlider>(e); s.IsSome())
		return s.Unwrap()->orient == Orientation::Horizontal ? sdl3::FPoint{160.f, 20.f} : sdl3::FPoint{20.f, 160.f};
	if (world.HasComponent<UiProgress>(e))
		return {160.f, 8.f};
	if (auto sp = world.GetComponent<UiSeparator>(e); sp.IsSome())
		return sp.Unwrap()->orient == Orientation::Horizontal ? sdl3::FPoint{10.f, sp.Unwrap()->thickness}
															  : sdl3::FPoint{sp.Unwrap()->thickness, 10.f};
	if (world.HasComponent<UiInput>(e))
		return {160.f, fs + 14.f};
	if (world.HasComponent<UiInputArea>(e))
		return {240.f, fs * 1.3f * 4.f + 14.f}; // ~4 lignes visibles
	if (world.HasComponent<UiDragValue>(e))
		return {90.f, fs + 14.f};
	if (world.HasComponent<UiColorSwatch>(e))
		return {28.f, 28.f};
	if (world.HasComponent<UiSVSquare>(e))
		return {160.f, 160.f};
	if (world.HasComponent<UiHueSlider>(e))
		return {160.f, 16.f};
	if (world.HasComponent<UiAlphaSlider>(e))
		return {160.f, 16.f};
	if (auto mb = world.GetComponent<UiMenuBarItem>(e); mb.IsSome()) {
		auto t = sys.MeasureText(mb.Unwrap()->text, fs);
		return {t.x + 20.f, t.y + 12.f};
	}
	if (auto mi = world.GetComponent<UiMenuItem>(e); mi.IsSome()) {
		auto t = sys.MeasureText(mi.Unwrap()->text, fs);
		float shortcutW =
			mi.Unwrap()->shortcut.IsEmpty() ? 0.f : sys.MeasureText(mi.Unwrap()->shortcut, fs).x + 20.f;
		float arrowW = mi.Unwrap()->hasSubmenu ? 16.f : 0.f;
		return {t.x + 24.f + shortcutW + arrowW, t.y + 12.f};
	}
	if (auto tb = world.GetComponent<UiTable>(e); tb.IsSome()) {
		const UiTable &t = *tb.Unwrap();
		return {t.TotalWidth(), t.headerHeight + t.ContentHeight()};
	}
	if (world.HasComponent<UiPlot>(e))
		return {300.f, 200.f};
	if (world.HasComponent<UiCalendar>(e))
		return {220.f, 200.f};
	if (auto rd = world.GetComponent<UiRadio>(e); rd.IsSome()) {
		auto t = sys.MeasureText(rd.Unwrap()->text, fs);
		return {18.f + (rd.Unwrap()->text.IsEmpty() ? 0.f : 6.f + t.x), sdl3::Max(18.f, t.y)};
	}
	if (auto sb = world.GetComponent<UiScrollBar>(e); sb.IsSome())
		return sb.Unwrap()->orient == Orientation::Horizontal ? sdl3::FPoint{160.f, 12.f} : sdl3::FPoint{12.f, 160.f};
	if (world.HasComponent<UiKnob>(e))
		return {48.f, 48.f};
	if (world.HasComponent<UiCanvas>(e))
		return {100.f, 100.f};
	if (world.HasComponent<UiComboBox>(e))
		return {160.f, fs + 14.f};
	if (world.HasComponent<ListBox>(e))
		return {160.f, 120.f};
	if (world.HasComponent<UiSpinner>(e))
		return {28.f, 28.f};
	if (auto bd = world.GetComponent<UiBadge>(e); bd.IsSome()) {
		auto t = sys.MeasureText(bd.Unwrap()->text, fs);
		return {t.x + 14.f, t.y + 4.f};
	}
	return rects.at(e.id).size;
}

float LayoutSystem::Pass::AutoSize(ecs::Entity e, bool isW) {
	auto fit = flows.find(e.id);
	if (fit == flows.end()) {
		auto s = Intrinsic(e);
		return isW ? s.x : s.y;
	}
	const UiFlow &flow = fit->second;
	// gap/margin/padding : mêmes float/math::Sides bruts qu'ailleurs, mis à
	// l'échelle explicitement avec le scale DE `e` (cf. place()/
	// placeLinear() plus bas — même raisonnement).
	const float SCALE = FontsOf(e).scale;
	auto kids = VisibleChildren(e);
	float main = 0.f, cross = 0.f;
	int n = 0;
	for (ecs::Entity c : kids) {
		auto it = ItemOf(c);
		if (it.attach != AttachLayout::RELATIVE)
			continue;
		++n;
		sdl3::FPoint cm = Measure(c);
		float cw = cm.x + it.margin.H() * SCALE, ch = cm.y + it.margin.V() * SCALE;
		if (flow.dir == LayoutDir::Column) {
			main += ch;
			cross = sdl3::Max(cross, cw);
		} else {
			main += cw;
			cross = sdl3::Max(cross, ch);
		}
	}
	if (n > 1)
		main += flow.gap * SCALE * float(n - 1);
	float w = (flow.dir == LayoutDir::Column) ? cross : main;
	float h = (flow.dir == LayoutDir::Column) ? main : cross;
	// La bordure s'ajoute au contenu (modèle border-box) : sans elle,
	// les bords droit/bas d'un conteneur ajusté seraient rognés.
	const math::Sides BORDER = BorderOf(e);
	return isW ? (w + flow.padding.H() * SCALE + BORDER.H()) : (h + flow.padding.V() * SCALE + BORDER.V());
}

const String * LayoutSystem::Pass::TextOf(ecs::Entity e) const {
	if (auto l = world.GetComponent<UiLabel>(e); l.IsSome())
		return &l.Unwrap()->text;
	if (auto b = world.GetComponent<UiButton>(e); b.IsSome())
		return &b.Unwrap()->text;
	return nullptr;
}

Option<float> LayoutSystem::Pass::WrappedHeightOf(ecs::Entity e, float availableWidth) {
	if (TextOverflowOf(world, e) != TextOverflow::WRAP || availableWidth <= 0.f)
		return NONE;
	const String *text = TextOf(e);
	if (!text || text->IsEmpty())
		return NONE;
	return Some(WrappedHeight(*text, FontsOf(e).em, availableWidth, sys.measureText, sys.tabStop));
}

Option<sdl3::FPoint> LayoutSystem::Pass::TextContentSize(ecs::Entity e, const sdl3::FRect &screen) {
	const TextOverflow mode = TextOverflowOf(world, e);
	if (mode != TextOverflow::SCROLL && mode != TextOverflow::WRAP)
		return NONE;
	const String *text = TextOf(e);
	if (!text || text->IsEmpty())
		return NONE;
	const float fs = FontsOf(e).em;
	if (mode == TextOverflow::SCROLL) {
		const float width = sys.MeasureText(*text, fs).x + 2.f * TEXT_INSET;
		return Some(sdl3::FPoint{width, screen.h});
	}
	const float height = WrappedHeight(*text, fs, sdl3::Max(1.f, screen.w - 2.f * TEXT_INSET),
									   sys.measureText, sys.tabStop);
	return Some(sdl3::FPoint{screen.w, height});
}

math::Sides LayoutSystem::Pass::BorderOf(ecs::Entity e) const {
	auto it = borders.find(e.id);
	return it == borders.end() ? math::Sides{} : it->second;
}

sdl3::FPoint LayoutSystem::Pass::Measure(ecs::Entity e) {
	if (auto it = measured.find(e.id); it != measured.end())
		return it->second;
	measured[e.id] = {0.f, 0.f}; // garde anti-cycle
	auto [wv, hv] = SizeVals(e);
	EmBases fb = FontsOf(e);
	float w = wv.IsAuto() ? AutoSize(e, true) : (wv.IsGrow() ? 0.f : wv.Resolve(0.f, rootW, fb));
	float h = hv.IsAuto() ? AutoSize(e, false) : (hv.IsGrow() ? 0.f : hv.Resolve(0.f, rootH, fb));
	auto it = ItemOf(e);
	sdl3::FPoint m{ClampOpt(w, it.minWidth, it.maxWidth), ClampOpt(h, it.minHeight, it.maxHeight)};
	measured[e.id] = m;
	return m;
}

std::pair<float, float> LayoutSystem::Pass::ResolveSize(ecs::Entity e, float parentW, float parentH) {
	auto [wv, hv] = SizeVals(e);
	EmBases fb = FontsOf(e);
	float w = wv.IsAuto() ? Measure(e).x : (wv.IsGrow() ? parentW : wv.Resolve(parentW, rootW, fb));
	float h = hv.IsAuto() ? Measure(e).y : (hv.IsGrow() ? parentH : hv.Resolve(parentH, rootH, fb));
	auto it = ItemOf(e);
	w = ClampOpt(w, it.minWidth, it.maxWidth);
	h = ClampOpt(h, it.minHeight, it.maxHeight);
	return {w, h};
}

void LayoutSystem::Pass::Place(ecs::Entity e, sdl3::FRect screen, sdl3::FRect drawClip) {
	UiRect &r = rects[e.id];
	r.size = {screen.w, screen.h};
	r.ClampScroll();

	UiComputed c;
	c.screen = screen;
	c.clip = drawClip;
	const sdl3::FPoint PREVIOUS_CONTENT = r.content;
	c.measured = measured.contains(e.id) ? measured[e.id] : sdl3::FPoint{screen.w, screen.h};

	// Marquee : c'est ici, et seulement ici, qu'on dispose de la vraie
	// mesure du texte (cf. UiTextOverflow::textWidth).
	if (auto overflow = world.GetComponent<UiTextOverflow>(e);
		overflow.IsSome() && overflow.Unwrap()->mode == TextOverflow::MARQUEE)
		if (const String *text = TextOf(e))
			overflow.Unwrap()->textWidth = sys.MeasureText(*text, FontsOf(e).em).x;

	auto fit = flows.find(e.id);
	const bool HAS_FLOW = fit != flows.end();
	// padding n'est PAS un Dimension (juste un math::Sides/math::Sides brut en px)
	// donc ne bénéficie pas automatiquement du scale appliqué dans
	// Dimension::Resolve() — mis à l'échelle explicitement ici, avec
	// le scale de CE conteneur (cf. EmBases::scale, Phase 0 node-graph).
	const float SELF_SCALE = FontsOf(e).scale;
	const math::Sides RAW_PAD = HAS_FLOW ? fit->second.padding : math::Sides{};
	// La bordure fait partie de la boîte : le contenu commence à
	// l'intérieur, sinon les enfants la recouvrent (cf. BorderOf).
	const math::Sides BORDER = BorderOf(e);
	const math::Sides PAD{RAW_PAD.left * SELF_SCALE + BORDER.left, RAW_PAD.top * SELF_SCALE + BORDER.top,
					RAW_PAD.right * SELF_SCALE + BORDER.right, RAW_PAD.bottom * SELF_SCALE + BORDER.bottom};
	const sdl3::FRect inner{screen.x + BORDER.left, screen.y + BORDER.top,
					  sdl3::Max(0.f, screen.w - BORDER.H()), sdl3::Max(0.f, screen.h - BORDER.V())};
	newSizes[e.id] = {screen.w, screen.h};

	auto kids = VisibleChildren(e);
	std::vector<ecs::Entity> flowKids, absKids;
	for (ecs::Entity k : kids)
		(HAS_FLOW && ItemOf(k).attach == AttachLayout::RELATIVE ? flowKids : absKids).push_back(k);

	// Les barres de défilement occupent leur propre bande (UiRect::
	// gutter) : le contenu est placé dans ce qui reste. Leur présence
	// dépend du contenu, lui-même placé dans la boîte réduite — on
	// part donc de l'état de l'image précédente (le cas stable ne
	// coûte qu'un passage) et on replace tant que ce choix change.
	sdl3::FPoint gutter = r.gutter;
	for (int attempt = 0; attempt < 3; ++attempt) {
		r.gutter = gutter;
		sdl3::FRect contentBox{screen.x + PAD.left, screen.y + PAD.top,
						 sdl3::Max(0.f, screen.w - PAD.H() - gutter.x),
						 sdl3::Max(0.f, screen.h - PAD.V() - gutter.y)};
		c.contentOrigin = {contentBox.x - r.scroll.x, contentBox.y - r.scroll.y};
		// Toujours borné à sa propre boîte (bordure et barres exclues) :
		// le contenu d'un widget ne doit jamais s'afficher hors de sa
		// zone, ni sous sa bordure ou ses barres de défilement. Les
		// enfants détachés (AttachLayout::Fixed) contournent
		// volontairement ce clip via leur propre chemin plus bas.
		c.childClip = drawClip.Intersection(sdl3::FRect{inner.x, inner.y, sdl3::Max(0.f, inner.w - gutter.x),
													   sdl3::Max(0.f, inner.h - gutter.y)});
		out[e.id] = c;
		newContent.erase(e.id);

		// Débordement textuel : le contenu logique dépasse la boîte,
		// d'où la barre de défilement (cf. TextContentSize).
		if (Option<sdl3::FPoint> textContent = TextContentSize(
				e, sdl3::FRect{screen.x, screen.y, sdl3::Max(0.f, screen.w - gutter.x),
							   sdl3::Max(0.f, screen.h - gutter.y)});
			textContent.IsSome())
			newContent[e.id] = textContent.Unwrap();

		if (HAS_FLOW && !flowKids.empty())
			PlaceLinear(e, fit->second, contentBox, c.contentOrigin, c.childClip, flowKids);

		for (ecs::Entity k : absKids) {
			auto [w, h] = ResolveSize(k, contentBox.w, contentBox.h);
			UiRect kr = rects[k.id];
			kr.size = {w, h};
			sdl3::FRect ks = kr.ResolveIn(c.contentOrigin, contentBox.w, contentBox.h);
			ks.w = w;
			ks.h = h;
			// Fixed : même position (ancre/offset relatifs au parent), mais
			// clip élargi à la fenêtre entière — RenderSystem le dessine hors
			// de l'arbre normal (overlay), donc son clip d'entrée ne doit pas
			// rester borné à celui, potentiellement étroit, de son parent.
			bool isFixed = ItemOf(k).attach == AttachLayout::FIXED;
			Place(k, ks, isFixed ? sdl3::FRect{0.f, 0.f, rootW, rootH} : c.childClip);
		}

		auto content = newContent.find(e.id);
		// Le contenu d'un conteneur inclut sa bordure (cf. AutoSize).
		if (content != newContent.end() && HAS_FLOW && !flowKids.empty()) {
			content->second.x += BORDER.H();
			content->second.y += BORDER.V();
		}
		// Sans contenu calculé ici, celui posé ailleurs (InputSystem
		// pour les zones de saisie) fait foi.
		const sdl3::FPoint next = UiRect::GutterFor(
			content == newContent.end() ? PREVIOUS_CONTENT : content->second, {screen.w, screen.h});
		if (next.x == gutter.x && next.y == gutter.y)
			break;
		gutter = next;
	}
	r.gutter = gutter;
	if (auto content = newContent.find(e.id); content != newContent.end())
		r.content = content->second;
	r.ClampScroll();
	newGutters[e.id] = gutter;
}

void LayoutSystem::Pass::PlaceLinear(ecs::Entity parent, const UiFlow &flow, sdl3::FRect box, sdl3::FPoint origin, sdl3::FRect childClip,
		const std::vector<ecs::Entity> &kids) {
	const bool IS_COL = flow.dir == LayoutDir::Column;
	const float MAIN_SIZE = IS_COL ? box.h : box.w;
	const float CROSS_SIZE = IS_COL ? box.w : box.h;
	// gap/margin ne sont PAS des Dimension (floats/math::Sides bruts en px)
	// donc ne bénéficient pas automatiquement du scale appliqué dans
	// Dimension::Resolve() — mis à l'échelle explicitement ici, avec
	// le scale DU CONTENEUR `parent` (cf. EmBases::scale).
	const float SCALE = FontsOf(parent).scale;
	const float GAP = flow.gap * SCALE;

	struct KidInfo {
		ecs::Entity e;
		float main, cross, mB, mA, cB, cA;
		float growW;
	};
	std::vector<KidInfo> infos;
	infos.reserve(kids.size());

	float fixedMain = 0.f, growTotal = 0.f;
	for (ecs::Entity k : kids) {
		auto it = ItemOf(k);
		auto [w, h] = ResolveSize(k, box.w, box.h);
		auto [wv, hv] = SizeVals(k);

		// Retour automatique dans une COLONNE : la largeur est l'axe
		// TRANSVERSE, donc déjà connue ici (étirement ou taille
		// résolue). La hauteur, elle, en découle — c'est le seul
		// endroit où on puisse la calculer avant de répartir l'espace.
		if (IS_COL && hv.IsAuto()) {
			CrossAlign align = it.alignSelf.IsSome() ? it.alignSelf.Unwrap() : flow.align;
			const float availW = (align == CrossAlign::Stretch && wv.IsAuto())
									 ? sdl3::Max(0.f, CROSS_SIZE - (it.margin.left + it.margin.right) * SCALE)
									 : w;
			if (Option<float> wrapped = WrappedHeightOf(k, availW); wrapped.IsSome())
				h = ClampOpt(wrapped.Unwrap(), it.minHeight, it.maxHeight);
		}
		KidInfo ki{k,
				   IS_COL ? h : w,
				   IS_COL ? w : h,
				   (IS_COL ? it.margin.top : it.margin.left) * SCALE,
				   (IS_COL ? it.margin.bottom : it.margin.right) * SCALE,
				   (IS_COL ? it.margin.left : it.margin.top) * SCALE,
				   (IS_COL ? it.margin.right : it.margin.bottom) * SCALE,
				   0.f};
		Dimension mainV = IS_COL ? hv : wv;
		if (mainV.IsGrow()) {
			ki.growW = sdl3::Max(0.001f, mainV.value);
			growTotal += ki.growW;
		} else
			fixedMain += ki.main;
		fixedMain += ki.mB + ki.mA;
		infos.push_back(ki);
	}
	if (infos.size() > 1)
		fixedMain += GAP * float(infos.size() - 1);

	// Distribution de l'espace restant aux enfants Grow.
	float avail = sdl3::Max(0.f, MAIN_SIZE - fixedMain);
	for (auto &ki : infos) {
		if (ki.growW <= 0.f)
			continue;
		float v = avail * ki.growW / growTotal;
		auto it = ItemOf(ki.e);
		ki.main = IS_COL ? ClampOpt(v, it.minHeight, it.maxHeight) : ClampOpt(v, it.minWidth, it.maxWidth);
	}

	// Retour automatique dans une RANGÉE : la largeur est l'axe
	// PRINCIPAL, donc définitive seulement après la répartition
	// ci-dessus. La hauteur (axe transverse) n'entre pas dans le
	// calcul de `used`, elle peut donc être corrigée ici sans rien
	// invalider.
	if (!IS_COL)
		for (auto &ki : infos) {
			auto it = ItemOf(ki.e);
			if (Option<float> wrapped = WrappedHeightOf(ki.e, ki.main); wrapped.IsSome())
				ki.cross = ClampOpt(wrapped.Unwrap(), it.minHeight, it.maxHeight);
		}

	float used = (infos.size() > 1) ? GAP * float(infos.size() - 1) : 0.f;
	for (auto &ki : infos)
		used += ki.main + ki.mB + ki.mA;

	float cursor = 0.f, between = GAP;
	switch (flow.justify) {
	case Justify::Start:
		break;
	case Justify::Center:
		cursor = (MAIN_SIZE - used) * 0.5f;
		break;
	case Justify::End:
		cursor = MAIN_SIZE - used;
		break;
	case Justify::SpaceBetween:
		if (infos.size() > 1)
			between += (MAIN_SIZE - used) / float(infos.size() - 1);
		break;
	}

	float maxCrossExtent = 0.f;
	for (size_t i = 0; i < infos.size(); ++i) {
		auto &ki = infos[i];
		auto it = ItemOf(ki.e);
		auto [wv, hv] = SizeVals(ki.e);
		Dimension crossV = IS_COL ? wv : hv;

		CrossAlign align = it.alignSelf.IsSome() ? it.alignSelf.Unwrap() : flow.align;
		float cross = ki.cross;
		if (align == CrossAlign::Stretch && crossV.IsAuto()) {
			float stretched = sdl3::Max(0.f, CROSS_SIZE - ki.cB - ki.cA);
			cross = IS_COL ? ClampOpt(stretched, it.minWidth, it.maxWidth)
						  : ClampOpt(stretched, it.minHeight, it.maxHeight);
		}

		float crossPos = ki.cB;
		switch (align) {
		case CrossAlign::Start:
		case CrossAlign::Stretch:
			break;
		case CrossAlign::Center:
			crossPos = (CROSS_SIZE - cross) * 0.5f;
			break;
		case CrossAlign::End:
			crossPos = CROSS_SIZE - cross - ki.cA;
			break;
		}

		cursor += ki.mB;
		sdl3::FRect ks = IS_COL ? sdl3::FRect{origin.x + crossPos, origin.y + cursor, cross, ki.main}
						 : sdl3::FRect{origin.x + cursor, origin.y + crossPos, ki.main, cross};
		Place(ki.e, ks, childClip);

		cursor += ki.main + ki.mA;
		if (i + 1 < infos.size())
			cursor += between;
		maxCrossExtent = sdl3::Max(maxCrossExtent, crossPos + cross + ki.cA);
	}

	// Taille du contenu (pour le scroll) : étendue totale + padding
	// (mise à l'échelle, cf. `scale` en tête de fonction).
	sdl3::FPoint content = IS_COL ? sdl3::FPoint{maxCrossExtent + flow.padding.H() * SCALE, cursor + flow.padding.V() * SCALE}
						   : sdl3::FPoint{cursor + flow.padding.H() * SCALE, maxCrossExtent + flow.padding.V() * SCALE};
	newContent[parent.id] = content;
}

void LayoutSystem::Pass::WriteBack() {
	world.Query<UiComputed>([&](ecs::Entity e, UiComputed &c) {
		if (auto it = out.find(e.id); it != out.end()) {
			c = it->second;
			out.erase(it);
		}
	});
	// Entités visitées qui n'avaient pas encore de UiComputed.
	for (auto &[id, c] : out) {
		world.Query<UiRect>([&](ecs::Entity e, UiRect &) {
			if (e.id == id && !world.HasComponent<UiComputed>(e))
				world.AddComponent(e, c);
		});
	}
	world.Query<UiRect>([&](ecs::Entity e, UiRect &r) {
		if (auto it = newSizes.find(e.id); it != newSizes.end())
			r.size = it->second;
		if (auto it = newContent.find(e.id); it != newContent.end())
			r.content = it->second;
		if (auto it = newGutters.find(e.id); it != newGutters.end())
			r.gutter = it->second;
		r.ClampScroll();
	});
}

void OpenPopup(ecs::ArchetypeRegistry &world, LayoutSystem &layout, ecs::Entity popupRoot, ecs::Entity trigger) {
	if (auto ps = world.GetComponent<UiPopupState>(popupRoot); ps.IsSome()) {
		ps.Unwrap()->open = true;
		ps.Unwrap()->trigger = trigger;
	}
	if (world.HasComponent<UiHidden>(popupRoot))
		world.RemoveComponent<UiHidden>(popupRoot);
	layout.MarkDirty();
}

void OpenPopupAt(ecs::ArchetypeRegistry &world, LayoutSystem &layout, ecs::Entity popupRoot, sdl3::FPoint at) {
	if (auto r = world.GetComponent<UiRect>(popupRoot); r.IsSome()) {
		r.Unwrap()->anchor = Anchor::TopLeft;
		r.Unwrap()->offset = at;
	}
	OpenPopup(world, layout, popupRoot, ecs::Entity{});
}

void ClosePopup(ecs::ArchetypeRegistry &world, LayoutSystem &layout, ecs::Entity popupRoot) {
	if (auto ps = world.GetComponent<UiPopupState>(popupRoot); ps.IsSome())
		ps.Unwrap()->open = false;
	if (!world.HasComponent<UiHidden>(popupRoot))
		world.AddComponent(popupRoot, UiHidden{});
	layout.MarkDirty();
}

ecs::Entity NearestPopupAncestor(ecs::ArchetypeRegistry &world, ecs::Entity e) {
	ecs::Entity cur = e;
	while (cur.Valid()) {
		if (world.HasComponent<UiPopupState>(cur))
			return cur;
		auto p = world.GetComponent<UiParent>(cur);
		if (p.IsNone())
			break;
		cur = p.Unwrap()->parent;
	}
	return ecs::Entity{};
}

void PositionPopupBelow(ecs::ArchetypeRegistry &world, ecs::Entity popupRoot, const sdl3::FRect &triggerScreen) {
	if (auto r = world.GetComponent<UiRect>(popupRoot); r.IsSome()) {
		r.Unwrap()->anchor = Anchor::TopLeft;
		r.Unwrap()->offset = {triggerScreen.x, triggerScreen.y + triggerScreen.h};
	}
}

void PositionPopupRightOf(ecs::ArchetypeRegistry &world, ecs::Entity popupRoot, const sdl3::FRect &triggerScreen) {
	if (auto r = world.GetComponent<UiRect>(popupRoot); r.IsSome()) {
		r.Unwrap()->anchor = Anchor::TopLeft;
		r.Unwrap()->offset = {triggerScreen.x + triggerScreen.w, triggerScreen.y - MENU_POPUP_PAD_Y - 1.f};
	}
}

void CloseSubmenusOf(ecs::ArchetypeRegistry &world, LayoutSystem &layout, ecs::Entity popup, ecs::Entity keep) {
	std::vector<ecs::Entity> open;
	if (auto kids = world.GetComponent<UiChildren>(popup); kids.IsSome())
		for (ecs::Entity item : kids.Unwrap()->list)
			if (auto mi = world.GetComponent<UiMenuItem>(item); mi.IsSome() && mi.Unwrap()->submenuPopup.Valid() &&
																mi.Unwrap()->submenuPopup != keep)
				if (auto ps = world.GetComponent<UiPopupState>(mi.Unwrap()->submenuPopup); ps.IsSome() && ps.Unwrap()->open)
					open.push_back(mi.Unwrap()->submenuPopup);
	for (ecs::Entity sub : open) {
		CloseSubmenusOf(world, layout, sub);
		ClosePopup(world, layout, sub);
	}
}

void CloseMenuChain(ecs::ArchetypeRegistry &world, LayoutSystem &layout, ecs::Entity clickedItem) {
	ecs::Entity popup = NearestPopupAncestor(world, clickedItem);
	while (popup.Valid()) {
		ecs::Entity trigger{};
		if (auto ps = world.GetComponent<UiPopupState>(popup); ps.IsSome())
			trigger = ps.Unwrap()->trigger;
		ClosePopup(world, layout, popup);
		popup = trigger.Valid() ? NearestPopupAncestor(world, trigger) : ecs::Entity{};
	}
}

// ── InputSystem ──────────────────────────────────────────────────────────────

void InputSystem::OpenModal(ecs::ArchetypeRegistry &world, LayoutSystem &layout, ecs::Entity modalRoot) {
	if (auto ps = world.GetComponent<UiPopupState>(modalRoot); ps.IsSome()) {
		ps.Unwrap()->open = true;
		ps.Unwrap()->modal = true;
	}
	if (world.HasComponent<UiHidden>(modalRoot))
		world.RemoveComponent<UiHidden>(modalRoot);
	modalStack.push_back(modalRoot);
	layout.MarkDirty();
}

void InputSystem::CloseModal(ecs::ArchetypeRegistry &world, LayoutSystem &layout, ecs::Entity modalRoot) {
	if (modalStack.empty() || modalStack.back() != modalRoot)
		return;
	modalStack.pop_back();
	if (auto ps = world.GetComponent<UiPopupState>(modalRoot); ps.IsSome())
		ps.Unwrap()->open = false;
	if (!world.HasComponent<UiHidden>(modalRoot))
		world.AddComponent(modalRoot, UiHidden{});
	layout.MarkDirty();
}

ecs::Entity InputSystem::FrontMostAt(ecs::ArchetypeRegistry &world, LayoutSystem &layout, sdl3::FPoint p) {
	hitIndex.Refresh(world, layout.PassCount());
	return hitIndex.TopMostAt(world, p);
}

bool InputSystem::DefaultKeyPassThrough(const sdl3::Event &ev) noexcept {
	if (!ev.IsKeyDown() && !ev.IsKeyUp())
		return false; // texte saisi, composition IME : toujours au champ
	const SDL_Keycode key = ev.Keycode();
	if ((key >= SDLK_F1 && key <= SDLK_F12) || (key >= SDLK_F13 && key <= SDLK_F24))
		return true;
	const SDL_Keymod mod = ev.raw.key.mod;
	// AltGr (Alt droite / MODE) compose des caractères (@, #, {…) : ce
	// n'est pas un raccourci.
	const bool command = (mod & (SDL_KMOD_CTRL | SDL_KMOD_LALT | SDL_KMOD_GUI)) != 0 &&
						 (mod & (SDL_KMOD_RALT | SDL_KMOD_MODE)) == 0;
	if (!command)
		return false;
	switch (key) {
		// Édition de texte : reste au champ.
		case SDLK_A:
		case SDLK_C:
		case SDLK_V:
		case SDLK_X:
		case SDLK_Z:
		case SDLK_Y:
		case SDLK_LEFT:
		case SDLK_RIGHT:
		case SDLK_UP:
		case SDLK_DOWN:
		case SDLK_HOME:
		case SDLK_END:
		case SDLK_BACKSPACE:
		case SDLK_DELETE:
			return false;
		default:
			return true;
	}
}

ecs::Entity InputSystem::KeyboardFocus(ecs::ArchetypeRegistry &world) const {
	ecs::Entity focus{};
	world.Query<UiInput>([&](ecs::Entity e, UiInput &f) {
		if (f.focused && !focus.Valid())
			focus = e;
	});
	world.Query<UiInputArea>([&](ecs::Entity e, UiInputArea &f) {
		if (f.focused && !focus.Valid())
			focus = e;
	});
	world.Query<UiDragValue>([&](ecs::Entity e, UiDragValue &d) {
		if (d.editing && !focus.Valid())
			focus = e;
	});
	if (focus.Valid() && IsHiddenRecursive(world, focus))
		return ecs::Entity{};
	return focus;
}

void InputSystem::ClearKeyboardFocus(ecs::ArchetypeRegistry &world) const {
	world.Query<UiInput>([](ecs::Entity, UiInput &f) { f.focused = false; });
	world.Query<UiInputArea>([](ecs::Entity, UiInputArea &f) { f.focused = false; });
}

bool InputSystem::ClaimsKey(ecs::ArchetypeRegistry &world, const sdl3::Event &ev, ecs::Entity focus) const {
	auto ioModeOf = [&world](ecs::Entity e) -> Option<IOMode> {
		if (auto f = world.GetComponent<UiInput>(e); f.IsSome())
			return Some(f.Unwrap()->ioMode);
		if (auto f = world.GetComponent<UiInputArea>(e); f.IsSome())
			return Some(f.Unwrap()->ioMode);
		return NONE;
	};
	if (!focus.Valid())
		return false;
	const bool keyboard = ev.IsKeyDown() || ev.IsKeyUp() || ev.IsTextInput() ||
						  ev.Type() == uint32_t(sdl3::EventType::TEXT_EDITING);
	if (!keyboard)
		return false;
	if (keyPassThrough ? keyPassThrough(ev, focus) : DefaultKeyPassThrough(ev))
		return false;
	// Champ en lecture seule (console, journal) : il ne capte que la
	// navigation et la copie ; une lettre tapée reste un raccourci.
	IOMode mode = IOMode::READ_WRITE_COPY_AND_PASTE;
	if (auto f = ioModeOf(focus); f.IsSome())
		mode = f.Unwrap();
	if (IoCanWrite(mode))
		return true;
	if (ev.IsTextInput() || ev.Type() == uint32_t(sdl3::EventType::TEXT_EDITING))
		return false;
	const bool ctrl = (ev.raw.key.mod & SDL_KMOD_CTRL) != 0;
	switch (ev.Keycode()) {
		case SDLK_LEFT:
		case SDLK_RIGHT:
		case SDLK_UP:
		case SDLK_DOWN:
		case SDLK_HOME:
		case SDLK_END:
		case SDLK_PAGEUP:
		case SDLK_PAGEDOWN:
		case SDLK_ESCAPE:
		case SDLK_LSHIFT:
		case SDLK_RSHIFT:
			return true;
		case SDLK_A:
		case SDLK_C:
			return ctrl;
		default:
			return false;
	}
}

bool InputSystem::HandleEvent(ecs::ArchetypeRegistry &world, sdl3::Event &ev, LayoutSystem &layout) {
	if (ev.IsConsumed())
		return true;
	if (HandleEvent(world, static_cast<const sdl3::Event &>(ev), layout))
		ev.Consume();
	return ev.IsConsumed();
}

bool InputSystem::HandleEvent(ecs::ArchetypeRegistry &world, const sdl3::Event &ev, LayoutSystem &layout) {
	if (ev.IsConsumed())
		return true;
	const ecs::Entity focus = ev.IsKeyboard() || ev.IsTextInput() ? KeyboardFocus(world) : ecs::Entity{};
	const bool claimed = ClaimsKey(world, ev, focus);
	// Échap sur un champ de saisie : le champ rend le focus (un second
	// Échap fermera la modale, etc.) — la valeur en cours d'édition
	// (UiDragValue) gère elle-même son annulation dans Dispatch.
	if (claimed && ev.IsKeyDown(SDLK_ESCAPE) && !world.HasComponent<UiDragValue>(focus)) {
		ClearKeyboardFocus(world);
		return true;
	}
	const bool modalEscape = !focus.Valid() && ev.IsKeyDown(SDLK_ESCAPE) && !modalStack.empty();

	Frame in;
	in.down = down;
	in.rightDown = rightDown;
	in.mouseX = mouseX;
	in.mouseY = mouseY;

	if (ev.IsMouseMotion()) {
		mouseX = in.mouseX = ev.MouseMotion().x;
		mouseY = in.mouseY = ev.MouseMotion().y;
	} else if (ev.IsMouseDown(SDL_BUTTON_LEFT)) {
		mouseX = in.mouseX = ev.MouseButton().x;
		mouseY = in.mouseY = ev.MouseButton().y;
		down = true;
		in.down = true;
		in.pressed = true;
		in.doubleClick = ev.MouseButton().clicks >= 2;
	} else if (ev.IsMouseUp(SDL_BUTTON_LEFT)) {
		mouseX = in.mouseX = ev.MouseButton().x;
		mouseY = in.mouseY = ev.MouseButton().y;
		down = false;
		in.down = false;
		in.released = true;
	} else if (ev.IsMouseDown(SDL_BUTTON_RIGHT)) {
		// Uniquement utilisé par UiPlot::boxZooming (zoom-rectangle,
		// Phase 3) pour l'instant — aucun autre widget ne lit
		// in.rightDown/rightPressed/rightReleased.
		mouseX = in.mouseX = ev.MouseButton().x;
		mouseY = in.mouseY = ev.MouseButton().y;
		rightDown = true;
		in.rightDown = true;
		in.rightPressed = true;
	} else if (ev.IsMouseUp(SDL_BUTTON_RIGHT)) {
		mouseX = in.mouseX = ev.MouseButton().x;
		mouseY = in.mouseY = ev.MouseButton().y;
		rightDown = false;
		in.rightDown = false;
		in.rightReleased = true;
	} else if (ev.IsMouseWheel()) {
		in.wheelY = ev.MouseWheel().y;
	} else if (ev.IsTextInput()) {
		in.textInput = String(ev.TextInput().text);
	} else if (ev.IsKeyDown()) {
		bool ctrl = (ev.raw.key.mod & SDL_KMOD_CTRL) != 0;
		in.shift = (ev.raw.key.mod & SDL_KMOD_SHIFT) != 0;
		if (ev.Keycode() == SDLK_BACKSPACE)
			in.backspace = true;
		else if (ev.Keycode() == SDLK_DELETE)
			in.del = true;
		else if (ev.Keycode() == SDLK_RETURN || ev.Keycode() == SDLK_KP_ENTER)
			in.enter = true;
		else if (ev.Keycode() == SDLK_LEFT)
			in.arrowLeft = true;
		else if (ev.Keycode() == SDLK_RIGHT)
			in.arrowRight = true;
		else if (ev.Keycode() == SDLK_UP)
			in.arrowUp = true;
		else if (ev.Keycode() == SDLK_DOWN)
			in.arrowDown = true;
		else if (ev.Keycode() == SDLK_HOME)
			in.home = true;
		else if (ev.Keycode() == SDLK_END)
			in.end = true;
		else if (ev.Keycode() == SDLK_ESCAPE)
			in.escape = true;
		else if (ctrl && ev.Keycode() == SDLK_C)
			in.copy = true;
		else if (ctrl && ev.Keycode() == SDLK_V)
			in.paste = true;
		else if (ctrl && ev.Keycode() == SDLK_X)
			in.cut = true;
		else if (ctrl && ev.Keycode() == SDLK_A)
			in.selectAll = true;
		else
			return claimed;
	} else {
		return claimed;
	}

	Dispatch(world, in, layout);
	return claimed || modalEscape;
}

void InputSystem::Tick(ecs::ArchetypeRegistry &world, LayoutSystem &layout, float dt) {
	world.Query<UiToggle>([&](ecs::Entity, UiToggle &t) {
		float target = t.checked ? 1.f : 0.f;
		float speed = 10.f * dt;
		t.animT += sdl3::Clamp(target - t.animT, -speed, speed);
	});
	world.Query<UiSpinner>([&](ecs::Entity, UiSpinner &sp) { sp.angle = sdl3::Fmod(sp.angle + sp.speed * dt, 360.f); });

	// Défilement automatique (TextOverflow::MARQUEE) : aller-retour, avec
	// une pause à chaque extrémité. L'amplitude est ce qui DÉPASSE de la
	// boîte — un texte qui tient ne bouge donc pas du tout, sans que
	// l'appelant ait à désactiver quoi que ce soit.
	world.Query<UiTextOverflow, UiComputed>([&](ecs::Entity e, UiTextOverflow &o, UiComputed &c) {
		if (o.mode != TextOverflow::MARQUEE)
			return;
		const float travel = sdl3::Max(0.f, o.textWidth - sdl3::Max(1.f, c.screen.w - 2.f * TEXT_INSET));
		if (travel <= 0.f) {
			o.offset = 0.f;
			o.hold = 0.f;
			o.forward = true;
			return;
		}
		if (o.hold > 0.f) {
			o.hold -= dt;
			return;
		}
		o.offset += (o.forward ? 1.f : -1.f) * o.speed * dt;
		if (o.offset >= travel) {
			o.offset = travel;
			o.forward = false;
			o.hold = o.pause;
		} else if (o.offset <= 0.f) {
			o.offset = 0.f;
			o.forward = true;
			o.hold = o.pause;
		}
	});
	world.Query<UiInput>([&](ecs::Entity, UiInput &f) {
		if (f.focused)
			f.blink += dt;
	});
	world.Query<UiInputArea>([&](ecs::Entity, UiInputArea &f) {
		if (f.focused)
			f.blink += dt;
	});
	world.Query<UiDragValue>([&](ecs::Entity, UiDragValue &d) {
		if (d.editing)
			d.blink += dt;
	});

	// ── UiInput : contenu = texte enveloppé (word-wrap) dans la largeur
	// du widget ; le défilement suit le CURSEUR (pas toujours la fin —
	// il peut être déplacé n'importe où dans le texte enveloppé).
	world.Query<UiInput, UiRect, UiComputed>([&](ecs::Entity e, UiInput &f, UiRect &r, UiComputed &c) {
		r.clipContent = true;
		float fs = GetResolved(world, e).FontSize(14.f);
		float innerW = sdl3::Max(1.f, c.screen.w - 16.f); // cf. marge de drawTextCentered (8px de chaque côté)
		auto lines = WrapText(f.text, fs, innerW);
		// Largeur 0 : le texte se replie, il ne déborde jamais en largeur
		// (la barre verticale loge dans la marge droite de 8 px). Une seule
		// ligne n'a rien à faire défiler, même si le champ est moins haut
		// que ligne + marges : pas de barre sur un champ de recherche.
		r.content = {0.f, lines.size() <= 1 ? 0.f : float(lines.size()) * LineHeightApprox(fs) + 8.f};
		r.UpdateGutter();
		ScrollIntoView(r, c, LineIndexOf(lines, f.cursor), fs);
	});

	// ── UiInputArea : contenu = lignes brutes (pas de wrap). Suit le
	// curseur pendant une interaction active (focus) ; sinon ne suit le
	// bas automatiquement que si l'utilisateur n'a pas remonté consulter
	// l'historique (ex. panneau de logs qui reçoit de nouvelles lignes).
	world.Query<UiInputArea, UiRect, UiComputed>([&](ecs::Entity e, UiInputArea &f, UiRect &r, UiComputed &c) {
		r.clipContent = true;
		float fs = GetResolved(world, e).FontSize(14.f);
		auto lines = SplitLines(f.text);
		const float cw = AreaCellWidth(f, fs);
		const float gutter = AreaGutterWidth(f, lines.size(), cw);
		float maxLineW = 0.f;
		for (const TextLine &l : lines)
			maxLineW = sdl3::Max(maxLineW, float(DisplayColumns(l.text, tabStop)) * cw);
		bool wasAtBottom = r.MaxScroll().y <= 0.f || r.scroll.y >= r.MaxScroll().y - 1.f;
		r.content = {maxLineW + 16.f + gutter, float(lines.size()) * LineHeightApprox(fs) + 8.f};
		r.UpdateGutter();
		r.ClampScroll();
		f.stickToBottom = wasAtBottom;
		if (f.focused) {
			const size_t li = LineIndexOf(lines, f.cursor);
			ScrollIntoView(r, c, li, fs);
			if (li < lines.size())
				ScrollColumnIntoView(r, r.ViewSize().x - gutter,
									 DisplayColumn(lines[li].text, f.cursor - lines[li].offset, tabStop), cw);
		} else if (wasAtBottom && f.followTail) {
			r.scroll.y = r.MaxScroll().y;
		}
	});

	const float MX = mouseX, MY = mouseY;
	// Même filtre de premier plan que Dispatch : l'infobulle affichée est
	// celle du widget VU sous le curseur, pas celle d'un widget recouvert.
	UpdateFrontMost(world, layout, sdl3::FPoint{MX, MY});
	auto hitOk = [&](ecs::Entity e, const UiComputed &c) {
		// Test géométrique EN PREMIER : il rejette l'immense majorité des
		// widgets sans une seule recherche de composant (les trois tests
		// suivants remontent tous la chaîne des parents).
		sdl3::FPoint p{MX, MY};
		if (!c.screen.Contains(p) || !c.clip.Contains(p))
			return false;
		if (!IsFrontMost(e))
			return false;
		if (!modalStack.empty() && !IsDescendantOrSelf(world, e, modalStack.back()))
			return false;
		return !IsHiddenRecursive(world, e) && !IsDisabledRecursive(world, e);
	};
	ecs::Entity tipE{};
	float bestArea = 1e30f;
	String tipText;
	world.Query<UiTooltip, UiComputed>([&](ecs::Entity e, UiTooltip &t, UiComputed &c) {
		if (!hitOk(e, c))
			return;
		float area = c.screen.w * c.screen.h;
		if (area < bestArea) {
			bestArea = area;
			tipE = e;
			tipText = t.text;
		}
	});
	if (tipE.Valid() && tipE == lastTip)
		hoverTime += dt;
	else {
		hoverTime = 0.f;
		lastTip = tipE;
	}
	tooltip.visible = tipE.Valid() && hoverTime >= tooltipDelay && !down;
	tooltip.text = tipText;
	tooltip.pos = {MX + 14.f, MY + 20.f};

	NavigateMenus(world, layout, dt);
}

void InputSystem::NavigateMenus(ecs::ArchetypeRegistry &world, LayoutSystem &layout, float dt) {
	// Barre de menus.
	struct BarEntry {
		ecs::Entity item, popup;
		sdl3::FRect screen;
		bool hovered, open;
	};
	std::vector<BarEntry> bar;
	world.Query<UiMenuBarItem, UiComputed>([&](ecs::Entity e, UiMenuBarItem &mb, UiComputed &c) {
		if (!mb.menuPopup.Valid())
			return;
		bool open = false;
		if (auto ps = world.GetComponent<UiPopupState>(mb.menuPopup); ps.IsSome())
			open = ps.Unwrap()->open;
		bar.push_back({e, mb.menuPopup, c.screen, mb.hovered, open});
	});
	for (const BarEntry &hovered : bar) {
		if (!hovered.hovered || hovered.open)
			continue;
		bool anotherOpen = false;
		for (const BarEntry &other : bar)
			anotherOpen = anotherOpen || (other.open && other.item != hovered.item &&
										  UiParentOf(world, other.item) == UiParentOf(world, hovered.item));
		if (!anotherOpen)
			continue;
		for (const BarEntry &other : bar)
			if (other.open && UiParentOf(world, other.item) == UiParentOf(world, hovered.item)) {
				CloseSubmenusOf(world, layout, other.popup);
				ClosePopup(world, layout, other.popup);
			}
		PositionPopupBelow(world, hovered.popup, hovered.screen);
		OpenPopup(world, layout, hovered.popup, hovered.item);
		break;
	}

	// Entrées de menus déroulants.
	ecs::Entity hovered{};
	UiMenuItem hoveredItem;
	sdl3::FRect hoveredScreen{};
	world.Query<UiMenuItem, UiComputed>([&](ecs::Entity e, UiMenuItem &mi, UiComputed &c) {
		if (mi.hovered && !IsHiddenRecursive(world, e)) {
			hovered = e;
			hoveredItem = mi;
			hoveredScreen = c.screen;
		}
	});
	if (hovered != menuHover) {
		menuHover = hovered;
		menuHoverTime = 0.f;
	} else {
		menuHoverTime += dt;
	}
	if (!hovered.Valid() || menuHoverTime < submenuDelay)
		return;
	const ecs::Entity popup = NearestPopupAncestor(world, hovered);
	if (hoveredItem.hasSubmenu && hoveredItem.submenuPopup.Valid()) {
		bool open = false;
		if (auto ps = world.GetComponent<UiPopupState>(hoveredItem.submenuPopup); ps.IsSome())
			open = ps.Unwrap()->open;
		if (open)
			return;
		CloseSubmenusOf(world, layout, popup, hoveredItem.submenuPopup);
		PositionPopupRightOf(world, hoveredItem.submenuPopup, hoveredScreen);
		OpenPopup(world, layout, hoveredItem.submenuPopup, hovered);
	} else if (popup.Valid()) {
		CloseSubmenusOf(world, layout, popup);
	}
}

ecs::Entity InputSystem::UiParentOf(ecs::ArchetypeRegistry &world, ecs::Entity e) {
	auto p = world.GetComponent<UiParent>(e);
	return p.IsSome() ? p.Unwrap()->parent : ecs::Entity{};
}

bool InputSystem::IsFrontMost(ecs::Entity e) const noexcept {
	if (frontMostChain.empty())
		return true;
	// Chaîne = la cible puis ses ancêtres (quelques entrées) : une
	// comparaison linéaire sur un petit vecteur contigu, là où un
	// IsDescendantOrSelf par widget remonterait les parents — donc ferait
	// une recherche de composant par niveau — pour CHACUNE des ~25 passes
	// Query<> de Dispatch.
	for (ecs::Entity a : frontMostChain)
		if (a == e)
			return true;
	return false;
}

void InputSystem::UpdateFrontMost(ecs::ArchetypeRegistry &world, LayoutSystem &layout, sdl3::FPoint p) {
	hitIndex.Refresh(world, layout.PassCount());
	frontMost = hitIndex.TopMostAt(world, p);
	frontMostChain.clear();
	for (ecs::Entity cur = frontMost; cur.Valid();) {
		frontMostChain.push_back(cur);
		auto parent = world.GetComponent<UiParent>(cur);
		if (parent.IsNone())
			break;
		cur = parent.Unwrap()->parent;
	}
}

void InputSystem::MoveCursor(const String &text, const std::vector<TextLine> &lines, size_t &cursor,
		size_t &selectionAnchor, const Frame &in) {
	bool hadSelection = cursor != selectionAnchor;
	size_t lo = sdl3::Min(cursor, selectionAnchor), hi = sdl3::Max(cursor, selectionAnchor);
	size_t newCursor = cursor;

	if (in.arrowLeft) {
		newCursor = (!in.shift && hadSelection) ? lo : PrevCodepoint(text, cursor);
	} else if (in.arrowRight) {
		newCursor = (!in.shift && hadSelection) ? hi : NextCodepoint(text, cursor);
	} else if (in.arrowUp || in.arrowDown) {
		size_t li = LineIndexOf(lines, cursor);
		size_t col = cursor - lines[li].offset;
		size_t targetLi = in.arrowUp ? (li > 0 ? li - 1 : li) : sdl3::Min(li + 1, lines.size() - 1);
		newCursor = lines[targetLi].offset + sdl3::Min(col, lines[targetLi].text.size());
	} else if (in.home) {
		newCursor = lines[LineIndexOf(lines, cursor)].offset;
	} else if (in.end) {
		size_t li = LineIndexOf(lines, cursor);
		newCursor = lines[li].offset + lines[li].text.size();
	} else {
		return; // aucune touche de déplacement dans cet évènement
	}

	cursor = newCursor;
	if (!in.shift)
		selectionAnchor = newCursor;
}

bool InputSystem::ApplyTextEdit(String &text, size_t &cursor, size_t &selectionAnchor, IOMode mode,
		size_t maxLen, const Frame &in, bool enterInsertsNewline) {
	bool changed = false;
	auto hasSelection = [&] { return cursor != selectionAnchor; };
	auto selLo = [&] { return sdl3::Min(cursor, selectionAnchor); };
	auto selHi = [&] { return sdl3::Max(cursor, selectionAnchor); };
	auto deleteSelection = [&] {
		size_t lo = selLo(), hi = selHi();
		String before = text.Substr(0, lo);
		before.Append(text.Substr(hi));
		text = std::move(before);
		cursor = selectionAnchor = lo;
	};
	auto insertAtCursor = [&](const String &s) {
		if (s.IsEmpty())
			return;
		size_t room = maxLen > text.size() ? maxLen - text.size() : 0;
		String ins = s.size() <= room ? s : s.Substr(0, room);
		if (ins.IsEmpty())
			return;
		String before = text.Substr(0, cursor);
		before.Append(ins);
		before.Append(text.Substr(cursor));
		text = std::move(before);
		cursor += ins.size();
		selectionAnchor = cursor;
		changed = true;
	};

	if (in.selectAll) {
		selectionAnchor = 0;
		cursor = text.size();
	}
	if (IoCanWrite(mode)) {
		if (!in.textInput.IsEmpty()) {
			if (hasSelection())
				deleteSelection();
			insertAtCursor(in.textInput);
		}
		if (in.backspace) {
			if (hasSelection()) {
				deleteSelection();
				changed = true;
			} else if (cursor > 0) {
				size_t p = PrevCodepoint(text, cursor);
				String before = text.Substr(0, p);
				before.Append(text.Substr(cursor));
				text = std::move(before);
				cursor = selectionAnchor = p;
				changed = true;
			}
		}
		if (in.del) {
			if (hasSelection()) {
				deleteSelection();
				changed = true;
			} else if (cursor < text.size()) {
				size_t nx = NextCodepoint(text, cursor);
				String before = text.Substr(0, cursor);
				before.Append(text.Substr(nx));
				text = std::move(before);
				changed = true;
			}
		}
		if (in.enter && enterInsertsNewline) {
			if (hasSelection())
				deleteSelection();
			insertAtCursor(String("\n"));
		}
	}
	// Copie : la sélection si elle existe, sinon tout le texte (préserve
	// le raccourci "copier tous les logs" quand rien n'est sélectionné).
	if (in.copy && IoCanCopy(mode))
		sdl3::clipboard::SetText(hasSelection() ? text.Substr(selLo(), selHi() - selLo()) : text);
	if (in.cut && IoCanWrite(mode) && IoCanCopy(mode) && hasSelection()) {
		sdl3::clipboard::SetText(text.Substr(selLo(), selHi() - selLo()));
		deleteSelection();
		changed = true;
	}
	if (in.paste && IoCanPaste(mode)) {
		if (hasSelection())
			deleteSelection();
		insertAtCursor(sdl3::clipboard::GetText());
	}
	return changed;
}

void InputSystem::Dispatch(ecs::ArchetypeRegistry &world, const Frame &in, LayoutSystem &layout) {
	const float MX = in.mouseX, MY = in.mouseY;
	std::vector<std::function<void()>> pending;

	// Échap ferme la modale du sommet de la pile (et seulement elle —
	// pas les popups non-modaux, qui se ferment au clic extérieur ci-
	// dessous). N'affecte rien d'autre cette frame.
	// Un widget qui a le focus clavier garde Échap pour lui (cf. HandleEvent).
	if (in.escape && !modalStack.empty() && !KeyboardFocus(world).Valid()) {
		ecs::Entity top = modalStack.back();
		CloseModal(world, layout, top);
		return;
	}

	// Widget le plus en avant sous le pointeur, calculé UNE fois pour tout
	// l'évènement (cf. HitTestIndex) : lui seul — et ses ancêtres —
	// pourra réagir, quel que soit l'ordre des Query<> ci-dessous, qui
	// suit le type des widgets et non leur profondeur d'affichage.
	UpdateFrontMost(world, layout, sdl3::FPoint{MX, MY});

	// La souris doit être dans la partie VISIBLE du widget : son rect écran
	// ET la région de clip héritée des ancêtres. Les widgets désactivés
	// (UiDisabled, récursif) sont inertes. Une modale ouverte bloque tout
	// le reste de l'arbre (seuls elle-même et ses descendants passent), et
	// un widget recouvert par un autre ne reçoit plus rien (IsFrontMost).
	auto hitOk = [&](ecs::Entity e, const UiComputed &c) {
		// Test géométrique EN PREMIER : il rejette l'immense majorité des
		// widgets sans une seule recherche de composant (les trois tests
		// suivants remontent tous la chaîne des parents).
		sdl3::FPoint p{MX, MY};
		if (!c.screen.Contains(p) || !c.clip.Contains(p))
			return false;
		if (!IsFrontMost(e))
			return false;
		if (!modalStack.empty() && !IsDescendantOrSelf(world, e, modalStack.back()))
			return false;
		return !IsHiddenRecursive(world, e) && !IsDisabledRecursive(world, e);
	};

	// Un combo ouvert capte le clic (sélection OU fermeture) : rien
	// d'autre ne doit le recevoir. Même logique pour la molette au-dessus
	// d'un widget qui la consomme (listbox, slider, knob).
	bool clickConsumed = false;
	bool wheelConsumed = false;

	// ── Popups ancrés (non-modaux) : ferme au clic en dehors du popup ET
	// de son déclencheur (un clic SUR le déclencheur est géré par le
	// widget lui-même, ex: re-cliquer un bouton qui rouvre). Les
	// popups sont collectés puis fermés hors itération (cf. le même
	// motif que StyleSystem::Resolve — retirer un composant PENDANT un
	// Query<T> sur ce même T invaliderait l'itération).
	if (in.pressed || in.rightPressed) {
		std::vector<ecs::Entity> toClose;
		world.Query<UiPopupState, UiComputed>([&](ecs::Entity e, UiPopupState &ps, UiComputed &c) {
			if (!ps.open || ps.modal)
				return;
			sdl3::FPoint p{MX, MY};
			bool insidePopup = c.screen.Contains(p);
			bool insideTrigger = false;
			if (ps.trigger.Valid()) {
				if (auto tc = world.GetComponent<UiComputed>(ps.trigger); tc.IsSome())
					insideTrigger = tc.Unwrap()->screen.Contains(p);
			}
			if (!insidePopup && !insideTrigger)
				toClose.push_back(e);
		});
		for (ecs::Entity e : toClose) {
			if (auto ps = world.GetComponent<UiPopupState>(e); ps.IsSome())
				ps.Unwrap()->open = false;
			if (!world.HasComponent<UiHidden>(e))
				world.AddComponent(e, UiHidden{});
		}
		if (!toClose.empty())
			layout.MarkDirty();
	}

	// ── Menu contextuel : le clic DROIT notifie le plus proche porteur
	// d'`onContextMenu` dans la chaîne de premier plan (la cible, puis ses
	// ancêtres) — la ligne d'un arbre l'emporte sur le panneau qui la
	// contient. Un clic droit DANS un popup ouvert ne rouvre rien.
	if (in.rightPressed) {
		bool insideOpenPopup = false;
		for (ecs::Entity a : frontMostChain)
			if (auto ps = world.GetComponent<UiPopupState>(a); ps.IsSome() && ps.Unwrap()->open)
				insideOpenPopup = true;
		if (!insideOpenPopup) {
			for (ecs::Entity candidate : frontMostChain) {
				if (!modalStack.empty() && !IsDescendantOrSelf(world, candidate, modalStack.back()))
					break;
				auto cb = world.GetComponent<UiCallbacks>(candidate);
				if (cb.IsNone() || !cb.Unwrap()->onContextMenu)
					continue;
				if (IsHiddenRecursive(world, candidate) || IsDisabledRecursive(world, candidate))
					break;
				auto fn = cb.Unwrap()->onContextMenu;
				pending.push_back([fn, MX, MY] { fn(MX, MY); });
				break;
			}
		}
	}

	// ── ComboBox (en premier : la liste ouverte est un overlay modal) ───
	world.Query<UiComboBox, UiComputed>([&](ecs::Entity e, UiComboBox &cb, UiComputed &c) {
		cb.hovered = hitOk(e, c);
		cb.hoveredItem = -1;
		cb.hoveredGroup = -1;
		if (cb.open) {
			sdl3::FRect dd = cb.DropdownRect(c.screen);
			sdl3::FPoint p{MX, MY};
			// La liste est en overlay : pas de clip d'ancêtres à respecter.
			bool inDrop = cb.OverlayContains(c.screen, p) && IsFrontMost(e) && !IsHiddenRecursive(world, e) &&
						  !IsDisabledRecursive(world, e);
			if (inDrop && cb.itemHeight > 0.f) {
				const sdl3::FRect sub = cb.SubmenuRect(c.screen, cb.openGroup);
				if (cb.openGroup >= 0 && sub.Contains(p)) {
					// Sous-menu de la catégorie ouverte.
					const UiComboGroup &g = cb.groups[size_t(cb.openGroup)];
					const int row = sdl3::Clamp(int((MY - sub.y) / cb.itemHeight), 0, sdl3::Max(0, g.count - 1));
					if (g.count > 0)
						cb.hoveredItem = g.first + row;
				} else if (dd.Contains(p)) {
					const int row = sdl3::Clamp(int((MY - dd.y) / cb.itemHeight), 0, sdl3::Max(0, cb.TopRows() - 1));
					if (!cb.Grouped()) {
						cb.hoveredItem = row;
					} else {
						const std::vector<int> top = cb.TopItems();
						if (row < int(top.size())) {
							cb.hoveredItem = top[size_t(row)];
							cb.openGroup = -1;
						} else {
							// Survoler une catégorie ouvre son sous-menu (comme un Menu).
							cb.hoveredGroup = row - int(top.size());
							cb.openGroup = cb.hoveredGroup;
						}
					}
				}
			}
			if (in.wheelY != 0.f && inDrop)
				wheelConsumed = true;
			if (in.pressed && !clickConsumed && inDrop && cb.hoveredGroup >= 0) {
				clickConsumed = true; // une catégorie : le sous-menu reste ouvert
			} else if (in.pressed && !clickConsumed) {
				clickConsumed = true;
				if (inDrop && cb.hoveredItem >= 0 && cb.hoveredItem != cb.selected) {
					cb.selected = cb.hoveredItem;
					if (auto cbk = world.GetComponent<UiCallbacks>(e); cbk.IsSome() && cbk.Unwrap()->onChange) {
						auto fn = cbk.Unwrap()->onChange;
						float v = float(cb.selected);
						pending.push_back([fn, v] { fn(v); });
					}
				}
				cb.open = false;
				cb.openGroup = -1;
			}
		} else if (in.pressed && !clickConsumed && cb.hovered) {
			cb.open = true;
			cb.openGroup = cb.GroupOf(cb.selected); // la catégorie de l'élément choisi, déjà dépliée
			clickConsumed = true;
		}
	});

	// ── Auto-scrollbar : drag du pouce ──────────────────────────────────
	// Un pouce en cours de drag capte la souris tant que le bouton reste
	// enfoncé, même si elle sort du rect du pouce (comportement standard
	// "drag" — cf. sliders/knobs plus bas, même logique).
	if (scrollDrag.Valid()) {
		if (in.down) {
			auto r = world.GetComponent<UiRect>(scrollDrag);
			auto c = world.GetComponent<UiComputed>(scrollDrag);
			if (r.IsSome() && c.IsSome()) {
				UiRect &rr = *r.Unwrap();
				float trackSize = scrollDragVertical ? c.Unwrap()->screen.h : c.Unwrap()->screen.w;
				float contentSize = scrollDragVertical ? rr.ContentSize().y : rr.ContentSize().x;
				float maxSc = scrollDragVertical ? rr.MaxScroll().y : rr.MaxScroll().x;
				float thumbSize =
					sdl3::Clamp(trackSize * trackSize / sdl3::Max(1.f, contentSize), K_SCROLLBAR_MIN_THUMB, trackSize);
				float draggable = sdl3::Max(1.f, trackSize - thumbSize);
				float delta = (scrollDragVertical ? MY : MX) - scrollDragStartMouse;
				float newOffset = sdl3::Clamp(scrollDragStartOffset + delta * (maxSc / draggable), 0.f, maxSc);
				if (scrollDragVertical)
					rr.scroll.y = newOffset;
				else
					rr.scroll.x = newOffset;
				layout.MarkDirty();
			}
			clickConsumed = true;
		}
		if (in.released)
			scrollDrag = ecs::Entity{};
	} else if (in.pressed && !clickConsumed) {
		ecs::Entity best{};
		bool bestVertical = true;
		float bestArea = 1e30f;
		sdl3::FPoint p{MX, MY};
		world.Query<UiRect, UiComputed>([&](ecs::Entity e, UiRect &r, UiComputed &c) {
			// Auto-scrollbar : réactif dès qu'il y a débordement réel
			// (r.maxScroll() > 0), sans opt-in explicite requis — cf.
			// vScrollbarThumbRect/hScrollbarThumbRect, qui renvoient déjà
			// un rect vide en l'absence de débordement.
			if (!IsFrontMost(e) || IsHiddenRecursive(world, e) || IsDisabledRecursive(world, e))
				return;
			float area = c.screen.w * c.screen.h;
			if (area >= bestArea)
				return;
			if (sdl3::FRect vt = VScrollbarThumbRect(r, c.screen); vt.w > 0.f && vt.Contains(p)) {
				bestArea = area;
				best = e;
				bestVertical = true;
			} else if (sdl3::FRect ht = HScrollbarThumbRect(r, c.screen); ht.w > 0.f && ht.Contains(p)) {
				bestArea = area;
				best = e;
				bestVertical = false;
			}
		});
		if (best.Valid()) {
			scrollDrag = best;
			scrollDragVertical = bestVertical;
			scrollDragStartMouse = bestVertical ? MY : MX;
			if (auto r = world.GetComponent<UiRect>(best); r.IsSome())
				scrollDragStartOffset = bestVertical ? r.Unwrap()->scroll.y : r.Unwrap()->scroll.x;
			clickConsumed = true;
		}
	}

	// ── UiResizeHandle : drag générique (splitters, colonnes de tableau,
	// cf. interaction.hpp) — même motif latch-puis-suit que l'auto-
	// scrollbar ci-dessus.
	if (resizeDrag.Valid()) {
		if (in.down) {
			if (auto h = world.GetComponent<UiResizeHandle>(resizeDrag); h.IsSome()) {
				float coord = h.Unwrap()->orient == Orientation::Horizontal ? MX : MY;
				ResolveResizeDrag(*h.Unwrap(), coord);
				layout.MarkDirty();
			}
			clickConsumed = true;
		}
		if (in.released) {
			if (auto h = world.GetComponent<UiResizeHandle>(resizeDrag); h.IsSome())
				h.Unwrap()->dragging = false;
			resizeDrag = ecs::Entity{};
		}
	} else if (in.pressed && !clickConsumed) {
		world.Query<UiResizeHandle, UiComputed>([&](ecs::Entity e, UiResizeHandle &h, UiComputed &c) {
			if (resizeDrag.Valid() || clickConsumed || !hitOk(e, c))
				return;
			resizeDrag = e;
			h.dragging = true;
			h.dragStartMouse = h.orient == Orientation::Horizontal ? MX : MY;
			h.dragStartBefore = h.getBefore ? h.getBefore() : 0.f;
			h.dragStartAfter = h.getAfter ? h.getAfter() : 0.f;
			clickConsumed = true;
		});
	} else if (!resizeDrag.Valid()) {
		// Survol (hors drag) : utile pour un futur curseur redimension.
		world.Query<UiResizeHandle, UiComputed>(
			[&](ecs::Entity e, UiResizeHandle &h, UiComputed &c) { h.hovered = hitOk(e, c); });
	}

	// ── UiDraggable : déplacement/redimensionnement de panneau flottant
	// (Phase 9/10 : cf. interaction.hpp, ui::PanelChrome) — même motif
	// latch-puis-suit que UiResizeHandle ci-dessus.
	if (draggableDrag.Valid()) {
		if (in.down) {
			if (auto d = world.GetComponent<UiDraggable>(draggableDrag); d.IsSome()) {
				ecs::Entity target = d.Unwrap()->target.Valid() ? d.Unwrap()->target : draggableDrag;
				sdl3::FPoint delta{MX - d.Unwrap()->dragStartMouse.x, MY - d.Unwrap()->dragStartMouse.y};
				if (d.Unwrap()->resizeMode) {
					if (auto it = world.GetComponent<UiItem>(target); it.IsSome()) {
						float nw = sdl3::Max(d.Unwrap()->minWidth, d.Unwrap()->dragStartSize.x + delta.x);
						float nh = sdl3::Max(d.Unwrap()->minHeight, d.Unwrap()->dragStartSize.y + delta.y);
						it.Unwrap()->width = Dimension::Px(nw);
						it.Unwrap()->height = Dimension::Px(nh);
					}
				} else if (auto r = world.GetComponent<UiRect>(target); r.IsSome()) {
					r.Unwrap()->offset = {d.Unwrap()->dragStartOffset.x + delta.x,
										  d.Unwrap()->dragStartOffset.y + delta.y};
				}
				layout.MarkDirty();
			}
			clickConsumed = true;
		}
		if (in.released) {
			if (auto d = world.GetComponent<UiDraggable>(draggableDrag); d.IsSome())
				d.Unwrap()->dragging = false;
			draggableDrag = ecs::Entity{};
		}
	} else if (in.pressed && !clickConsumed) {
		world.Query<UiDraggable, UiComputed>([&](ecs::Entity e, UiDraggable &d, UiComputed &c) {
			if (draggableDrag.Valid() || clickConsumed || !hitOk(e, c))
				return;
			draggableDrag = e;
			d.dragging = true;
			d.dragStartMouse = {MX, MY};
			ecs::Entity target = d.target.Valid() ? d.target : e;
			if (auto r = world.GetComponent<UiRect>(target); r.IsSome())
				d.dragStartOffset = r.Unwrap()->offset;
			if (auto it = world.GetComponent<UiItem>(target); it.IsSome())
				d.dragStartSize = {it.Unwrap()->width.value, it.Unwrap()->height.value};
			clickConsumed = true;
		});
	}

	// Clic effectif pour tous les widgets suivants.
	// ── Glisser-déposer entre widgets ───────────────────────────────────
	// Source et cible sont cherchées dans la CHAÎNE du widget de premier
	// plan (cf. frontMostChain) : la ligne d'un outliner porte le
	// marqueur, mais c'est son libellé qui est réellement sous le
	// curseur — remonter les ancêtres évite d'exiger le marqueur sur
	// chaque enfant.
	if (in.pressed && !dragPayloadSource.Valid()) {
		for (ecs::Entity candidate : frontMostChain)
			if (world.HasComponent<UiDragPayload>(candidate) && !IsDisabledRecursive(world, candidate)) {
				dragPayloadSource = candidate;
				dragPayloadStartX = MX;
				dragPayloadStartY = MY;
				dragPayloadMoved = false;
				break;
			}
	}
	if (dragPayloadSource.Valid() && in.down) {
		const bool wasMoved = dragPayloadMoved;
		if (sdl3::Abs(MX - dragPayloadStartX) > 4.f || sdl3::Abs(MY - dragPayloadStartY) > 4.f)
			dragPayloadMoved = true;
		if (auto payload = world.GetComponent<UiDragPayload>(dragPayloadSource); payload.IsSome()) {
			payload.Unwrap()->dragging = dragPayloadMoved;
			payload.Unwrap()->pointer = {MX, MY};
		}
		// Début réel du glissé : la source peut ajuster son fantôme.
		if (dragPayloadMoved && !wasMoved)
			if (auto cb = world.GetComponent<UiCallbacks>(dragPayloadSource); cb.IsSome() && cb.Unwrap()->onDragStart)
				pending.push_back(cb.Unwrap()->onDragStart);

		ecs::Entity target{};
		if (dragPayloadMoved) {
			String payloadKind;
			if (auto p = world.GetComponent<UiDragPayload>(dragPayloadSource); p.IsSome())
				payloadKind = p.Unwrap()->kind;
			for (ecs::Entity candidate : frontMostChain) {
				auto drop = world.GetComponent<UiDropTarget>(candidate);
				if (drop.IsNone() || candidate == dragPayloadSource)
					continue;
				const String &accepts = drop.Unwrap()->accepts;
				if (accepts.IsEmpty() || accepts == payloadKind) {
					target = candidate;
					break;
				}
			}
		}
		if (target != dropHover) {
			if (auto previous = world.GetComponent<UiDropTarget>(dropHover); previous.IsSome())
				previous.Unwrap()->hovered = false;
			dropHover = target;
			if (auto current = world.GetComponent<UiDropTarget>(dropHover); current.IsSome())
				current.Unwrap()->hovered = true;
		}
	}
	if (in.released && dragPayloadSource.Valid()) {
		if (dragPayloadMoved && dropHover.Valid()) {
			int64_t id = 0;
			if (auto payload = world.GetComponent<UiDragPayload>(dragPayloadSource); payload.IsSome())
				id = payload.Unwrap()->id;
			if (auto cb = world.GetComponent<UiCallbacks>(dropHover); cb.IsSome() && cb.Unwrap()->onDrop) {
				auto fn = cb.Unwrap()->onDrop;
				pending.push_back([fn, id] { fn(id); });
			}
			// Un dépôt n'est pas un clic : sans cela, la ligne survolée
			// serait AUSSI sélectionnée par le même relâchement.
			clickConsumed = true;
		}
		if (auto payload = world.GetComponent<UiDragPayload>(dragPayloadSource); payload.IsSome())
			payload.Unwrap()->dragging = false;
		if (auto drop = world.GetComponent<UiDropTarget>(dropHover); drop.IsSome())
			drop.Unwrap()->hovered = false;
		dragPayloadSource = ecs::Entity{};
		dropHover = ecs::Entity{};
		dragPayloadMoved = false;
	}

	const bool PRESSED = in.pressed && !clickConsumed;

	// ── Boutons ─────────────────────────────────────────────────────────
	world.Query<UiButton, UiComputed>([&](ecs::Entity e, UiButton &b, UiComputed &c) {
		b.clicked = false;
		bool hover = hitOk(e, c);
		b.hovered = hover;
		if (PRESSED && hover)
			b.pressed = true;
		if (PRESSED && hover && in.doubleClick)
			if (auto cb = world.GetComponent<UiCallbacks>(e); cb.IsSome() && cb.Unwrap()->onDoubleClick)
				pending.push_back(cb.Unwrap()->onDoubleClick);
		if (in.released) {
			if (b.pressed && hover) {
				b.clicked = true;
				if (auto cb = world.GetComponent<UiCallbacks>(e); cb.IsSome() && cb.Unwrap()->onClick) {
					auto fn = cb.Unwrap()->onClick;
					pending.push_back(std::move(fn));
				}
			}
			b.pressed = false;
		}
	});

	// ── Toggle / Checkbox ────────────────────────────────────────────────
	world.Query<UiToggle, UiComputed>([&](ecs::Entity e, UiToggle &t, UiComputed &c) {
		t.hovered = hitOk(e, c);
		if (PRESSED && t.hovered) {
			t.checked = !t.checked;
			if (auto cb = world.GetComponent<UiCallbacks>(e); cb.IsSome() && cb.Unwrap()->onToggle) {
				auto fn = cb.Unwrap()->onToggle;
				bool v = t.checked;
				pending.push_back([fn, v] { fn(v); });
			}
		}
	});
	world.Query<UiCheckbox, UiComputed>([&](ecs::Entity e, UiCheckbox &cb, UiComputed &c) {
		cb.hovered = hitOk(e, c);
		if (PRESSED && cb.hovered) {
			cb.checked = !cb.checked;
			if (auto callback = world.GetComponent<UiCallbacks>(e); callback.IsSome() && callback.Unwrap()->onToggle) {
				auto fn = callback.Unwrap()->onToggle;
				bool v = cb.checked;
				pending.push_back([fn, v] { fn(v); });
			}
		}
	});

	// ── Sliders (drag + molette, quantifiés par `step`) ──────────────────
	world.Query<UiSlider, UiComputed>([&](ecs::Entity e, UiSlider &s, UiComputed &c) {
		s.hovered = hitOk(e, c);
		if (PRESSED && s.hovered)
			s.dragging = true;
		if (in.released)
			s.dragging = false;
		float nv = s.value;
		if (s.dragging && in.down) {
			float t = (s.orient == Orientation::Horizontal) ? (MX - c.screen.x) / sdl3::Max(1.f, c.screen.w)
															: 1.f - (MY - c.screen.y) / sdl3::Max(1.f, c.screen.h);
			nv = s.Snap(s.min + sdl3::Clamp(t, 0.f, 1.f) * (s.max - s.min));
		} else if (s.hovered && in.wheelY != 0.f && !wheelConsumed) {
			float delta = s.step > 0.f ? s.step : (s.max - s.min) * 0.02f;
			nv = s.Snap(s.value + in.wheelY * delta);
			wheelConsumed = true;
		}
		if (nv != s.value) {
			s.value = nv;
			if (auto cb = world.GetComponent<UiCallbacks>(e); cb.IsSome() && cb.Unwrap()->onChange) {
				auto fn = cb.Unwrap()->onChange;
				float v = s.value;
				pending.push_back([fn, v] { fn(v); });
			}
		}
	});

	// ── Radios : exclusivité par groupe ──────────────────────────────────
	// 1re passe : détecter le clic ; 2de passe : décocher le reste du groupe
	// (pas de changement structurel → itérations sûres ; callbacks différés
	// quand même car un onToggle utilisateur pourrait, lui, muter l'ECS).
	{
		ecs::Entity clicked{};
		String clickedGroup;
		world.Query<UiRadio, UiComputed>([&](ecs::Entity e, UiRadio &r, UiComputed &c) {
			r.hovered = hitOk(e, c);
			if (PRESSED && r.hovered && !r.checked) {
				clicked = e;
				clickedGroup = r.group;
			}
		});
		if (clicked.Valid()) {
			world.Query<UiRadio>([&](ecs::Entity e, UiRadio &r) {
				if (r.group != clickedGroup)
					return;
				bool nowChecked = (e == clicked);
				if (r.checked == nowChecked)
					return;
				r.checked = nowChecked;
				if (auto cb = world.GetComponent<UiCallbacks>(e); cb.IsSome() && cb.Unwrap()->onToggle) {
					auto fn = cb.Unwrap()->onToggle;
					pending.push_back([fn, nowChecked] { fn(nowChecked); });
				}
			});
		}
	}

	// ── ScrollBars autonomes ─────────────────────────────────────────────
	world.Query<UiScrollBar, UiComputed>([&](ecs::Entity e, UiScrollBar &sb, UiComputed &c) {
		sb.hovered = hitOk(e, c);
		if (PRESSED && sb.hovered)
			sb.dragging = true;
		if (in.released)
			sb.dragging = false;
		if (sb.dragging && in.down && sb.MaxOffset() > 0.f) {
			bool horiz = sb.orient == Orientation::Horizontal;
			float trackLen = horiz ? c.screen.w : c.screen.h;
			float thumbLen = trackLen * sb.ThumbRatio();
			float pos = horiz ? (MX - c.screen.x) : (MY - c.screen.y);
			// Le centre du pouce suit la souris.
			float t = (pos - thumbLen * 0.5f) / sdl3::Max(1.f, trackLen - thumbLen);
			float nv = sdl3::Clamp(t, 0.f, 1.f) * sb.MaxOffset();
			if (nv != sb.offset) {
				sb.offset = nv;
				if (auto cb = world.GetComponent<UiCallbacks>(e); cb.IsSome() && cb.Unwrap()->onScroll) {
					auto fn = cb.Unwrap()->onScroll;
					float v = sb.offset;
					pending.push_back([fn, v] { fn(v); });
				}
			}
		}
	});

	// ── Knobs : drag vertical (vers le haut = augmente) + molette ────────
	world.Query<UiKnob, UiComputed>([&](ecs::Entity e, UiKnob &k, UiComputed &c) {
		k.hovered = hitOk(e, c);
		if (PRESSED && k.hovered) {
			k.dragging = true;
			k.dragStartY = MY;
			k.dragStartValue = k.value;
		}
		if (in.released)
			k.dragging = false;
		float nv = k.value;
		if (k.dragging && in.down) {
			// 200 px de drag couvrent toute la plage.
			nv = k.Snap(k.dragStartValue + (k.dragStartY - MY) * 0.005f * (k.max - k.min));
		} else if (k.hovered && in.wheelY != 0.f && !wheelConsumed) {
			float delta = k.step > 0.f ? k.step : (k.max - k.min) * 0.02f;
			nv = k.Snap(k.value + in.wheelY * delta);
			wheelConsumed = true;
		}
		if (nv != k.value) {
			k.value = nv;
			if (auto cb = world.GetComponent<UiCallbacks>(e); cb.IsSome() && cb.Unwrap()->onChange) {
				auto fn = cb.Unwrap()->onChange;
				float v = k.value;
				pending.push_back([fn, v] { fn(v); });
			}
		}
	});

	// ── DragValue : glisser horizontal change la valeur (comme un slider
	// sans piste visible) ; un DOUBLE-clic bascule en édition texte
	// directe (cf. UiDragValue — saisie minimale, curseur toujours en fin
	// de texte). Un simple clic sans glisser ne fait rien (contrairement
	// à un bouton) : c'est le double-clic, seul, qui ouvre l'édition.
	world.Query<UiDragValue, UiComputed>([&](ecs::Entity e, UiDragValue &d, UiComputed &c) {
		d.hovered = hitOk(e, c);

		if (d.editing) {
			if (in.escape) {
				d.editing = false;
				return;
			}
			bool commit = in.enter || (PRESSED && !d.hovered);
			if (commit) {
				float nv = d.Snap(d.editText.ToFloat());
				d.editing = false;
				if (nv != d.value) {
					d.value = nv;
					if (auto cb = world.GetComponent<UiCallbacks>(e); cb.IsSome() && cb.Unwrap()->onChange) {
						auto fn = cb.Unwrap()->onChange;
						float v = d.value;
						pending.push_back([fn, v] { fn(v); });
					}
				}
				return;
			}
			if (in.backspace && !d.editText.IsEmpty())
				d.editText = d.editText.Substr(0, d.editText.size() - 1);
			for (char ch : in.textInput) {
				bool isSign = ch == '-' && d.editText.IsEmpty();
				bool isDigit = ch >= '0' && ch <= '9';
				bool isDot = ch == '.' && !d.editText.Contains('.');
				if (isDigit || isSign || isDot)
					d.editText.push_back(ch);
			}
			return;
		}

		if (PRESSED && d.hovered) {
			d.dragging = true;
			d.dragMoved = false;
			d.dragWasDoubleClick = in.doubleClick;
			d.dragStartX = MX;
			d.dragStartValue = d.value;
		} else if (d.dragging && in.down) {
			float dx = MX - d.dragStartX;
			if (sdl3::Abs(dx) > 2.f)
				d.dragMoved = true;
			float nv = d.Snap(d.dragStartValue + dx * d.speed);
			if (nv != d.value) {
				d.value = nv;
				if (auto cb = world.GetComponent<UiCallbacks>(e); cb.IsSome() && cb.Unwrap()->onChange) {
					auto fn = cb.Unwrap()->onChange;
					float v = d.value;
					pending.push_back([fn, v] { fn(v); });
				}
			}
		} else if (in.released && d.dragging) {
			bool wantEdit = d.hovered && d.dragWasDoubleClick;
			d.dragging = false;
			if (wantEdit) {
				d.editing = true;
				d.editText = d.Formatted();
				d.blink = 0.f;
			}
		} else if (d.hovered && in.wheelY != 0.f && !wheelConsumed) {
			float delta = d.step > 0.f ? d.step : (d.max - d.min) * 0.02f;
			float nv = d.Snap(d.value + in.wheelY * delta);
			if (nv != d.value) {
				d.value = nv;
				if (auto cb = world.GetComponent<UiCallbacks>(e); cb.IsSome() && cb.Unwrap()->onChange) {
					auto fn = cb.Unwrap()->onChange;
					float v = d.value;
					pending.push_back([fn, v] { fn(v); });
				}
			}
			wheelConsumed = true;
		}
	});

	// ── ColorSwatch : interaction identique à un bouton (hovered/pressed
	// /clicked → onClick), sans texte — cf. UiFactory::ColorSwatch.
	world.Query<UiColorSwatch, UiComputed>([&](ecs::Entity e, UiColorSwatch &sw, UiComputed &c) {
		sw.clicked = false;
		bool hover = hitOk(e, c);
		sw.hovered = hover;
		if (PRESSED && hover)
			sw.pressed = true;
		if (in.released) {
			if (sw.pressed && hover) {
				sw.clicked = true;
				if (auto cb = world.GetComponent<UiCallbacks>(e); cb.IsSome() && cb.Unwrap()->onClick) {
					auto fn = cb.Unwrap()->onClick;
					pending.push_back(std::move(fn));
				}
			}
			sw.pressed = false;
		}
	});

	// ── UiColorPicker : carré SV (drag 2D) + glissières teinte/alpha
	// (drag 1D) — cf. UiFactory::ColorPicker. Chacune notifie son
	// UiCallbacks::onChange (valeur ignorée, sert de simple "ping" : le
	// composite relit l'état courant des 3 widgets pour se resynchroniser
	// — cf. factory.hpp).
	world.Query<UiSVSquare, UiComputed>([&](ecs::Entity e, UiSVSquare &sv, UiComputed &c) {
		sv.hovered = hitOk(e, c);
		if (PRESSED && sv.hovered)
			sv.dragging = true;
		if (in.released)
			sv.dragging = false;
		if (sv.dragging && in.down) {
			float ns = sdl3::Clamp((MX - c.screen.x) / sdl3::Max(1.f, c.screen.w), 0.f, 1.f);
			float nv = sdl3::Clamp(1.f - (MY - c.screen.y) / sdl3::Max(1.f, c.screen.h), 0.f, 1.f);
			if (ns != sv.s || nv != sv.v) {
				sv.s = ns;
				sv.v = nv;
				if (auto cb = world.GetComponent<UiCallbacks>(e); cb.IsSome() && cb.Unwrap()->onChange) {
					auto fn = cb.Unwrap()->onChange;
					pending.push_back([fn] { fn(0.f); });
				}
			}
		}
	});
	world.Query<UiHueSlider, UiComputed>([&](ecs::Entity e, UiHueSlider &h, UiComputed &c) {
		h.hovered = hitOk(e, c);
		if (PRESSED && h.hovered)
			h.dragging = true;
		if (in.released)
			h.dragging = false;
		if (h.dragging && in.down) {
			float t = sdl3::Clamp((MX - c.screen.x) / sdl3::Max(1.f, c.screen.w), 0.f, 1.f);
			float nh = t * 360.f;
			if (nh != h.hue) {
				h.hue = nh;
				if (auto cb = world.GetComponent<UiCallbacks>(e); cb.IsSome() && cb.Unwrap()->onChange) {
					auto fn = cb.Unwrap()->onChange;
					pending.push_back([fn] { fn(0.f); });
				}
			}
		}
	});
	world.Query<UiAlphaSlider, UiComputed>([&](ecs::Entity e, UiAlphaSlider &a, UiComputed &c) {
		a.hovered = hitOk(e, c);
		if (PRESSED && a.hovered)
			a.dragging = true;
		if (in.released)
			a.dragging = false;
		if (a.dragging && in.down) {
			float t = sdl3::Clamp((MX - c.screen.x) / sdl3::Max(1.f, c.screen.w), 0.f, 1.f);
			if (t != a.alpha) {
				a.alpha = t;
				if (auto cb = world.GetComponent<UiCallbacks>(e); cb.IsSome() && cb.Unwrap()->onChange) {
					auto fn = cb.Unwrap()->onChange;
					pending.push_back([fn] { fn(0.f); });
				}
			}
		}
	});

	// ── Selectable/TreeNode : clic applique applySelectionClick (ctrl/
	// shift lus via l'état clavier COURANT — SDL_MouseButtonEvent ne
	// porte pas les modificateurs, contrairement aux évènements clavier)
	// sur le UiSelection du plus proche ANCÊTRE qui en porte un. 2 passes
	// comme les UiRadio d'un groupe plus haut : 1re détecte le clic (un
	// clic sur la FLÈCHE d'un UiTreeNode replie/déplie à la place, cf.
	// bloc suivant — n'affecte pas la sélection) et calcule le nouvel
	// état ; 2de reflète `.selected` sur tout le conteneur.
	{
		bool ctrlDown = (sdl3::keyboard::Mods() & SDL_KMOD_CTRL) != 0;
		ecs::Entity clickedContainer{};
		// Un clic qui vise un widget interactif PLACÉ DANS la ligne (flèche de
		// dépliage, bouton d'action, champ) est le sien, pas celui de la ligne.
		auto innerControlHit = [&](ecs::Entity row) {
			for (ecs::Entity cur : frontMostChain) {
				if (cur == row)
					return false;
				if (world.HasComponent<UiButton>(cur) || world.HasComponent<UiInput>(cur) ||
					world.HasComponent<UiCheckbox>(cur) || world.HasComponent<UiSelectable>(cur))
					return true;
			}
			return false;
		};
		// `onClick` d'une ligne : appui PUIS relâchement sur elle, sans dépôt
		// entre les deux (un glisser-déposer marque clickConsumed).
		auto clickRow = [&](ecs::Entity e, bool hovered, bool &pressed) {
			if (PRESSED && hovered && !innerControlHit(e)) {
				pressed = true;
				if (in.doubleClick)
					if (auto cb = world.GetComponent<UiCallbacks>(e); cb.IsSome() && cb.Unwrap()->onDoubleClick)
						pending.push_back(cb.Unwrap()->onDoubleClick);
			}
			if (!in.released)
				return;
			if (pressed && hovered && !clickConsumed)
				if (auto cb = world.GetComponent<UiCallbacks>(e); cb.IsSome() && cb.Unwrap()->onClick)
					pending.push_back(cb.Unwrap()->onClick);
			pressed = false;
		};
		world.Query<UiSelectable, UiComputed>([&](ecs::Entity e, UiSelectable &sel, UiComputed &c) {
			sel.hovered = hitOk(e, c);
			clickRow(e, sel.hovered, sel.pressed);
			if (PRESSED && sel.hovered) {
				ecs::Entity container = NearestSelectionAncestor(world, e);
				if (container.Valid()) {
					if (auto us = world.GetComponent<UiSelection>(container); us.IsSome()) {
						ApplySelectionClick(us.Unwrap()->state, sel.index, ctrlDown, in.shift,
											us.Unwrap()->multiSelect);
						clickedContainer = container;
					}
				}
			}
		});
		world.Query<UiTreeNode, UiComputed>([&](ecs::Entity e, UiTreeNode &tn, UiComputed &c) {
			sdl3::FPoint p{MX, MY};
			sdl3::FRect header{c.screen.x, c.screen.y, c.screen.w, tn.headerHeight};
			sdl3::FRect arrow{c.screen.x, c.screen.y, 20.f, tn.headerHeight};
			bool overHeader = header.Contains(p) && c.clip.Contains(p) && IsFrontMost(e) &&
							  !IsHiddenRecursive(world, e) &&
							  !IsDisabledRecursive(world, e);
			tn.hovered = overHeader;
			tn.hoveredArrow = overHeader && arrow.Contains(p);
			clickRow(e, overHeader && !tn.hoveredArrow, tn.pressed);
			if (PRESSED && overHeader && !tn.hoveredArrow) {
				ecs::Entity container = NearestSelectionAncestor(world, e);
				if (container.Valid()) {
					if (auto us = world.GetComponent<UiSelection>(container); us.IsSome()) {
						ApplySelectionClick(us.Unwrap()->state, tn.index, ctrlDown, in.shift,
											us.Unwrap()->multiSelect);
						clickedContainer = container;
					}
				}
			}
		});
		if (clickedContainer.Valid()) {
			if (auto us = world.GetComponent<UiSelection>(clickedContainer); us.IsSome()) {
				const auto &selectedSet = us.Unwrap()->state.selected;
				world.Query<UiSelectable>([&](ecs::Entity e, UiSelectable &sel) {
					if (NearestSelectionAncestor(world, e) == clickedContainer)
						sel.selected = selectedSet.contains(sel.index);
				});
				world.Query<UiTreeNode>([&](ecs::Entity e, UiTreeNode &tn) {
					if (NearestSelectionAncestor(world, e) == clickedContainer)
						tn.selected = selectedSet.contains(tn.index);
				});
			}
		}
	}

	// ── UiTreeNode : clic sur la FLÈCHE replie/déplie les enfants
	// (structurel → différé, même motif que UiExpander).
	{
		std::vector<ecs::Entity> toggled;
		world.Query<UiTreeNode, UiComputed>([&](ecs::Entity e, UiTreeNode &tn, UiComputed &) {
			if (PRESSED && tn.hoveredArrow) {
				tn.expanded = !tn.expanded;
				toggled.push_back(e);
			}
		});
		for (ecs::Entity e : toggled) {
			auto tn = world.GetComponent<UiTreeNode>(e);
			if (tn.IsNone())
				continue;
			bool expanded = tn.Unwrap()->expanded;
			if (auto ch = world.GetComponent<UiChildren>(e); ch.IsSome()) {
				std::vector<ecs::Entity> kids = ch.Unwrap()->list;
				for (ecs::Entity k : kids) {
					if (expanded)
						world.RemoveComponent<UiHidden>(k);
					else if (!world.HasComponent<UiHidden>(k))
						world.AddComponent(k, UiHidden{});
				}
			}
			layout.MarkDirty();
			if (auto cb = world.GetComponent<UiCallbacks>(e); cb.IsSome() && cb.Unwrap()->onToggle) {
				auto fn = cb.Unwrap()->onToggle;
				pending.push_back([fn, expanded] { fn(expanded); });
			}
		}
	}

	// ── UiReorderable : glisser vertical réordonne l'entité parmi ses
	// frères directs (cf. interaction.hpp) — un seuil de mouvement
	// (dragMoved) évite qu'un simple clic de sélection (souvent posé sur
	// la MÊME entité, cf. blocs ci-dessus) ne déclenche un
	// réordonnancement parasite. Mute directement UiChildren du parent
	// (reorderChild) : l'ordre visuel EST la vérité.
	if (reorderDrag.Valid()) {
		if (in.down) {
			if (auto rc = world.GetComponent<UiReorderable>(reorderDrag); rc.IsSome()) {
				if (sdl3::Abs(MY - rc.Unwrap()->dragStartMouseY) > 4.f)
					rc.Unwrap()->dragMoved = true;
				if (rc.Unwrap()->dragMoved) {
					ecs::Entity parent{};
					if (auto p = world.GetComponent<UiParent>(reorderDrag); p.IsSome())
						parent = p.Unwrap()->parent;
					if (auto ch = world.GetComponent<UiChildren>(parent); ch.IsSome()) {
						ecs::Entity closest{};
						float bestDist = 1e30f;
						for (ecs::Entity sib : ch.Unwrap()->list) {
							if (sib == reorderDrag || !world.HasComponent<UiReorderable>(sib))
								continue;
							auto c = world.GetComponent<UiComputed>(sib);
							if (c.IsNone())
								continue;
							float cy = c.Unwrap()->screen.y + c.Unwrap()->screen.h * 0.5f;
							float d = sdl3::Abs(cy - MY);
							if (d < bestDist) {
								bestDist = d;
								closest = sib;
							}
						}
						if (closest.Valid())
							reorderTarget = closest;
					}
				}
			}
			clickConsumed = true;
		}
		if (in.released) {
			bool didMove = false;
			if (auto rc = world.GetComponent<UiReorderable>(reorderDrag); rc.IsSome()) {
				didMove = rc.Unwrap()->dragMoved;
				rc.Unwrap()->dragging = false;
				rc.Unwrap()->dragMoved = false;
			}
			if (didMove && reorderTarget.Valid() && reorderTarget != reorderDrag) {
				ecs::Entity parent{};
				if (auto p = world.GetComponent<UiParent>(reorderDrag); p.IsSome())
					parent = p.Unwrap()->parent;
				if (parent.Valid()) {
					int fromIdx = -1, toIdx = -1;
					if (auto ch = world.GetComponent<UiChildren>(parent); ch.IsSome()) {
						const auto &list = ch.Unwrap()->list;
						for (size_t i = 0; i < list.size(); ++i) {
							if (list[i] == reorderDrag)
								fromIdx = int(i);
							if (list[i] == reorderTarget)
								toIdx = int(i);
						}
					}
					ecs::Entity dragged = reorderDrag, target = reorderTarget;
					if (fromIdx >= 0 && toIdx >= 0 && fromIdx != toIdx) {
						ReorderChild(world, parent, dragged, target);
						layout.MarkDirty();
						if (auto cb = world.GetComponent<UiCallbacks>(parent);
							cb.IsSome() && cb.Unwrap()->onReorder) {
							auto fn = cb.Unwrap()->onReorder;
							pending.push_back([fn, fromIdx, toIdx] { fn(fromIdx, toIdx); });
						}
					}
				}
			}
			reorderDrag = ecs::Entity{};
			reorderTarget = ecs::Entity{};
		}
	} else if (PRESSED) {
		world.Query<UiReorderable, UiComputed>([&](ecs::Entity e, UiReorderable &rc, UiComputed &c) {
			if (reorderDrag.Valid() || !hitOk(e, c))
				return;
			reorderDrag = e;
			reorderTarget = ecs::Entity{};
			rc.dragging = true;
			rc.dragMoved = false;
			rc.dragStartMouseY = MY;
		});
	}

	// ── MenuBar / Menu / sous-menu (Phase 5) : clic sur un UiMenuBarItem
	// bascule son popup déroulant (positionné SOUS l'item à l'ouverture,
	// cf. positionPopupBelow) ; clic sur un UiMenuItem SANS sous-menu
	// exécute onClick puis ferme toute la chaîne de popups (cf.
	// closeMenuChain) ; AVEC sous-menu, bascule l'ouverture de celui-ci
	// (positionné à DROITE, cf. positionPopupRightOf) au lieu d'exécuter/
	// fermer. La fermeture au clic EXTÉRIEUR (tous niveaux) est déjà
	// gérée par le bloc popups non-modaux plus haut dans dispatch() —
	// chaque UiPopupState ouvert y est testé indépendamment, donc une
	// chaîne à plusieurs niveaux se referme déjà en cascade sans code
	// supplémentaire ici. openPopup/closePopup mutent UiHidden
	// (add_component/remove_component) : comme partout ailleurs dans
	// cette fonction, on COLLECTE d'abord (pendant les Query<>), on
	// applique APRÈS — muter pendant l'itération migrerait l'archétype
	// et pourrait réallouer le vecteur en cours de parcours.
	struct MenuToggle {
		ecs::Entity popup, trigger;
		sdl3::FRect triggerScreen;
		bool open;
	};
	std::vector<MenuToggle> menuToggles;
	std::vector<ecs::Entity> clickedLeaf;

	world.Query<UiMenuBarItem, UiComputed>([&](ecs::Entity e, UiMenuBarItem &mb, UiComputed &c) {
		mb.hovered = hitOk(e, c);
		if (PRESSED && mb.hovered && mb.menuPopup.Valid()) {
			bool open = false;
			if (auto ps = world.GetComponent<UiPopupState>(mb.menuPopup); ps.IsSome())
				open = ps.Unwrap()->open;
			menuToggles.push_back({mb.menuPopup, e, c.screen, !open});
		}
	});
	world.Query<UiMenuItem, UiComputed>([&](ecs::Entity e, UiMenuItem &mi, UiComputed &c) {
		mi.hovered = hitOk(e, c);
		if (PRESSED && mi.hovered) {
			if (mi.hasSubmenu && mi.submenuPopup.Valid()) {
				// Toujours OUVRIR : le survol l'a souvent déjà déployé (cf.
				// Tick), un clic qui le refermerait surprendrait.
				menuToggles.push_back({mi.submenuPopup, e, c.screen, true});
			} else {
				clickedLeaf.push_back(e);
			}
		}
	});
	for (auto &t : menuToggles) {
		if (t.open) {
			// Un item SANS sous-menu place le popup SOUS lui (barre de
			// menu) ; AVEC déclencheur = un UiMenuItem (donc DANS un
			// popup existant), à DROITE (sous-menu) — distingué via le
			// type du composant porté par `t.trigger`.
			if (world.HasComponent<UiMenuItem>(t.trigger)) {
				// Un seul sous-menu déployé par niveau.
				CloseSubmenusOf(world, layout, NearestPopupAncestor(world, t.trigger), t.popup);
				PositionPopupRightOf(world, t.popup, t.triggerScreen);
			} else {
				PositionPopupBelow(world, t.popup, t.triggerScreen);
			}
			OpenPopup(world, layout, t.popup, t.trigger);
		} else {
			ClosePopup(world, layout, t.popup);
		}
	}
	for (ecs::Entity e : clickedLeaf) {
		if (auto cb = world.GetComponent<UiCallbacks>(e); cb.IsSome() && cb.Unwrap()->onClick) {
			auto fn = cb.Unwrap()->onClick;
			pending.push_back(std::move(fn));
		}
		CloseMenuChain(world, layout, e);
	}

	// ── Table (Phase 6) : clic d'en-tête trie (bascule NONE→Asc→Desc→
	// NONE) SAUF dans la zone de redimensionnement (bordure droite de
	// colonne, ~6px) qui redimensionne à la place ; clic de ligne
	// applique applySelectionClick sur le UiSelection posé sur la MÊME
	// entité (cf. UiFactory::Table()) ; glisser une ligne (si
	// `reorderable`) la réordonne parmi ses sœurs — mêmes algorithmes
	// que UiResizeHandle/UiReorderable (Phase 2/4), réimplémentés ici
	// sur les tableaux internes de UiTable plutôt que sur des
	// accesseurs/entités indépendantes (cf. le composant, components.hpp,
	// pour la justification : une grille de cellules-entités serait
	// hors de propos pour un widget sans interactivité par cellule).
	world.Query<UiTable, UiComputed>([&](ecs::Entity e, UiTable &t, UiComputed &c) {
		bool ctrlDown = (sdl3::keyboard::Mods() & SDL_KMOD_CTRL) != 0;
		sdl3::FPoint p{MX, MY};
		bool overWidget = c.screen.Contains(p) && c.clip.Contains(p) && IsFrontMost(e) &&
						  !IsHiddenRecursive(world, e) && !IsDisabledRecursive(world, e);
		sdl3::FRect headerRect{c.screen.x, c.screen.y, c.screen.w, t.headerHeight};
		sdl3::FRect bodyRect{c.screen.x, c.screen.y + t.headerHeight, c.screen.w,
					   sdl3::Max(0.f, c.screen.h - t.headerHeight)};
		bool overHeader = overWidget && headerRect.Contains(p);
		bool overBody = overWidget && bodyRect.Contains(p);

		// Redimensionnement de colonne en cours : capte tout, rien
		// d'autre ne doit réagir tant que le bouton reste enfoncé.
		if (t.resizingColumn >= 0) {
			if (in.down) {
				float delta = MX - t.resizeStartMouseX;
				auto &col = t.columns[size_t(t.resizingColumn)];
				col.width = sdl3::Max(col.minWidth, t.resizeStartWidth + delta);
			}
			if (in.released)
				t.resizingColumn = -1;
			return;
		}

		// Glisser de ligne en cours : idem.
		if (t.reorderRow >= 0) {
			if (in.down) {
				if (sdl3::Abs(MY - t.reorderStartMouseY) > 4.f)
					t.reorderMoved = true;
				if (t.reorderMoved && overBody && !t.rows.empty()) {
					float relY = MY - bodyRect.y + t.scroll;
					int target = int(relY / t.rowHeight);
					t.reorderTargetRow = sdl3::Clamp(target, 0, int(t.rows.size()) - 1);
				}
			}
			if (in.released) {
				if (t.reorderMoved && t.reorderTargetRow >= 0 && t.reorderTargetRow != t.reorderRow &&
					t.reorderTargetRow < int(t.rows.size())) {
					int from = t.reorderRow, toIdx = t.reorderTargetRow;
					auto moved = std::move(t.rows[size_t(from)]);
					t.rows.erase(t.rows.begin() + from);
					t.rows.insert(t.rows.begin() + toIdx, std::move(moved));
					if (auto cb = world.GetComponent<UiCallbacks>(e); cb.IsSome() && cb.Unwrap()->onReorder) {
						auto fn = cb.Unwrap()->onReorder;
						pending.push_back([fn, from, toIdx] { fn(from, toIdx); });
					}
				}
				t.reorderRow = -1;
				t.reorderMoved = false;
				t.reorderTargetRow = -1;
			}
			return;
		}

		// Survol (hors drag).
		t.hoveredColumn = -1;
		t.hoveredRow = -1;
		if (overHeader) {
			float relX = MX - c.screen.x;
			float x = 0.f;
			for (int i = 0; i < int(t.columns.size()); ++i) {
				float w = t.columns[size_t(i)].width;
				if (relX >= x && relX < x + w) {
					t.hoveredColumn = i;
					break;
				}
				x += w;
			}
		} else if (overBody) {
			float relY = MY - bodyRect.y + t.scroll;
			int row = int(relY / t.rowHeight);
			if (row >= 0 && row < int(t.rows.size()))
				t.hoveredRow = row;
		}

		if (overWidget && in.wheelY != 0.f && !wheelConsumed) {
			t.scroll = sdl3::Clamp(t.scroll - in.wheelY * wheelSpeed, 0.f, t.MaxScroll(bodyRect.h));
			wheelConsumed = true;
		}

		if (!PRESSED || !overWidget)
			return;

		if (overHeader && t.hoveredColumn >= 0) {
			// Zone de redimensionnement : les ~6 derniers pixels de la colonne.
			float edgeX = t.ColumnX(t.hoveredColumn) + t.columns[size_t(t.hoveredColumn)].width;
			float relX = MX - c.screen.x;
			if (relX >= edgeX - 6.f && relX <= edgeX + 6.f) {
				t.resizingColumn = t.hoveredColumn;
				t.resizeStartMouseX = MX;
				t.resizeStartWidth = t.columns[size_t(t.hoveredColumn)].width;
			} else if (t.columns[size_t(t.hoveredColumn)].sortable) {
				if (t.sortColumn != t.hoveredColumn) {
					t.sortColumn = t.hoveredColumn;
					t.sortOrder = SortOrder::ASCENDING;
				} else {
					t.sortOrder = t.sortOrder == SortOrder::ASCENDING    ? SortOrder::DESCENDING
								  : t.sortOrder == SortOrder::DESCENDING ? SortOrder::NONE
																		 : SortOrder::ASCENDING;
				}
				int col = t.sortColumn;
				if (t.sortOrder == SortOrder::NONE) {
					t.sortColumn = -1;
				} else {
					bool asc = t.sortOrder == SortOrder::ASCENDING;
					std::stable_sort(t.rows.begin(), t.rows.end(),
									 [col, asc](const std::vector<String> &a, const std::vector<String> &b) {
										 String av = size_t(col) < a.size() ? a[size_t(col)] : String();
										 String bv = size_t(col) < b.size() ? b[size_t(col)] : String();
										 return asc ? av < bv : bv < av;
									 });
				}
				// Le tri réordonne les lignes : les index de sélection ne
				// correspondraient plus aux bonnes lignes — plus sûr de
				// vider que d'afficher une surbrillance sur la mauvaise
				// ligne (limitation connue : pas d'ID de ligne stable).
				if (auto us = world.GetComponent<UiSelection>(e); us.IsSome())
					us.Unwrap()->state = SelectionState{};
				if (auto cb = world.GetComponent<UiCallbacks>(e); cb.IsSome() && cb.Unwrap()->onChange) {
					auto fn = cb.Unwrap()->onChange;
					float v = float(col);
					pending.push_back([fn, v] { fn(v); });
				}
			}
		} else if (overBody && t.hoveredRow >= 0) {
			if (auto us = world.GetComponent<UiSelection>(e); us.IsSome())
				ApplySelectionClick(us.Unwrap()->state, t.hoveredRow, ctrlDown, in.shift, us.Unwrap()->multiSelect);
			if (t.reorderable) {
				t.reorderRow = t.hoveredRow;
				t.reorderMoved = false;
				t.reorderStartMouseY = MY;
				t.reorderTargetRow = t.hoveredRow;
			}
		}
	});

	// ── Plot (cf. ui/plot.hpp) : survol (point le plus proche, pour
	// l'infobulle) + légende (survol = surbrillance, clic = bascule
	// visible/invisible d'une série) + navigation (Phase 3 : pan glisser
	// gauche, zoom molette par axe/combiné, zoom-rectangle glisser
	// droit, retour à l'ajustement automatique au double-clic).
	world.Query<UiPlot, UiComputed>([&](ecs::Entity e, UiPlot &p, UiComputed &c) {
		p.hovered = hitOk(e, c);
		sdl3::FRect plotRect = ComputePlotRect(p, c.screen);

		// ── PlotMode::PIE (Phase 5) : ni axes ni navigation (pan/zoom
		// n'ont pas de sens sur un camembert) — juste légende (survol/
		// bascule, comme XY) et survol direct d'une part.
		if (p.mode == PlotMode::PIE) {
			p.hoveredIndex = -1; // pas de notion d'"index dans la serie" pour Pie
			p.legendHover = -1;
			if (!p.hovered) {
				p.hoveredSeries = -1;
				return;
			}
			if (p.showLegend) {
				sdl3::FRect legendBox = LegendBoxRect(p, c.screen, plotRect);
				for (size_t i = 0; i < p.pieSlices.size(); ++i) {
					if (LegendRowRect(legendBox, i).Contains({MX, MY})) {
						p.legendHover = int(i);
						if (in.pressed)
							p.pieSlices[i].visible = !p.pieSlices[i].visible;
						break;
					}
				}
			}
			if (p.legendHover >= 0) {
				p.hoveredSeries = -1;
				return;
			}
			sdl3::FRect circleRect = ComputePieCircleRect(plotRect);
			sdl3::FPoint center{circleRect.x + circleRect.w * 0.5f, circleRect.y + circleRect.h * 0.5f};
			auto angles = ComputePieAngles(p.pieSlices);
			p.hoveredSeries = PieHitTest(p.pieSlices, angles, center, circleRect.w * 0.5f, {MX, MY});
			return;
		}

		// ── PlotMode::HEATMAP (Phase 6) : pas d'axes/légende/navigation —
		// juste le survol d'une cellule (valeur sous le curseur).
		if (p.mode == PlotMode::HEATMAP) {
			p.hoveredIndex = -1;
			p.legendHover = -1;
			p.hoveredSeries = p.hovered ? HeatmapHitTest(p.heatmap, plotRect, {MX, MY}) : -1;
			return;
		}

		// ── PlotMode::CANDLE (Phase 6) : graduations/grille réutilisées de
		// XY (cf. resolveCandleAxis()), mais PAS de pan/zoom/box-zoom/reset
		// — un chandelier n'a pas de plage manuelle à préserver d'une frame
		// à l'autre (toujours entièrement auto-ajusté), et PAS de légende
		// (une seule série de bougies par plot). Survol = bougie la plus
		// proche EN X (cf. candleNearestBar(), pas plotNearestPoint()).
		if (p.mode == PlotMode::CANDLE) {
			p.legendHover = -1;
			p.hoveredIndex = -1;
			if (!p.hovered) {
				p.hoveredSeries = -1;
				return;
			}
			PlotAxis xr = ResolveCandleAxis(p.xAxis, p.candleBars, true);
			p.hoveredSeries = CandleNearestBar(p.candleBars, plotRect, xr, {MX, MY});
			return;
		}

		// Poursuite d'un pan/zoom-rectangle déjà en cours : NE DÉPEND PAS
		// de `p.hovered` — l'utilisateur peut glisser hors du widget en
		// cours de geste (SDL ne clippe pas le mouvement au widget
		// d'origine), le geste doit continuer à suivre la souris.
		if (p.panning) {
			p.dragCurrentMouse = {MX, MY};
			if (in.down)
				ApplyPlotPan(p, plotRect, p.dragStartMouse, p.dragCurrentMouse, p.dragStartXAxis,
							p.dragStartYAxis, p.dragStartYAxis2);
			else
				p.panning = false;
			return;
		}
		if (p.boxZooming) {
			p.dragCurrentMouse = {MX, MY};
			if (!in.rightDown) {
				p.boxZooming = false;
				float dw = sdl3::Abs(p.dragCurrentMouse.x - p.dragStartMouse.x);
				float dh = sdl3::Abs(p.dragCurrentMouse.y - p.dragStartMouse.y);
				if (dw > 4.f && dh > 4.f) // vrai glisser, pas un simple clic-droit
					ApplyPlotBoxZoom(p, plotRect, p.dragStartMouse, p.dragCurrentMouse, p.dragStartXAxis,
									p.dragStartYAxis, p.dragStartYAxis2);
			}
			return;
		}

		p.hoveredSeries = p.hoveredIndex = -1;
		p.legendHover = -1;
		if (!p.hovered)
			return;

		if (p.showLegend) {
			sdl3::FRect legendBox = LegendBoxRect(p, c.screen, plotRect);
			for (size_t i = 0; i < p.series.size(); ++i) {
				if (LegendRowRect(legendBox, i).Contains({MX, MY})) {
					p.legendHover = int(i);
					if (in.pressed)
						p.series[i].visible = !p.series[i].visible;
					break;
				}
			}
		}
		if (p.legendHover >= 0)
			return; // un clic/survol sur la légende n'affecte pas le survol de données/la navigation

		if (in.pressed && in.doubleClick) {
			ResetPlotToAutoFit(p);
			return;
		}
		if (in.pressed) { // début d'un pan
			p.panning = true;
			p.hoveredSeries = p.hoveredIndex = -1;
			p.dragStartMouse = p.dragCurrentMouse = {MX, MY};
			p.dragStartXAxis = ResolveAxis(p.xAxis, p.series, true);
			p.dragStartYAxis = ResolveAxis(p.yAxis, p.series, false);
			p.dragStartYAxis2 = ResolveAxis(p.yAxis2, p.series, false, true);
			return;
		}
		if (in.rightPressed) { // début d'un zoom-rectangle
			p.boxZooming = true;
			p.hoveredSeries = p.hoveredIndex = -1;
			p.dragStartMouse = p.dragCurrentMouse = {MX, MY};
			p.dragStartXAxis = ResolveAxis(p.xAxis, p.series, true);
			p.dragStartYAxis = ResolveAxis(p.yAxis, p.series, false);
			p.dragStartYAxis2 = ResolveAxis(p.yAxis2, p.series, false, true);
			return;
		}
		if (in.wheelY != 0.f && !wheelConsumed) {
			// Molette au-dessus d'une bande de graduation RÉSERVÉE ->
			// zoome CET axe seul (primaire, secondaire — Phase 4 — ou X) ;
			// ailleurs sur le widget -> X + les DEUX axes Y ensemble (cf.
			// plan : "par axe ou combiné").
			bool overXTicks = !p.xAxis.tickOverlay &&
							  ((p.xAxis.tickPosition == PlotEdge::Bottom && MY > plotRect.y + plotRect.h) ||
							   (p.xAxis.tickPosition == PlotEdge::Top && MY < plotRect.y));
			bool overYTicks = !p.yAxis.tickOverlay &&
							  ((p.yAxis.tickPosition == PlotEdge::Right && MX > plotRect.x + plotRect.w) ||
							   (p.yAxis.tickPosition == PlotEdge::Left && MX < plotRect.x));
			bool overY2Ticks = PlotHasSecondaryY(p) && !p.yAxis2.tickOverlay &&
							   ((p.yAxis2.tickPosition == PlotEdge::Right && MX > plotRect.x + plotRect.w) ||
								(p.yAxis2.tickPosition == PlotEdge::Left && MX < plotRect.x));
			if (overXTicks) {
				ApplyPlotAxisZoom(p.xAxis, p.series, true, plotRect.x, plotRect.x + plotRect.w, MX, in.wheelY);
			} else if (overYTicks) {
				ApplyPlotAxisZoom(p.yAxis, p.series, false, plotRect.y + plotRect.h, plotRect.y, MY, in.wheelY);
			} else if (overY2Ticks) {
				ApplyPlotAxisZoom(p.yAxis2, p.series, false, plotRect.y + plotRect.h, plotRect.y, MY, in.wheelY,
								  true);
			} else {
				ApplyPlotAxisZoom(p.xAxis, p.series, true, plotRect.x, plotRect.x + plotRect.w, MX, in.wheelY);
				ApplyPlotAxisZoom(p.yAxis, p.series, false, plotRect.y + plotRect.h, plotRect.y, MY, in.wheelY);
				if (PlotHasSecondaryY(p))
					ApplyPlotAxisZoom(p.yAxis2, p.series, false, plotRect.y + plotRect.h, plotRect.y, MY,
									  in.wheelY, true);
			}
			wheelConsumed = true;
			return;
		}

		PlotAxis xr = ResolveAxis(p.xAxis, p.series, true);
		PlotAxis yr = ResolveAxis(p.yAxis, p.series, false);
		PlotAxis yr2 = ResolveAxis(p.yAxis2, p.series, false, true);
		auto [si, idx] = PlotNearestPoint(p, plotRect, xr, yr, yr2, {MX, MY});
		p.hoveredSeries = si;
		p.hoveredIndex = idx;
	});

	// ── UiCalendar (Phase 8, DatePicker) : en-tête (flèches précédent/
	// suivant, mutation directe de month/year — pas de add_component,
	// sûr en ligne) change de mois ; clic sur une case pose selectedDay
	// et notifie onDaySelected (différé via `pending`, comme tout
	// callback applicatif — cf. le commentaire de dispatch()).
	world.Query<UiCalendar, UiComputed>([&](ecs::Entity e, UiCalendar &cal, UiComputed &c) {
		sdl3::FPoint p{MX, MY};
		bool overWidget = c.screen.Contains(p) && c.clip.Contains(p) && IsFrontMost(e) &&
						  !IsHiddenRecursive(world, e) &&
						  !IsDisabledRecursive(world, e);
		cal.hoveredDay = -1;
		cal.hoveredPrevArrow = false;
		cal.hoveredNextArrow = false;
		if (!overWidget)
			return;
		const float HEADER_H = 28.f;
		sdl3::FRect header{c.screen.x, c.screen.y, c.screen.w, HEADER_H};
		sdl3::FRect prevArrow{c.screen.x, c.screen.y, 28.f, HEADER_H};
		sdl3::FRect nextArrow{c.screen.x + c.screen.w - 28.f, c.screen.y, 28.f, HEADER_H};
		if (header.Contains(p)) {
			cal.hoveredPrevArrow = prevArrow.Contains(p);
			cal.hoveredNextArrow = nextArrow.Contains(p);
			if (PRESSED) {
				if (cal.hoveredPrevArrow) {
					if (--cal.month < 1) {
						cal.month = 12;
						--cal.year;
					}
				} else if (cal.hoveredNextArrow) {
					if (++cal.month > 12) {
						cal.month = 1;
						++cal.year;
					}
				}
			}
			return;
		}
		sdl3::FRect grid{c.screen.x, c.screen.y + HEADER_H, c.screen.w, sdl3::Max(0.f, c.screen.h - HEADER_H)};
		if (!grid.Contains(p))
			return;
		float cellW = grid.w / 7.f, cellH = grid.h / 6.f;
		int col = sdl3::Clamp(int((MX - grid.x) / sdl3::Max(1.f, cellW)), 0, 6);
		int row = sdl3::Clamp(int((MY - grid.y) / sdl3::Max(1.f, cellH)), 0, 5);
		int offset = FirstWeekdayOfMonth(cal.year, cal.month);
		int dayIdx = row * 7 + col - offset + 1;
		int dim = DaysInMonth(cal.year, cal.month);
		if (dayIdx >= 1 && dayIdx <= dim) {
			cal.hoveredDay = dayIdx;
			if (PRESSED) {
				cal.selectedDay = dayIdx;
				if (cal.onDaySelected) {
					auto fn = cal.onDaySelected;
					int y = cal.year, m = cal.month, d = dayIdx;
					pending.push_back([fn, y, m, d] { fn(y, m, d); });
				}
			}
		}
	});

	// ── Champs de saisie (mono-ligne, texte enveloppé) ───────────────────
	world.Query<UiInput, UiRect, UiComputed>([&](ecs::Entity e, UiInput &f, UiRect &r, UiComputed &c) {
		f.changed = false;
		f.submitted = false;
		f.hovered = hitOk(e, c);

		float fs = GetResolved(world, e).FontSize(14.f);
		float innerW = sdl3::Max(1.f, c.screen.w - 16.f);
		auto lines = WrapText(f.text, fs, innerW);

		// Clic : (re)focus + place curseur/ancre de sélection sous la
		// souris et démarre un glisser de sélection. Glisser en cours :
		// ne bouge que le curseur (l'ancre reste fixe).
		if (PRESSED) {
			f.focused = f.hovered;
			if (f.focused) {
				f.cursor = f.selectionAnchor = HitTestOffset(lines, f.text, fs, c.screen, r.scroll, MX, MY);
				textDrag = e;
			}
		} else if (textDrag == e && in.down) {
			f.cursor = HitTestOffset(lines, f.text, fs, c.screen, r.scroll, MX, MY);
		}
		if (in.released && textDrag == e)
			textDrag = ecs::Entity{};
		if (!f.focused)
			return;

		MoveCursor(f.text, lines, f.cursor, f.selectionAnchor, in);

		// enterInsertsNewline=false : Entrée soumet (ci-dessous), n'insère rien.
		Frame editIn = in;
		editIn.enter = false;
		f.changed = ApplyTextEdit(f.text, f.cursor, f.selectionAnchor, f.ioMode, f.maxLen, editIn, false);
		if (f.changed) {
			if (auto cb = world.GetComponent<UiCallbacks>(e); cb.IsSome() && cb.Unwrap()->onTextChange) {
				auto fn = cb.Unwrap()->onTextChange;
				String v = f.text;
				pending.push_back([fn, v] { fn(v); });
			}
		}
		if (in.enter && IoCanWrite(f.ioMode)) {
			f.submitted = true;
			if (auto cb = world.GetComponent<UiCallbacks>(e); cb.IsSome() && cb.Unwrap()->onSubmit) {
				auto fn = cb.Unwrap()->onSubmit;
				String v = f.text;
				pending.push_back([fn, v] { fn(v); });
			}
		}
	});

	// ── Zones de texte multi-ligne ───────────────────────────────────────
	world.Query<UiInputArea, UiRect, UiComputed>([&](ecs::Entity e, UiInputArea &f, UiRect &r, UiComputed &c) {
		f.changed = false;
		f.hovered = hitOk(e, c);

		float fs = GetResolved(world, e).FontSize(14.f);
		auto lines = SplitLines(f.text);
		const float cw = AreaCellWidth(f, fs);
		const sdl3::FRect box = AreaTextBox(c.screen, AreaGutterWidth(f, lines.size(), cw));

		if (PRESSED) {
			f.focused = f.hovered;
			if (f.focused) {
				f.cursor = f.selectionAnchor =
					HitTestOffset(lines, f.text, fs, box, r.scroll, MX, MY, tabStop, cw);
				textDrag = e;
			}
		} else if (textDrag == e && in.down) {
			f.cursor = HitTestOffset(lines, f.text, fs, box, r.scroll, MX, MY, tabStop, cw);
		}
		if (in.released && textDrag == e)
			textDrag = ecs::Entity{};
		if (!f.focused)
			return;

		MoveCursor(f.text, lines, f.cursor, f.selectionAnchor, in);

		f.changed = ApplyTextEdit(f.text, f.cursor, f.selectionAnchor, f.ioMode, f.maxLen, in,
								  /*enterInsertsNewline=*/true);
		if (f.changed) {
			if (auto cb = world.GetComponent<UiCallbacks>(e); cb.IsSome() && cb.Unwrap()->onTextChange) {
				auto fn = cb.Unwrap()->onTextChange;
				String v = f.text;
				pending.push_back([fn, v] { fn(v); });
			}
		}
	});

	// ── ListBox : sélection au clic, scroll interne à la molette ─────────
	world.Query<ListBox, UiComputed>([&](ecs::Entity e, ListBox &lb, UiComputed &c) {
		lb.hovered = hitOk(e, c);
		lb.hoveredItem = -1;
		lb.scroll = sdl3::Clamp(lb.scroll, 0.f, lb.MaxScroll(c.screen.h));
		if (lb.hovered && lb.itemHeight > 0.f) {
			int idx = int((MY - c.screen.y + lb.scroll) / lb.itemHeight);
			if (idx >= 0 && idx < int(lb.items.size()))
				lb.hoveredItem = idx;
			if (in.wheelY != 0.f && !wheelConsumed && lb.MaxScroll(c.screen.h) > 0.f) {
				lb.scroll = sdl3::Clamp(lb.scroll - in.wheelY * wheelSpeed, 0.f, lb.MaxScroll(c.screen.h));
				wheelConsumed = true;
			}
		}
		if (PRESSED && lb.hoveredItem >= 0 && lb.hoveredItem != lb.selected) {
			lb.selected = lb.hoveredItem;
			if (auto cb = world.GetComponent<UiCallbacks>(e); cb.IsSome() && cb.Unwrap()->onChange) {
				auto fn = cb.Unwrap()->onChange;
				float v = float(lb.selected);
				pending.push_back([fn, v] { fn(v); });
			}
		}
	});

	// ── Expander : clic sur l'en-tête = replier/déplier les enfants ──────
	// Le changement de visibilité (UiHidden) est structurel → différé
	// après la query.
	{
		std::vector<ecs::Entity> toggled;
		world.Query<UiExpander, UiComputed>([&](ecs::Entity e, UiExpander &x, UiComputed &c) {
			sdl3::FPoint p{MX, MY};
			sdl3::FRect header{c.screen.x, c.screen.y, c.screen.w, x.headerHeight};
			x.hovered = header.Contains(p) && c.clip.Contains(p) && IsFrontMost(e) &&
						!IsHiddenRecursive(world, e) &&
						!IsDisabledRecursive(world, e);
			if (PRESSED && x.hovered) {
				x.expanded = !x.expanded;
				toggled.push_back(e);
			}
		});
		for (ecs::Entity e : toggled) {
			auto x = world.GetComponent<UiExpander>(e);
			if (x.IsNone())
				continue;
			bool expanded = x.Unwrap()->expanded;
			if (auto ch = world.GetComponent<UiChildren>(e); ch.IsSome()) {
				std::vector<ecs::Entity> kids = ch.Unwrap()->list;
				for (ecs::Entity k : kids) {
					if (expanded)
						world.RemoveComponent<UiHidden>(k);
					else if (!world.HasComponent<UiHidden>(k))
						world.AddComponent(k, UiHidden{});
				}
			}
			layout.MarkDirty();
			if (auto cb = world.GetComponent<UiCallbacks>(e); cb.IsSome() && cb.Unwrap()->onToggle) {
				auto fn = cb.Unwrap()->onToggle;
				pending.push_back([fn, expanded] { fn(expanded); });
			}
		}
	}

	// ── TabView : clic dans la barre = changer d'onglet actif ────────────
	{
		std::vector<ecs::Entity> switched;
		world.Query<UiTabView, UiComputed>([&](ecs::Entity e, UiTabView &tv, UiComputed &c) {
			tv.hoveredTab = -1;
			if (tv.tabs.empty())
				return;
			sdl3::FPoint p{MX, MY};
			sdl3::FRect bar{c.screen.x, c.screen.y, c.screen.w, tv.tabHeight};
			bool overBar = bar.Contains(p) && c.clip.Contains(p) && IsFrontMost(e) &&
						   !IsHiddenRecursive(world, e) &&
						   !IsDisabledRecursive(world, e);
			if (overBar) {
				float tabW = c.screen.w / float(tv.tabs.size());
				tv.hoveredTab =
					sdl3::Clamp(int((MX - c.screen.x) / sdl3::Max(1.f, tabW)), 0, int(tv.tabs.size()) - 1);
			}
			if (PRESSED && tv.hoveredTab >= 0 && tv.hoveredTab != tv.active) {
				tv.active = tv.hoveredTab;
				switched.push_back(e);
			}
		});
		for (ecs::Entity e : switched) {
			auto tv = world.GetComponent<UiTabView>(e);
			if (tv.IsNone())
				continue;
			int active = tv.Unwrap()->active;
			if (auto ch = world.GetComponent<UiChildren>(e); ch.IsSome()) {
				std::vector<ecs::Entity> kids = ch.Unwrap()->list;
				for (size_t i = 0; i < kids.size(); ++i) {
					if (int(i) == active)
						world.RemoveComponent<UiHidden>(kids[i]);
					else if (!world.HasComponent<UiHidden>(kids[i]))
						world.AddComponent(kids[i], UiHidden{});
				}
			}
			layout.MarkDirty();
			if (auto cb = world.GetComponent<UiCallbacks>(e); cb.IsSome() && cb.Unwrap()->onChange) {
				auto fn = cb.Unwrap()->onChange;
				float v = float(active);
				pending.push_back([fn, v] { fn(v); });
			}
		}
	}

	// ── Molette : scroll du conteneur scrollable le plus profond sous la souris ──
	if (in.wheelY != 0.f && !wheelConsumed) {
		ecs::Entity best{};
		float bestArea = 1e30f;
		world.Query<UiRect, UiComputed>([&](ecs::Entity e, UiRect &r, UiComputed &c) {
			if (r.MaxScroll().y <= 0.f && r.MaxScroll().x <= 0.f)
				return;
			if (!hitOk(e, c))
				return;
			float area = c.screen.w * c.screen.h;
			if (area < bestArea) {
				bestArea = area;
				best = e;
			}
		});
		if (best.Valid()) {
			if (auto r = world.GetComponent<UiRect>(best); r.IsSome()) {
				r.Unwrap()->scroll.y -= in.wheelY * wheelSpeed;
				r.Unwrap()->ClampScroll();
				layout.MarkDirty(); // le scroll déplace les enfants → re-place
			}
		}
	}

	// Callbacks applicatifs : exécutés maintenant que toutes les queries
	// de cet évènement sont terminées — sûr même s'ils mutent l'ECS
	// (changement de scène, despawn...).
	for (auto &fn : pending)
		fn();
}

// ── RenderSystem ─────────────────────────────────────────────────────────────

void RenderSystem::SetTextEngine(sdl3::TextEngine *engine, sdl3::Font *font) {
	m_engine = engine;
	m_font = font;
}

void RenderSystem::Run(ecs::ArchetypeRegistry &world, IUiRenderBackend &ren, const InputSystem::Tooltip *tip) {
	ren.SetBlendMode(sdl3::BlendMode::BLEND);
	m_currentFontSize = 0.f;
	std::vector<ecs::Entity> roots;
	world.Query<UiRect>([&](ecs::Entity e, UiRect &) {
		if (!world.HasComponent<UiParent>(e) && !world.HasComponent<UiHidden>(e))
			roots.push_back(e);
	});
	for (ecs::Entity root : roots)
		DrawTree(world, ren, root);
	ren.ClearClipRect();

	// ── Overlays : widgets AttachLayout::Fixed (détachés de l'arbre de
	// dessin normal pour flotter au premier plan — popups, modales, menus
	// déroulants, HUD, panneaux flottants...), puis listes des combos
	// ouverts, puis infobulle : chaque catégorie peut se superposer à la
	// précédente. Triés par UiOverlayLayer.order (défaut 0) plutôt que
	// dessinés dans l'ordre d'itération brut, pour qu'un sous-menu/popup
	// ouvert par-dessus une modale se dessine bien au-dessus d'elle.
	std::vector<std::pair<int, ecs::Entity>> fixedRoots;
	world.Query<UiItem, UiComputed>([&](ecs::Entity e, UiItem &it, UiComputed &) {
		if (it.attach == AttachLayout::FIXED && !IsHiddenRecursive(world, e)) {
			int order = 0;
			if (auto ol = world.GetComponent<UiOverlayLayer>(e); ol.IsSome())
				order = ol.Unwrap()->order;
			fixedRoots.push_back({order, e});
		}
	});
	std::stable_sort(fixedRoots.begin(), fixedRoots.end(),
					 [](const auto &a, const auto &b) { return a.first < b.first; });
	for (auto &[order, e] : fixedRoots)
		DrawTree(world, ren, e, /*overlayEntry=*/true);
	ren.ClearClipRect();

	world.Query<UiComboBox, UiComputed>([&](ecs::Entity e, UiComboBox &cb, UiComputed &c) {
		if (cb.open && !IsHiddenRecursive(world, e)) {
			if (sdl3::Renderer *native = ren.NativeRenderer())
				cb.screenBottom = float(native->OutputSize().y); // pour placer le sous-menu
			DrawDropdown(ren, cb, c.screen, GetResolved(world, e));
		}
	});
	DrawDragFeedback(world, ren);
	if (tip && tip->visible && !tip->text.IsEmpty())
		DrawTooltip(ren, *tip);
}

void RenderSystem::DrawDragFeedback(ecs::ArchetypeRegistry &world, IUiRenderBackend &ren) {
	// Cibles de dépôt survolées par un glissé compatible : cadre d'accent,
	// découpé comme la cible (une ligne d'une liste qui défile ne déborde pas).
	world.Query<UiDropTarget, UiComputed>([&](ecs::Entity e, UiDropTarget &drop, UiComputed &c) {
		if (!drop.hovered || !drop.highlight || IsHiddenRecursive(world, e))
			return;
		ren.SetClipRect(ToClipRect(c.clip));
		const sdl3::FRect r{c.screen.x + 1.f, c.screen.y + 1.f, c.screen.w - 2.f, c.screen.h - 2.f};
		ren.SetDrawColor(sdl3::FColor{dragAccent.r, dragAccent.g, dragAccent.b, 0.16f});
		ren.FillRoundedRect(r, math::Corners(4.f));
		ren.SetDrawColor(dragAccent);
		ren.DrawRoundedRect(r, math::Corners(4.f));
		ren.DrawRoundedRect(sdl3::FRect{r.x + 1.f, r.y + 1.f, r.w - 2.f, r.h - 2.f}, math::Corners(3.f));
	});
	ren.ClearClipRect();
	// Fantôme de la source glissée, près du pointeur.
	world.Query<UiDragPayload>([&](ecs::Entity, UiDragPayload &payload) {
		if (!payload.dragging || payload.label.IsEmpty())
			return;
		m_currentFontSize = TOOLTIP_FONT_SIZE;
		const sdl3::FPoint sz = MeasureCached(payload.label, TOOLTIP_FONT_SIZE);
		const bool many = payload.count > 1;
		const float badge = many ? 20.f : 0.f;
		sdl3::FRect box{payload.pointer.x + 16.f, payload.pointer.y + 10.f, sdl3::Ceil(sz.x) + 18.f + badge,
						sdl3::Ceil(sz.y) + 10.f};
		ren.SetDrawColor(sdl3::FColor{tooltipBg.r, tooltipBg.g, tooltipBg.b, 0.92f});
		ren.FillRoundedRect(box, math::Corners(5.f));
		ren.SetDrawColor(dragAccent);
		ren.DrawRoundedRect(box, math::Corners(5.f));
		DrawTextRaw(ren, payload.label, tooltipText, box.x + 9.f + badge, box.y, box.h);
		if (many) {
			const String count = payload.count > 99 ? String("99+") : String::Format("%d", payload.count);
			const sdl3::FPoint csz = MeasureCached(count, TOOLTIP_FONT_SIZE);
			const float w = sdl3::Max(18.f, sdl3::Ceil(csz.x) + 8.f);
			const sdl3::FRect pill{box.x + 5.f, box.y + (box.h - 18.f) * 0.5f, w, 18.f};
			ren.SetDrawColor(dragAccent);
			ren.FillRoundedRect(pill, math::Corners(9.f));
			DrawTextRaw(ren, count, sdl3::FColor{1.f, 1.f, 1.f, 1.f}, pill.x + (w - csz.x) * 0.5f, pill.y, pill.h);
		}
		m_currentFontSize = 0.f;
	});
}

void RenderSystem::ClearTextCache() {
	textCache.clear();
	strCache.clear();
}

void RenderSystem::DrawTree(ecs::ArchetypeRegistry &world, IUiRenderBackend &ren, ecs::Entity e, bool overlayEntry) {
	if (world.HasComponent<UiHidden>(e))
		return;
	if (!overlayEntry) {
		if (auto it = world.GetComponent<UiItem>(e); it.IsSome() && it.Unwrap()->attach == AttachLayout::FIXED)
			return;
	}
	auto comp = world.GetComponent<UiComputed>(e);
	if (comp.IsNone())
		return;
	UiComputed c = *comp.Unwrap();
	if (c.clip.w <= 0.f || c.clip.h <= 0.f)
		return;

	// Ce qu'un widget dessine de LUI-MÊME est borné à SA PROPRE boîte
	// visible, et non au clip hérité de son parent.
	//
	// `c.clip` est la région léguée par le parent ; elle ne dit rien de la
	// boîte du widget. Un widget qui peint son propre contenu (champ de
	// texte, tableau, liste, tracé) débordait donc librement de lui-même
	// tant qu'il restait dans son parent : c'est ce qui faisait passer la
	// dernière ligne d'un éditeur de script SOUS sa bordure, et la
	// première ligne PAR-DESSUS. `c.childClip`, calculé par le layout
	// comme `clip ∩ screen`, est exactement « la partie visible de ce
	// widget » — c'est donc lui qui borne son dessin comme celui de ses
	// enfants.
	//
	// Les widgets qui doivent délibérément déborder (liste déroulante d'un
	// combo, infobulle, overlays AttachLayout::FIXED) ne passent pas par
	// ici : ils sont dessinés par des passes séparées de Run(), après un
	// ClearClipRect().
	//
	// La région est RECALCULÉE à partir des deux rects qui font autorité
	// plutôt que lue dans `c.childClip` — qui vaut exactement la même
	// chose après une passe de layout, mais reste vide pour un
	// `UiComputed` rempli à la main (ce que font le système d'effets et
	// ses tests, qui ne posent que `screen` et `clip`). Lire le champ
	// faisait alors sortir d'ici sans rien dessiner.
	//
	// Une seule exception, explicite : le halo de lueur « verre » est
	// dessiné HORS de la boîte par conception (cf. DrawGlowRing). Les
	// widgets qui en portent un reçoivent donc exactement cette marge —
	// jamais plus, et toujours dans les limites du parent.
	const float overflow = GetResolved(world, e).HasGlow() ? GLOW_MAX_OUTSET : 0.f;
	const sdl3::FRect selfClip = c.clip.Intersection(InsetRect(c.screen, -overflow));
	if (selfClip.w <= 0.f || selfClip.h <= 0.f)
		return;

	ren.SetClipRect(ToClipRect(selfClip));
	DrawWidget(world, ren, e, c);

	// UiShaderEffect (M23) avec texture prête : DrawWidget ci-dessus vient
	// de blitter le sous-arbre déjà post-traité (chrome + descendants
	// inclus, cf. shader_effect.hpp::ShaderEffectSystem::UpdateOne, étape
	// (a)) — descendre dans les enfants ici les dessinerait une SECONDE
	// fois, par-dessus. Absence d'entrée (pas encore traité, ou échec sans
	// texture précédente) : on descend normalement, comme n'importe quel
	// autre widget — dégradation "gratuite" (cf. shader_effect.hpp).
	bool subtreeAlreadyComposited = world.HasComponent<UiShaderEffect>(e) && effectTextures.contains(e);
	if (auto children = world.GetComponent<UiChildren>(e); children.IsSome() && !subtreeAlreadyComposited) {
		std::vector<ecs::Entity> kids = children.Unwrap()->list;
		for (ecs::Entity k : kids)
			DrawTree(world, ren, k);
	}

	// Auto-scrollbar : dès qu'il y a débordement réel, sans opt-in requis
	// (drawScrollbars ne dessine rien si maxScroll() == 0 sur les deux
	// axes) — par-dessus le contenu, bornée au clip de CE widget (déjà
	// actif ci-dessus), jamais celui, plus large, de ses enfants.
	if (auto r = world.GetComponent<UiRect>(e); r.IsSome()) {
		ren.SetClipRect(ToClipRect(selfClip));
		DrawScrollbars(ren, *r.Unwrap(), c.screen);
	}

	// Sous-arbre désactivé : voile sombre par-dessus (une seule fois, à la
	// racine du marqueur UiDisabled).
	if (world.HasComponent<UiDisabled>(e)) {
		ren.SetClipRect(ToClipRect(selfClip));
		ren.SetDrawColor(sdl3::FColor{15/255.f, 15/255.f, 20/255.f, 110/255.f});
		ren.FillRect(c.screen);
	}
}

void RenderSystem::DrawWidget(ecs::ArchetypeRegistry &world, IUiRenderBackend &ren, ecs::Entity e, const UiComputed &c) {
	// Taille de texte de CE widget : tout texte qu'il dessine (libellé,
	// bouton, icône, champ…) passe par DrawFont, qui la respecte.
	m_currentFontSize = GetResolved(world, e).FontSize(0.f);
	const sdl3::FRect &s = c.screen;

	if (world.HasComponent<UiPanel>(e)) {
		// UiPanel est un marqueur pur ("cette entité a un fond à
		// dessiner") — fond/dégradé/bordure/radius viennent entièrement
		// du style résolu (classes "root-panel"/inline, cf. styles.hpp).
		ResolvedStyle rs = GetResolved(world, e);
		sdl3::FColor bg = rs.Bg(sdl3::FColor{});
		Option<sdl3::FColor> grad = rs.BgGradientOpt();
		math::Corners radius = rs.BordersRadius(math::Corners{});
		bool hasBorder = rs.HasBorderColor();
		sdl3::FColor border = rs.BorderColor(sdl3::FColor{});

		if (grad.IsSome()) {
			DrawVGradient(ren, s, bg, grad.Unwrap());
		} else if (bg.a > 0) {
			ren.SetDrawColor(bg);
			ren.FillRoundedRect(s, radius);
		}
		if (hasBorder) {
			ren.SetDrawColor(border);
			// Épaisseur : contours concentriques (1 trait par pixel).
			// `.top` sert de représentatif uniforme — le cas courant
			// (bordure identique sur les 4 côtés) reste exact ; un
			// borderWidth non-uniforme n'est pas encore rendu par côté.
			int bw = int(BorderSides(rs).top);
			for (int i = 0; i < bw; ++i) {
				sdl3::FRect br{s.x + float(i), s.y + float(i), s.w - 2.f * float(i), s.h - 2.f * float(i)};
				if (br.w <= 0.f || br.h <= 0.f)
					break;
				float fi = float(i);
				math::Corners inset{sdl3::Max(0.f, radius.tl - fi), sdl3::Max(0.f, radius.tr - fi),
							  sdl3::Max(0.f, radius.bl - fi), sdl3::Max(0.f, radius.br - fi)};
				ren.DrawRoundedRect(br, inset);
			}
		}
		if (rs.HasGlow())
			DrawGlowRing(ren, s, radius.tl, rs.Glow(0.f), rs.GlowColor(sdl3::FColor::WHITE()));
		if (rs.HasGloss())
			DrawGlossHighlight(ren, s, radius.tl, rs.Gloss(0.f));
	}

	if (auto img = world.GetComponent<UiImage>(e); img.IsSome()) {
		auto it = textures.find(img.Unwrap()->textureKey.c_str());
		if (it != textures.end())
			DrawImageFit(ren, it->second, s, img.Unwrap()->fit);
	}

	// Contenu déjà entièrement rendu par Viewport3DSystem::Update, AVANT ce
	// passage de dessin 2D (cf. ui.hpp::Ui::Render() et viewport3d.hpp en-tête
	// de fichier pour l'ordre exact) — ici, un simple blit de la texture
	// résultat, comme UiImage juste au-dessus. displayTexture reste NONE tant
	// qu'aucune frame n'a pu être rendue (backend sans sdl3::Renderer réel,
	// widget pas encore passé par layout...) : dans ce cas on ne dessine
	// simplement rien ce frame-là (dégradation cohérente avec le reste du
	// dépôt). FILL (pas CONTAIN/COVER) : camera.aspect est déjà aligné sur les
	// dimensions EXACTES de l'OffscreenTarget par RenderObjectToTexture (M21),
	// donc la texture n'est jamais déformée par ce blit — FILL se contente de
	// la positionner pile sur le rectangle du widget, sans marges ni rognage.
	if (auto vp3 = world.GetComponent<UiViewport3D>(e); vp3.IsSome() && vp3.Unwrap()->displayTexture.IsSome())
		DrawImageFit(ren, *vp3.Unwrap()->displayTexture, s, ImageFit::FILL);

	// UiShaderEffect (M23, shader_effect.hpp) : même idée que UiViewport3D
	// juste au-dessus — le contenu (sous-arbre entier, chrome inclus) a
	// déjà été entièrement rendu ET post-traité par ShaderEffectSystem::
	// Update, AVANT ce passage de dessin 2D (cf. ui.hpp::Ui::Render()) ;
	// ici, un simple blit, comme UiImage/UiViewport3D. `effectTextures`
	// (pas le composant, cf. sa doc) porte la texture — absente tant
	// qu'aucun frame n'a pu être post-traité avec succès (dégradation :
	// DrawTree ci-dessus descend alors normalement dans les enfants à la
	// place, cf. son propre commentaire).
	if (world.HasComponent<UiShaderEffect>(e)) {
		if (auto it = effectTextures.find(e); it != effectTextures.end())
			DrawImageFit(ren, it->second, s, ImageFit::FILL);
	}

	if (auto ic = world.GetComponent<UiIcon>(e); ic.IsSome()) {
		const UiIcon &icon = *ic.Unwrap();
		sdl3::Font *f = m_font;
		if (auto fit = iconFonts.find(icon.font); fit != iconFonts.end())
			f = fit->second;
		// La taille d'une icône est la SIENNE (UiIcon::size, en pixels —
		// points à 72 ppp), pas la taille de texte héritée du parent.
		if (icon.size > 0.f)
			m_currentFontSize = icon.size;
		sdl3::FColor tint = GetResolved(world, e).TextColor(sdl3::FColor::UI_TEXT_PRIMARY());
		DrawTextCentered(ren, e, icon.glyph, tint, s, TextAlign::Center, f);
	}

	if (auto b = world.GetComponent<UiButton>(e); b.IsSome()) {
		const UiButton &btn = *b.Unwrap();
		ResolvedStyle rs = GetResolved(world, e);
		sdl3::FColor normal = rs.Bg(sdl3::FColor{40/255.f, 44/255.f, 64/255.f, 255/255.f});
		sdl3::FColor hoveredC = rs.BgHovered(sdl3::FColor::UI_BORDER_MUTED2());
		sdl3::FColor pressedC = rs.BgPressed(sdl3::FColor{24/255.f, 26/255.f, 40/255.f, 255/255.f});
		sdl3::FColor bg = btn.pressed ? pressedC : (btn.hovered ? hoveredC : normal);
		math::Corners radius = rs.BordersRadius(math::Corners(6.f));

		if (auto grad = rs.BgGradientOpt(); grad.IsSome())
			DrawVGradient(ren, s, bg, grad.Unwrap());
		else {
			ren.SetDrawColor(bg);
			ren.FillRoundedRect(s, radius);
		}
		sdl3::FColor borderNormal = rs.BorderColor(sdl3::FColor{0/255.f, 0/255.f, 0/255.f, 120/255.f});
		sdl3::FColor borderHover = rs.BorderFocus(sdl3::FColor{255/255.f, 255/255.f, 255/255.f, 60/255.f});
		ren.SetDrawColor(btn.hovered ? borderHover : borderNormal);
		ren.DrawRoundedRect(s, radius);
		if (rs.HasGlow())
			DrawGlowRing(ren, s, radius.tl, rs.Glow(0.f), rs.GlowColor(sdl3::FColor::WHITE()));
		if (rs.HasGloss())
			DrawGlossHighlight(ren, s, radius.tl, rs.Gloss(0.f));
		DrawTextCentered(ren, e, btn.text, rs.TextColor(sdl3::FColor::UI_TEXT_PRIMARY()), s, TextAlign::Center);
	}

	if (auto l = world.GetComponent<UiLabel>(e); l.IsSome()) {
		ResolvedStyle rs = GetResolved(world, e);
		const String &text = l.Unwrap()->text;
		sdl3::FColor textColor = rs.TextColor(sdl3::FColor::UI_TEXT_PRIMARY());
		TextAlign align = rs.TextAlign(TextAlign::Left);
		float fs = rs.FontSize(14.f);
		sdl3::Font *useFont = PickTextFont(rs.Bold(false), rs.Italic(false));

		bool hl = rs.Highlight(false), ul = rs.Underline(false), st = rs.Strikethrough(false);
		bool needsDecoration = (hl || ul || st) && !text.IsEmpty();
		sdl3::FPoint tsz{};
		float tx = s.x, ty = s.y;
		if (needsDecoration) {
			// Reproduit l'alignement interne de drawTextCentered pour
			// positionner les décorations exactement sous/sur/derrière le
			// texte réellement dessiné (pas de valeur de retour exposée
			// par drawTextCentered — dupliqué ici plutôt que de changer
			// sa signature, utilisée ailleurs sans ce besoin).
			tsz = MeasureCached(text, fs);
			tx = s.x + 8.f;
			if (align == TextAlign::Center)
				tx = s.x + (s.w - tsz.x) * 0.5f;
			else if (align == TextAlign::Right)
				tx = s.x + s.w - tsz.x - 8.f;
			ty = s.y + (s.h - tsz.y) * 0.5f;
		}
		if (hl && needsDecoration) {
			ren.SetDrawColor(rs.HighlightColor(sdl3::FColor{255/255.f, 230/255.f, 90/255.f, 130/255.f}));
			ren.FillRect({tx - 2.f, ty - 1.f, tsz.x + 4.f, tsz.y + 2.f});
		}

		DrawOverflowText(ren, world, e, text, textColor, s, align, useFont);

		if (needsDecoration) {
			ren.SetDrawColor(textColor);
			if (ul)
				ren.FillRect({tx, ty + tsz.y - 2.f, tsz.x, 1.5f});
			if (st)
				ren.FillRect({tx, ty + tsz.y * 0.5f, tsz.x, 1.5f});
		}
	}

	if (auto t = world.GetComponent<UiToggle>(e); t.IsSome()) {
		const UiToggle &tg = *t.Unwrap();
		ResolvedStyle rs = GetResolved(world, e);
		float r = s.h * 0.5f;
		sdl3::FColor track = tg.checked ? rs.BgChecked(sdl3::FColor::UI_ACCENT_BLUE())
								  : (tg.hovered ? rs.BgHovered(sdl3::FColor::UI_BORDER_SLATE()) : rs.Bg(sdl3::FColor::UI_PANEL_DARK()));
		ren.SetDrawColor(track);
		ren.FillRoundedRect(s, math::Corners(r));
		float knobR = r - 3.f;
		float kx = sdl3::Lerp(s.x + r, s.x + s.w - r, tg.animT);
		ren.SetDrawColor(rs.TextColor(sdl3::FColor::UI_TEXT_PRIMARY()));
		ren.FillCircle({kx, s.y + r}, knobR);
	}

	if (auto cb = world.GetComponent<UiCheckbox>(e); cb.IsSome()) {
		const UiCheckbox &ck = *cb.Unwrap();
		ResolvedStyle rs = GetResolved(world, e);
		sdl3::FColor bg = ck.checked ? rs.BgChecked(sdl3::FColor::UI_ACCENT_BLUE())
							   : (ck.hovered ? rs.BgHovered(sdl3::FColor::UI_BORDER_SLATE()) : rs.Bg(sdl3::FColor::UI_PANEL_DARK()));
		ren.SetDrawColor(bg);
		ren.FillRoundedRect(s, math::Corners(4.f));
		if (ck.checked) {
			ren.SetDrawColor(rs.TextColor(sdl3::FColor::UI_TEXT_PRIMARY()));
			float x = s.x, y = s.y, w = s.w, h = s.h;
			ren.DrawLine(x + w * 0.22f, y + h * 0.52f, x + w * 0.42f, y + h * 0.72f);
			ren.DrawLine(x + w * 0.42f, y + h * 0.72f, x + w * 0.78f, y + h * 0.30f);
		}
	}

	if (auto sl = world.GetComponent<UiSlider>(e); sl.IsSome()) {
		const UiSlider &s2 = *sl.Unwrap();
		ResolvedStyle rs = GetResolved(world, e);
		sdl3::FColor cNormal = rs.Bg(sdl3::FColor::UI_PANEL_DARK());
		sdl3::FColor cFill = rs.BgChecked(sdl3::FColor::UI_ACCENT_BLUE());
		sdl3::FColor cThumb = s2.dragging ? rs.BgPressed(sdl3::FColor::UI_ACCENT_BLUE()) : rs.BgHovered(sdl3::FColor::UI_ACCENT_BLUE_LIGHT());
		float t = s2.Normalized();
		if (s2.orient == Orientation::Horizontal) {
			sdl3::FRect track{s.x, s.y + s.h * 0.5f - 3.f, s.w, 6.f};
			ren.SetDrawColor(cNormal);
			ren.FillRoundedRect(track, math::Corners(3.f));
			sdl3::FRect fill{track.x, track.y, track.w * t, track.h};
			ren.SetDrawColor(cFill);
			ren.FillRoundedRect(fill, math::Corners(3.f));
			ren.SetDrawColor(cThumb);
			ren.FillCircle({s.x + s.w * t, s.y + s.h * 0.5f}, s.h * 0.5f);
		} else {
			sdl3::FRect track{s.x + s.w * 0.5f - 3.f, s.y, 6.f, s.h};
			ren.SetDrawColor(cNormal);
			ren.FillRoundedRect(track, math::Corners(3.f));
			sdl3::FRect fill{track.x, track.y + track.h * (1.f - t), track.w, track.h * t};
			ren.SetDrawColor(cFill);
			ren.FillRoundedRect(fill, math::Corners(3.f));
			ren.SetDrawColor(cThumb);
			ren.FillCircle({s.x + s.w * 0.5f, s.y + s.h * (1.f - t)}, s.w * 0.5f);
		}
	}

	if (auto pr = world.GetComponent<UiProgress>(e); pr.IsSome()) {
		const UiProgress &p2 = *pr.Unwrap();
		ResolvedStyle rs = GetResolved(world, e);
		math::Corners radius = rs.BordersRadius(math::Corners(4.f));
		ren.SetDrawColor(rs.Bg(sdl3::FColor::UI_PANEL_DARK()));
		ren.FillRoundedRect(s, radius);
		sdl3::FRect fill{s.x, s.y, s.w * p2.Normalized(), s.h};
		ren.SetDrawColor(rs.BgChecked(sdl3::FColor::UI_ACCENT_BLUE()));
		ren.FillRoundedRect(fill, radius);
	}

	if (world.HasComponent<UiSeparator>(e)) {
		ren.SetDrawColor(GetResolved(world, e).Bg(sdl3::FColor::UI_BORDER_MUTED()));
		ren.FillRect(s);
	}

	if (auto in = world.GetComponent<UiInput>(e); in.IsSome()) {
		const UiInput &f = *in.Unwrap();
		ResolvedStyle rs = GetResolved(world, e);
		float fs = rs.FontSize(14.f);
		sdl3::FColor textColor = rs.TextColor(sdl3::FColor::UI_TEXT_PRIMARY());
		sdl3::FColor selColor = rs.BgChecked(sdl3::FColor::WHITE());
		ren.SetDrawColor(rs.Bg(sdl3::FColor::UI_PANEL_DARKER()));
		ren.FillRoundedRect(s, math::Corners(4.f));
		ren.SetDrawColor(f.focused ? rs.BorderFocus(sdl3::FColor::UI_ACCENT_BLUE_HOVER()) : rs.BorderColor(sdl3::FColor::UI_BORDER_MUTED()));
		ren.DrawRoundedRect(s, math::Corners(4.f));
		bool empty = f.text.IsEmpty();
		if (empty) {
			DrawTextCentered(ren, e, f.placeholder, rs.BgHovered(sdl3::FColor::UI_TEXT_MUTED()), s, TextAlign::Left);
		} else {
			sdl3::FPoint scroll{};
			if (auto r = world.GetComponent<UiRect>(e); r.IsSome())
				scroll = r.Unwrap()->scroll;
			float innerW = sdl3::Max(1.f, s.w - 16.f);
			std::vector<TextLine> lines = WrapText(f.text, fs, innerW);
			float lh = LineHeightApprox(fs);
			// Contenu (sélection, texte, curseur) borné à l'INTÉRIEUR du
			// cadre : une ligne partiellement visible en haut ou en bas
			// d'un champ défilant se coupe net sur le bord au lieu de
			// chevaucher la bordure.
			const sdl3::FRect contentClip = c.childClip.Intersection(InsetRect(s, FIELD_BORDER));
			ren.SetClipRect(ToClipRect(contentClip));
			DrawSelection(ren, lines, fs, f.cursor, f.selectionAnchor, s, scroll, selColor);
			float y = s.y + 4.f - scroll.y;
			for (const TextLine &line : lines) {
				if (y + lh >= s.y && y <= s.y + s.h)
					DrawTextRaw(ren, line.text, textColor, s.x + 8.f, y, lh);
				y += lh;
			}
			if (f.focused && sdl3::Fmod(f.blink, 1.f) < 0.5f)
				DrawCaret(ren, lines, fs, f.cursor, s, scroll, selColor);
			ren.SetClipRect(ToClipRect(c.childClip));
		}
	}

	if (auto ina = world.GetComponent<UiInputArea>(e); ina.IsSome()) {
		UiInputArea &f = *ina.Unwrap();
		ResolvedStyle rs = GetResolved(world, e);
		float fs = rs.FontSize(14.f);
		sdl3::FColor textColor = rs.TextColor(sdl3::FColor::UI_TEXT_PRIMARY());
		sdl3::FColor selColor = rs.BgChecked(sdl3::FColor::WHITE());
		ren.SetDrawColor(rs.Bg(sdl3::FColor::UI_PANEL_DARKER()));
		ren.FillRoundedRect(s, math::Corners(4.f));
		ren.SetDrawColor(f.focused ? rs.BorderFocus(sdl3::FColor::UI_ACCENT_BLUE_HOVER()) : rs.BorderColor(sdl3::FColor::UI_BORDER_MUTED()));
		ren.DrawRoundedRect(s, math::Corners(4.f));

		// Police et cellule : en chasse fixe, la largeur de cellule est
		// MESURÉE sur la police dessinée et publiée dans le composant, pour
		// que la saisie (clic, glisser) et la mise en page placent le
		// curseur exactement sous le caractère dessiné.
		sdl3::Font *areaFont = (f.monospace && monoFont) ? DrawFont(monoFont) : nullptr;
		if (areaFont) {
			auto cell = m_cellWidths.find(areaFont);
			if (cell == m_cellWidths.end())
				if (auto size = areaFont->Measure(String("0123456789")); size.IsSome())
					cell = m_cellWidths.emplace(areaFont, float(size.Unwrap().x) / 10.f).first;
			if (cell != m_cellWidths.end())
				f.cellWidth = cell->second;
		}
		const float cw = AreaCellWidth(f, fs);

		bool empty = f.text.IsEmpty();
		if (empty && !f.lineNumbers) {
			DrawTextRaw(ren, f.placeholder, rs.BgHovered(sdl3::FColor::UI_TEXT_MUTED()), s.x + 8.f, s.y + 4.f,
					   LineHeightApprox(fs), areaFont);
		} else {
			sdl3::FPoint scroll{};
			if (auto r = world.GetComponent<UiRect>(e); r.IsSome())
				scroll = r.Unwrap()->scroll;
			std::vector<TextLine> lines = SplitLines(f.text);
			float lh = LineHeightApprox(fs);
			const float gutter = AreaGutterWidth(f, lines.size(), cw);
			const sdl3::FRect box = AreaTextBox(s, gutter);
			const size_t cursorLine = LineIndexOf(lines, f.cursor);

			// Gouttière : fond, puis numéros alignés à droite. Elle ne
			// défile que verticalement, comme dans tout éditeur.
			if (gutter > 0.f) {
				const sdl3::FRect gutterRect{s.x, s.y, gutter, s.h};
				ren.SetClipRect(ToClipRect(c.childClip.Intersection(InsetRect(gutterRect, FIELD_BORDER))));
				sdl3::FColor muted = rs.BgHovered(sdl3::FColor::UI_TEXT_MUTED());
				ren.SetDrawColor(sdl3::FColor{0.f, 0.f, 0.f, 0.18f});
				ren.FillRect(InsetRect(gutterRect, FIELD_BORDER));
				float y = s.y + 4.f - scroll.y;
				for (size_t li = 0; li < lines.size(); ++li, y += lh) {
					if (y + lh < s.y || y > s.y + s.h)
						continue;
					String number = String::From(int(li + 1));
					const float x = s.x + gutter - 8.f - float(number.size()) * cw;
					DrawTextRaw(ren, number, (f.focused && li == cursorLine) ? textColor : muted, x, y, lh, areaFont);
				}
			}

			// Cf. UiInput : le contenu reste dans le cadre (et ici hors de
			// la gouttière).
			const sdl3::FRect contentClip = c.childClip.Intersection(InsetRect(box, FIELD_BORDER));
			ren.SetClipRect(ToClipRect(contentClip));
			if (f.highlightCurrentLine && f.focused && !lines.empty()) {
				ren.SetDrawColor(sdl3::FColor{1.f, 1.f, 1.f, 0.05f});
				ren.FillRect({box.x, box.y + 4.f - scroll.y + float(cursorLine) * lh, box.w, lh});
			}
			DrawSelection(ren, lines, fs, f.cursor, f.selectionAnchor, box, scroll, selColor, cw);
			float y = box.y + 4.f - scroll.y;
			const float x0 = box.x + 8.f - scroll.x;
			std::vector<UiTextSpan> spans;
			for (const TextLine &line : lines) {
				if (y + lh >= box.y && y <= box.y + box.h) {
					if (!f.highlighter) {
						DrawTextRaw(ren, line.text, textColor, x0, y, lh, areaFont);
					} else {
						// La coloration travaille sur la ligne AFFICHÉE
						// (tabulations développées) : un morceau se place
						// alors à sa colonne, sans recalcul de tabulation
						// au milieu d'une ligne.
						const String shown = NormalizeDisplayText(line.text, tabStop);
						spans.clear();
						f.highlighter(shown, spans);
						size_t at = 0;
						auto emit = [&](size_t from, size_t to, sdl3::FColor color) {
							if (to <= from)
								return;
							const float x = x0 + float(DisplayColumn(shown, from, tabStop)) * cw;
							DrawTextRaw(ren, shown.Substr(from, to - from), color, x, y, lh, areaFont);
						};
						for (const UiTextSpan &span : spans) {
							const size_t begin = sdl3::Min(span.begin, shown.size());
							const size_t end = sdl3::Min(span.end, shown.size());
							if (begin < at || end <= begin)
								continue; // morceau mal formé : ignoré plutôt que dessiné deux fois
							emit(at, begin, textColor);
							emit(begin, end, span.color);
							at = end;
						}
						emit(at, shown.size(), textColor);
					}
				}
				y += lh;
			}
			if (f.focused && sdl3::Fmod(f.blink, 1.f) < 0.5f)
				DrawCaret(ren, lines, fs, f.cursor, box, scroll, selColor, cw);
			ren.SetClipRect(ToClipRect(c.childClip));
		}
	}

	if (auto dv = world.GetComponent<UiDragValue>(e); dv.IsSome()) {
		const UiDragValue &d = *dv.Unwrap();
		ResolvedStyle rs = GetResolved(world, e);
		float fs = rs.FontSize(14.f);
		ren.SetDrawColor(rs.Bg(sdl3::FColor{28/255.f, 30/255.f, 44/255.f, 1.f}));
		ren.FillRoundedRect(s, math::Corners(4.f));
		ren.SetDrawColor(d.editing ? rs.BorderFocus(sdl3::FColor{120/255.f, 170/255.f, 250/255.f, 1.f}) : rs.BorderColor(sdl3::FColor{70/255.f, 76/255.f, 110/255.f, 1.f}));
		ren.DrawRoundedRect(s, math::Corners(4.f));
		if (!d.editing) {
			// Bande de remplissage proportionnelle à la valeur (repère
			// visuel façon slider, sans thumb — glisser reste possible
			// partout dans le widget, pas juste sur la bande).
			float t = (d.max > d.min) ? sdl3::Clamp((d.value - d.min) / (d.max - d.min), 0.f, 1.f) : 0.f;
			sdl3::FColor fill = rs.BgChecked(sdl3::FColor{70/255.f, 130/255.f, 210/255.f, 1.f});
			ren.SetDrawColor(sdl3::FColor{fill.r, fill.g, fill.b, fill.a / 4.f});
			ren.FillRoundedRect({s.x, s.y, s.w * t, s.h}, math::Corners(4.f));
			DrawTextCentered(ren, e, d.Formatted(), rs.TextColor(sdl3::FColor{220/255.f, 222/255.f, 232/255.f, 1.f}), s, TextAlign::Center);
		} else {
			sdl3::FColor textColor = rs.TextColor(sdl3::FColor{220/255.f, 222/255.f, 232/255.f, 1.f});
			DrawTextRaw(ren, d.editText, textColor, s.x + 8.f, s.y + (s.h - LineHeightApprox(fs)) * 0.5f,
					   LineHeightApprox(fs));
			if (sdl3::Fmod(d.blink, 1.f) < 0.5f) {
				float caretX = s.x + 8.f + float(d.editText.size()) * CharWidthApprox(fs);
				ren.SetDrawColor(textColor);
				ren.FillRect({caretX, s.y + 3.f, 1.5f, s.h - 6.f});
			}
		}
	}

	if (auto sw = world.GetComponent<UiColorSwatch>(e); sw.IsSome()) {
		const UiColorSwatch &swatch = *sw.Unwrap();
		ResolvedStyle rs = GetResolved(world, e);
		math::Corners radius = rs.BordersRadius(math::Corners(4.f));
		if (swatch.color.a < 255)
			DrawCheckerboard(ren, s, radius);
		if (swatch.color.a > 0) {
			ren.SetDrawColor(swatch.color);
			ren.FillRoundedRect(s, radius);
		}
		ren.SetDrawColor(swatch.hovered ? rs.BorderFocus(sdl3::FColor{1.f, 1.f, 1.f, 160/255.f})
										: rs.BorderColor(sdl3::FColor{0.f, 0.f, 0.f, 120/255.f}));
		ren.DrawRoundedRect(s, radius);
	}

	if (auto svp = world.GetComponent<UiSVSquare>(e); svp.IsSome()) {
		const UiSVSquare &sv = *svp.Unwrap();
		sdl3::FColor hueColor = HsvToColor(sv.h, 1.f, 1.f);
		DrawQuadGradient(ren, s, sdl3::FColor::WHITE(), hueColor, sdl3::FColor::BLACK(), sdl3::FColor::BLACK());
		float cx = s.x + sv.s * s.w, cy = s.y + (1.f - sv.v) * s.h;
		ren.SetDrawColor(sdl3::FColor::WHITE());
		ren.DrawCircle({cx, cy}, 6.f);
		ren.SetDrawColor(sdl3::FColor::BLACK());
		ren.DrawCircle({cx, cy}, 5.f);
	}

	if (auto hsp = world.GetComponent<UiHueSlider>(e); hsp.IsSome()) {
		const UiHueSlider &h = *hsp.Unwrap();
		DrawHueBar(ren, s);
		float cx = s.x + (h.hue / 360.f) * s.w;
		ren.SetDrawColor(sdl3::FColor::WHITE());
		ren.FillRect({cx - 2.f, s.y - 2.f, 4.f, s.h + 4.f});
		ren.SetDrawColor(sdl3::FColor::BLACK());
		ren.DrawRect({cx - 2.f, s.y - 2.f, 4.f, s.h + 4.f});
	}

	if (auto asp = world.GetComponent<UiAlphaSlider>(e); asp.IsSome()) {
		const UiAlphaSlider &a = *asp.Unwrap();
		DrawCheckerboard(ren, s, math::Corners{});
		DrawHGradient(ren, s, sdl3::FColor{a.baseColor.r, a.baseColor.g, a.baseColor.b, 0.f},
					  sdl3::FColor{a.baseColor.r, a.baseColor.g, a.baseColor.b, 1.f});
		float cx = s.x + a.alpha * s.w;
		ren.SetDrawColor(sdl3::FColor::WHITE());
		ren.FillRect({cx - 2.f, s.y - 2.f, 4.f, s.h + 4.f});
		ren.SetDrawColor(sdl3::FColor::BLACK());
		ren.DrawRect({cx - 2.f, s.y - 2.f, 4.f, s.h + 4.f});
	}

	if (auto mb = world.GetComponent<UiMenuBarItem>(e); mb.IsSome()) {
		const UiMenuBarItem &m = *mb.Unwrap();
		ResolvedStyle rs = GetResolved(world, e);
		bool open = false;
		if (m.menuPopup.Valid())
			if (auto ps = world.GetComponent<UiPopupState>(m.menuPopup); ps.IsSome())
				open = ps.Unwrap()->open;
		// Au repos : le fond RÉSOLU du widget (classe « root-menubaritem »,
		// donc thémé), transparent si le thème n'en définit pas. L'ancien
		// `FColor::BLACK()` peignait un rectangle NOIR OPAQUE quel que
		// soit le thème — invisible sur fond sombre, mais une barre de
		// menus illisible en thème clair (texte foncé sur noir).
		sdl3::FColor bg = open ? rs.BgPressed(sdl3::FColor::UI_BORDER_MUTED2())
							   : (m.hovered ? rs.BgHovered(sdl3::FColor{55 / 255.f, 60 / 255.f, 84 / 255.f, 1.f})
											: rs.Bg(sdl3::FColor{0.f, 0.f, 0.f, 0.f}));
		if (bg.a > 0) {
			ren.SetDrawColor(bg);
			ren.FillRoundedRect(s, rs.BordersRadius(math::Corners(4.f)));
		}
		DrawTextCentered(ren, e, m.text, rs.TextColor(sdl3::FColor{220/255.f, 222/255.f, 232/255.f, 1.f}), s, TextAlign::Center);
	}

	if (auto mi = world.GetComponent<UiMenuItem>(e); mi.IsSome()) {
		const UiMenuItem &m = *mi.Unwrap();
		ResolvedStyle rs = GetResolved(world, e);
		bool submenuOpen = false;
		if (m.submenuPopup.Valid())
			if (auto ps = world.GetComponent<UiPopupState>(m.submenuPopup); ps.IsSome())
				submenuOpen = ps.Unwrap()->open;
		// Rendu d'une ligne de liste de combo (cf. DrawDropdown) : rien au
		// repos — c'est le fond du popup qui se voit, d'un seul tenant —,
		// la couleur de sélection du thème, pleine largeur et sans arrondi,
		// sur l'entrée survolée ou dont le sous-menu est ouvert. L'ancien
		// fond NOIR opaque par entrée donnait une pile de boutons séparés,
		// illisible en thème clair.
		if (m.hovered || submenuOpen) {
			ren.SetDrawColor(rs.BgChecked(sdl3::FColor::UI_ACCENT_BLUE_DEEP()));
			ren.FillRect(s);
		}
		sdl3::FColor textColor = rs.TextColor(sdl3::FColor::UI_TEXT_PRIMARY());
		float arrowW = m.hasSubmenu ? 16.f : 0.f;
		sdl3::FRect textBox{s.x, s.y, sdl3::Max(0.f, s.w - arrowW), s.h};
		DrawTextCentered(ren, e, m.text, textColor, textBox, TextAlign::Left);
		if (!m.shortcut.IsEmpty()) {
			float fs = rs.FontSize(14.f);
			sdl3::FColor muted = textColor;
			muted.a *= 0.55f;
			sdl3::FPoint sz = MeasureCached(m.shortcut, fs);
			DrawTextRaw(ren, m.shortcut, muted, s.x + s.w - arrowW - sz.x - 8.f, s.y, s.h);
		}
		if (m.hasSubmenu) {
			float cx = s.x + s.w - 10.f, cy = s.y + s.h * 0.5f;
			std::array<sdl3::Vertex, 3> tri{{
				{{cx - 3.f, cy - 5.f}, textColor, {}},
				{{cx - 3.f, cy + 5.f}, textColor, {}},
				{{cx + 4.f, cy}, textColor, {}},
			}};
			ren.RenderGeometry(tri);
		}
	}

	if (auto tbo = world.GetComponent<UiTable>(e); tbo.IsSome()) {
		const UiTable &t = *tbo.Unwrap();
		ResolvedStyle rs = GetResolved(world, e);
		sdl3::FColor textColor = rs.TextColor(sdl3::FColor::UI_TEXT_PRIMARY());
		float fs = rs.FontSize(14.f);
		ren.SetDrawColor(rs.Bg(sdl3::FColor::UI_BG_DEEP()));
		ren.FillRoundedRect(s, math::Corners(4.f));
		ren.SetDrawColor(rs.BorderColor(sdl3::FColor::UI_BORDER_MUTED()));
		ren.DrawRoundedRect(s, math::Corners(4.f));

		// ── En-tête ────────────────────────────────────────────────────
		sdl3::FRect headerRect{s.x, s.y, s.w, t.headerHeight};
		ren.SetDrawColor(rs.BgHovered(sdl3::FColor{36/255.f, 40/255.f, 58/255.f, 255/255.f}));
		ren.FillRect(headerRect);
		for (int i = 0; i < int(t.columns.size()); ++i) {
			const UiTableColumn &col = t.columns[size_t(i)];
			float x = s.x + t.ColumnX(i);
			sdl3::FRect cell{x, s.y, col.width, t.headerHeight};
			if (i == t.hoveredColumn) {
				ren.SetDrawColor(rs.BgHovered(sdl3::FColor::UI_BORDER_SLATE()));
				ren.FillRect(cell);
			}
			sdl3::FPoint tsz = MeasureCached(col.title, fs);
			DrawTextRaw(ren, col.title, textColor, x + 8.f, s.y, t.headerHeight);
			if (i == t.sortColumn && t.sortOrder != SortOrder::NONE) {
				float ax = x + 8.f + tsz.x + 6.f, ay = s.y + t.headerHeight * 0.5f;
				bool asc = t.sortOrder == SortOrder::ASCENDING;
				std::array<sdl3::Vertex, 3> tri =
					asc ? std::array<sdl3::Vertex, 3>{{{{ax - 4.f, ay + 2.f}, textColor, {}},
													  {{ax + 4.f, ay + 2.f}, textColor, {}},
													  {{ax, ay - 3.f}, textColor, {}}}}
						: std::array<sdl3::Vertex, 3>{{{{ax - 4.f, ay - 2.f}, textColor, {}},
													  {{ax + 4.f, ay - 2.f}, textColor, {}},
													  {{ax, ay + 3.f}, textColor, {}}}};
				ren.RenderGeometry(tri);
			}
			if (i > 0) {
				ren.SetDrawColor(rs.BorderColor(sdl3::FColor::UI_BORDER_MUTED()));
				ren.DrawLine(x, s.y + 4.f, x, s.y + t.headerHeight - 4.f);
			}
		}

		// ── Corps (lignes visibles seulement — clip resserré) ────────────
		sdl3::FRect bodyRect{s.x, s.y + t.headerHeight, s.w, sdl3::Max(0.f, s.h - t.headerHeight)};
		sdl3::FRect inner = c.childClip.Intersection(bodyRect);
		if (inner.w > 0.f && inner.h > 0.f && t.rowHeight > 0.f && !t.rows.empty()) {
			ren.SetClipRect(ToClipRect(inner));
			int first = sdl3::Max(0, int(t.scroll / t.rowHeight));
			int last = sdl3::Min(int(t.rows.size()) - 1, int((t.scroll + bodyRect.h) / t.rowHeight));
			bool selMulti = false;
			const std::unordered_set<int> *selectedSet = nullptr;
			if (auto us = world.GetComponent<UiSelection>(e); us.IsSome()) {
				selectedSet = &us.Unwrap()->state.selected;
				selMulti = us.Unwrap()->multiSelect;
			}
			(void)selMulti;
			for (int row = first; row <= last; ++row) {
				float ry = bodyRect.y + float(row) * t.rowHeight - t.scroll;
				sdl3::FRect rowRect{s.x, ry, s.w, t.rowHeight};
				bool isSelected = selectedSet && selectedSet->contains(row);
				if (isSelected) {
					ren.SetDrawColor(rs.BgChecked(sdl3::FColor::UI_ACCENT_BLUE_GLOW()));
					ren.FillRect(rowRect);
				} else if (row == t.hoveredRow) {
					ren.SetDrawColor(rs.BgHovered(sdl3::FColor{255/255.f, 255/255.f, 255/255.f, 18/255.f}));
					ren.FillRect(rowRect);
				}
				const auto &rowData = t.rows[size_t(row)];
				for (int col = 0; col < int(t.columns.size()) && col < int(rowData.size()); ++col) {
					float cx = s.x + t.ColumnX(col) + 8.f;
					DrawTextRaw(ren, rowData[size_t(col)], textColor, cx, ry, t.rowHeight);
				}
			}
			float maxSc = t.MaxScroll(bodyRect.h);
			if (maxSc > 0.f) {
				float ratio = sdl3::Clamp(bodyRect.h / t.ContentHeight(), 0.05f, 1.f);
				float thumbH = bodyRect.h * ratio;
				float thumbY = bodyRect.y + (bodyRect.h - thumbH) * (t.scroll / maxSc);
				ren.SetDrawColor(rs.BgPressed(sdl3::FColor::UI_ACCENT_BLUE_LIGHT()));
				ren.FillRoundedRect({s.x + s.w - 6.f, thumbY, 4.f, thumbH}, math::Corners(2.f));
			}
			ren.SetClipRect(ToClipRect(c.childClip));
		}
	}

	if (auto po = world.GetComponent<UiPlot>(e); po.IsSome()) {
		const UiPlot &p = *po.Unwrap();
		ResolvedStyle rs = GetResolved(world, e);
		ren.SetDrawColor(rs.Bg(sdl3::FColor::UI_BG_DEEP()));
		ren.FillRoundedRect(s, math::Corners(4.f));
		ren.SetDrawColor(rs.BorderColor(sdl3::FColor::UI_BORDER_MUTED()));
		ren.DrawRoundedRect(s, math::Corners(4.f));

		// Intersection, et non `s` seul : poser le rect du widget comme
		// clip ÉCRASERAIT celui hérité, et un tracé placé dans un panneau
		// défilant peindrait par-dessus ce panneau.
		ren.SetClipRect(ToClipRect(c.childClip.Intersection(s)));
		sdl3::FRect plotRect = ComputePlotRect(p, s);
		sdl3::FColor tickTextColor = rs.TextColor(sdl3::FColor{190/255.f, 194/255.f, 210/255.f, 255/255.f});
		float tickFs = rs.FontSize(11.f);

		if (p.mode == PlotMode::PIE) {
			// ── PlotMode::PIE (Phase 5) : pas d'axes/grille/graduations —
			// juste le camembert et sa légende (parts = entrées).
			sdl3::FRect circleRect = ComputePieCircleRect(plotRect);
			sdl3::FPoint center{circleRect.x + circleRect.w * 0.5f, circleRect.y + circleRect.h * 0.5f};
			float radius = circleRect.w * 0.5f;
			auto angles = ComputePieAngles(p.pieSlices);
			// La légende prime sur le survol direct pour la surbrillance
			// (même hiérarchie que XY : legendHover dicte le dimFactor).
			int hoveredSlice = p.legendHover >= 0 ? p.legendHover : p.hoveredSeries;
			DrawPieChart(ren, p.pieSlices, angles, center, radius, hoveredSlice,
						rs.BorderColor(sdl3::FColor{20/255.f, 21/255.f, 28/255.f, 220/255.f}));

			// ── Étiquette de la part survolée (légende ou tracé) ─────────
			if (hoveredSlice >= 0 && size_t(hoveredSlice) < p.pieSlices.size()) {
				const PieSlice &hs = p.pieSlices[size_t(hoveredSlice)];
				float total = 0.f;
				for (const PieSlice &sl : p.pieSlices)
					if (sl.visible)
						total += sdl3::Max(0.f, sl.value);
				float pct = total > 0.f ? sdl3::Max(0.f, hs.value) / total * 100.f : 0.f;
				String full = hs.label.IsEmpty() ? String::From(hs.value, 1) : hs.label;
				full.Append(" (");
				full.Append(String::From(pct, 1));
				full.Append("%)");
				float fs = rs.FontSize(12.f);
				sdl3::FPoint tsz = MeasureCached(full, fs);
				float tx = sdl3::Clamp(s.x + s.w * 0.5f - tsz.x * 0.5f, s.x, sdl3::Max(s.x, s.x + s.w - tsz.x));
				DrawTextRaw(ren, full, rs.TextColor(sdl3::FColor::UI_TEXT_PRIMARY()), tx, s.y + 2.f, tsz.y);
			}

			// ── Légende (fond+pastilles via plot.hpp, texte ici) ─────────
			if (p.showLegend && !p.pieSlices.empty()) {
				sdl3::FRect legendBox = LegendBoxRect(p, s, plotRect);
				DrawPlotLegendBackground(ren, p, legendBox);
				float legendFs = rs.FontSize(11.f);
				for (size_t i = 0; i < p.pieSlices.size(); ++i) {
					if (p.pieSlices[i].label.IsEmpty())
						continue;
					sdl3::FRect labelRect = LegendLabelRect(LegendRowRect(legendBox, i));
					sdl3::FColor lc = tickTextColor;
					if (!p.pieSlices[i].visible)
						lc.a = lc.a * 0.5f;
					sdl3::FPoint tsz = MeasureCached(p.pieSlices[i].label, legendFs);
					DrawTextRaw(ren, p.pieSlices[i].label, lc, labelRect.x,
								labelRect.y + (labelRect.h - tsz.y) * 0.5f, legendFs);
				}
			}

			ren.ClearClipRect();
			if (!p.title.IsEmpty())
				DrawTextCentered(ren, e, p.title, rs.TextColor(sdl3::FColor::UI_TEXT_PRIMARY()), sdl3::FRect{s.x, s.y + 2.f, s.w, 16.f},
								 TextAlign::Center);
			return;
		}

		if (p.mode == PlotMode::HEATMAP) {
			// ── PlotMode::HEATMAP (Phase 6) : pas d'axes/légende — juste la
			// grille colorée et la valeur de la cellule survolée.
			DrawHeatmap(ren, plotRect, p.heatmap);
			if (p.hoveredSeries >= 0 && size_t(p.hoveredSeries) < p.heatmap.values.size()) {
				String valText = String::From(p.heatmap.values[size_t(p.hoveredSeries)], 2);
				float fs = rs.FontSize(12.f);
				sdl3::FPoint tsz = MeasureCached(valText, fs);
				float tx = sdl3::Clamp(s.x + s.w * 0.5f - tsz.x * 0.5f, s.x, sdl3::Max(s.x, s.x + s.w - tsz.x));
				DrawTextRaw(ren, valText, rs.TextColor(sdl3::FColor::UI_TEXT_PRIMARY()), tx, s.y + 2.f, tsz.y);
			}
			ren.ClearClipRect();
			if (!p.title.IsEmpty())
				DrawTextCentered(ren, e, p.title, rs.TextColor(sdl3::FColor::UI_TEXT_PRIMARY()), sdl3::FRect{s.x, s.y + 2.f, s.w, 16.f},
								 TextAlign::Center);
			return;
		}

		if (p.mode == PlotMode::CANDLE) {
			// ── PlotMode::CANDLE (Phase 6) : graduations/grille dans le
			// même style que XY, mais DUPLIQUÉES plutôt que partagées — les
			// deux blocs sont structurellement proches mais pas identiques
			// (pas de légende, pas d'axe Y secondaire, axes TOUJOURS résolus
			// depuis candleBars via resolveCandleAxis() plutôt que
			// resolveAxis()) ; les entremêler risquerait plus de bugs que la
			// duplication n'en évite (même raisonnement déjà appliqué à
			// detail::appendThickSegment entre plot.hpp et nodegraph.hpp).
			PlotAxis xr = ResolveCandleAxis(p.xAxis, p.candleBars, true);
			PlotAxis yr = ResolveCandleAxis(p.yAxis, p.candleBars, false);
			DrawPlotGrid(ren, plotRect, xr, yr, sdl3::FColor::UI_PANEL_DARK2());
			DrawCandleChart(ren, plotRect, p.candleBars, xr, yr, p.candleWidth, p.candleBullColor,
							p.candleBearColor, p.hoveredSeries);

			{
				bool top = p.xAxis.tickPosition == PlotEdge::Top;
				float ly = p.xAxis.tickOverlay ? (top ? plotRect.y + 2.f : plotRect.y + plotRect.h - tickFs - 2.f)
											   : (top ? plotRect.y - K_PLOT_TICK_MARGIN + 2.f
													  : plotRect.y + plotRect.h + 4.f);
				auto ticks = GenerateTicks(xr.min, xr.max);
				int dec = ticks.size() >= 2 ? TickDecimals(ticks[1] - ticks[0]) : 1;
				if (p.xAxis.tickOverlay && !ticks.empty()) {
					ren.SetDrawColor(p.xAxis.tickBoxColor);
					ren.FillRect({plotRect.x, ly - 1.f, plotRect.w, tickFs + 2.f});
				}
				for (float t : ticks) {
					float sx = DataToScreen(xr, plotRect.x, plotRect.x + plotRect.w, t);
					String label = String::From(t, dec);
					sdl3::FPoint tsz = MeasureCached(label, tickFs);
					DrawTextRaw(ren, label, tickTextColor, sx - tsz.x * 0.5f, ly, tickFs);
				}
			}
			{
				bool right = p.yAxis.tickPosition == PlotEdge::Right;
				auto ticks = GenerateTicks(yr.min, yr.max);
				int dec = ticks.size() >= 2 ? TickDecimals(ticks[1] - ticks[0]) : 1;
				float boxW = K_PLOT_TICK_MARGIN - 2.f;
				if (p.yAxis.tickOverlay && !ticks.empty()) {
					float bx = right ? plotRect.x + plotRect.w - boxW : plotRect.x;
					ren.SetDrawColor(p.yAxis.tickBoxColor);
					ren.FillRect({bx, plotRect.y, boxW, plotRect.h});
				}
				for (float t : ticks) {
					float sy = DataToScreen(yr, plotRect.y + plotRect.h, plotRect.y, t);
					String label = String::From(t, dec);
					sdl3::FPoint tsz = MeasureCached(label, tickFs);
					float lx = p.yAxis.tickOverlay
								  ? (right ? plotRect.x + plotRect.w - tsz.x - 3.f : plotRect.x + 3.f)
								  : (right ? plotRect.x + plotRect.w + 3.f : plotRect.x - tsz.x - 3.f);
					DrawTextRaw(ren, label, tickTextColor, lx, sy - tsz.y * 0.5f, tsz.y);
				}
			}

			// ── Étiquette de la bougie survolée (OHLC) ────────────────────
			if (p.hoveredSeries >= 0 && size_t(p.hoveredSeries) < p.candleBars.size()) {
				const OhlcBar &hb = p.candleBars[size_t(p.hoveredSeries)];
				String full;
				full.Append("O:");
				full.Append(String::From(hb.open, 2));
				full.Append(" H:");
				full.Append(String::From(hb.high, 2));
				full.Append(" L:");
				full.Append(String::From(hb.low, 2));
				full.Append(" C:");
				full.Append(String::From(hb.close, 2));
				float fs = rs.FontSize(12.f);
				sdl3::FPoint tsz = MeasureCached(full, fs);
				float tx = sdl3::Clamp(s.x + s.w * 0.5f - tsz.x * 0.5f, s.x, sdl3::Max(s.x, s.x + s.w - tsz.x));
				DrawTextRaw(ren, full, rs.TextColor(sdl3::FColor::UI_TEXT_PRIMARY()), tx, s.y + 2.f, tsz.y);
			}

			ren.ClearClipRect();
			if (!p.title.IsEmpty())
				DrawTextCentered(ren, e, p.title, rs.TextColor(sdl3::FColor::UI_TEXT_PRIMARY()), sdl3::FRect{s.x, s.y + 2.f, s.w, 16.f},
								 TextAlign::Center);
			return;
		}

		PlotAxis xr = ResolveAxis(p.xAxis, p.series, true);
		PlotAxis yr = ResolveAxis(p.yAxis, p.series, false);
		bool hasY2 = PlotHasSecondaryY(p);
		PlotAxis yr2 = hasY2 ? ResolveAxis(p.yAxis2, p.series, false, true) : PlotAxis{};

		// Pas de grille pour yAxis2 (Phase 4) — convention courante (ImPlot
		// compris) : une seconde grille superposée à la première brouille
		// plus qu'elle n'aide, l'axe secondaire garde ses graduations mais
		// pas ses propres lignes.
		DrawPlotGrid(ren, plotRect, xr, yr, sdl3::FColor::UI_PANEL_DARK2());

		// Séries : estompées si une AUTRE entrée de légende est survolée
		// (cf. UiPlot::legendHover, Phase 2) ; chacune choisit yAxis ou
		// yAxis2 selon PlotSeries::useSecondaryY (Phase 4).
		for (size_t i = 0; i < p.series.size(); ++i) {
			float dim = (p.legendHover < 0 || int(i) == p.legendHover) ? 1.f : 0.25f;
			const PlotAxis &yForSeries = p.series[i].useSecondaryY ? yr2 : yr;
			DrawPlotSeries(ren, plotRect, p.series[i], xr, yForSeries, dim);
		}

		// ── Graduations X (bas/haut, réservées ou superposées) ──────────
		// Les labels restent dessinés même si `showGrid` masque les
		// lignes de grille elles-mêmes — deux réglages indépendants.
		{
			bool top = p.xAxis.tickPosition == PlotEdge::Top;
			float ly = p.xAxis.tickOverlay ? (top ? plotRect.y + 2.f : plotRect.y + plotRect.h - tickFs - 2.f)
										   : (top ? plotRect.y - K_PLOT_TICK_MARGIN + 2.f : plotRect.y + plotRect.h + 4.f);
			auto ticks = xr.logScale ? GenerateLogTicks(xr.min, xr.max) : GenerateTicks(xr.min, xr.max);
			int dec = ticks.size() >= 2 ? TickDecimals(ticks[1] - ticks[0]) : 1;
			if (p.xAxis.tickOverlay && !ticks.empty()) {
				ren.SetDrawColor(p.xAxis.tickBoxColor);
				ren.FillRect({plotRect.x, ly - 1.f, plotRect.w, tickFs + 2.f});
			}
			for (float t : ticks) {
				float sx = DataToScreen(xr, plotRect.x, plotRect.x + plotRect.w, t);
				String label = String::From(t, dec);
				sdl3::FPoint tsz = MeasureCached(label, tickFs);
				DrawTextRaw(ren, label, tickTextColor, sx - tsz.x * 0.5f, ly, tickFs);
			}
		}
		// ── Graduations Y (gauche/droite, réservées ou superposées) ─────
		{
			bool right = p.yAxis.tickPosition == PlotEdge::Right;
			auto ticks = yr.logScale ? GenerateLogTicks(yr.min, yr.max) : GenerateTicks(yr.min, yr.max);
			int dec = ticks.size() >= 2 ? TickDecimals(ticks[1] - ticks[0]) : 1;
			float boxW = K_PLOT_TICK_MARGIN - 2.f;
			if (p.yAxis.tickOverlay && !ticks.empty()) {
				float bx = right ? plotRect.x + plotRect.w - boxW : plotRect.x;
				ren.SetDrawColor(p.yAxis.tickBoxColor);
				ren.FillRect({bx, plotRect.y, boxW, plotRect.h});
			}
			for (float t : ticks) {
				float sy = DataToScreen(yr, plotRect.y + plotRect.h, plotRect.y, t);
				String label = String::From(t, dec);
				sdl3::FPoint tsz = MeasureCached(label, tickFs);
				float lx = p.yAxis.tickOverlay
							  ? (right ? plotRect.x + plotRect.w - tsz.x - 3.f : plotRect.x + 3.f)
							  : (right ? plotRect.x + plotRect.w + 3.f : plotRect.x - tsz.x - 3.f);
				DrawTextRaw(ren, label, tickTextColor, lx, sy - tsz.y * 0.5f, tsz.y);
			}
		}
		// ── Graduations Y secondaires (Phase 4) — même logique que
		// l'axe Y primaire ci-dessus, actif seulement si au moins une
		// série a `useSecondaryY=true` ; pas de grille dédiée (cf. plus
		// haut), seulement les graduations.
		if (hasY2) {
			bool right = p.yAxis2.tickPosition == PlotEdge::Right;
			auto ticks = yr2.logScale ? GenerateLogTicks(yr2.min, yr2.max) : GenerateTicks(yr2.min, yr2.max);
			int dec = ticks.size() >= 2 ? TickDecimals(ticks[1] - ticks[0]) : 1;
			float boxW = K_PLOT_TICK_MARGIN - 2.f;
			if (p.yAxis2.tickOverlay && !ticks.empty()) {
				float bx = right ? plotRect.x + plotRect.w - boxW : plotRect.x;
				ren.SetDrawColor(p.yAxis2.tickBoxColor);
				ren.FillRect({bx, plotRect.y, boxW, plotRect.h});
			}
			for (float t : ticks) {
				float sy = DataToScreen(yr2, plotRect.y + plotRect.h, plotRect.y, t);
				String label = String::From(t, dec);
				sdl3::FPoint tsz = MeasureCached(label, tickFs);
				float lx = p.yAxis2.tickOverlay
							  ? (right ? plotRect.x + plotRect.w - tsz.x - 3.f : plotRect.x + 3.f)
							  : (right ? plotRect.x + plotRect.w + 3.f : plotRect.x - tsz.x - 3.f);
				DrawTextRaw(ren, label, tickTextColor, lx, sy - tsz.y * 0.5f, tsz.y);
			}
		}

		// ── Curseur/infobulle (point survolé) ────────────────────────────
		if (p.hoveredSeries >= 0 && size_t(p.hoveredSeries) < p.series.size()) {
			const PlotSeries &hs = p.series[size_t(p.hoveredSeries)];
			if (size_t(p.hoveredIndex) < hs.x.size()) {
				float hx = DataToScreen(xr, plotRect.x, plotRect.x + plotRect.w, hs.x[size_t(p.hoveredIndex)]);
				ren.SetDrawColor(sdl3::FColor{220/255.f, 222/255.f, 232/255.f, 100/255.f});
				ren.DrawLine(hx, plotRect.y, hx, plotRect.y + plotRect.h);
				String valText = String::From(hs.y[size_t(p.hoveredIndex)], 2);
				float fs = rs.FontSize(12.f);
				sdl3::FPoint tsz = MeasureCached(valText, fs);
				float tx = sdl3::Clamp(hx - tsz.x * 0.5f, s.x, sdl3::Max(s.x, s.x + s.w - tsz.x));
				DrawTextRaw(ren, valText, rs.TextColor(sdl3::FColor::UI_TEXT_PRIMARY()), tx, s.y + 2.f, tsz.y);
			}
		}

		// ── Légende (fond+pastilles via plot.hpp, texte ici) ─────────────
		if (p.showLegend && !p.series.empty()) {
			sdl3::FRect legendBox = LegendBoxRect(p, s, plotRect);
			DrawPlotLegendBackground(ren, p, legendBox);
			float legendFs = rs.FontSize(11.f);
			for (size_t i = 0; i < p.series.size(); ++i) {
				if (p.series[i].label.IsEmpty())
					continue;
				sdl3::FRect labelRect = LegendLabelRect(LegendRowRect(legendBox, i));
				sdl3::FColor lc = tickTextColor;
				if (!p.series[i].visible)
					lc.a = lc.a * 0.5f;
				sdl3::FPoint tsz = MeasureCached(p.series[i].label, legendFs);
				DrawTextRaw(ren, p.series[i].label, lc, labelRect.x, labelRect.y + (labelRect.h - tsz.y) * 0.5f,
						   legendFs);
			}
		}

		// ── Rectangle de zoom-rectangle en cours (Phase 3, clic-droit
		// glissé) ────────────────────────────────────────────────────
		DrawPlotBoxZoomRect(ren, p);

		ren.ClearClipRect();

		if (!p.title.IsEmpty())
			DrawTextCentered(ren, e, p.title, rs.TextColor(sdl3::FColor::UI_TEXT_PRIMARY()), sdl3::FRect{s.x, s.y + 2.f, s.w, 16.f},
							 TextAlign::Center);
	}

	if (auto rh = world.GetComponent<UiResizeHandle>(e); rh.IsSome()) {
		// Rendu minimal (barre pleine, teinte au survol/glisser) — cf.
		// interaction.hpp : UiResizeHandle ne dessine RIEN par lui-même
		// en tant que primitive transverse (Phase 2), mais un Splitter
		// (Phase 8) a besoin d'une poignée VISIBLE, contrairement aux
		// colonnes de Table (Phase 6) qui redessinent leur propre bordure.
		const UiResizeHandle &h = *rh.Unwrap();
		ResolvedStyle rs = GetResolved(world, e);
		sdl3::FColor bg = (h.dragging || h.hovered) ? rs.BgHovered(sdl3::FColor::UI_ACCENT_BLUE_BRIGHT()) : rs.Bg(sdl3::FColor::UI_BORDER_MUTED());
		ren.SetDrawColor(bg);
		ren.FillRoundedRect(s, rs.BordersRadius(math::Corners(2.f)));
	}

	if (auto calo = world.GetComponent<UiCalendar>(e); calo.IsSome()) {
		const UiCalendar &cal = *calo.Unwrap();
		ResolvedStyle rs = GetResolved(world, e);
		sdl3::FColor textColor = rs.TextColor(sdl3::FColor::UI_TEXT_PRIMARY());
		ren.SetDrawColor(rs.Bg(sdl3::FColor::UI_BG_DEEP()));
		ren.FillRoundedRect(s, math::Corners(4.f));
		ren.SetDrawColor(rs.BorderColor(sdl3::FColor::UI_BORDER_MUTED()));
		ren.DrawRoundedRect(s, math::Corners(4.f));

		const float HEADER_H = 28.f;
		sdl3::FRect header{s.x, s.y, s.w, HEADER_H};
		if (cal.hoveredPrevArrow || cal.hoveredNextArrow) {
			ren.SetDrawColor(rs.BgHovered(sdl3::FColor::UI_BORDER_SLATE()));
			ren.FillRect(cal.hoveredPrevArrow ? sdl3::FRect{s.x, s.y, 28.f, HEADER_H}
											  : sdl3::FRect{s.x + s.w - 28.f, s.y, 28.f, HEADER_H});
		}
		float pcx = s.x + 14.f, pcy = s.y + HEADER_H * 0.5f;
		std::array<sdl3::Vertex, 3> prevTri{{
			{{pcx + 4.f, pcy - 5.f}, textColor, {}},
			{{pcx + 4.f, pcy + 5.f}, textColor, {}},
			{{pcx - 4.f, pcy}, textColor, {}},
		}};
		ren.RenderGeometry(prevTri);
		float ncx = s.x + s.w - 14.f, ncy = pcy;
		std::array<sdl3::Vertex, 3> nextTri{{
			{{ncx - 4.f, ncy - 5.f}, textColor, {}},
			{{ncx - 4.f, ncy + 5.f}, textColor, {}},
			{{ncx + 4.f, ncy}, textColor, {}},
		}};
		ren.RenderGeometry(nextTri);
		String title = String::Format("%s %d", MonthNameFr(cal.month), cal.year);
		DrawTextCentered(ren, e, title, textColor, header, TextAlign::Center);

		sdl3::FRect grid{s.x, s.y + HEADER_H, s.w, sdl3::Max(0.f, s.h - HEADER_H)};
		float cellW = grid.w / 7.f, cellH = grid.h / 6.f;
		int offset = FirstWeekdayOfMonth(cal.year, cal.month);
		int dim = DaysInMonth(cal.year, cal.month);
		float fs = rs.FontSize(13.f);
		for (int day = 1; day <= dim; ++day) {
			int idx = offset + day - 1;
			int row = idx / 7, col = idx % 7;
			sdl3::FRect cell{grid.x + float(col) * cellW, grid.y + float(row) * cellH, cellW, cellH};
			if (day == cal.selectedDay) {
				ren.SetDrawColor(rs.BgChecked(sdl3::FColor::UI_ACCENT_BLUE_DEEP()));
				ren.FillRoundedRect({cell.x + 2.f, cell.y + 2.f, cell.w - 4.f, cell.h - 4.f}, math::Corners(3.f));
			} else if (day == cal.hoveredDay) {
				ren.SetDrawColor(rs.BgHovered(sdl3::FColor::UI_BORDER_SLATE()));
				ren.FillRoundedRect({cell.x + 2.f, cell.y + 2.f, cell.w - 4.f, cell.h - 4.f}, math::Corners(3.f));
			}
			String dayText = String::From(day);
			sdl3::FPoint tsz = MeasureCached(dayText, fs);
			DrawTextRaw(ren, dayText, textColor, cell.x + (cell.w - tsz.x) * 0.5f, cell.y + (cell.h - tsz.y) * 0.5f,
					   tsz.y);
		}
	}

	if (auto selp = world.GetComponent<UiSelectable>(e); selp.IsSome()) {
		const UiSelectable &sl = *selp.Unwrap();
		ResolvedStyle rs = GetResolved(world, e);
		// Même correctif que pour UiMenuBarItem ci-dessus : au repos, fond
		// RÉSOLU (thémé) et non un noir opaque codé en dur.
		sdl3::FColor bg = sl.selected
							  ? rs.BgChecked(sdl3::FColor::UI_ACCENT_BLUE_GLOW())
							  : (sl.hovered ? rs.BgHovered(sdl3::FColor::UI_HIGHLIGHT_FAINT())
											: rs.Bg(sdl3::FColor{0.f, 0.f, 0.f, 0.f}));
		if (bg.a > 0) {
			ren.SetDrawColor(bg);
			ren.FillRoundedRect(s, rs.BordersRadius(math::Corners(4.f)));
		}
	}

	if (auto tnp = world.GetComponent<UiTreeNode>(e); tnp.IsSome()) {
		const UiTreeNode &tn = *tnp.Unwrap();
		ResolvedStyle rs = GetResolved(world, e);
		sdl3::FRect header{s.x, s.y, s.w, tn.headerHeight};
		sdl3::FColor bg = tn.selected
							  ? rs.BgChecked(sdl3::FColor::UI_ACCENT_BLUE_GLOW())
							  : (tn.hovered ? rs.BgHovered(sdl3::FColor::UI_HIGHLIGHT_FAINT())
											: rs.Bg(sdl3::FColor{0.f, 0.f, 0.f, 0.f}));
		if (bg.a > 0) {
			ren.SetDrawColor(bg);
			ren.FillRoundedRect(header, rs.BordersRadius(math::Corners(4.f)));
		}
		bool hasKids = false;
		if (auto ch = world.GetComponent<UiChildren>(e); ch.IsSome())
			hasKids = !ch.Unwrap()->list.empty();
		if (hasKids) {
			sdl3::FColor arrowColor = rs.TextColor(sdl3::FColor::UI_TEXT_PRIMARY());
			float cx = s.x + 10.f, cy = s.y + tn.headerHeight * 0.5f;
			if (tn.expanded) {
				std::array<sdl3::Vertex, 3> tri{{
					{{cx - 5.f, cy - 4.f}, arrowColor, {}},
					{{cx + 5.f, cy - 4.f}, arrowColor, {}},
					{{cx, cy + 4.f}, arrowColor, {}},
				}};
				ren.RenderGeometry(tri);
			} else {
				std::array<sdl3::Vertex, 3> tri{{
					{{cx - 3.f, cy - 5.f}, arrowColor, {}},
					{{cx - 3.f, cy + 5.f}, arrowColor, {}},
					{{cx + 5.f, cy}, arrowColor, {}},
				}};
				ren.RenderGeometry(tri);
			}
		}
		sdl3::FRect titleBox{s.x + 20.f, s.y, sdl3::Max(0.f, s.w - 20.f), tn.headerHeight};
		DrawTextCentered(ren, e, tn.title, rs.TextColor(sdl3::FColor::UI_TEXT_PRIMARY()), titleBox, TextAlign::Left);
	}

	if (auto rd = world.GetComponent<UiRadio>(e); rd.IsSome()) {
		const UiRadio &r = *rd.Unwrap();
		ResolvedStyle rs = GetResolved(world, e);
		sdl3::FColor textColor = rs.TextColor(sdl3::FColor::UI_TEXT_PRIMARY());
		float d = sdl3::Min(18.f, s.h); // diamètre du cercle
		sdl3::FPoint center{s.x + d * 0.5f, s.y + s.h * 0.5f};
		ren.SetDrawColor(r.hovered ? rs.BgHovered(sdl3::FColor::UI_BORDER_SLATE()) : rs.Bg(sdl3::FColor::UI_PANEL_DARK()));
		ren.FillCircle(center, d * 0.5f);
		ren.SetDrawColor(textColor);
		ren.DrawCircle(center, d * 0.5f);
		if (r.checked) {
			ren.SetDrawColor(rs.BgChecked(sdl3::FColor::UI_ACCENT_BLUE()));
			ren.FillCircle(center, d * 0.28f);
		}
		if (!r.text.IsEmpty()) {
			sdl3::FRect labelBox{s.x + d + 6.f, s.y, sdl3::Max(0.f, s.w - d - 6.f), s.h};
			DrawTextCentered(ren, e, r.text, textColor, labelBox, TextAlign::Left);
		}
	}

	if (auto sb = world.GetComponent<UiScrollBar>(e); sb.IsSome()) {
		const UiScrollBar &b2 = *sb.Unwrap();
		ResolvedStyle rs = GetResolved(world, e);
		float radius = sdl3::Min(s.w, s.h) * 0.5f;
		ren.SetDrawColor(rs.Bg(sdl3::FColor::UI_PANEL_DARK()));
		ren.FillRoundedRect(s, math::Corners(radius));
		bool horiz = b2.orient == Orientation::Horizontal;
		float trackLen = horiz ? s.w : s.h;
		float thumbLen = trackLen * b2.ThumbRatio();
		float thumbPos = (trackLen - thumbLen) * b2.Normalized();
		sdl3::FRect thumb = horiz ? sdl3::FRect{s.x + thumbPos, s.y, thumbLen, s.h} : sdl3::FRect{s.x, s.y + thumbPos, s.w, thumbLen};
		sdl3::FColor thumbColor = b2.dragging ? rs.BgPressed(sdl3::FColor::UI_ACCENT_BLUE())
										: (b2.hovered ? rs.BgHovered(sdl3::FColor::UI_ACCENT_BLUE_LIGHT())
													  : rs.BgChecked(sdl3::FColor::UI_ACCENT_BLUE_LIGHT()));
		ren.SetDrawColor(thumbColor);
		ren.FillRoundedRect(thumb, math::Corners(radius));
	}

	if (auto kn = world.GetComponent<UiKnob>(e); kn.IsSome()) {
		const UiKnob &k = *kn.Unwrap();
		ResolvedStyle rs = GetResolved(world, e);
		sdl3::FPoint center{s.x + s.w * 0.5f, s.y + s.h * 0.5f};
		float radius = sdl3::Min(s.w, s.h) * 0.5f - 2.f;
		ren.SetDrawColor(k.hovered || k.dragging ? rs.BgHovered(sdl3::FColor::UI_BORDER_SLATE()) : rs.Bg(sdl3::FColor::UI_PANEL_DARK()));
		ren.FillCircle(center, radius);
		ren.SetDrawColor(rs.TextColor(sdl3::FColor::UI_TEXT_PRIMARY()));
		ren.DrawCircle(center, radius);
		// Balayage standard de potentiomètre : 135° (bas-gauche) → 405°.
		float ang = sdl3::DegToRad(135.f + k.value * 270.f);
		ren.SetDrawColor(rs.BgChecked(sdl3::FColor::UI_ACCENT_BLUE()));
		sdl3::FPoint tip{center.x + sdl3::Cos(ang) * radius * 0.75f, center.y + sdl3::Sin(ang) * radius * 0.75f};
		ren.DrawLine(center, tip);
		ren.FillCircle(tip, 2.5f);
	}

	if (auto cv = world.GetComponent<UiCanvas>(e); cv.IsSome()) {
		if (cv.Unwrap()->onDraw)
			if (auto *nr = ren.NativeRenderer())
				cv.Unwrap()->onDraw(*nr, s);
	}

	if (auto cbo = world.GetComponent<UiComboBox>(e); cbo.IsSome()) {
		const UiComboBox &cb = *cbo.Unwrap();
		ResolvedStyle rs = GetResolved(world, e);
		sdl3::FColor textColor = rs.TextColor(sdl3::FColor::UI_TEXT_PRIMARY());
		ren.SetDrawColor(cb.hovered && !cb.open ? rs.BgHovered(sdl3::FColor::UI_BORDER_SLATE()) : rs.Bg(sdl3::FColor::UI_PANEL_DARKER()));
		ren.FillRoundedRect(s, math::Corners(4.f));
		ren.SetDrawColor(cb.open ? rs.BorderFocus(sdl3::FColor::UI_ACCENT_BLUE_HOVER()) : rs.BorderColor(sdl3::FColor::UI_BORDER_MUTED()));
		ren.DrawRoundedRect(s, math::Corners(4.f));
		if (cb.selected >= 0 && cb.selected < int(cb.items.size())) {
			sdl3::FRect textBox{s.x, s.y, sdl3::Max(0.f, s.w - 24.f), s.h};
			DrawTextCentered(ren, e, cb.items[size_t(cb.selected)], textColor, textBox, TextAlign::Left);
		}
		// Flèche ▼ (ou ▲ liste ouverte) à droite.
		float axc = s.x + s.w - 14.f, ayc = s.y + s.h * 0.5f;
		ren.SetDrawColor(textColor);
		if (cb.open) {
			ren.DrawLine(axc - 5.f, ayc + 2.5f, axc, ayc - 2.5f);
			ren.DrawLine(axc, ayc - 2.5f, axc + 5.f, ayc + 2.5f);
		} else {
			ren.DrawLine(axc - 5.f, ayc - 2.5f, axc, ayc + 2.5f);
			ren.DrawLine(axc, ayc + 2.5f, axc + 5.f, ayc - 2.5f);
		}
	}

	if (auto lbo = world.GetComponent<ListBox>(e); lbo.IsSome()) {
		const ListBox &lb = *lbo.Unwrap();
		ResolvedStyle rs = GetResolved(world, e);
		sdl3::FColor textColor = rs.TextColor(sdl3::FColor::UI_TEXT_PRIMARY());
		ren.SetDrawColor(rs.Bg(sdl3::FColor::UI_BG_DEEP()));
		ren.FillRoundedRect(s, math::Corners(4.f));
		ren.SetDrawColor(rs.BorderColor(sdl3::FColor::UI_BORDER_MUTED()));
		ren.DrawRoundedRect(s, math::Corners(4.f));

		// Clip resserré au rect de la liste pour les items.
		sdl3::FRect inner = c.childClip.Intersection(s);
		if (inner.w > 0.f && inner.h > 0.f && lb.itemHeight > 0.f) {
			ren.SetClipRect(ToClipRect(inner));
			int first = sdl3::Max(0, int(lb.scroll / lb.itemHeight));
			int last = sdl3::Min(int(lb.items.size()) - 1, int((lb.scroll + s.h) / lb.itemHeight));
			for (int i = first; i <= last; ++i) {
				sdl3::FRect row{s.x, s.y + float(i) * lb.itemHeight - lb.scroll, s.w, lb.itemHeight};
				if (i == lb.selected) {
					ren.SetDrawColor(rs.BgChecked(sdl3::FColor::UI_ACCENT_BLUE_DEEP()));
					ren.FillRect(row);
				} else if (i == lb.hoveredItem) {
					ren.SetDrawColor(rs.BgHovered(sdl3::FColor::UI_BORDER_SLATE()));
					ren.FillRect(row);
				}
				DrawTextRaw(ren, lb.items[size_t(i)], textColor, row.x + 8.f, row.y, row.h);
			}
			// Barre de défilement inline à droite si débordement.
			float maxSc = lb.MaxScroll(s.h);
			if (maxSc > 0.f) {
				float ratio = sdl3::Clamp(s.h / lb.ContentHeight(), 0.05f, 1.f);
				float thumbH = s.h * ratio;
				float thumbY = s.y + (s.h - thumbH) * (lb.scroll / maxSc);
				ren.SetDrawColor(rs.BgPressed(sdl3::FColor::UI_ACCENT_BLUE_LIGHT()));
				ren.FillRoundedRect({s.x + s.w - 6.f, thumbY, 4.f, thumbH}, math::Corners(2.f));
			}
			ren.SetClipRect(ToClipRect(c.childClip));
		}
	}

	if (auto xo = world.GetComponent<UiExpander>(e); xo.IsSome()) {
		const UiExpander &x = *xo.Unwrap();
		ResolvedStyle rs = GetResolved(world, e);
		sdl3::FColor textColor = rs.TextColor(sdl3::FColor::UI_TEXT_PRIMARY());
		sdl3::FRect header{s.x, s.y, s.w, x.headerHeight};
		ren.SetDrawColor(x.hovered ? rs.BgHovered(sdl3::FColor::UI_BORDER_SLATE()) : rs.Bg(sdl3::FColor::UI_PANEL_DARK()));
		ren.FillRoundedRect(header, math::Corners(4.f));
		// Flèche ▶ (replié) / ▼ (déplié).
		float acx = s.x + 12.f, acy = s.y + x.headerHeight * 0.5f;
		ren.SetDrawColor(textColor);
		if (x.expanded) {
			ren.DrawLine(acx - 4.f, acy - 2.f, acx, acy + 3.f);
			ren.DrawLine(acx, acy + 3.f, acx + 4.f, acy - 2.f);
		} else {
			ren.DrawLine(acx - 2.f, acy - 4.f, acx + 3.f, acy);
			ren.DrawLine(acx + 3.f, acy, acx - 2.f, acy + 4.f);
		}
		sdl3::FRect titleBox{s.x + 22.f, s.y, sdl3::Max(0.f, s.w - 22.f), x.headerHeight};
		DrawTextCentered(ren, e, x.title, textColor, titleBox, TextAlign::Left);
	}

	if (auto tvo = world.GetComponent<UiTabView>(e); tvo.IsSome()) {
		const UiTabView &tv = *tvo.Unwrap();
		ResolvedStyle rs = GetResolved(world, e);
		sdl3::FColor textColor = rs.TextColor(sdl3::FColor::UI_TEXT_PRIMARY());
		float fs = rs.FontSize(14.f);
		if (!tv.tabs.empty()) {
			sdl3::FRect bar{s.x, s.y, s.w, tv.tabHeight};
			ren.SetDrawColor(rs.Bg(sdl3::FColor{24/255.f, 26/255.f, 36/255.f, 255/255.f}));
			ren.FillRect(bar);
			float tabW = s.w / float(tv.tabs.size());
			for (size_t i = 0; i < tv.tabs.size(); ++i) {
				sdl3::FRect tab{s.x + float(i) * tabW, s.y, tabW, tv.tabHeight};
				if (int(i) == tv.active) {
					ren.SetDrawColor(rs.BgChecked(sdl3::FColor{45/255.f, 52/255.f, 76/255.f, 255/255.f}));
					ren.FillRect(tab);
				} else if (int(i) == tv.hoveredTab) {
					ren.SetDrawColor(rs.BgHovered(sdl3::FColor::UI_BORDER_SLATE()));
					ren.FillRect(tab);
				}
				sdl3::FPoint tsz = MeasureCached(tv.tabs[i], fs);
				DrawTextRaw(ren, tv.tabs[i], textColor, tab.x + (tab.w - tsz.x) * 0.5f, tab.y, tab.h);
			}
			// Soulignement de l'onglet actif.
			sdl3::FRect underline{s.x + float(tv.active) * tabW, s.y + tv.tabHeight - 2.f, tabW, 2.f};
			ren.SetDrawColor(rs.BorderFocus(sdl3::FColor::UI_ACCENT_BLUE_BRIGHT()));
			ren.FillRect(underline);
		}
	}

	if (auto spo = world.GetComponent<UiSpinner>(e); spo.IsSome()) {
		const UiSpinner &sp = *spo.Unwrap();
		sdl3::FPoint center{s.x + s.w * 0.5f, s.y + s.h * 0.5f};
		float radius = sdl3::Min(s.w, s.h) * 0.5f - 2.f;
		ren.SetDrawColor(GetResolved(world, e).Bg(sdl3::FColor::UI_ACCENT_BLUE_BRIGHT()));
		// Arc de 300° tournant — épaissi par cercles concentriques.
		int th = sdl3::Max(1, int(sp.thickness));
		for (int i = 0; i < th; ++i)
			ren.DrawArc(center, radius - float(i), sp.angle, sp.angle + 300.f);
	}

	if (auto bdo = world.GetComponent<UiBadge>(e); bdo.IsSome()) {
		const UiBadge &bd = *bdo.Unwrap();
		ResolvedStyle rs = GetResolved(world, e);
		ren.SetDrawColor(rs.Bg(sdl3::FColor{200/255.f, 60/255.f, 50/255.f, 255/255.f}));
		ren.FillRoundedRect(s, math::Corners(s.h * 0.5f));
		DrawTextCentered(ren, e, bd.text, rs.TextColor(sdl3::FColor::WHITE()), s, TextAlign::Center);
	}
}

void RenderSystem::DrawScrollbars(IUiRenderBackend &ren, const UiRect &r, const sdl3::FRect &screen) {
	if (sdl3::FRect thumb = VScrollbarThumbRect(r, screen); thumb.w > 0.f) {
		sdl3::FRect track = VScrollbarTrackRect(r, screen);
		ren.SetDrawColor(scrollbarTrack);
		ren.FillRect(track);
		ren.SetDrawColor(scrollbarThumb);
		ren.FillRoundedRect(thumb, math::Corners(K_SCROLLBAR_THICKNESS * 0.5f));
	}
	if (sdl3::FRect thumb = HScrollbarThumbRect(r, screen); thumb.w > 0.f) {
		sdl3::FRect track = HScrollbarTrackRect(r, screen);
		ren.SetDrawColor(scrollbarTrack);
		ren.FillRect(track);
		ren.SetDrawColor(scrollbarThumb);
		ren.FillRoundedRect(thumb, math::Corners(K_SCROLLBAR_THICKNESS * 0.5f));
	}
}

void RenderSystem::DrawDropdown(IUiRenderBackend &ren, const UiComboBox &cb, const sdl3::FRect &box, const ResolvedStyle &rs) {
	m_currentFontSize = rs.FontSize(0.f);
	ren.ClearClipRect();
	const sdl3::FColor textColor = rs.TextColor(sdl3::FColor::UI_TEXT_PRIMARY());
	// Un panneau de lignes : éléments (index dans `items`) ou catégories.
	struct Row {
		int item = -1;
		int group = -1;
	};
	auto panel = [&](const sdl3::FRect &dd, const std::vector<Row> &rows) {
		ren.SetDrawColor(rs.Bg(sdl3::FColor::UI_PANEL_DARKER()));
		ren.FillRoundedRect(dd, math::Corners(4.f));
		for (size_t i = 0; i < rows.size(); ++i) {
			const Row &r = rows[i];
			sdl3::FRect row{dd.x, dd.y + float(i) * cb.itemHeight, dd.w, cb.itemHeight};
			const bool chosen = r.item >= 0 ? r.item == cb.selected : cb.GroupOf(cb.selected) == r.group;
			const bool hot = r.item >= 0 ? r.item == cb.hoveredItem : (r.group == cb.openGroup || r.group == cb.hoveredGroup);
			if (chosen && r.item >= 0) {
				ren.SetDrawColor(rs.BgChecked(sdl3::FColor::UI_ACCENT_BLUE_DEEP()));
				ren.FillRect(row);
			} else if (hot) {
				ren.SetDrawColor(rs.BgHovered(sdl3::FColor::UI_BORDER_SLATE()));
				ren.FillRect(row);
			}
			if (r.item >= 0) {
				DrawTextRaw(ren, cb.items[size_t(r.item)], textColor, row.x + 8.f, row.y, row.h);
				continue;
			}
			// Catégorie : titre (accentué si elle contient le choix courant) et ▸.
			const sdl3::FColor titleColor = chosen ? rs.BorderFocus(sdl3::FColor::UI_ACCENT_BLUE_HOVER()) : textColor;
			DrawTextRaw(ren, cb.groups[size_t(r.group)].title, titleColor, row.x + 8.f, row.y, row.h);
			const float ax = row.x + row.w - 12.f, ay = row.y + row.h * 0.5f;
			ren.SetDrawColor(textColor);
			ren.DrawLine(ax - 2.5f, ay - 5.f, ax + 2.5f, ay);
			ren.DrawLine(ax + 2.5f, ay, ax - 2.5f, ay + 5.f);
		}
		ren.SetDrawColor(rs.BorderFocus(sdl3::FColor::UI_ACCENT_BLUE_HOVER()));
		ren.DrawRoundedRect(dd, math::Corners(4.f));
	};

	std::vector<Row> top;
	if (!cb.Grouped()) {
		for (size_t i = 0; i < cb.items.size(); ++i)
			top.push_back({int(i), -1});
	} else {
		for (int i : cb.TopItems())
			top.push_back({i, -1});
		for (size_t g = 0; g < cb.groups.size(); ++g)
			top.push_back({-1, int(g)});
	}
	panel(cb.DropdownRect(box), top);
	if (cb.openGroup >= 0 && cb.openGroup < int(cb.groups.size())) {
		const UiComboGroup &g = cb.groups[size_t(cb.openGroup)];
		std::vector<Row> sub;
		for (int i = g.first; i < g.first + g.count && i < int(cb.items.size()); ++i)
			sub.push_back({i, -1});
		panel(cb.SubmenuRect(box, cb.openGroup), sub);
	}
}

void RenderSystem::DrawTooltip(IUiRenderBackend &ren, const InputSystem::Tooltip &tip) {
	// Mesure et dessin à la MÊME taille : la boîte était mesurée à 13 px
	// mais le texte dessiné à la taille de chargement de la police
	// (m_currentFontSize = 0), plus grande — il débordait de sa boîte.
	m_currentFontSize = TOOLTIP_FONT_SIZE;
	ren.ClearClipRect();
	const sdl3::FPoint sz = MeasureCached(tip.text, TOOLTIP_FONT_SIZE);
	sdl3::FRect box{tip.pos.x, tip.pos.y, sdl3::Ceil(sz.x) + 16.f, sdl3::Ceil(sz.y) + 8.f};
	// Toujours dans la fenêtre : près d'un bord droit ou bas, l'infobulle
	// passe à gauche / au-dessus du pointeur plutôt que d'être coupée.
	if (sdl3::Renderer *native = ren.NativeRenderer()) {
		const sdl3::Point out = native->OutputSize();
		const float W = float(out.x), H = float(out.y);
		if (W > 0.f && box.x + box.w > W)
			box.x = sdl3::Max(0.f, W - box.w - 2.f);
		if (H > 0.f && box.y + box.h > H)
			box.y = sdl3::Max(0.f, tip.pos.y - box.h - 24.f);
	}
	ren.SetDrawColor(tooltipBg);
	ren.FillRoundedRect(box, math::Corners(4.f));
	ren.SetDrawColor(tooltipBorder);
	ren.DrawRoundedRect(box, math::Corners(4.f));
	DrawTextRaw(ren, tip.text, tooltipText, box.x + 8.f, box.y, box.h);
	m_currentFontSize = 0.f;
}

void RenderSystem::DrawImageFit(IUiRenderBackend &ren, const sdl3::Texture &tex, const sdl3::FRect &dst, ImageFit fit) {
	sdl3::FPoint ts = tex.GetSize();
	if (ts.x <= 0.f || ts.y <= 0.f || dst.w <= 0.f || dst.h <= 0.f)
		return;
	switch (fit) {
	case ImageFit::FILL:
		ren.Render(tex, dst);
		break;
	case ImageFit::CONTAIN: {
		float k = sdl3::Min(dst.w / ts.x, dst.h / ts.y);
		sdl3::FRect d{dst.x + (dst.w - ts.x * k) * 0.5f, dst.y + (dst.h - ts.y * k) * 0.5f, ts.x * k, ts.y * k};
		ren.Render(tex, d);
		break;
	}
	case ImageFit::COVER: {
		// Rogne la source pour couvrir la destination sans déformer.
		float k = sdl3::Max(dst.w / ts.x, dst.h / ts.y);
		sdl3::FRect src{(ts.x - dst.w / k) * 0.5f, (ts.y - dst.h / k) * 0.5f, dst.w / k, dst.h / k};
		ren.Render(tex, src, dst);
		break;
	}
	case ImageFit::NONE: {
		// Taille d'origine, centrée, rognée à la destination.
		sdl3::FRect src{sdl3::Max(0.f, (ts.x - dst.w) * 0.5f), sdl3::Max(0.f, (ts.y - dst.h) * 0.5f),
				  sdl3::Min(ts.x, dst.w), sdl3::Min(ts.y, dst.h)};
		sdl3::FRect d{dst.x + sdl3::Max(0.f, (dst.w - ts.x) * 0.5f), dst.y + sdl3::Max(0.f, (dst.h - ts.y) * 0.5f), src.w,
				src.h};
		ren.Render(tex, src, d);
		break;
	}
	}
}

sdl3::Font * RenderSystem::FontAt(sdl3::Font *base, float size) {
	if (!base || size <= 0.f)
		return base;
	if (sdl3::Abs(base->PointSize() - size) < 0.2f)
		return base;
	const std::pair<const sdl3::Font *, int> key{base, int(size * 4.f + 0.5f)};
	if (auto it = m_sizedFonts.find(key); it != m_sizedFonts.end())
		return it->second.get();
	auto copy = base->Copy();
	if (!copy)
		return base; // au pire, la taille de chargement : jamais d'absence de texte
	copy.Value().SetSize(float(key.second) / 4.f);
	auto owned = std::make_unique<sdl3::Font>(std::move(copy.Value()));
	sdl3::Font *raw = owned.get();
	m_sizedFonts.emplace(key, std::move(owned));
	return raw;
}

sdl3::Font * RenderSystem::DrawFont(sdl3::Font *font) {
	sdl3::Font *base = font ? font : m_font;
	return m_currentFontSize > 0.f ? FontAt(base, m_currentFontSize) : base;
}

sdl3::Font * RenderSystem::PickTextFont(bool bold, bool italic) const {
	if (bold && italic && boldItalicFont)
		return boldItalicFont;
	if (bold && boldFont)
		return boldFont;
	if (italic && italicFont)
		return italicFont;
	return m_font;
}

sdl3::FPoint RenderSystem::MeasureCached(const String &text, float fontSize) {
	auto measureLine = [this, fontSize](const String &line, float fs) -> sdl3::FPoint {
		if (sdl3::Font *sized = FontAt(m_font, fontSize)) {
			if (auto sz = sized->Measure(line); sz.IsSome())
				return {float(sz.Unwrap().x), float(sz.Unwrap().y)};
		}
		(void)fontSize;
		return {float(DisplayCells(line)) * CharWidthApprox(fs), LineHeightApprox(fs)};
	};
	const TextMetrics metrics = MeasureTextBlock(text, fontSize, measureLine, tabStop);
	return {metrics.width, metrics.height};
}

void RenderSystem::DrawTextRaw([[maybe_unused]] IUiRenderBackend &ren, const String &rawText, sdl3::FColor color, float x,
		float y, float rowH, sdl3::Font *font) {
	sdl3::Font *drawFont = DrawFont(font);
	if (!m_engine || !drawFont || rawText.IsEmpty())
		return;
	// Forme normalisée : SDL_ttf dessine une boîte « glyphe manquant »
	// pour `\r`, `\t` et tout autre caractère de contrôle (cf.
	// NormalizeDisplayText) — et c'est cette forme qui a été mesurée.
	const String text = NormalizeDisplayText(rawText, tabStop);
	// La clé porte la police quand ce n'est pas celle par défaut : la même
	// chaîne en chasse fixe et en proportionnelle sont deux textures.
	String key = drawFont == m_font ? String(text.c_str()) : String::Format("\x01%p\x01%s", (void *)drawFont, text.c_str());
	auto it = strCache.find(key);
	if (it == strCache.end()) {
		auto res = sdl3::Text::Create(*m_engine, *drawFont, text);
		if (!res)
			return;
		it = strCache.insert_or_assign(std::move(key), CachedStr{color, std::move(res.Value())}).first;
		it->second.text.SetColor(color);
	} else if (!(it->second.color == color)) {
		it->second.text.SetColor(color);
		it->second.color = color;
	}
	float th = rowH;
	if (auto s = it->second.text.GetSize(); s.IsSome())
		th = float(s.Unwrap().y);
	// Origine au PIXEL ENTIER : un texte posé à une demi-coordonnée est
	// échantillonné entre deux pixels (filtrage bilinéaire de l'atlas de
	// glyphes) et paraît dédoublé, flou.
	it->second.text.Draw(sdl3::Round(x), sdl3::Round(y + (rowH - th) * 0.5f));
}

void RenderSystem::DrawSelection(IUiRenderBackend &ren, const std::vector<TextLine> &lines, float fontSize, size_t cursor,
		size_t selectionAnchor, const sdl3::FRect &s, const sdl3::FPoint &scroll, sdl3::FColor accent,
		float cellWidth) {
	if (cursor == selectionAnchor || lines.empty())
		return;
	size_t lo = sdl3::Min(cursor, selectionAnchor), hi = sdl3::Max(cursor, selectionAnchor);
	size_t loLi = LineIndexOf(lines, lo), hiLi = LineIndexOf(lines, hi);
	float lh = LineHeightApprox(fontSize);
	float cw = cellWidth > 0.f ? cellWidth : CharWidthApprox(fontSize);
	ren.SetDrawColor(sdl3::FColor{accent.r, accent.g, accent.b, 90});
	for (size_t li = loLi; li <= hiLi; ++li) {
		const TextLine &line = lines[li];
		// Colonnes D'AFFICHAGE (tabulations développées, UTF-8 compté en
		// caractères) : la bande doit couvrir le texte tel qu'il est
		// dessiné, pas tel qu'il est stocké.
		size_t a = DisplayColumn(line.text, (li == loLi) ? (lo - line.offset) : 0, tabStop);
		size_t b = DisplayColumn(line.text, (li == hiLi) ? (hi - line.offset) : line.text.size(), tabStop);
		float x0 = s.x + 8.f - scroll.x + float(a) * cw;
		float x1 = s.x + 8.f - scroll.x + float(b) * cw;
		float y = s.y + 4.f - scroll.y + float(li) * lh;
		if (y + lh >= s.y && y <= s.y + s.h)
			ren.FillRect({x0, y, sdl3::Max(1.f, x1 - x0), lh});
	}
}

void RenderSystem::DrawCaret(IUiRenderBackend &ren, const std::vector<TextLine> &lines, float fontSize, size_t cursor,
		const sdl3::FRect &s, const sdl3::FPoint &scroll, sdl3::FColor color, float cellWidth) {
	if (lines.empty())
		return;
	size_t li = LineIndexOf(lines, cursor);
	size_t col = DisplayColumn(lines[li].text, cursor - lines[li].offset, tabStop);
	float lh = LineHeightApprox(fontSize);
	const float cw = cellWidth > 0.f ? cellWidth : CharWidthApprox(fontSize);
	float x = s.x + 8.f - scroll.x + float(col) * cw;
	float y = s.y + 4.f - scroll.y + float(li) * lh;
	ren.SetDrawColor(color);
	ren.FillRect({x, y, 2.f, lh});
}

void RenderSystem::DrawVGradient(IUiRenderBackend &ren, const sdl3::FRect &r, sdl3::FColor top, sdl3::FColor bottom) {
	std::array<sdl3::Vertex, 6> v{{
		{{r.x, r.y}, top, {}},
		{{r.x + r.w, r.y}, top, {}},
		{{r.x, r.y + r.h}, bottom, {}},
		{{r.x + r.w, r.y}, top, {}},
		{{r.x + r.w, r.y + r.h}, bottom, {}},
		{{r.x, r.y + r.h}, bottom, {}},
	}};
	ren.RenderGeometry(v);
}

void RenderSystem::DrawHGradient(IUiRenderBackend &ren, const sdl3::FRect &r, sdl3::FColor left, sdl3::FColor right) {
	std::array<sdl3::Vertex, 6> v{{
		{{r.x, r.y}, left, {}},
		{{r.x + r.w, r.y}, right, {}},
		{{r.x, r.y + r.h}, left, {}},
		{{r.x + r.w, r.y}, right, {}},
		{{r.x + r.w, r.y + r.h}, right, {}},
		{{r.x, r.y + r.h}, left, {}},
	}};
	ren.RenderGeometry(v);
}

void RenderSystem::DrawQuadGradient(IUiRenderBackend &ren, const sdl3::FRect &r, sdl3::FColor tl, sdl3::FColor tr, sdl3::FColor bl, sdl3::FColor br) {
	std::array<sdl3::Vertex, 6> v{{
		{{r.x, r.y}, tl, {}},
		{{r.x + r.w, r.y}, tr, {}},
		{{r.x, r.y + r.h}, bl, {}},
		{{r.x + r.w, r.y}, tr, {}},
		{{r.x + r.w, r.y + r.h}, br, {}},
		{{r.x, r.y + r.h}, bl, {}},
	}};
	ren.RenderGeometry(v);
}

void RenderSystem::DrawHueBar(IUiRenderBackend &ren, const sdl3::FRect &s) {
	constexpr int K_STOPS = 6;
	float segW = s.w / float(K_STOPS);
	for (int i = 0; i < K_STOPS; ++i) {
		sdl3::FColor left = HsvToColor(float(i) * 60.f, 1.f, 1.f);
		sdl3::FColor right = HsvToColor(float(i + 1) * 60.f, 1.f, 1.f);
		sdl3::FRect seg{s.x + float(i) * segW, s.y, segW + 0.5f, s.h}; // +0.5 : pas d'interstice visible
		DrawHGradient(ren, seg, left, right);
	}
}

void RenderSystem::DrawCheckerboard(IUiRenderBackend &ren, const sdl3::FRect &s, const math::Corners &radius) {
	constexpr float K_CELL = 6.f;
	if (radius.tl > 0.f || radius.tr > 0.f || radius.bl > 0.f || radius.br > 0.f) {
		ren.SetDrawColor(sdl3::FColor{200/255.f, 200/255.f, 200/255.f, 255/255.f});
		ren.FillRoundedRect(s, radius);
	} else {
		ren.SetDrawColor(sdl3::FColor{200/255.f, 200/255.f, 200/255.f, 255/255.f});
		ren.FillRect(s);
	}
	ren.SetDrawColor(sdl3::FColor{150/255.f, 150/255.f, 150/255.f, 255/255.f});
	int cols = int(sdl3::Ceil(s.w / K_CELL)), rows = int(sdl3::Ceil(s.h / K_CELL));
	for (int row = 0; row < rows; ++row) {
		for (int col = 0; col < cols; ++col) {
			if ((row + col) % 2 != 0)
				continue;
			sdl3::FRect cell{s.x + float(col) * K_CELL, s.y + float(row) * K_CELL, K_CELL, K_CELL};
			cell = cell.Intersection(s);
			if (cell.w > 0.f && cell.h > 0.f)
				ren.FillRect(cell);
		}
	}
}

void RenderSystem::DrawGlossHighlight(IUiRenderBackend &ren, const sdl3::FRect &s, float radius, float strength) {
	if (strength <= 0.f)
		return;
	float inset = sdl3::Max(1.f, radius * 0.4f);
	sdl3::FRect band{s.x + inset, s.y + inset, s.w - 2.f * inset, s.h * 0.42f};
	if (band.w <= 0.f || band.h <= 0.f)
		return;
	sdl3::FColor top{1.0f, 1.0f, 1.0f, sdl3::Clamp(strength, 0.f, 1.f)};
	sdl3::FColor bottom{1.0f, 1.0f, 1.0f, 0};
	DrawVGradient(ren, band, top, bottom);
}

void RenderSystem::DrawGlowRing(IUiRenderBackend &ren, const sdl3::FRect &s, float radius, float strength, sdl3::FColor glowColor) {
	if (strength <= 0.f)
		return;
	constexpr int K_RINGS = 3;
	for (int i = 0; i < K_RINGS; ++i) {
		float t = float(i) / float(K_RINGS);
		float alpha = strength * (1.f - t) * 255.f * 0.5f;
		if (alpha < 2.f)
			continue;
		float outset = 1.f + float(i) * 1.5f;
		sdl3::FRect r{s.x - outset, s.y - outset, s.w + 2.f * outset, s.h + 2.f * outset};
		ren.SetDrawColor(sdl3::FColor{glowColor.r, glowColor.g, glowColor.b, alpha / 255.f});
		ren.DrawRoundedRect(r, math::Corners(radius + outset));
	}
}

void RenderSystem::DrawOverflowText(IUiRenderBackend &ren, ecs::ArchetypeRegistry &world, ecs::Entity e, const String &text,
		sdl3::FColor color, const sdl3::FRect &box, TextAlign align, sdl3::Font *font) {
	const TextOverflow mode = TextOverflowOf(world, e);
	if (mode == TextOverflow::CLIP) {
		DrawTextCentered(ren, e, text, color, box, align, font);
		return;
	}

	sdl3::FPoint scroll{};
	if (auto r = world.GetComponent<UiRect>(e); r.IsSome())
		scroll = r.Unwrap()->scroll;
	const float inner = sdl3::Max(1.f, box.w - 2.f * TEXT_INSET);
	const float fs = GetResolved(world, e).FontSize(14.f);

	switch (mode) {
	case TextOverflow::CLIP:
		break; // traité plus haut
	case TextOverflow::ELLIPSIS: {
		const String shown = TruncateWithEllipsis(text, fs, inner);
		DrawTextCentered(ren, e, shown, color, box, align, font);
		break;
	}
	case TextOverflow::SCROLL: {
		// Défilement horizontal : le texte entier est dessiné, décalé ;
		// le clip du widget fait le reste, et la barre vient de
		// UiRect::content (posé par le layout).
		DrawTextRaw(ren, text, color, box.x + TEXT_INSET - scroll.x, box.y, box.h);
		break;
	}
	case TextOverflow::MARQUEE: {
		float offset = 0.f;
		if (auto o = world.GetComponent<UiTextOverflow>(e); o.IsSome())
			offset = o.Unwrap()->offset;
		DrawTextRaw(ren, text, color, box.x + TEXT_INSET - offset, box.y, box.h);
		break;
	}
	case TextOverflow::WRAP: {
		auto measureLine = [this, font](const String &line, float size) -> sdl3::FPoint {
			return MeasureLineWith(line, size, font);
		};
		const std::vector<WrappedLine> lines = WrapTextToWidth(text, fs, inner, measureLine, tabStop);
		float lineHeight = LineHeightApprox(fs);
		if (const float measured = measureLine(String("Ag"), fs).y; measured > 0.f)
			lineHeight = measured;
		float y = box.y - scroll.y;
		for (const WrappedLine &line : lines) {
			if (y + lineHeight >= box.y && y <= box.y + box.h)
				DrawWrappedLine(ren, line, color, box, y, lineHeight, align, font, fs);
			y += lineHeight;
		}
		break;
	}
	}
}

String RenderSystem::TruncateWithEllipsis(const String &text, float fontSize, float maxWidth) {
	const String ellipsis("…");
	if (MeasureCached(text, fontSize).x <= maxWidth)
		return text;
	// Frontières de caractères (UTF-8) : couper entre deux octets d'un
	// même caractère produirait un glyphe invalide.
	std::vector<size_t> bounds;
	bounds.push_back(0);
	for (size_t i = 0; i < text.size(); ++i)
		if ((static_cast<unsigned char>(text[i]) & 0xC0) != 0x80 && i > 0)
			bounds.push_back(i);
	bounds.push_back(text.size());

	size_t lo = 0, hi = bounds.size() - 1;
	while (lo < hi) {
		const size_t mid = (lo + hi + 1) / 2;
		if (MeasureCached(text.Substr(0, bounds[mid]) + ellipsis, fontSize).x <= maxWidth)
			lo = mid;
		else
			hi = mid - 1;
	}
	// Même « … » seul ne tient pas : on rend une chaîne vide plutôt qu'un
	// glyphe qui dépasserait.
	if (lo == 0 && MeasureCached(ellipsis, fontSize).x > maxWidth)
		return String();
	return text.Substr(0, bounds[lo]) + ellipsis;
}

void RenderSystem::DrawWrappedLine(IUiRenderBackend &ren, const WrappedLine &line, sdl3::FColor color, const sdl3::FRect &box,
		float y, float lineHeight, TextAlign align, sdl3::Font *font, float fontSize) {
	const float inner = sdl3::Max(1.f, box.w - 2.f * TEXT_INSET);
	if (align == TextAlign::Justify && !line.lastOfParagraph) {
		std::vector<String> words;
		for (const String &word : line.text.Split(' '))
			if (!word.IsEmpty())
				words.push_back(word);
		if (words.size() > 1) {
			float wordsWidth = 0.f;
			for (const String &word : words)
				wordsWidth += MeasureLineWith(word, fontSize, font).x;
			const float gap = (inner - wordsWidth) / float(words.size() - 1);
			float x = box.x + TEXT_INSET;
			for (const String &word : words) {
				DrawTextRaw(ren, word, color, x, y, lineHeight);
				x += MeasureLineWith(word, fontSize, font).x + gap;
			}
			return;
		}
	}
	float x = box.x + TEXT_INSET;
	if (align == TextAlign::Center)
		x = box.x + (box.w - line.width) * 0.5f;
	else if (align == TextAlign::Right)
		x = box.x + box.w - line.width - TEXT_INSET;
	DrawTextRaw(ren, line.text, color, x, y, lineHeight);
}

sdl3::FPoint RenderSystem::MeasureLineWith(const String &line, float fontSize, sdl3::Font *font) {
	sdl3::Font *f = font ? font : m_font;
	if (f)
		if (auto size = f->Measure(line); size.IsSome())
			return {float(size.Unwrap().x), float(size.Unwrap().y)};
	return {float(DisplayCells(line)) * CharWidthApprox(fontSize), LineHeightApprox(fontSize)};
}

void RenderSystem::DrawTextCentered([[maybe_unused]] IUiRenderBackend &ren, ecs::Entity e, const String &rawText,
		sdl3::FColor color, const sdl3::FRect &box, TextAlign align, sdl3::Font *font) {
	sdl3::Font *f = DrawFont(font);
	if (!m_engine || !f || rawText.IsEmpty())
		return;
	// Cf. DrawTextRaw : on dessine — et on met en cache — la forme
	// normalisée, celle que le layout a mesurée.
	const String text = NormalizeDisplayText(rawText, tabStop);

	auto it = textCache.find(e.id);
	if (it == textCache.end() || it->second.str != text || it->second.font != f) {
		auto res = sdl3::Text::Create(*m_engine, *f, text);
		if (!res)
			return;
		it = textCache.insert_or_assign(e.id, CachedText{text, color, f, std::move(res.Value())}).first;
		it->second.text.SetColor(color);
	} else if (!(it->second.color == color)) {
		it->second.text.SetColor(color);
		it->second.color = color;
	}

	sdl3::FPoint sz{0.f, 0.f};
	if (auto s = it->second.text.GetSize(); s.IsSome())
		sz = {float(s.Unwrap().x), float(s.Unwrap().y)};

	// Cf. TextOriginX : la marge de confort est rabotée quand la boîte
	// n'est pas plus large que le texte — sinon un `UiLabel` déborde de
	// 8 px à droite, ce qui restait invisible tant qu'un widget pouvait
	// peindre dans le clip de son parent, mais se voyait tranché net
	// depuis que chacun est borné à sa propre boîte (les libellés de
	// l'outliner y perdaient leur dernière lettre).
	float x = TextOriginX(box, sz.x, align);
	float y = box.y + (box.h - sz.y) * 0.5f;
	// Cf. DrawTextRaw : pixel entier, sinon le texte paraît dédoublé.
	it->second.text.Draw(sdl3::Round(x), sdl3::Round(y));
}

} // namespace ui
