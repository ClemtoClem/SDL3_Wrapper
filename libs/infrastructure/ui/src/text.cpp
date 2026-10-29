// Définitions de ui/text.hpp
#include <ui/text.hpp>

#include <math/math.hpp>

namespace ui {

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
	const auto *base = reinterpret_cast<const uint8_t *>(text.c_str());
	const auto *p = base + i;
	return CodepointCells(char32_t(unicode::DecodeNext(p, base + text.size())));
}

bool IsNonPrintableControl(unsigned char c) noexcept { return (c < 0x20 && c != '\n' && c != '\r' && c != '\t') || c == 0x7F; }

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
		if ((c & 0xC0) != 0x80)
			++column;
	}
	return out;
}

size_t DisplayColumn(const String &line, size_t byteOffset, int tabStop) noexcept {
	const int stop = tabStop > 0 ? tabStop : 1;
	size_t column = 0;
	const size_t end = math::Min(byteOffset, line.size());
	for (size_t i = 0; i < end; ++i) {
		const unsigned char c = static_cast<unsigned char>(line[i]);
		if (c == '\t')
			column += size_t(stop) - (column % size_t(stop));
		else if (IsNonPrintableControl(c) || c == '\n' || c == '\r')
			continue;
		else if ((c & 0xC0) != 0x80)
			column += size_t(CellsAt(line, i));
	}
	return column;
}

size_t DisplayColumns(const String &line, int tabStop) noexcept { return DisplayColumn(line, line.size(), tabStop); }

