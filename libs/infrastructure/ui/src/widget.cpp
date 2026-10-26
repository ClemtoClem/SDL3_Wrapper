// Définitions de ui/widget.hpp
#include <ui/builder.hpp>
#include <ui/systems.hpp>

#include <algorithm>

namespace ui {

namespace {

StyleProps &Dummy() {
	static StyleProps dummy;
	dummy = StyleProps{};
	return dummy;
}

void Collect(const ecs::ArchetypeRegistry &world, ecs::Entity e, const std::vector<Selector> &selectors,
			 std::vector<Widget> &out, ecs::ArchetypeRegistry &mutableWorld, bool firstOnly) {
	for (const Selector &s : selectors)
		if (MatchesSelector(world, e, s)) {
			out.emplace_back(mutableWorld, e);
			break;
		}
	if (firstOnly && !out.empty())
		return;
	for (ecs::Entity c : ChildList(world, e)) {
		Collect(world, c, selectors, out, mutableWorld, firstOnly);
		if (firstOnly && !out.empty())
			return;
	}
}

ecs::Entity FindWith(const ecs::ArchetypeRegistry &world, ecs::Entity e, bool (*pred)(const ecs::ArchetypeRegistry &, ecs::Entity)) {
	if (pred(world, e))
		return e;
	for (ecs::Entity c : ChildList(world, e))
		if (ecs::Entity f = FindWith(world, c, pred); f.Valid())
			return f;
	return ecs::Entity{};
}

} // namespace

// ── Identité ────────────────────────────────────────────────────────────────

String Widget::Type() const {
	if (Valid())
		if (auto el = m_world->GetComponent<Element>(m_entity); el.IsSome())
			return el.Unwrap()->type;
	return String();
}

String Widget::Id() const {
	if (Valid())
		if (auto el = m_world->GetComponent<Element>(m_entity); el.IsSome())
			return el.Unwrap()->id;
	return String();
}

Widget &Widget::SetId(String id) {
	if (Valid())
		if (auto el = m_world->GetComponent<Element>(m_entity); el.IsSome()) {
			el.Unwrap()->id = std::move(id);
			MarkStyleDirty(*m_world, m_entity);
		}
	return *this;
}

bool Widget::HasClass(StringView name) const {
	if (Valid())
		if (auto el = m_world->GetComponent<Element>(m_entity); el.IsSome())
			return el.Unwrap()->HasClass(name);
	return false;
}

Widget &Widget::AddClass(String name) {
	if (Valid())
		if (auto el = m_world->GetComponent<Element>(m_entity); el.IsSome() && !el.Unwrap()->HasClass(name.View())) {
			el.Unwrap()->classes.push_back(std::move(name));
			MarkStyleDirty(*m_world, m_entity);
		}
	return *this;
}

Widget &Widget::RemoveClass(StringView name) {
	if (Valid())
		if (auto el = m_world->GetComponent<Element>(m_entity); el.IsSome()) {
			std::erase_if(el.Unwrap()->classes, [&](const String &c) { return c.View() == name; });
			MarkStyleDirty(*m_world, m_entity);
		}
	return *this;
}

bool Widget::ToggleClass(String name) {
	if (HasClass(name.View())) {
		RemoveClass(name.View());
		return false;
	}
	AddClass(std::move(name));
	return true;
}

// ── Arbre ───────────────────────────────────────────────────────────────────

Option<Widget> Widget::Parent() const {
	if (!Valid())
		return NONE;
	if (auto p = ecs::ParentOf(*m_world, m_entity); p.IsSome())
		return Some(Widget(*m_world, p.Unwrap()));
	return NONE;
}

std::vector<Widget> Widget::Children() const {
	std::vector<Widget> out;
	if (Valid())
		for (ecs::Entity c : ChildList(*m_world, m_entity))
			out.emplace_back(*m_world, c);
	return out;
}

size_t Widget::ChildCount() const { return Valid() ? ChildList(*m_world, m_entity).size() : 0; }

namespace {
/// Le corps d'un widget composé (enfant direct marqué ContentSlot), sinon lui-même.
ecs::Entity ContentTarget(const ecs::ArchetypeRegistry &world, ecs::Entity e) {
	for (ecs::Entity c : ChildList(world, e))
		if (world.HasComponent<ContentSlot>(c))
			return c;
	return e;
}
} // namespace

Widget Widget::Append(const AnyWidget &child) {
	if (!Valid())
		return Widget();
	return child.Build(*m_world, ContentTarget(*m_world, m_entity));
}

Widget Widget::Insert(size_t index, const AnyWidget &child) {
	if (!Valid())
		return Widget();
	return child.Build(*m_world, ContentTarget(*m_world, m_entity), Some(index));
}

bool Widget::MoveTo(Widget parent) {
	if (!Valid() || !parent.Valid() || parent.m_world != m_world)
		return false;
	const Option<ecs::Entity> old = ecs::ParentOf(*m_world, m_entity);
	if (!ecs::SetParent(*m_world, m_entity, ContentTarget(*m_world, parent.m_entity)))
		return false;
	if (old.IsSome())
		MarkStyleDirty(*m_world, old.Unwrap());
	MarkStyleDirty(*m_world, m_entity);
	return true;
}

bool Widget::Contains(const Widget &other) const {
	if (!Valid() || !other.Valid() || other.m_world != m_world)
		return false;
	for (Option<ecs::Entity> up = Some(other.m_entity); up.IsSome(); up = ecs::ParentOf(*m_world, up.Unwrap()))
		if (up.Unwrap() == m_entity)
			return true;
	return false;
}

Widget &Widget::Clear() {
	if (!Valid())
		return *this;
	const ecs::Entity target = ContentTarget(*m_world, m_entity);
	const std::vector<ecs::Entity> children = ChildList(*m_world, target);
	for (ecs::Entity c : children)
		Widget(*m_world, c).Destroy();
	MarkStyleDirty(*m_world, target);
	return *this;
}

void Widget::Destroy() {
	if (!Valid())
		return;
	Context *ctx = ContextOf(*m_world);
	if (ctx) {
		// Popups ouverts dans le sous-arbre : fermés proprement d'abord.
		const std::vector<ecs::Entity> popups = ctx->input.popups;
		for (ecs::Entity p : popups)
			if (m_world->IsAlive(p) && IsDescendantOrSelf(*m_world, p, m_entity))
				ClosePopup(*m_world, p);
		if (ctx->input.focus.Valid() && IsDescendantOrSelf(*m_world, ctx->input.focus, m_entity))
			ClearFocus(*m_world);
	}
	const Option<ecs::Entity> parent = ecs::ParentOf(*m_world, m_entity);
	(void)ecs::DespawnRecursive(*m_world, m_entity);
	if (ctx) {
		std::erase_if(ctx->input.popups, [&](ecs::Entity p) { return !m_world->IsAlive(p); });
		std::erase_if(ctx->input.hoverChain, [&](ecs::Entity p) { return !m_world->IsAlive(p); });
		if (parent.IsSome())
			ctx->MarkStyleDirty(parent.Unwrap());
		ctx->MarkLayoutDirty();
	}
	m_entity = ecs::Entity{};
}

Option<Widget> Widget::Find(StringView selector) const {
	if (!Valid())
		return NONE;
	std::vector<Widget> out;
	Collect(*m_world, m_entity, ParseSelectorList(selector), out, *m_world, true);
	if (out.empty())
		return NONE;
	return Some(out.front());
}

std::vector<Widget> Widget::FindAll(StringView selector) const {
	std::vector<Widget> out;
	if (Valid())
		Collect(*m_world, m_entity, ParseSelectorList(selector), out, *m_world, false);
	return out;
}

Option<Widget> Widget::FindType(StringView type) const {
	if (!Valid())
		return NONE;
	std::vector<ecs::Entity> stack{m_entity};
	while (!stack.empty()) {
		const ecs::Entity e = stack.back();
		stack.pop_back();
		if (auto el = m_world->GetComponent<Element>(e); el.IsSome() && el.Unwrap()->type.View() == type)
			return Some(Widget(*m_world, e));
		const std::vector<ecs::Entity> &kids = ChildList(*m_world, e);
		for (auto it = kids.rbegin(); it != kids.rend(); ++it)
			stack.push_back(*it);
	}
	return NONE;
}

// ── Visibilité, activation ──────────────────────────────────────────────────

Widget &Widget::Show() { return SetVisible(true); }
Widget &Widget::Hide() { return SetVisible(false); }

Widget &Widget::SetVisible(bool visible) {
	if (!Valid() || visible == !m_world->HasComponent<Hidden>(m_entity))
		return *this;
	if (visible)
		m_world->RemoveComponent<Hidden>(m_entity);
	else
		m_world->AddComponent(m_entity, Hidden{});
	MarkLayoutDirty(*m_world);
	return *this;
}

bool Widget::IsVisible() const { return Valid() && !IsHiddenRecursive(*m_world, m_entity); }

Widget &Widget::Enable() { return SetEnabled(true); }
Widget &Widget::Disable() { return SetEnabled(false); }

Widget &Widget::SetEnabled(bool enabled) {
	if (!Valid() || enabled == !m_world->HasComponent<Disabled>(m_entity))
		return *this;
	if (enabled)
		m_world->RemoveComponent<Disabled>(m_entity);
	else
		m_world->AddComponent(m_entity, Disabled{});
	MarkStyleDirty(*m_world, m_entity);
	return *this;
}

bool Widget::IsEnabled() const { return Valid() && !IsDisabledRecursive(*m_world, m_entity); }

// ── Style ───────────────────────────────────────────────────────────────────

StyleProps &Widget::Props() {
	if (!Valid())
		return Dummy();
	if (!m_world->HasComponent<InlineStyle>(m_entity))
		m_world->AddComponent(m_entity, InlineStyle{});
	MarkStyleDirty(*m_world, m_entity);
	return m_world->GetComponent<InlineStyle>(m_entity).Unwrap()->own;
}

Widget &Widget::SetStyle(const Style &style) {
	Props().Merge(style.Props());
	return *this;
}

Widget &Widget::Cascade(const Style &style) {
	if (!Valid())
		return *this;
	(void)Props();
	m_world->GetComponent<InlineStyle>(m_entity).Unwrap()->shared.Merge(style.Props());
	return *this;
}

Widget &Widget::ClearStyle() {
	if (Valid())
		if (auto inl = m_world->GetComponent<InlineStyle>(m_entity); inl.IsSome()) {
			*inl.Unwrap() = InlineStyle{};
			MarkStyleDirty(*m_world, m_entity);
		}
	return *this;
}

Widget &Widget::SetProperty(StringView property, StringView value) {
	(void)SetDeclaration(Props(), property, value);
	return *this;
}

Result<bool, String> Widget::Import(const String &path) {
	if (!Valid())
		return Err(String("widget invalide"));
	Context *ctx = ContextOf(*m_world);
	Result<StyleSheetPtr, String> sheet = ctx ? ctx->LoadSheet(path) : Result<StyleSheetPtr, String>(Err(String("sans contexte")));
	if (!ctx) {
		auto loaded = StyleSheet::Load(path);
		if (loaded.IsError())
			return Err(loaded.Error());
		sheet = Ok(StyleSheetPtr(loaded.Value()));
	}
	if (sheet.IsError())
		return Err(sheet.Error());
	AddStyleSheet(sheet.Value());
	return Ok(true);
}

Widget &Widget::AddStyleSheet(StyleSheetPtr sheet) {
	if (!Valid() || !sheet)
		return *this;
	if (!m_world->HasComponent<StyleScope>(m_entity))
		m_world->AddComponent(m_entity, StyleScope{});
	m_world->GetComponent<StyleScope>(m_entity).Unwrap()->sheets.push_back(std::move(sheet));
	MarkStyleDirty(*m_world, m_entity);
	return *this;
}

const ComputedStyle *Widget::Computed() const {
	if (Valid())
		if (auto cs = m_world->GetComponent<ComputedStyle>(m_entity); cs.IsSome())
			return cs.Unwrap().Get();
	return nullptr;
}

sdl3::FRect Widget::Bounds() const {
	if (Valid())
		if (auto r = m_world->GetComponent<Rect>(m_entity); r.IsSome())
			return r.Unwrap()->box;
	return {};
}

sdl3::FRect Widget::ContentBounds() const {
	if (Valid())
		if (auto r = m_world->GetComponent<Rect>(m_entity); r.IsSome())
			return r.Unwrap()->content;
	return {};
}

State Widget::GetState() const {
	if (Valid())
		if (auto s = m_world->GetComponent<WidgetState>(m_entity); s.IsSome())
			return s.Unwrap()->flags;
	return State::None;
}

// ── Contenu et valeurs ──────────────────────────────────────────────────────

namespace {

/// Le widget lui-même s'il porte du texte, sinon son libellé interne.
ecs::Entity TextTarget(const ecs::ArchetypeRegistry &world, ecs::Entity e) {
	if (world.HasComponent<TextContent>(e) || world.HasComponent<TextEditor>(e))
		return e;
	const ecs::Entity slot =
		FindWith(world, e, [](const ecs::ArchetypeRegistry &w, ecs::Entity x) { return w.HasComponent<TextSlot>(x); });
	return slot.Valid() ? slot : e;
}

} // namespace

String Widget::Text() const {
	if (Valid())
		if (auto t = m_world->GetComponent<TextContent>(TextTarget(*m_world, m_entity)); t.IsSome())
			return t.Unwrap()->value;
	return String();
}

Widget &Widget::SetText(String text) {
	if (Valid())
		SetTextValue(*m_world, TextTarget(*m_world, m_entity), std::move(text), false);
	return *this;
}

Widget &Widget::SetImage(String source) {
	if (Valid()) {
		m_world->AddComponent(m_entity, ImageContent{std::move(source)});
		MarkLayoutDirty(*m_world);
	}
	return *this;
}

Widget &Widget::SetTooltip(String text) {
	if (!Valid())
		return *this;
	if (text.IsEmpty())
		m_world->RemoveComponent<ui::Tooltip>(m_entity);
	else
		m_world->AddComponent(m_entity, ui::Tooltip{std::move(text)});
	return *this;
}

float Widget::Value() const {
	if (Valid())
		if (auto r = m_world->GetComponent<Range>(m_entity); r.IsSome())
			return r.Unwrap()->value;
	return 0.f;
}

Widget &Widget::SetValue(float value, bool notify) {
	if (Valid())
		SetRangeValue(*m_world, m_entity, value, notify);
	return *this;
}

Widget &Widget::SetRange(float min, float max) {
	if (Valid())
		if (auto r = m_world->GetComponent<Range>(m_entity); r.IsSome()) {
			r.Unwrap()->min = min;
			r.Unwrap()->max = max;
			const float v = r.Unwrap()->value;
			SetRangeValue(*m_world, m_entity, v, false);
		}
	return *this;
}

bool Widget::Checked() const {
	if (Valid())
		if (auto c = m_world->GetComponent<Check>(m_entity); c.IsSome())
			return c.Unwrap()->on;
	return false;
}

Widget &Widget::SetChecked(bool checked, bool notify) {
	if (Valid())
		SetCheckState(*m_world, m_entity, checked, notify);
	return *this;
}

int Widget::Selected() const {
	if (Valid())
		if (auto s = m_world->GetComponent<Selection>(m_entity); s.IsSome())
			return s.Unwrap()->First();
	return -1;
}

std::vector<int> Widget::SelectedAll() const {
	if (Valid())
		if (auto s = m_world->GetComponent<Selection>(m_entity); s.IsSome())
			return s.Unwrap()->selected;
	return {};
}

Widget &Widget::SetSelected(int index, bool notify) {
	if (Valid())
		SetSelection(*m_world, m_entity, index, notify);
	return *this;
}

Widget &Widget::SetItems(std::vector<String> items) {
	if (!Valid())
		return *this;
	const ecs::Entity host =
		FindWith(*m_world, m_entity, [](const ecs::ArchetypeRegistry &w, ecs::Entity e) { return w.HasComponent<ItemsHost>(e); });
	if (host.Valid()) {
		const ItemsHost make = *m_world->GetComponent<ItemsHost>(host).Unwrap();
		Widget h(*m_world, host);
		h.Clear();
		for (size_t i = 0; i < items.size(); ++i)
			(void)h.Append(make.make(items[i], int(i)));
	}
	// Libellés liés à la sélection (texte d'un menu déroulant).
	std::vector<ecs::Entity> stack{m_entity};
	while (!stack.empty()) {
		const ecs::Entity e = stack.back();
		stack.pop_back();
		if (auto st = m_world->GetComponent<SelectionText>(e); st.IsSome())
			st.Unwrap()->items = items;
		for (ecs::Entity c : ChildList(*m_world, e))
			stack.push_back(c);
	}
	if (auto sel = m_world->GetComponent<Selection>(m_entity); sel.IsSome()) {
		const int first = sel.Unwrap()->First();
		SetSelection(*m_world, m_entity, first >= 0 && size_t(first) < items.size() ? first : -1, false);
	}
	MarkStyleDirty(*m_world, m_entity);
	return *this;
}

ui::Color Widget::ColorValue() const {
	if (Valid())
		if (auto c = m_world->GetComponent<ColorModel>(m_entity); c.IsSome())
			return c.Unwrap()->value;
	return ui::Color{};
}

Widget &Widget::SetColorValue(ui::Color c, bool notify) {
	if (!Valid())
		return *this;
	if (auto m = m_world->GetComponent<ColorModel>(m_entity); m.IsSome())
		m.Unwrap()->value = c;
	else
		m_world->AddComponent(m_entity, ColorModel{c});
	MarkStyleDirty(*m_world, m_entity);
	WidgetEvent ev;
	ev.trigger = Trigger::Of(InputSource::Custom);
	ev.text = "color-sync";
	DispatchEvent(*m_world, m_entity, ev, false);
	if (notify) {
		WidgetEvent change;
		change.trigger = Trigger::Of(InputSource::Change);
		change.value = c.a;
		DispatchEvent(*m_world, m_entity, change, false);
	}
	return *this;
}

// ── Popups ──────────────────────────────────────────────────────────────────

namespace {

ecs::Entity PopupEntity(const ecs::ArchetypeRegistry &world, ecs::Entity e) {
	return FindWith(world, e, [](const ecs::ArchetypeRegistry &w, ecs::Entity x) { return w.HasComponent<PopupState>(x); });
}

} // namespace

Widget &Widget::Open() {
	if (Valid())
		if (ecs::Entity p = PopupEntity(*m_world, m_entity); p.Valid())
			OpenPopup(*m_world, p);
	return *this;
}

Widget &Widget::OpenAt(sdl3::FPoint at) {
	if (Valid())
		if (ecs::Entity p = PopupEntity(*m_world, m_entity); p.Valid())
			OpenPopupAt(*m_world, p, at);
	return *this;
}

Widget &Widget::OpenBelow(const Widget &anchor) {
	if (Valid())
		if (ecs::Entity p = PopupEntity(*m_world, m_entity); p.Valid()) {
			if (auto popup = m_world->GetComponent<PopupState>(p); popup.IsSome())
				popup.Unwrap()->placement = Placement::Below;
			OpenPopup(*m_world, p, anchor.Entity());
		}
	return *this;
}

Widget &Widget::Close() {
	if (Valid())
		if (ecs::Entity p = PopupEntity(*m_world, m_entity); p.Valid())
			ClosePopupChain(*m_world, p);
	return *this;
}

bool Widget::IsOpen() const {
	if (!Valid())
		return false;
	const ecs::Entity p = PopupEntity(*m_world, m_entity);
	return p.Valid() && IsPopupOpen(*m_world, p);
}

// ── Focus, défilement ───────────────────────────────────────────────────────

Widget &Widget::Focus() {
	if (Valid())
		SetFocus(*m_world, m_entity, true);
	return *this;
}

Widget &Widget::Blur() {
	if (Valid() && FocusedWidget(*m_world) == m_entity)
		ClearFocus(*m_world);
	return *this;
}

Widget &Widget::ScrollTo(sdl3::FPoint offset) {
	if (!Valid())
		return *this;
	if (!m_world->HasComponent<Scroll>(m_entity))
		m_world->AddComponent(m_entity, Scroll{});
	auto s = m_world->GetComponent<Scroll>(m_entity);
	s.Unwrap()->offset = offset;
	s.Unwrap()->Clamp();
	MarkLayoutDirty(*m_world);
	return *this;
}

sdl3::FPoint Widget::ScrollOffset() const {
	if (Valid())
		if (auto s = m_world->GetComponent<Scroll>(m_entity); s.IsSome())
			return s.Unwrap()->offset;
	return {};
}

Widget &Widget::ScrollIntoView() {
	if (!Valid())
		return *this;
	const sdl3::FRect box = Bounds();
	for (Option<ecs::Entity> up = ecs::ParentOf(*m_world, m_entity); up.IsSome(); up = ecs::ParentOf(*m_world, up.Unwrap())) {
		auto s = m_world->GetComponent<Scroll>(up.Unwrap());
		auto r = m_world->GetComponent<Rect>(up.Unwrap());
		if (s.IsNone() || r.IsNone() || (s.Unwrap()->MaxOffset().x <= 0.f && s.Unwrap()->MaxOffset().y <= 0.f))
			continue;
		const sdl3::FRect view = r.Unwrap()->content;
		Scroll &sc = *s.Unwrap();
		if (box.y < view.y)
			sc.offset.y -= view.y - box.y;
		else if (box.y + box.h > view.y + view.h)
			sc.offset.y += (box.y + box.h) - (view.y + view.h);
		if (box.x < view.x)
			sc.offset.x -= view.x - box.x;
		else if (box.x + box.w > view.x + view.w)
			sc.offset.x += (box.x + box.w) - (view.x + view.w);
		sc.Clamp();
		MarkLayoutDirty(*m_world);
		break;
	}
	return *this;
}

// ── Évènements ──────────────────────────────────────────────────────────────

uint32_t Widget::AddBinding(std::vector<Trigger> triggers, EventCallback cb, bool internal) {
	if (!Valid())
		return 0;
	if (!m_world->HasComponent<Bindings>(m_entity))
		m_world->AddComponent(m_entity, Bindings{});
	return m_world->GetComponent<Bindings>(m_entity).Unwrap()->Add(std::move(triggers), std::move(cb), internal);
}

Bindings *Widget::BindingsPtr() {
	if (Valid())
		if (auto b = m_world->GetComponent<Bindings>(m_entity); b.IsSome())
			return b.Unwrap().Get();
	return nullptr;
}

void Widget::Emit(Trigger trigger, WidgetEvent event, bool bubble) {
	if (!Valid())
		return;
	event.trigger = trigger;
	DispatchEvent(*m_world, m_entity, std::move(event), bubble);
}

void Emit(Widget target, const String &name, bool bubble) {
	WidgetEvent ev;
	ev.text = name;
	target.Emit(Trigger::Of(InputSource::Custom), std::move(ev), bubble);
}

std::vector<Widget> QueryAll(ecs::ArchetypeRegistry &world, StringView selector) {
	std::vector<Widget> out;
	const std::vector<Selector> selectors = ParseSelectorList(selector);
	if (const Context *ctx = ContextOf(world); ctx && world.IsAlive(ctx->root)) {
		Collect(world, ctx->root, selectors, out, world, false);
		return out;
	}
	// Sans contexte : toutes les racines (widgets sans parent).
	world.Query<Element, ecs::Without<ecs::ChildOf>>([&](ecs::Entity e, Element &) {
		Collect(world, e, selectors, out, world, false);
	});
	return out;
}

Option<Widget> Query(ecs::ArchetypeRegistry &world, StringView selector) {
	std::vector<Widget> all = QueryAll(world, selector);
	if (all.empty())
		return NONE;
	return Some(all.front());
}

} // namespace ui
