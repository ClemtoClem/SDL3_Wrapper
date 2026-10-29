// Définitions de ui/fonts.hpp
#include <ui/fonts.hpp>

#include <cctype>

namespace ui {

namespace {

String FamilyKey(StringView family) {
	String out;
	for (size_t i = 0; i < family.GetSize(); ++i)
		out.PushBack(char(std::tolower(static_cast<unsigned char>(family[i]))));
	return out;
}

} // namespace

Fonts::~Fonts() {
	// Les copies dimensionnées référencent les polices de base : détruites d'abord.
	m_sized.clear();
	m_families.clear();
}

void Fonts::Register(StringView family, sdl3::Font &font, int weight, FontStyle style) {
	const String key = FamilyKey(family);
	Family &f = m_families[key];
	std::erase_if(f.variants, [&](const Variant &v) { return v.weight == weight && v.style == style; });
	Variant v;
	v.font = &font;
	v.weight = weight;
	v.style = style;
	f.variants.push_back(std::move(v));
	if (m_default.IsEmpty())
		m_default = key;
	m_measures.clear();
}

void Fonts::Register(StringView family, sdl3::Font &&font, int weight, FontStyle style) {
	const String key = FamilyKey(family);
	Family &f = m_families[key];
	std::erase_if(f.variants, [&](const Variant &v) { return v.weight == weight && v.style == style; });
	Variant v;
	v.owned = std::make_unique<sdl3::Font>(std::move(font));
	v.font = v.owned.get();
	v.weight = weight;
	v.style = style;
	f.variants.push_back(std::move(v));
	if (m_default.IsEmpty())
		m_default = key;
	m_measures.clear();
}

Result<bool, String> Fonts::Load(StringView family, const String &path, int weight, FontStyle style, float size) {
	auto font = sdl3::Font::Open(path, size);
	if (!font)
		return Err(String("police « ") + path + " » : " + String(font.Error()));
	Register(family, std::move(font.Value()), weight, style);
	return Ok(true);
}

void Fonts::AddFace(const FontFace &face) {
	for (const FontFace &f : m_pending)
		if (FamilyKey(f.family.View()) == FamilyKey(face.family.View()) && f.weight == face.weight &&
			f.style == face.style)
			return;
	m_pending.push_back(face);
}

void Fonts::LoadPending(StringView family) {
	const String key = FamilyKey(family);
	for (auto it = m_pending.begin(); it != m_pending.end();) {
		if (FamilyKey(it->family.View()) != key) {
			++it;
			continue;
		}
		// Échec de chargement : la famille retombe sur la police par défaut.
		(void)Load(it->family.View(), it->source, it->weight, it->style);
		it = m_pending.erase(it);
	}
}

Fonts::Family *Fonts::FindFamily(StringView family) {
	String key = FamilyKey(family);
	if (key.IsEmpty() || key == "sans-serif" || key == "serif" || key == "system-ui" || key == "default")
		key = m_default;
	else if (key == "monospace" && !m_monospace.IsEmpty())
		key = FamilyKey(m_monospace.View());
	LoadPending(key.View());
	auto it = m_families.find(key);
	if (it == m_families.end() || it->second.variants.empty()) {
		if (key == m_default)
			return nullptr;
		it = m_families.find(m_default);
		if (it == m_families.end())
			return nullptr;
	}
	return &it->second;
}

bool Fonts::HasFamily(StringView family) const {
	const String key = FamilyKey(family);
	if (m_families.count(key))
		return true;
	for (const FontFace &f : m_pending)
		if (FamilyKey(f.family.View()) == key)
			return true;
	return false;
}

sdl3::Font *Fonts::Sized(sdl3::Font *base, float size) {
	if (!base || size <= 0.f)
		return base;
	if (math::Abs(base->PointSize() - size) < 0.2f)
		return base;
	const std::pair<const sdl3::Font *, int> key{base, int(size * 4.f + 0.5f)};
	if (auto it = m_sized.find(key); it != m_sized.end())
		return it->second.get();
	auto copy = base->Copy();
	if (!copy)
		return base; // au pire la taille de chargement : jamais d'absence de texte
	copy.Value().SetSize(float(key.second) / 4.f);
	auto owned = std::make_unique<sdl3::Font>(std::move(copy.Value()));
	sdl3::Font *raw = owned.get();
	m_sized.emplace(key, std::move(owned));
	return raw;
}

sdl3::Font *Fonts::Resolve(StringView family, float size, int weight, FontStyle style) {
	Family *f = FindFamily(family);
	if (!f)
		return nullptr;
	const bool bold = weight >= 600;
	const bool italic = style == FontStyle::Italic;
	auto pick = [&](bool b, bool i) -> sdl3::Font * {
		for (const Variant &v : f->variants)
			if ((v.weight >= 600) == b && (v.style == FontStyle::Italic) == i)
				return v.font;
		return nullptr;
	};
	sdl3::Font *font = nullptr;
	if (bold && italic)
		font = pick(true, true);
	if (!font && bold)
		font = pick(true, false);
	if (!font && italic)
		font = pick(false, true);
	if (!font)
		font = pick(false, false);
	if (!font)
		font = f->variants.front().font;
	return Sized(font, size);
}

sdl3::FPoint Fonts::Measure(sdl3::Font *font, const String &line, float size) {
	if (!font)
		return {float(DisplayCells(line)) * CharWidthApprox(size), LineHeightApprox(size)};
	MeasureKey key{font, line};
	if (auto it = m_measures.find(key); it != m_measures.end())
		return it->second;
	sdl3::FPoint out{float(DisplayCells(line)) * CharWidthApprox(size), LineHeightApprox(size)};
	if (auto sz = font->Measure(line); sz.IsSome())
		out = {float(sz.Unwrap().x), float(sz.Unwrap().y)};
	if (out.y <= 0.f)
		out.y = float(font->Height());
	if (m_measures.size() > 50000)
		m_measures.clear();
	m_measures.emplace(std::move(key), out);
	return out;
}

LineMeasure Fonts::Measurer(StringView family, float size, int weight, FontStyle style) {
	sdl3::Font *font = Resolve(family, size, weight, style);
	if (!font)
		return HeuristicMeasure(size);
	return [this, font, size](const String &line) { return Measure(font, line, size); };
}

float Fonts::CellWidth(StringView family, float size) {
	sdl3::Font *font = Resolve(family, size);
	if (!font)
		return CharWidthApprox(size);
	if (auto it = m_cellWidths.find(font); it != m_cellWidths.end())
		return it->second;
	// Largeur moyenne mesurée sur une longue chaîne : exacte pour une chasse
	// fixe, approchée pour une police proportionnelle.
	const String sample("MMMMMMMMMMiiiiiiiiii0000000000");
	const float w = Measure(font, sample, size).x / float(sample.size());
	m_cellWidths[font] = w;
	return w;
}

void Fonts::ClearCaches() {
	m_measures.clear();
	m_cellWidths.clear();
	m_sized.clear();
}

} // namespace ui
