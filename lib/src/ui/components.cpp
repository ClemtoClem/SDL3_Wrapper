// Définitions de ui/components.hpp — fichier généré par splitter.py : le code
// vient tel quel de l'en-tête (seules les signatures sont réécrites).

#include "ui/components.hpp"

namespace ui {

// ── UiRect ───────────────────────────────────────────────────────────────────

sdl3::FPoint UiRect::ViewSize() const noexcept {
	return {sdl3::Max(0.f, size.x - gutter.x), sdl3::Max(0.f, size.y - gutter.y)};
}

sdl3::FPoint UiRect::ContentSize() const noexcept {
	auto v = ViewSize();
	return {sdl3::Max(content.x, v.x), sdl3::Max(content.y, v.y)};
}

sdl3::FPoint UiRect::MaxScroll() const noexcept {
	auto c = ContentSize();
	auto v = ViewSize();
	auto axis = [](float over) { return over > OVERFLOW_EPSILON ? over : 0.f; };
	return {axis(c.x - v.x), axis(c.y - v.y)};
}

sdl3::FPoint UiRect::GutterFor(sdl3::FPoint c, sdl3::FPoint box) noexcept {
	constexpr float T = SCROLLBAR_THICKNESS, E = OVERFLOW_EPSILON;
	bool v = c.y > box.y + E, h = c.x > box.x + E;
	if (v && !h)
		h = c.x > box.x - T + E;
	if (h && !v)
		v = c.y > box.y - T + E;
	// Boîte trop petite pour loger une barre : on n'en réserve pas.
	if (box.x <= 2.f * T)
		v = false;
	if (box.y <= 2.f * T)
		h = false;
	return {v ? T : 0.f, h ? T : 0.f};
}

bool UiRect::UpdateGutter() noexcept {
	sdl3::FPoint g = GutterFor(content, size);
	bool changed = g.x != gutter.x || g.y != gutter.y;
	gutter = g;
	return changed;
}

void UiRect::ClampScroll() noexcept {
	auto m = MaxScroll();
	scroll.x = sdl3::Clamp(scroll.x, 0.f, m.x);
	scroll.y = sdl3::Clamp(scroll.y, 0.f, m.y);
}

sdl3::FRect UiRect::ResolveIn(sdl3::FPoint origin, float w, float h) const noexcept {
	auto f = AnchorFactors(anchor);
	return {origin.x + f.x * w - f.x * size.x + offset.x, origin.y + f.y * h - f.y * size.y + offset.y, size.x,
			size.y};
}

TextOverflow TextOverflowOf(const ecs::ArchetypeRegistry &world, ecs::Entity e) noexcept {
	if (auto o = world.GetComponent<UiTextOverflow>(e); o.IsSome())
		return o.Unwrap()->mode;
	return TextOverflow::CLIP;
}

// ── UiSlider ─────────────────────────────────────────────────────────────────

float UiSlider::Snap(float v) const noexcept {
	if (step > 0.f)
		v = min + sdl3::Round((v - min) / step) * step;
	return sdl3::Clamp(v, min, max);
}

// ── UiProgress ───────────────────────────────────────────────────────────────

float UiProgress::Normalized() const noexcept {
	return (max > min) ? sdl3::Clamp((value - min) / (max - min), 0.f, 1.f) : 0.f;
}

// ── UiScrollBar ──────────────────────────────────────────────────────────────

float UiScrollBar::ThumbRatio() const noexcept {
	return contentSize > 0.f ? sdl3::Clamp(viewSize / contentSize, 0.05f, 1.f) : 1.f;
}

float UiScrollBar::Normalized() const noexcept {
	float m = MaxOffset();
	return m > 0.f ? offset / m : 0.f;
}

// ── UiKnob ───────────────────────────────────────────────────────────────────

float UiKnob::Normalized() const noexcept {
	return (max > min) ? sdl3::Clamp((value - min) / (max - min), 0.f, 1.f) : 0.f;
}

float UiKnob::Snap(float v) const noexcept {
	if (step > 0.f)
		v = min + sdl3::Round((v - min) / step) * step;
	return sdl3::Clamp(v, min, max);
}

// ── UiComboBox ───────────────────────────────────────────────────────────────

sdl3::FRect UiComboBox::DropdownRect(const sdl3::FRect &screen) const noexcept {
	return {screen.x, screen.y + screen.h + 2.f, screen.w, itemHeight * float(items.size())};
}

// ── UiDragValue ──────────────────────────────────────────────────────────────

float UiDragValue::Snap(float v) const noexcept {
	if (step > 0.f)
		v = min + sdl3::Round((v - min) / step) * step;
	return sdl3::Clamp(v, min, max);
}

sdl3::FColor HsvToColor(float h, float s, float v, float a) noexcept {
	h = sdl3::Fmod(sdl3::Fmod(h, 360.f) + 360.f, 360.f);
	s = sdl3::Clamp(s, 0.f, 1.f);
	v = sdl3::Clamp(v, 0.f, 1.f);
	float cVal = v * s;
	float x = cVal * (1.f - sdl3::Abs(sdl3::Fmod(h / 60.f, 2.f) - 1.f));
	float m = v - cVal;
	float r = 0.f, g = 0.f, b = 0.f;
	if (h < 60.f) {
		r = cVal;
		g = x;
	} else if (h < 120.f) {
		r = x;
		g = cVal;
	} else if (h < 180.f) {
		g = cVal;
		b = x;
	} else if (h < 240.f) {
		g = x;
		b = cVal;
	} else if (h < 300.f) {
		r = x;
		b = cVal;
	} else {
		r = cVal;
		b = x;
	}
	return sdl3::FColor{r + m, g + m, b + m, a};
}

HSV ColorToHsv(sdl3::FColor c) noexcept {
	float r = c.r, g = c.g, b = c.b;
	float mx = sdl3::Max(r, sdl3::Max(g, b));
	float mn = sdl3::Min(r, sdl3::Min(g, b));
	float d = mx - mn;
	float h = 0.f;
	if (d > 0.0001f) {
		if (mx == r)
			h = 60.f * sdl3::Fmod((g - b) / d, 6.f);
		else if (mx == g)
			h = 60.f * ((b - r) / d + 2.f);
		else
			h = 60.f * ((r - g) / d + 4.f);
	}
	if (h < 0.f)
		h += 360.f;
	float s = mx > 0.0001f ? d / mx : 0.f;
	return {h, s, mx};
}

String ColorToHex(sdl3::FColor c) {
	sdl3::Color b = c.ToByte();
	return String::Format("#%02X%02X%02X%02X", b.r, b.g, b.b, b.a);
}

sdl3::FColor HexToColor(const String &hex, sdl3::FColor fallback) {
	String h = hex;
	if (!h.IsEmpty() && h[0] == '#')
		h = h.Substr(1);
	if (h.size() != 6 && h.size() != 8)
		return fallback;
	auto hexByte = [&h](size_t i) -> uint8_t { return uint8_t(std::strtol(h.Substr(i, 2).c_str(), nullptr, 16)); };
	sdl3::FColor c = fallback;
	c.r = hexByte(0) / 255.f;
	c.g = hexByte(2) / 255.f;
	c.b = hexByte(4) / 255.f;
	c.a = (h.size() == 8 ? hexByte(6) : uint8_t(255)) / 255.f;
	return c;
}

// ── UiTable ──────────────────────────────────────────────────────────────────

float UiTable::TotalWidth() const noexcept {
	float w = 0.f;
	for (const auto &c : columns)
		w += c.width;
	return w;
}

float UiTable::ColumnX(int col) const noexcept {
	float x = 0.f;
	for (int i = 0; i < col && i < int(columns.size()); ++i)
		x += columns[size_t(i)].width;
	return x;
}

void SetParent(ecs::ArchetypeRegistry &world, ecs::Entity child, ecs::Entity parent) {
	world.AddComponent(child, UiParent{parent});
	auto children = world.GetOrAddComponent<UiChildren>(parent);
	if (children.IsSome())
		children.Unwrap()->list.push_back(child);
}

void DespawnTree(ecs::ArchetypeRegistry &world, ecs::Entity e) {
	if (auto children = world.GetComponent<UiChildren>(e); children.IsSome()) {
		// Copie : despawn migre/modifie les archétypes pendant l'itération.
		std::vector<ecs::Entity> kids = children.Unwrap()->list;
		for (ecs::Entity c : kids)
			DespawnTree(world, c);
	}
	world.Despawn(e);
}

Option<ecs::Entity> FindByName(ecs::ArchetypeRegistry &world, const String &name) {
	Option<ecs::Entity> found = NONE;
	world.Query<UiName>([&](ecs::Entity e, UiName &n) {
		if (found.IsNone() && n.name == name)
			found = Some(e);
	});
	return found;
}

bool IsHiddenRecursive(ecs::ArchetypeRegistry &world, ecs::Entity e) {
	ecs::Entity cur = e;
	while (cur.Valid()) {
		if (world.HasComponent<UiHidden>(cur))
			return true;
		auto p = world.GetComponent<UiParent>(cur);
		if (p.IsNone())
			break;
		cur = p.Unwrap()->parent;
	}
	return false;
}

bool IsDisabledRecursive(ecs::ArchetypeRegistry &world, ecs::Entity e) {
	ecs::Entity cur = e;
	while (cur.Valid()) {
		if (world.HasComponent<UiDisabled>(cur))
			return true;
		auto p = world.GetComponent<UiParent>(cur);
		if (p.IsNone())
			break;
		cur = p.Unwrap()->parent;
	}
	return false;
}

bool IsDescendantOrSelf(ecs::ArchetypeRegistry &world, ecs::Entity e, ecs::Entity ancestor) {
	ecs::Entity cur = e;
	while (cur.Valid()) {
		if (cur == ancestor)
			return true;
		auto p = world.GetComponent<UiParent>(cur);
		if (p.IsNone())
			break;
		cur = p.Unwrap()->parent;
	}
	return false;
}

// ── HitTestIndex ─────────────────────────────────────────────────────────────

void HitTestIndex::Refresh(ecs::ArchetypeRegistry &world, uint64_t layoutPass) {
	const size_t entities = world.EntityCount();
	if (m_built && layoutPass == m_layoutPass && entities == m_entityCount)
		return;
	Rebuild(world);
	m_layoutPass = layoutPass;
	m_entityCount = entities;
	m_built = true;
	++m_revision;
}

ecs::Entity HitTestIndex::TopMostAt(ecs::ArchetypeRegistry &world, sdl3::FPoint p) const {
	// Listes déroulantes ouvertes : dessinées après tout le reste.
	for (auto it = m_combos.rbegin(); it != m_combos.rend(); ++it) {
		auto cb = world.GetComponent<UiComboBox>(it->entity);
		if (cb.IsNone() || !cb.Unwrap()->open)
			continue;
		if (!cb.Unwrap()->DropdownRect(it->screen).Contains(p))
			continue;
		if (IsHiddenRecursive(world, it->entity))
			continue;
		return it->entity;
	}
	// Puis les widgets, du dernier dessiné au premier.
	for (auto it = m_nodes.rbegin(); it != m_nodes.rend(); ++it) {
		if (!it->screen.Contains(p) || !it->clip.Contains(p))
			continue;
		if (world.HasComponent<UiPointerThrough>(it->entity))
			continue; // visible mais transparent au pointeur
		if (IsHiddenRecursive(world, it->entity))
			continue;
		return it->entity;
	}
	return ecs::Entity{};
}

void HitTestIndex::Rebuild(ecs::ArchetypeRegistry &world) {
	m_nodes.clear();
	m_combos.clear();

	// Arbres normaux d'abord (cf. RenderSystem::Run) — même requête, donc
	// même ordre d'itération que le dessin.
	std::vector<ecs::Entity> roots;
	world.Query<UiRect>([&](ecs::Entity e, UiRect &) {
		if (!world.HasComponent<UiParent>(e))
			roots.push_back(e);
	});
	for (ecs::Entity root : roots)
		Append(world, root);

	// Puis les overlays FIXED, triés par UiOverlayLayer::order.
	std::vector<std::pair<int, ecs::Entity>> fixedRoots;
	world.Query<UiItem, UiComputed>([&](ecs::Entity e, UiItem &it, UiComputed &) {
		if (it.attach != AttachLayout::FIXED)
			return;
		int order = 0;
		if (auto ol = world.GetComponent<UiOverlayLayer>(e); ol.IsSome())
			order = ol.Unwrap()->order;
		fixedRoots.push_back({order, e});
	});
	std::stable_sort(fixedRoots.begin(), fixedRoots.end(),
					 [](const auto &a, const auto &b) { return a.first < b.first; });
	for (auto &[order, e] : fixedRoots)
		Append(world, e, /*overlayEntry=*/true);
}

void HitTestIndex::Append(ecs::ArchetypeRegistry &world, ecs::Entity e, bool overlayEntry) {
	if (!overlayEntry) {
		if (auto it = world.GetComponent<UiItem>(e); it.IsSome() && it.Unwrap()->attach == AttachLayout::FIXED)
			return; // dessiné par la passe overlay, pas ici
	}
	auto comp = world.GetComponent<UiComputed>(e);
	if (comp.IsNone())
		return;
	const UiComputed c = *comp.Unwrap();
	if (c.clip.w <= 0.f || c.clip.h <= 0.f)
		return;

	m_nodes.push_back(Node{e, c.screen, c.clip});
	if (world.HasComponent<UiComboBox>(e))
		m_combos.push_back(Node{e, c.screen, c.clip});

	if (auto children = world.GetComponent<UiChildren>(e); children.IsSome()) {
		// Liste lue EN PLACE (pas de copie, contrairement à
		// RenderSystem::DrawTree) : cette descente ne fait que lire des
		// composants, aucune migration d'archétype ne peut donc invalider
		// la référence pendant la récursion.
		const std::vector<ecs::Entity> &kids = children.Unwrap()->list;
		for (ecs::Entity kid : kids)
			Append(world, kid);
	}
}

ecs::Entity HitTestTopMost(ecs::ArchetypeRegistry &world, sdl3::FPoint p) {
	HitTestIndex index;
	index.Refresh(world, 0);
	return index.TopMostAt(world, p);
}

void SetEnabled(ecs::ArchetypeRegistry &world, ecs::Entity e, bool enabled) {
	if (enabled)
		world.RemoveComponent<UiDisabled>(e);
	else if (!world.HasComponent<UiDisabled>(e))
		world.AddComponent(e, UiDisabled{});
}

} // namespace ui