size_t ColumnToByteOffset(const String &line, size_t column, int tabStop) noexcept {
	const int stop = tabStop > 0 ? tabStop : 1;
	size_t current = 0;
	size_t i = 0;
	while (i < line.size()) {
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

size_t PrevCodepoint(const String &text, size_t pos) noexcept {
	if (pos == 0)
		return 0;
	size_t n = math::Min(pos, text.size()) - 1;
	while (n > 0 && (uint8_t(text.c_str()[n]) & 0xC0) == 0x80)
		--n;
	return n;
}

size_t NextCodepoint(const String &text, size_t pos) noexcept {
	const size_t n = text.size();
	if (pos >= n)
		return n;
	++pos;
	while (pos < n && (uint8_t(text.c_str()[pos]) & 0xC0) == 0x80)
		++pos;
	return pos;
}

namespace {

bool IsWordByte(char c) noexcept {
	const unsigned char u = static_cast<unsigned char>(c);
	return u >= 0x80 || (u >= '0' && u <= '9') || (u >= 'a' && u <= 'z') || (u >= 'A' && u <= 'Z') || u == '_';
}

} // namespace

size_t WordStart(const String &text, size_t pos) noexcept {
	pos = math::Min(pos, text.size());
	while (pos > 0 && !IsWordByte(text[pos - 1]))
		--pos;
	while (pos > 0 && IsWordByte(text[pos - 1]))
		--pos;
	return pos;
}

size_t WordEnd(const String &text, size_t pos) noexcept {
	const size_t n = text.size();
	while (pos < n && !IsWordByte(text[pos]))
		++pos;
	while (pos < n && IsWordByte(text[pos]))
		++pos;
	return pos;
}

TextMetrics MeasureTextBlock(const String &text, float fontSize, const LineMeasure &measureLine, int tabStop) {
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
		const sdl3::FPoint size = measureLine(line);
		metrics.width = math::Max(metrics.width, size.x);
		hookHeight = math::Max(hookHeight, size.y);
		++metrics.lineCount;
		if (nl == String::npos)
			break;
		start = nl + 1;
	}
	// La hauteur rapportée par la mesure fait foi ; l'heuristique ne sert que
	// si elle n'en rapporte aucune (une ligne vide rend souvent 0).
	if (hookHeight > 0.f)
		metrics.lineHeight = hookHeight;
	metrics.height = metrics.lineHeight * float(metrics.lineCount);
	return metrics;
}

std::vector<WrappedLine> WrapTextToWidth(const String &text, float maxWidth, const LineMeasure &measureLine, int tabStop) {
	std::vector<WrappedLine> lines;
	if (!measureLine)
		return lines;
	const String normalized = NormalizeDisplayText(text, tabStop);
	auto widthOf = [&](const String &s) { return s.IsEmpty() ? 0.f : measureLine(s).x; };

	size_t paraStart = 0;
	while (true) {
		const size_t nl = normalized.Find('\n', paraStart);
		const String paragraph =
			nl == String::npos ? normalized.Substr(paraStart) : normalized.Substr(paraStart, nl - paraStart);
		String current;
		size_t currentStart = paraStart;
		size_t i = 0;
		while (i < paragraph.size()) {
			const size_t space = paragraph.Find(' ', i);
			const size_t end = space == String::npos ? paragraph.size() : space + 1;
			const String word = paragraph.Substr(i, end - i);
			const String candidate = current.IsEmpty() ? word : current + word;
			if (maxWidth > 0.f && widthOf(candidate) > maxWidth && !current.IsEmpty()) {
				lines.push_back({current, widthOf(current), false, currentStart});
				current = word;
				currentStart = paraStart + i;
				if (widthOf(word) > maxWidth) {
					// Le mot reste trop long seul : il sera coupé au tour suivant.
					current.Clear();
					continue;
				}
			} else if (maxWidth > 0.f && widthOf(candidate) > maxWidth) {
				String piece;
				size_t pieceStart = paraStart + i;
				for (size_t k = 0; k < word.size();) {
					size_t next = k + 1;
					while (next < word.size() && (static_cast<unsigned char>(word[next]) & 0xC0) == 0x80)
						++next;
					const String grown = piece + word.Substr(k, next - k);
					if (!piece.IsEmpty() && widthOf(grown) > maxWidth) {
						lines.push_back({piece, widthOf(piece), false, pieceStart});
						piece = word.Substr(k, next - k);
						pieceStart = paraStart + i + k;
					} else {
						piece = grown;
					}
					k = next;
				}
				current = piece;
				currentStart = pieceStart;
			} else {
				if (current.IsEmpty())
					currentStart = paraStart + i;
				current = candidate;
			}
			i = end;
		}
		lines.push_back({current, widthOf(current), true, currentStart});
		if (nl == String::npos)
			break;
		paraStart = nl + 1;
	}
	return lines;
}

std::vector<TextLine> SplitLines(const String &text) {
	std::vector<TextLine> lines;
	size_t start = 0;
	auto strip = [](String line) {
		if (!line.IsEmpty() && line[line.size() - 1] == '\r')
			return line.Substr(0, line.size() - 1);
		return line;
	};
	while (true) {
		const size_t nl = text.Find('\n', start);
		if (nl == String::npos) {
			lines.push_back({start, strip(text.Substr(start))});
			break;
		}
		lines.push_back({start, strip(text.Substr(start, nl - start))});
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

String TruncateWithEllipsis(const String &text, float maxWidth, const LineMeasure &measureLine) {
	const String ellipsis("…");
	if (!measureLine || measureLine(text).x <= maxWidth)
		return text;
	std::vector<size_t> bounds;
	bounds.push_back(0);
	for (size_t i = 1; i < text.size(); ++i)
		if ((static_cast<unsigned char>(text[i]) & 0xC0) != 0x80)
			bounds.push_back(i);
	bounds.push_back(text.size());
	size_t lo = 0, hi = bounds.size() - 1;
	while (lo < hi) {
		const size_t mid = (lo + hi + 1) / 2;
		if (measureLine(text.Substr(0, bounds[mid]) + ellipsis).x <= maxWidth)
			lo = mid;
		else
			hi = mid - 1;
	}
	if (lo == 0 && measureLine(ellipsis).x > maxWidth)
		return String();
	return text.Substr(0, bounds[lo]) + ellipsis;
}

LineMeasure HeuristicMeasure(float fontSize) {
	return [fontSize](const String &s) -> sdl3::FPoint {
		return {float(DisplayCells(s)) * CharWidthApprox(fontSize), LineHeightApprox(fontSize)};
	};
}

} // namespace ui
