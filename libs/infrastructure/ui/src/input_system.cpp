// InputSystem — évènements SDL → survol, focus, gestes, liaisons, comportements
#include <sdl3/misc.hpp>
#include <ui/builder.hpp>
#include <ui/geometry.hpp>
#include <ui/systems.hpp>

#include <algorithm>
#include <cmath>

namespace ui {

namespace {

using Key = sdl3::keyboard::Key;
using Keymod = sdl3::keyboard::Keymod;

InputSource SourceOf(sdl3::ButtonSource s) noexcept {
	switch (s) {
	case sdl3::ButtonSource::Mouse:
		return InputSource::MouseButton;
	case sdl3::ButtonSource::Key:
		return InputSource::Key;
	case sdl3::ButtonSource::Gamepad:
		return InputSource::GamepadButton;
	case sdl3::ButtonSource::Joystick:
		return InputSource::JoystickButton;
	case sdl3::ButtonSource::Finger:
		return InputSource::Finger;
	case sdl3::ButtonSource::Pen:
		return InputSource::PenButton;
	}
	return InputSource::Custom;
}

const ComputedStyle &StyleOf(const ecs::ArchetypeRegistry &world, ecs::Entity e) {
	if (auto cs = world.GetComponent<ComputedStyle>(e); cs.IsSome())
		return *cs.Unwrap().Get();
	return ComputedStyle::Initial();
}

/// Frère redimensionné par une poignée : le précédent, ou le suivant si `after`.
ecs::Entity ResizedSibling(const ecs::ArchetypeRegistry &world, ecs::Entity parent, ecs::Entity handle, bool after) {
	const std::vector<ecs::Entity> &sibs = ChildList(world, parent);
	auto it = std::find(sibs.begin(), sibs.end(), handle);
	if (it == sibs.end())
		return ecs::Entity{};
	if (after)
		return it + 1 == sibs.end() ? ecs::Entity{} : *(it + 1);
	return it == sibs.begin() ? ecs::Entity{} : *(it - 1);
}

void SetStateFlag(ecs::ArchetypeRegistry &world, Context &ctx, ecs::Entity e, State flag, bool on) {
	auto ws = world.GetComponent<WidgetState>(e);
	if (ws.IsNone())
		return;
	const State before = ws.Unwrap()->flags;
	ws.Unwrap()->flags = on ? (before | flag) : (before & ~flag);
	if (ws.Unwrap()->flags != before)
		ctx.MarkStyleDirty(e);
}

bool Contains(const sdl3::FRect &r, sdl3::FPoint p) noexcept {
	return p.x >= r.x && p.x < r.x + r.w && p.y >= r.y && p.y < r.y + r.h;
}

/// Accès générique aux composants sans tenir de référence au-delà d'un appel.
template <typename T> Option<T> Copy(const ecs::ArchetypeRegistry &world, ecs::Entity e) {
	if (auto c = world.GetComponent<T>(e); c.IsSome())
		return Some(T(*c.Unwrap()));
	return NONE;
}

class Input {
public:
	Input(ecs::ArchetypeRegistry &w, Context &c) : world(w), ctx(c), in(c.input) {}

	ecs::ArchetypeRegistry &world;
	Context &ctx;
	Interaction &in;

	// ── Utilitaires ─────────────────────────────────────────────────────────

	/// Le pointeur est « sur l'interface » si un widget de la chaîne visée est
	/// visible (fond, bordure, image, texte…) ou interactif : un conteneur de
	/// mise en page transparent (page, HUD plein écran) laisse passer les
	/// évènements vers l'application.
	bool Over(ecs::Entity hit) const {
		for (Option<ecs::Entity> cur = hit.Valid() ? Some(hit) : Option<ecs::Entity>(NONE); cur.IsSome();
			 cur = ecs::ParentOf(world, cur.Unwrap())) {
			const ecs::Entity e = cur.Unwrap();
			if (e == ctx.root)
				return false;
			if (IsSolid(e))
				return true;
		}
		return false;
	}

	bool IsSolid(ecs::Entity e) const {
		const ComputedStyle &cs = StyleOf(world, e);
		if (cs.backgroundColor.a > 0.f || cs.backgroundImage.kind != PaintKind::None || cs.HasBorder())
			return true;
		if (world.HasComponent<TextContent>(e) || world.HasComponent<ImageContent>(e) ||
			world.HasComponent<IconContent>(e) || world.HasComponent<Shape>(e) || world.HasComponent<CanvasPainter>(e))
			return true;
		if (world.HasComponent<Pressable>(e) || world.HasComponent<Focusable>(e) || world.HasComponent<RangeInput>(e) ||
			world.HasComponent<TextEditor>(e) || world.HasComponent<PopupState>(e) || world.HasComponent<DragSource>(e) ||
			world.HasComponent<DropTarget>(e) || world.HasComponent<MoveHandle>(e) || world.HasComponent<ResizeHandle>(e))
			return true;
		if (auto sc = world.GetComponent<Scroll>(e); sc.IsSome() && (sc.Unwrap()->gutter.x > 0.f || sc.Unwrap()->gutter.y > 0.f))
			return true;
		if (auto b = world.GetComponent<Bindings>(e); b.IsSome())
			for (const Binding &binding : b.Unwrap()->list)
				for (const Trigger &t : binding.triggers)
					if (t.source == InputSource::MouseButton || t.source == InputSource::Wheel ||
						t.source == InputSource::Finger || t.source == InputSource::PenButton ||
						t.source == InputSource::ContextMenu || t.source == InputSource::PointerEnter ||
						t.source == InputSource::PointerLeave || t.source == InputSource::PointerMove)
						return true;
		for (const Extension &ext : ctx.extensions)
			if (ext.measure && ext.measure(world, ctx, e, cs).IsSome())
				return true;
		return false;
	}

	WidgetEvent MakeEvent(const Trigger &t, const sdl3::Event *raw) const {
		WidgetEvent ev;
		ev.world = &world;
		ev.trigger = t;
		ev.raw = raw;
		ev.pointer = in.pointer;
		ev.mods = uint16_t(SDL_GetModState());
		return ev;
	}

	void Emit(ecs::Entity target, Trigger t, WidgetEvent ev, bool bubble) {
		ev.world = &world;
		ev.trigger = t;
		ev.target = target;
		DispatchEvent(world, target, std::move(ev), bubble);
	}

	ecs::Entity TopPopup() const { return in.popups.empty() ? ecs::Entity{} : in.popups.back(); }

	ecs::Entity TopModal() const {
		for (auto it = in.popups.rbegin(); it != in.popups.rend(); ++it)
			if (auto p = world.GetComponent<PopupState>(*it); p.IsSome() && p.Unwrap()->modal)
				return *it;
		return ecs::Entity{};
	}

	/// Un widget sous une modale ouverte n'est pas atteignable.
	bool Reachable(ecs::Entity e) const {
		const ecs::Entity modal = TopModal();
		if (!modal.Valid())
			return true;
		if (IsDescendantOrSelf(world, e, modal))
			return true;
		// Popups ouverts au-dessus de la modale (menus d'un champ de la modale).
		bool after = false;
		for (ecs::Entity p : in.popups) {
			if (p == modal) {
				after = true;
				continue;
			}
			if (after && IsDescendantOrSelf(world, e, p))
				return true;
		}
		return false;
	}

	// ── Survol ──────────────────────────────────────────────────────────────

	void UpdateHover(const sdl3::Event *raw) {
		ecs::Entity hit = in.pointerInside ? HitTest(world, in.pointer) : ecs::Entity{};
		if (hit.Valid() && !Reachable(hit))
			hit = TopModal();
		std::vector<ecs::Entity> chain;
		for (Option<ecs::Entity> cur = hit.Valid() ? Some(hit) : Option<ecs::Entity>(NONE); cur.IsSome();
			 cur = ecs::ParentOf(world, cur.Unwrap()))
			chain.push_back(cur.Unwrap());
		const std::vector<ecs::Entity> old = in.hoverChain;
		in.hovered = hit;
		in.hoverChain = chain;
		in.overUi = Over(hit);
		auto contains = [](const std::vector<ecs::Entity> &v, ecs::Entity e) {
			return std::find(v.begin(), v.end(), e) != v.end();
		};
		for (ecs::Entity e : old)
			if (world.IsAlive(e) && !contains(chain, e)) {
				SetStateFlag(world, ctx, e, State::Hover, false);
				Emit(e, Trigger::PointerLeave(), MakeEvent(Trigger::PointerLeave(), raw), false);
			}
		for (auto it = chain.rbegin(); it != chain.rend(); ++it)
			if (!contains(old, *it)) {
				SetStateFlag(world, ctx, *it, State::Hover, true);
				Emit(*it, Trigger::PointerEnter(), MakeEvent(Trigger::PointerEnter(), raw), false);
			}
		if (hit != in.tooltipEntity) {
			in.tooltipEntity = hit;
			in.hoverTime = 0.f;
			in.tooltipVisible = false;
		}
		UpdateCursor();
	}

	void UpdateCursor() {
		Cursor cursor = Cursor::DEFAULT;
		if (in.drag == Interaction::Drag::Resize || in.drag == Interaction::Drag::Column)
			cursor = in.dragVertical ? Cursor::SIZE_NS : Cursor::SIZE_EW;
		else if (in.hovered.Valid())
			cursor = StyleOf(world, in.hovered).cursor;
		in.cursor = cursor;
		if (!ctx.window || cursor == ctx.appliedCursor)
			return;
		ctx.appliedCursor = cursor;
		auto it = ctx.cursors.find(int(cursor));
		if (it == ctx.cursors.end()) {
			auto created = sdl3::Cursor::FromSystem(cursor);
			if (!created)
				return;
			it = ctx.cursors.emplace(int(cursor), std::move(created.Value())).first;
		}
		(void)it->second.Set();
	}

	// ── Focus ───────────────────────────────────────────────────────────────

	std::vector<ecs::Entity> FocusOrder() const {
		std::vector<ecs::Entity> out;
		for (const PaintEntry &p : ctx.paintOrder)
			if (world.HasComponent<Focusable>(p.entity) && !IsDisabledRecursive(world, p.entity) &&
				Reachable(p.entity))
				if (auto r = world.GetComponent<Rect>(p.entity); r.IsSome() && r.Unwrap()->visible)
					out.push_back(p.entity);
		std::stable_sort(out.begin(), out.end(), [&](ecs::Entity a, ecs::Entity b) {
			const int ta = world.GetComponent<Focusable>(a).Unwrap()->tabIndex;
			const int tb = world.GetComponent<Focusable>(b).Unwrap()->tabIndex;
			return (ta == 0 ? 1 << 30 : ta) < (tb == 0 ? 1 << 30 : tb);
		});
		return out;
	}

	void MoveFocus(int direction) {
		const std::vector<ecs::Entity> order = FocusOrder();
		if (order.empty())
			return;
		auto it = std::find(order.begin(), order.end(), in.focus);
		size_t i = 0;
		if (it == order.end())
			i = direction > 0 ? 0 : order.size() - 1;
		else
			i = (size_t(it - order.begin()) + order.size() + size_t(direction > 0 ? 1 : order.size() - 1)) % order.size();
		SetFocus(world, order[i], true);
		Widget(world, order[i]).ScrollIntoView();
	}

	/// Navigation spatiale (croix directionnelle, flèches) entre widgets focalisables.
	bool MoveFocusSpatial(float dx, float dy) {
		const std::vector<ecs::Entity> order = FocusOrder();
		if (order.empty())
			return false;
		if (!in.focus.Valid() || !world.IsAlive(in.focus)) {
			SetFocus(world, order.front(), true);
			return true;
		}
		const sdl3::FRect from = Widget(world, in.focus).Bounds();
		const sdl3::FPoint c{from.x + from.w * 0.5f, from.y + from.h * 0.5f};
		ecs::Entity best{};
		float bestScore = 1.0e30f;
		for (ecs::Entity e : order) {
			if (e == in.focus)
				continue;
			const sdl3::FRect r = Widget(world, e).Bounds();
			const sdl3::FPoint p{r.x + r.w * 0.5f, r.y + r.h * 0.5f};
			const float along = (p.x - c.x) * dx + (p.y - c.y) * dy;
			if (along <= 1.f)
				continue;
			const float across = std::fabs((p.x - c.x) * dy) + std::fabs((p.y - c.y) * dx);
			const float score = along + across * 2.f;
			if (score < bestScore) {
				bestScore = score;
				best = e;
			}
		}
		if (!best.Valid())
			return false;
		SetFocus(world, best, true);
		Widget(world, best).ScrollIntoView();
		return true;
	}

	// ── Comportements par défaut (clic) ─────────────────────────────────────

	/// Actions des briques après un clic (ou une activation clavier/manette).
	void Activate(ecs::Entity target, uint16_t mods) {
		const bool ctrl = (mods & uint16_t(Keymod::CTRL)) != 0;
		const bool shift = (mods & uint16_t(Keymod::SHIFT)) != 0;
		bool handled = false;
		bool itemClicked = false;
		for (Option<ecs::Entity> cur = Some(target); cur.IsSome() && !handled; cur = ecs::ParentOf(world, cur.Unwrap())) {
			const ecs::Entity e = cur.Unwrap();
			if (IsDisabledRecursive(world, e))
				return;
			if (auto ck = Copy<Checkable>(world, e); ck.IsSome()) {
				const bool on = world.GetComponent<Check>(e).IsSome() && world.GetComponent<Check>(e).Unwrap()->on;
				SetCheckState(world, e, ck.Unwrap().mode == CheckMode::Radio ? true : !on, true);
				handled = true;
			}
			if (world.HasComponent<ToggleParent>(e)) {
				const ecs::Entity owner = AncestorWith<Check>(world, e);
				if (owner.Valid())
					SetCheckState(world, owner, !world.GetComponent<Check>(owner).Unwrap()->on, true);
				handled = true;
			}
			if (auto item = Copy<SelectItem>(world, e); item.IsSome()) {
				const ecs::Entity owner = AncestorWith<Selection>(world, e);
				if (owner.Valid()) {
					std::vector<int> before = world.GetComponent<Selection>(owner).Unwrap()->selected;
					world.GetComponent<Selection>(owner).Unwrap()->Click(item.Unwrap().index, ctrl, shift);
					MarkSelectionDirty(owner);
					WidgetEvent ev = MakeEvent(Trigger::Of(InputSource::Select), nullptr);
					ev.index = item.Unwrap().index;
					Emit(owner, Trigger::Of(InputSource::Select), ev, false);
				}
				handled = true;
				itemClicked = true;
			}
			if (auto step = Copy<RangeStep>(world, e); step.IsSome()) {
				const ecs::Entity owner = NearestWith<Range>(world, e);
				if (owner.Valid()) {
					const Range r = *world.GetComponent<Range>(owner).Unwrap();
					const float delta = r.step > 0.f ? r.step : (r.Span() / 100.f);
					SetRangeValue(world, owner, r.value + delta * step.Unwrap().direction, true);
				}
				handled = true;
			}
			if (auto trig = Copy<PopupTrigger>(world, e); trig.IsSome() && trig.Unwrap().mode != PopupOpen::ContextMenu) {
				const ecs::Entity popup = FindPopupChild(e);
				if (popup.Valid()) {
					if (IsPopupOpen(world, popup)) {
						if (trig.Unwrap().mode == PopupOpen::Click)
							ClosePopupChain(world, popup);
					} else {
						world.GetComponent<PopupState>(popup).Unwrap()->placement = trig.Unwrap().placement;
						OpenPopup(world, popup, e);
					}
				}
				handled = true;
			}
			if (auto header = Copy<TableHeader>(world, e); header.IsSome()) {
				SortTable(e, header.Unwrap().column);
				handled = true;
			}
			if (world.HasComponent<Pressable>(e))
				itemClicked = true;
			if (world.HasComponent<Pressable>(e) || world.HasComponent<Focusable>(e))
				break;
		}
		// Menus : un clic sur une entrée (hors déclencheur de sous-menu) ferme la chaîne.
		if (itemClicked) {
			const ecs::Entity popup = PopupOf(world, target);
			// Un déclencheur de sous-menu ENTRE la cible et son popup ne ferme rien.
			bool submenuEntry = false;
			for (Option<ecs::Entity> cur = Some(target); cur.IsSome() && cur.Unwrap() != popup;
				 cur = ecs::ParentOf(world, cur.Unwrap()))
				if (world.HasComponent<PopupTrigger>(cur.Unwrap()))
					submenuEntry = true;
			if (popup.Valid() && !submenuEntry)
				if (auto p = world.GetComponent<PopupState>(popup); p.IsSome() && p.Unwrap()->closeOnItemClick)
					CloseMenuChain(popup);
		}
	}

	/// Ferme la chaîne de menus de `popup` : les menus parents et les sous-menus
	/// ouverts (ceux qui se ferment sur un clic d'entrée), mais pas ce que
	/// l'action de l'entrée vient d'ouvrir au-dessus (boîte de dialogue).
	void CloseMenuChain(ecs::Entity popup) {
		auto closesOnClick = [&](ecs::Entity e) {
			auto pp = world.GetComponent<PopupState>(e);
			return pp.IsSome() && pp.Unwrap()->closeOnItemClick;
		};
		const auto at = std::find(in.popups.begin(), in.popups.end(), popup);
		if (at == in.popups.end()) {
			ClosePopup(world, popup);
			return;
		}
		size_t first = size_t(at - in.popups.begin()), last = first;
		while (first > 0 && closesOnClick(in.popups[first - 1]))
			--first;
		while (last + 1 < in.popups.size() && closesOnClick(in.popups[last + 1]))
			++last;
		const std::vector<ecs::Entity> chain(in.popups.begin() + std::ptrdiff_t(first),
											 in.popups.begin() + std::ptrdiff_t(last) + 1);
		for (auto r = chain.rbegin(); r != chain.rend(); ++r)
			ClosePopup(world, *r);
	}

	void MarkSelectionDirty(ecs::Entity owner) {
		// :selected des éléments et libellés liés.
		ctx.MarkStyleDirty(owner);
		ctx.MarkLayoutDirty();
		SyncSelectionText(owner);
	}

	void SyncSelectionText(ecs::Entity owner) {
		auto sel = world.GetComponent<Selection>(owner);
		if (sel.IsNone())
			return;
		const int index = sel.Unwrap()->First();
		std::vector<ecs::Entity> stack(ChildList(world, owner));
		while (!stack.empty()) {
			const ecs::Entity e = stack.back();
			stack.pop_back();
			if (world.HasComponent<Selection>(e))
				continue; // sélection imbriquée : la sienne
			if (auto st = world.GetComponent<SelectionText>(e); st.IsSome()) {
				const SelectionText text = *st.Unwrap();
				const String value = index >= 0 && size_t(index) < text.items.size() ? text.items[size_t(index)]
																					  : text.placeholder;
				if (auto tc = world.GetComponent<TextContent>(e); tc.IsSome())
					tc.Unwrap()->value = value;
			}
			for (ecs::Entity c : ChildList(world, e))
				stack.push_back(c);
		}
	}

	ecs::Entity FindPopupChild(ecs::Entity e) const {
		for (ecs::Entity c : ChildList(world, e))
			if (world.HasComponent<PopupState>(c))
				return c;
		for (ecs::Entity c : ChildList(world, e))
			if (!world.HasComponent<PopupTrigger>(c))
				if (ecs::Entity p = FindPopupChild(c); p.Valid())
					return p;
		return ecs::Entity{};
	}

	void SortTable(ecs::Entity header, int column) {
		const ecs::Entity table = AncestorWith<TableModel>(world, header);
		if (!table.Valid())
			return;
		auto model = world.GetComponent<TableModel>(table);
		if (column < 0 || size_t(column) >= model.Unwrap()->columns.size() ||
			!model.Unwrap()->columns[size_t(column)].sortable)
			return;
		TableModel &m = *model.Unwrap();
		if (m.sortColumn != column) {
			m.sortColumn = column;
			m.order = SortOrder::Ascending;
		} else {
			m.order = m.order == SortOrder::Ascending	 ? SortOrder::Descending
					  : m.order == SortOrder::Descending ? SortOrder::None
														 : SortOrder::Ascending;
		}
		const SortOrder order = m.order;
		// Corps : conteneur des lignes (enfants portant SelectItem).
		std::vector<ecs::Entity> stack{table};
		ecs::Entity body{};
		while (!stack.empty() && !body.Valid()) {
			const ecs::Entity e = stack.back();
			stack.pop_back();
			for (ecs::Entity c : ChildList(world, e))
				if (world.HasComponent<SelectItem>(c)) {
					body = e;
					break;
				}
			for (ecs::Entity c : ChildList(world, e))
				stack.push_back(c);
		}
		if (!body.Valid())
			return;
		auto cellText = [&](ecs::Entity row) -> String {
			for (ecs::Entity c : ChildList(world, row))
				if (auto tc = world.GetComponent<TableCell>(c); tc.IsSome() && tc.Unwrap()->column == column) {
					if (auto t = world.GetComponent<TextContent>(c); t.IsSome())
						return t.Unwrap()->value;
					for (ecs::Entity g : ChildList(world, c))
						if (auto t = world.GetComponent<TextContent>(g); t.IsSome())
							return t.Unwrap()->value;
				}
			return String();
		};
		auto children = world.GetComponent<ecs::Children>(body);
		std::vector<ecs::Entity> rows = children.Unwrap()->entities;
		if (order == SortOrder::None) {
			std::stable_sort(rows.begin(), rows.end(), [&](ecs::Entity a, ecs::Entity b) {
				return world.GetComponent<SelectItem>(a).Unwrap()->index < world.GetComponent<SelectItem>(b).Unwrap()->index;
			});
		} else {
			std::stable_sort(rows.begin(), rows.end(), [&](ecs::Entity a, ecs::Entity b) {
				const String ta = cellText(a), tb = cellText(b);
				auto na = ta.TryParseDouble(), nb = tb.TryParseDouble();
				const bool less = na.IsSome() && nb.IsSome() ? na.Unwrap() < nb.Unwrap() : ta < tb;
				const bool greater = na.IsSome() && nb.IsSome() ? nb.Unwrap() < na.Unwrap() : tb < ta;
				return order == SortOrder::Ascending ? less : greater;
			});
		}
		world.GetComponent<ecs::Children>(body).Unwrap()->entities = rows;
		std::vector<ecs::Entity> all{table};
		while (!all.empty()) {
			const ecs::Entity x = all.back();
			all.pop_back();
			if (auto si = world.GetComponent<SortIndicator>(x); si.IsSome())
				if (auto tc = world.GetComponent<TextContent>(x); tc.IsSome())
					tc.Unwrap()->value = si.Unwrap()->column != column ? String()
										 : order == SortOrder::Ascending ? String("▲")
										 : order == SortOrder::Descending ? String("▼")
																		  : String();
			if (x != body)
				for (ecs::Entity c : ChildList(world, x))
					all.push_back(c);
		}
		ctx.MarkAllDirty();
		WidgetEvent ev = MakeEvent(Trigger::Of(InputSource::Change), nullptr);
		ev.value = float(column);
		ev.index = column;
		Emit(table, Trigger::Of(InputSource::Change), ev, false);
	}

	// ── Glisser ─────────────────────────────────────────────────────────────

	void BeginDrag(Interaction::Drag kind, ecs::Entity e, sdl3::FPoint start) {
		in.drag = kind;
		in.dragEntity = e;
		in.dragStart = start;
		in.dragActive = kind != Interaction::Drag::Payload && kind != Interaction::Drag::Reorder;
	}

	void RangeFromPointer(ecs::Entity e, sdl3::FPoint p) {
		auto ri = Copy<RangeInput>(world, e);
		auto r = Copy<Range>(world, e);
		auto rect = Copy<Rect>(world, e);
		if (ri.IsNone() || r.IsNone() || rect.IsNone())
			return;
		const RangeInput input = ri.Unwrap();
		const Range range = r.Unwrap();
		const sdl3::FRect track = rect.Unwrap().content;
		if (input.mode == RangeMode::Relative) {
			const float d = input.axis == Orientation::Horizontal ? p.x - in.dragStart.x : in.dragStart.y - p.y;
			SetRangeValue(world, e, in.dragStartScalar + d * input.speed, true);
			return;
		}
		float thumb = input.thumbSize;
		if (thumb <= 0.f)
			for (ecs::Entity c : ChildList(world, e))
				if (auto rv = world.GetComponent<RangeView>(c); rv.IsSome() && rv.Unwrap()->part == RangePart::Thumb)
					if (auto cr = world.GetComponent<Rect>(c); cr.IsSome())
						thumb = input.axis == Orientation::Horizontal ? cr.Unwrap()->box.w : cr.Unwrap()->box.h;
		float t = 0.f;
		if (input.axis == Orientation::Horizontal) {
			const float len = std::max(1.f, track.w - thumb);
			t = (p.x - track.x - thumb * 0.5f) / len;
		} else {
			const float len = std::max(1.f, track.h - thumb);
			t = (p.y - track.y - thumb * 0.5f) / len;
			if (!input.invert)
				t = 1.f - t;
		}
		t = math::Clamp(t, 0.f, 1.f);
		SetRangeValue(world, e, range.min + t * std::max(0.f, range.Span() - range.page), true);
	}

	void Range2DFromPointer(ecs::Entity e, sdl3::FPoint p) {
		auto rect = Copy<Rect>(world, e);
		auto r = world.GetComponent<Range2D>(e);
		if (rect.IsNone() || r.IsNone())
			return;
		const sdl3::FRect c = rect.Unwrap().content;
		const sdl3::FPoint v{math::Clamp(c.w > 0.f ? (p.x - c.x) / c.w : 0.f, 0.f, 1.f),
							 math::Clamp(c.h > 0.f ? (p.y - c.y) / c.h : 0.f, 0.f, 1.f)};
		r.Unwrap()->value = v;
		ctx.MarkLayoutDirty();
		WidgetEvent ev = MakeEvent(Trigger::Of(InputSource::Change), nullptr);
		ev.value = v.x;
		ev.delta = v;
		Emit(e, Trigger::Of(InputSource::Change), ev, false);
	}

	void ContinueDrag(sdl3::FPoint p) {
		const ecs::Entity e = in.dragEntity;
		if (!world.IsAlive(e)) {
			in.drag = Interaction::Drag::None;
			return;
		}
		const float dx = p.x - in.dragStart.x, dy = p.y - in.dragStart.y;
		if (!in.dragActive && std::sqrt(dx * dx + dy * dy) >= ctx.dragThreshold) {
			in.dragActive = true;
			if (in.drag == Interaction::Drag::Payload) {
				SetStateFlag(world, ctx, e, State::Dragging, true);
				Emit(e, Trigger::Of(InputSource::DragStart), MakeEvent(Trigger::Of(InputSource::DragStart), nullptr), false);
			}
		}
		if (!in.dragActive)
			return;
		switch (in.drag) {
		case Interaction::Drag::Range:
			RangeFromPointer(e, p);
			break;
		case Interaction::Drag::Range2D:
			Range2DFromPointer(e, p);
			break;
		case Interaction::Drag::Scroll: {
			auto sc = world.GetComponent<Scroll>(e);
			auto rect = Copy<Rect>(world, e);
			if (sc.IsNone() || rect.IsNone())
				break;
			Scroll &s = *sc.Unwrap();
			const ScrollbarGeometry g =
				ScrollbarsOf(rect.Unwrap().box, StyleOf(world, e).BorderBox(), s, StyleOf(world, e).scrollbarWidth);
			if (in.dragVertical) {
				const float travel = std::max(1.f, g.vTrack.h - g.vThumb.h);
				s.offset.y = in.dragStartValue.y + dy / travel * s.MaxOffset().y;
			} else {
				const float travel = std::max(1.f, g.hTrack.w - g.hThumb.w);
				s.offset.x = in.dragStartValue.x + dx / travel * s.MaxOffset().x;
			}
			s.Clamp();
			const float value = in.dragVertical ? s.offset.y : s.offset.x;
			ctx.MarkLayoutDirty();
			WidgetEvent ev = MakeEvent(Trigger::Of(InputSource::Scroll), nullptr);
			ev.value = value;
			Emit(e, Trigger::Of(InputSource::Scroll), ev, false);
			break;
		}
		case Interaction::Drag::Text: {
			const TextGeometry g = TextGeometryOf(world, ctx, e);
			if (auto ed = world.GetComponent<TextEditor>(e); ed.IsSome()) {
				ed.Unwrap()->cursor = g.OffsetAt(p);
				ed.Unwrap()->blink = 0.f;
			}
			KeepCaretVisible(world, ctx, e);
			break;
		}
		case Interaction::Drag::Resize: {
			auto handle = Copy<ResizeHandle>(world, e);
			auto parent = ecs::ParentOf(world, e);
			if (handle.IsNone() || parent.IsNone())
				break;
			const ResizeHandle h = handle.Unwrap();
			const ecs::Entity resized = ResizedSibling(world, parent.Unwrap(), e, h.after);
			if (!resized.Valid())
				break;
			const bool horizontal = h.axis == Orientation::Horizontal;
			float total = 0.f;
			if (auto pr = world.GetComponent<Rect>(parent.Unwrap()); pr.IsSome())
				total = horizontal ? pr.Unwrap()->content.w : pr.Unwrap()->content.h;
			const float delta = horizontal ? dx : dy;
			const float own = h.after ? h.minAfter : h.minBefore, other = h.after ? h.minBefore : h.minAfter;
			float size = in.dragStartScalar + (h.after ? -delta : delta);
			float limit = std::max(own, total - other);
			if (h.maxSize > 0.f)
				limit = std::min(limit, std::max(own, h.maxSize));
			size = math::Clamp(size, own, limit);
			Widget w(world, resized);
			if (horizontal)
				w.Width(size);
			else
				w.Height(size);
			break;
		}
		case Interaction::Drag::Move: {
			auto mh = Copy<MoveHandle>(world, e);
			if (mh.IsNone())
				break;
			ecs::Entity target{};
			for (Option<ecs::Entity> cur = Some(e); cur.IsSome(); cur = ecs::ParentOf(world, cur.Unwrap()))
				if (StyleOf(world, cur.Unwrap()).position != Position::Relative) {
					target = cur.Unwrap();
					break;
				}
			if (!target.Valid())
				break;
			Widget w(world, target);
			if (mh.Unwrap().resize)
				w.Size(std::max(mh.Unwrap().minSize.x, in.dragStartValue.x + dx),
					   std::max(mh.Unwrap().minSize.y, in.dragStartValue.y + dy));
			else
				w.Offset(in.dragStartValue.x + dx, in.dragStartValue.y + dy);
			break;
		}
		case Interaction::Drag::Column: {
			auto cr = Copy<ColumnResizer>(world, e);
			const ecs::Entity table = AncestorWith<TableModel>(world, e);
			if (cr.IsNone() || !table.Valid())
				break;
			auto model = world.GetComponent<TableModel>(table);
			const size_t col = size_t(cr.Unwrap().column);
			if (col < model.Unwrap()->columns.size()) {
				TableColumn &c = model.Unwrap()->columns[col];
				c.width = std::max(c.minWidth, in.dragStartScalar + dx / std::max(0.01f, StyleOf(world, e).scale));
			}
			ctx.MarkLayoutDirty();
			break;
		}
		case Interaction::Drag::Reorder: {
			in.reorderTarget = ReorderTargetAt(e, p);
			break;
		}
		case Interaction::Drag::Payload: {
			const ecs::Entity hit = HitTest(world, p);
			ecs::Entity target = NearestWith<DropTarget>(world, hit);
			auto src = Copy<DragSource>(world, e);
			if (target.Valid() && src.IsSome()) {
				const String accepts = world.GetComponent<DropTarget>(target).Unwrap()->accepts;
				if (!accepts.IsEmpty() && accepts != src.Unwrap().kind)
					target = ecs::Entity{};
			}
			if (target == e)
				target = ecs::Entity{};
			if (target != in.dropHover) {
				if (in.dropHover.Valid() && world.IsAlive(in.dropHover))
					SetStateFlag(world, ctx, in.dropHover, State::DragOver, false);
				if (target.Valid())
					SetStateFlag(world, ctx, target, State::DragOver, true);
				in.dropHover = target;
			}
			break;
		}
		case Interaction::Drag::Pan:
		case Interaction::Drag::None:
			break;
		}
	}

	ecs::Entity ReorderTargetAt(ecs::Entity e, sdl3::FPoint p) const {
		auto parent = ecs::ParentOf(world, e);
		if (parent.IsNone())
			return ecs::Entity{};
		for (ecs::Entity s : ChildList(world, parent.Unwrap()))
			if (auto r = world.GetComponent<Rect>(s); r.IsSome()) {
				const sdl3::FRect b = r.Unwrap()->box;
				if (p.y < b.y + b.h * 0.5f)
					return s;
			}
		return ecs::Entity{}; // en fin de liste
	}

	void EndDrag(sdl3::FPoint p) {
		const ecs::Entity e = in.dragEntity;
		const Interaction::Drag kind = in.drag;
		const bool active = in.dragActive;
		in.drag = Interaction::Drag::None;
		in.dragEntity = ecs::Entity{};
		if (!world.IsAlive(e))
			return;
		if (kind == Interaction::Drag::Payload) {
			SetStateFlag(world, ctx, e, State::Dragging, false);
			const ecs::Entity target = in.dropHover;
			if (target.Valid() && world.IsAlive(target)) {
				SetStateFlag(world, ctx, target, State::DragOver, false);
				if (active)
					if (auto src = Copy<DragSource>(world, e); src.IsSome()) {
						WidgetEvent ev = MakeEvent(Trigger::Of(InputSource::DropReceived), nullptr);
						ev.payload = src.Unwrap().id;
						ev.text = src.Unwrap().kind;
						ev.pointer = p;
						Emit(target, Trigger::Of(InputSource::DropReceived), ev, false);
					}
			}
			in.dropHover = ecs::Entity{};
		}
		if (kind == Interaction::Drag::Reorder && active) {
			auto parent = ecs::ParentOf(world, e);
			if (parent.IsSome()) {
				auto children = world.GetComponent<ecs::Children>(parent.Unwrap());
				std::vector<ecs::Entity> &list = children.Unwrap()->entities;
				const auto from = std::find(list.begin(), list.end(), e);
				if (from != list.end()) {
					const int fromIndex = int(from - list.begin());
					list.erase(from);
					auto to = in.reorderTarget.Valid() ? std::find(list.begin(), list.end(), in.reorderTarget) : list.end();
					const int toIndex = int(to - list.begin());
					list.insert(to, e);
					ctx.MarkAllDirty();
					if (toIndex != fromIndex) {
						WidgetEvent ev = MakeEvent(Trigger::Of(InputSource::Reorder), nullptr);
						ev.index = fromIndex;
						ev.index2 = toIndex;
						Emit(parent.Unwrap(), Trigger::Of(InputSource::Reorder), ev, false);
					}
				}
			}
			in.reorderTarget = ecs::Entity{};
		}
		UpdateCursor();
	}

	/// Appui gauche : glisser possible sur la chaîne visée.
	bool StartPointerDrag(ecs::Entity target, sdl3::FPoint p, bool doubleClick) {
		for (Option<ecs::Entity> cur = Some(target); cur.IsSome(); cur = ecs::ParentOf(world, cur.Unwrap())) {
			const ecs::Entity e = cur.Unwrap();
			// Pouce d'une barre de défilement automatique
			if (auto sc = Copy<Scroll>(world, e); sc.IsSome() && (sc.Unwrap().gutter.x > 0.f || sc.Unwrap().gutter.y > 0.f)) {
				auto rect = Copy<Rect>(world, e);
				if (rect.IsSome()) {
					const ComputedStyle &cs = StyleOf(world, e);
					const ScrollbarGeometry g = ScrollbarsOf(rect.Unwrap().box, cs.BorderBox(), sc.Unwrap(), cs.scrollbarWidth);
					auto onTrack = [&](const sdl3::FRect &track, const sdl3::FRect &thumb, bool vertical) {
						if (!Contains(track, p))
							return false;
						if (!Contains(thumb, p)) {
							// Clic sur la piste : saut d'une page.
							auto s = world.GetComponent<Scroll>(e);
							if (vertical)
								s.Unwrap()->offset.y += (p.y < thumb.y ? -1.f : 1.f) * s.Unwrap()->view.y;
							else
								s.Unwrap()->offset.x += (p.x < thumb.x ? -1.f : 1.f) * s.Unwrap()->view.x;
							s.Unwrap()->Clamp();
							ctx.MarkLayoutDirty();
						}
						BeginDrag(Interaction::Drag::Scroll, e, p);
						in.dragVertical = vertical;
						in.dragStartValue = world.GetComponent<Scroll>(e).Unwrap()->offset;
						return true;
					};
					if (g.vertical && onTrack(g.vTrack, g.vThumb, true))
						return true;
					if (g.horizontal && onTrack(g.hTrack, g.hThumb, false))
						return true;
				}
			}
			if (world.HasComponent<TextEditor>(e)) {
				const TextGeometry g = TextGeometryOf(world, ctx, e);
				auto ed = world.GetComponent<TextEditor>(e);
				const size_t at = g.OffsetAt(p);
				TextEditor &t = *ed.Unwrap();
				if (doubleClick) {
					String text;
					if (auto tc = world.GetComponent<TextContent>(e); tc.IsSome())
						text = tc.Unwrap()->value;
					t.anchor = WordStart(text, at);
					t.cursor = WordEnd(text, at);
				} else {
					t.cursor = at;
					if (!(SDL_GetModState() & SDL_KMOD_SHIFT))
						t.anchor = at;
				}
				t.blink = 0.f;
				t.preferredX = -1.f;
				BeginDrag(Interaction::Drag::Text, e, p);
				return true;
			}
			if (world.HasComponent<Range2D>(e) && world.HasComponent<RangeInput>(e)) {
				BeginDrag(Interaction::Drag::Range2D, e, p);
				Range2DFromPointer(e, p);
				return true;
			}
			if (world.HasComponent<RangeInput>(e) && world.HasComponent<Range>(e)) {
				if (auto edit = world.GetComponent<EditOnDoubleClick>(e); edit.IsSome() && edit.Unwrap()->editing)
					return false;
				BeginDrag(Interaction::Drag::Range, e, p);
				in.dragStartScalar = world.GetComponent<Range>(e).Unwrap()->value;
				if (world.GetComponent<RangeInput>(e).Unwrap()->mode == RangeMode::Track)
					RangeFromPointer(e, p);
				else
					in.dragActive = false; // relatif : seuil avant de glisser (le double clic reste possible)
				return true;
			}
			if (auto h = Copy<ResizeHandle>(world, e); h.IsSome()) {
				auto parent = ecs::ParentOf(world, e);
				if (parent.IsSome()) {
					const ecs::Entity resized = ResizedSibling(world, parent.Unwrap(), e, h.Unwrap().after);
					if (resized.Valid())
						if (auto br = world.GetComponent<Rect>(resized); br.IsSome()) {
							BeginDrag(Interaction::Drag::Resize, e, p);
							in.dragVertical = h.Unwrap().axis == Orientation::Vertical;
							in.dragStartScalar = in.dragVertical ? br.Unwrap()->box.h : br.Unwrap()->box.w;
							return true;
						}
				}
			}
			if (auto cr = Copy<ColumnResizer>(world, e); cr.IsSome()) {
				const ecs::Entity table = AncestorWith<TableModel>(world, e);
				if (table.Valid()) {
					auto model = world.GetComponent<TableModel>(table);
					if (size_t(cr.Unwrap().column) < model.Unwrap()->columns.size()) {
						BeginDrag(Interaction::Drag::Column, e, p);
						in.dragStartScalar = model.Unwrap()->columns[size_t(cr.Unwrap().column)].width;
						return true;
					}
				}
			}
			if (auto mh = Copy<MoveHandle>(world, e); mh.IsSome()) {
				for (Option<ecs::Entity> up = Some(e); up.IsSome(); up = ecs::ParentOf(world, up.Unwrap()))
					if (StyleOf(world, up.Unwrap()).position != Position::Relative) {
						BeginDrag(Interaction::Drag::Move, e, p);
						const sdl3::FRect box = Widget(world, up.Unwrap()).Bounds();
						if (mh.Unwrap().resize) {
							in.dragStartValue = {box.w, box.h};
						} else {
							// Décalage actuel par rapport au parent (left/top).
							sdl3::FRect ref{0.f, 0.f, ctx.viewport.x, ctx.viewport.y};
							if (StyleOf(world, up.Unwrap()).position == Position::Absolute)
								if (auto pp = ecs::ParentOf(world, up.Unwrap()); pp.IsSome())
									ref = Widget(world, pp.Unwrap()).ContentBounds();
							in.dragStartValue = {box.x - ref.x, box.y - ref.y};
							Widget(world, up.Unwrap()).Anchor(Anchor::TopLeft);
						}
						return true;
					}
			}
			if (world.HasComponent<DragSource>(e)) {
				BeginDrag(Interaction::Drag::Payload, e, p);
				return false; // le clic reste possible tant que le seuil n'est pas franchi
			}
			if (world.HasComponent<Reorderable>(e)) {
				BeginDrag(Interaction::Drag::Reorder, e, p);
				return false;
			}
		}
		return false;
	}

	// ── Popups ──────────────────────────────────────────────────────────────

	/// Appui hors des popups ouverts : fermeture ; rend faux si l'appui est
	/// bloqué (hors d'une modale).
	bool HandleOutsideClick(ecs::Entity target) {
		for (size_t i = in.popups.size(); i-- > 0;) {
			if (i >= in.popups.size())
				continue;
			const ecs::Entity p = in.popups[i];
			auto popup = Copy<PopupState>(world, p);
			if (popup.IsNone())
				continue;
			if (target.Valid() && IsDescendantOrSelf(world, target, p))
				return true;
			if (popup.Unwrap().modal)
				return false;
			// L'ancre (déclencheur) gère elle-même la bascule.
			const ecs::Entity anchor = popup.Unwrap().anchor;
			if (anchor.Valid() && target.Valid() && IsDescendantOrSelf(world, target, anchor))
				return true;
			if (popup.Unwrap().closeOnOutside)
				ClosePopupChain(world, p);
		}
		return true;
	}

	// ── Édition de texte ────────────────────────────────────────────────────

	bool ClaimsKey(const sdl3::Event &ev, ecs::Entity focus) const {
		if (!focus.Valid())
			return false;
		const bool keyboard = ev.IsKeyDown() || ev.IsKeyUp() || ev.IsTextInput() || ev.IsTextEditing();
		if (!keyboard)
			return false;
		if (ctx.keyPassThrough ? ctx.keyPassThrough(ev, focus) : DefaultKeyPassThrough(ev))
			return false;
		auto ed = world.GetComponent<TextEditor>(focus);
		if (ed.IsNone())
			return false;
		const IOMode mode = ed.Unwrap()->mode;
		if (IoCanWrite(mode))
			return true;
		if (ev.IsTextInput() || ev.IsTextEditing())
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

	/// Frappe dans le champ focalisé ; vrai si elle a été traitée.
	bool EditText(ecs::Entity e, const sdl3::Event &ev) {
		auto edRef = world.GetComponent<TextEditor>(e);
		if (edRef.IsNone())
			return false;
		auto tcRef = world.GetComponent<TextContent>(e);
		if (tcRef.IsNone()) {
			world.AddComponent(e, TextContent{});
			return EditText(e, ev);
		}
		TextEditor &ed = *edRef.Unwrap();
		String &text = tcRef.Unwrap()->value;
		const String before = text;
		ed.cursor = std::min(ed.cursor, text.size());
		ed.anchor = std::min(ed.anchor, text.size());
		const IOMode mode = ed.mode;
		auto hasSel = [&] { return ed.cursor != ed.anchor; };
		auto lo = [&] { return std::min(ed.cursor, ed.anchor); };
		auto hi = [&] { return std::max(ed.cursor, ed.anchor); };
		auto eraseSel = [&] {
			const size_t a = lo(), b = hi();
			text = text.Substr(0, a) + text.Substr(b);
			ed.cursor = ed.anchor = a;
		};
		auto insert = [&](const String &s) {
			if (!IoCanWrite(mode) || s.IsEmpty())
				return;
			if (hasSel())
				eraseSel();
			const size_t room = ed.maxLength > text.size() ? ed.maxLength - text.size() : 0;
			String ins = s.size() <= room ? s : s.Substr(0, room);
			// Ne coupe jamais un caractère UTF-8.
			while (!ins.IsEmpty() && (static_cast<unsigned char>(ins[ins.size() - 1]) & 0xC0) == 0x80 &&
				   ins.size() < s.size())
				ins = ins.Substr(0, ins.size() - 1);
			if (!ed.multiline) {
				String flat;
				for (char c : ins)
					if (c != '\n' && c != '\r')
						flat.PushBack(c);
				ins = flat;
			}
			text = text.Substr(0, ed.cursor) + ins + text.Substr(ed.cursor);
			ed.cursor += ins.size();
			ed.anchor = ed.cursor;
		};
		bool submit = false;
		if (ev.IsTextInput()) {
			insert(String(ev.TextInput().text));
		} else if (ev.IsKeyDown()) {
			const SDL_Keycode key = ev.Keycode();
			const bool ctrl = (ev.raw.key.mod & SDL_KMOD_CTRL) != 0;
			const bool shift = (ev.raw.key.mod & SDL_KMOD_SHIFT) != 0;
			const TextGeometry g = TextGeometryOf(world, ctx, e);
			auto move = [&](size_t to) {
				ed.cursor = std::min(to, text.size());
				if (!shift)
					ed.anchor = ed.cursor;
			};
			switch (key) {
			case SDLK_LEFT:
				if (!shift && hasSel())
					move(lo());
				else
					move(ctrl ? WordStart(text, ed.cursor) : PrevCodepoint(text, ed.cursor));
				ed.preferredX = -1.f;
				break;
			case SDLK_RIGHT:
				if (!shift && hasSel())
					move(hi());
				else
					move(ctrl ? WordEnd(text, ed.cursor) : NextCodepoint(text, ed.cursor));
				ed.preferredX = -1.f;
				break;
			case SDLK_UP:
			case SDLK_DOWN:
			case SDLK_PAGEUP:
			case SDLK_PAGEDOWN: {
				if (!ed.multiline)
					return false;
				const size_t li = LineIndexOf(g.lines, ed.cursor);
				const float x = ed.preferredX >= 0.f ? ed.preferredX : g.XOf(li, ed.cursor - g.lines[li].offset);
				const int page = std::max(1, int(g.box.h / std::max(1.f, g.lineHeight)) - 1);
				const int delta = key == SDLK_UP ? -1 : key == SDLK_DOWN ? 1 : key == SDLK_PAGEUP ? -page : page;
				const int target = math::Clamp(int(li) + delta, 0, int(g.lines.size()) - 1);
				move(g.OffsetAt({x, g.YOf(size_t(target)) + g.lineHeight * 0.5f}));
				ed.preferredX = x;
				break;
			}
			case SDLK_HOME: {
				const size_t li = LineIndexOf(g.lines, ed.cursor);
				move(ctrl ? 0 : g.lines[li].offset);
				break;
			}
			case SDLK_END: {
				const size_t li = LineIndexOf(g.lines, ed.cursor);
				move(ctrl ? text.size() : g.lines[li].offset + g.lines[li].text.size());
				break;
			}
			case SDLK_BACKSPACE:
				if (!IoCanWrite(mode))
					break;
				if (hasSel())
					eraseSel();
				else if (ed.cursor > 0) {
					const size_t p = ctrl ? WordStart(text, ed.cursor) : PrevCodepoint(text, ed.cursor);
					text = text.Substr(0, p) + text.Substr(ed.cursor);
					ed.cursor = ed.anchor = p;
				}
				break;
			case SDLK_DELETE:
				if (!IoCanWrite(mode))
					break;
				if (hasSel())
					eraseSel();
				else if (ed.cursor < text.size()) {
					const size_t n = ctrl ? WordEnd(text, ed.cursor) : NextCodepoint(text, ed.cursor);
					text = text.Substr(0, ed.cursor) + text.Substr(n);
				}
				break;
			case SDLK_RETURN:
			case SDLK_KP_ENTER:
				if (ed.multiline && !ctrl)
					insert(String("\n"));
				else
					submit = true;
				break;
			case SDLK_TAB:
				if (ed.multiline && IoCanWrite(mode) && !ctrl) {
					insert(String("\t"));
					break;
				}
				return false;
			case SDLK_A:
				if (!ctrl)
					return true;
				ed.anchor = 0;
				ed.cursor = text.size();
				break;
			case SDLK_C:
				if (!ctrl)
					return true;
				if (IoCanCopy(mode) && !ed.password)
					(void)sdl3::clipboard::SetText(hasSel() ? text.Substr(lo(), hi() - lo()) : text);
				break;
			case SDLK_X:
				if (!ctrl)
					return true;
				if (IoCanCopy(mode) && IoCanWrite(mode) && hasSel() && !ed.password) {
					(void)sdl3::clipboard::SetText(text.Substr(lo(), hi() - lo()));
					eraseSel();
				}
				break;
			case SDLK_V:
				if (!ctrl)
					return true;
				if (IoCanPaste(mode))
					insert(sdl3::clipboard::GetText());
				break;
			case SDLK_ESCAPE:
				ClearFocus(world);
				return true;
			default:
				return true;
			}
		}
		ed.blink = 0.f;
		const bool changed = text != before;
		const String now = text;
		KeepCaretVisible(world, ctx, e);
		if (changed) {
			ctx.MarkLayoutDirty();
			if (ed.followTail)
				ed.stickToBottom = true;
			WidgetEvent ev2 = MakeEvent(Trigger::Of(InputSource::TextChange), &ev);
			ev2.text = now;
			Emit(e, Trigger::Of(InputSource::TextChange), ev2, false);
		}
		if (submit) {
			if (world.HasComponent<EditOnDoubleClick>(e)) {
				CommitValueEdit(e, true);
				return true;
			}
			WidgetEvent ev2 = MakeEvent(Trigger::Of(InputSource::Submit), &ev);
			ev2.text = now;
			Emit(e, Trigger::Of(InputSource::Submit), ev2, false);
		}
		return true;
	}

	/// Valeur glissée : double clic → saisie de la valeur au clavier.
	void BeginValueEdit(ecs::Entity e) {
		auto edit = world.GetComponent<EditOnDoubleClick>(e);
		auto r = Copy<Range>(world, NearestWith<Range>(world, e));
		if (edit.IsNone() || r.IsNone() || edit.Unwrap()->editing)
			return;
		edit.Unwrap()->editing = true;
		String text;
		if (auto tc = world.GetComponent<TextContent>(e); tc.IsSome())
			text = tc.Unwrap()->value;
		edit.Unwrap()->before = text;
		int decimals = 2;
		if (auto rl = world.GetComponent<RangeLabel>(e); rl.IsSome())
			decimals = rl.Unwrap()->decimals;
		const String value = String::From(r.Unwrap().value, decimals);
		TextEditor ed;
		ed.multiline = false;
		ed.maxLength = 32;
		ed.cursor = value.size();
		ed.anchor = 0;
		world.AddComponent(e, std::move(ed));
		if (auto tc = world.GetComponent<TextContent>(e); tc.IsSome())
			tc.Unwrap()->value = value;
		SetFocus(world, e, false);
		ctx.MarkStyleDirty(e);
		ctx.MarkLayoutDirty();
	}

	void CommitValueEdit(ecs::Entity e, bool commit) {
		auto edit = world.GetComponent<EditOnDoubleClick>(e);
		if (edit.IsNone() || !edit.Unwrap()->editing)
			return;
		edit.Unwrap()->editing = false;
		String text;
		if (auto tc = world.GetComponent<TextContent>(e); tc.IsSome())
			text = tc.Unwrap()->value;
		world.RemoveComponent<TextEditor>(e);
		const ecs::Entity owner = NearestWith<Range>(world, e);
		if (commit && owner.Valid())
			if (auto v = text.Replace(',', '.').Trim().TryParseDouble(); v.IsSome())
				SetRangeValue(world, owner, float(v.Unwrap()), true);
		// Libellé refait depuis la valeur (validée ou non).
		if (auto r = Copy<Range>(world, owner); r.IsSome())
			SetRangeValue(world, owner, r.Unwrap().value, false);
		ctx.MarkStyleDirty(e);
		ctx.MarkLayoutDirty();
	}

	// ── Clavier hors champ de texte ─────────────────────────────────────────

	bool NavigateKey(const sdl3::Event &ev) {
		if (!ev.IsKeyDown())
			return false;
		const SDL_Keycode key = ev.Keycode();
		const bool shift = (ev.raw.key.mod & SDL_KMOD_SHIFT) != 0;
		if (key == SDLK_TAB && !(ev.raw.key.mod & (SDL_KMOD_CTRL | SDL_KMOD_ALT))) {
			if (FocusOrder().empty())
				return false;
			MoveFocus(shift ? -1 : 1);
			return true;
		}
		if (key == SDLK_ESCAPE) {
			if (in.focus.Valid() && world.HasComponent<EditOnDoubleClick>(in.focus)) {
				CommitValueEdit(in.focus, false);
				return true;
			}
			const ecs::Entity top = TopPopup();
			if (top.Valid()) {
				if (auto p = Copy<PopupState>(world, top); p.IsSome() && p.Unwrap().closeOnEscape) {
					ClosePopupChain(world, top);
					return true;
				}
			}
			return false;
		}
		const ecs::Entity focus = in.focus;
		if (!focus.Valid() || !world.IsAlive(focus))
			return false;
		// Activation clavier d'un widget focalisé.
		if ((key == SDLK_RETURN || key == SDLK_KP_ENTER || key == SDLK_SPACE) && !ev.raw.key.repeat) {
			if (world.HasComponent<Pressable>(focus) || world.HasComponent<Checkable>(focus) ||
				world.HasComponent<SelectItem>(focus) || world.HasComponent<PopupTrigger>(focus)) {
				Activate(focus, uint16_t(ev.raw.key.mod));
				return true;
			}
		}
		// Valeur : flèches.
		if (world.HasComponent<Range>(focus) && world.HasComponent<RangeInput>(focus)) {
			const Range r = *world.GetComponent<Range>(focus).Unwrap();
			const float step = r.step > 0.f ? r.step : r.Span() / 100.f;
			float d = 0.f;
			if (key == SDLK_LEFT || key == SDLK_DOWN)
				d = -step;
			else if (key == SDLK_RIGHT || key == SDLK_UP)
				d = step;
			else if (key == SDLK_PAGEUP)
				d = step * 10.f;
			else if (key == SDLK_PAGEDOWN)
				d = -step * 10.f;
			else if (key == SDLK_HOME) {
				SetRangeValue(world, focus, r.min, true);
				return true;
			} else if (key == SDLK_END) {
				SetRangeValue(world, focus, r.max, true);
				return true;
			}
			if (d != 0.f) {
				SetRangeValue(world, focus, r.value + d, true);
				return true;
			}
		}
		// Liste : flèches haut/bas.
		if (auto sel = Copy<Selection>(world, focus); sel.IsSome() && (key == SDLK_UP || key == SDLK_DOWN)) {
			int count = 0;
			CountItems(focus, count);
			if (count > 0) {
				const int cur = sel.Unwrap().current;
				const int next = math::Clamp(cur + (key == SDLK_UP ? -1 : 1), 0, count - 1);
				SetSelection(world, focus, next, true);
				return true;
			}
		}
		if (key == SDLK_UP || key == SDLK_DOWN || key == SDLK_LEFT || key == SDLK_RIGHT) {
			if (!in.focusVisible)
				return false;
			const float dx = key == SDLK_LEFT ? -1.f : key == SDLK_RIGHT ? 1.f : 0.f;
			const float dy = key == SDLK_UP ? -1.f : key == SDLK_DOWN ? 1.f : 0.f;
			return MoveFocusSpatial(dx, dy);
		}
		return false;
	}

	void CountItems(ecs::Entity e, int &count) const {
		for (ecs::Entity c : ChildList(world, e)) {
			if (world.HasComponent<SelectItem>(c))
				count = std::max(count, world.GetComponent<SelectItem>(c).Unwrap()->index + 1);
			if (!world.HasComponent<Selection>(c))
				CountItems(c, count);
		}
	}

	bool NavigateGamepad(const sdl3::ButtonInput &b) {
		if (!b.down)
			return false;
		switch (sdl3::GamepadButton(b.code)) {
		case sdl3::GamepadButton::D_PAD_UP:
			return MoveFocusSpatial(0.f, -1.f);
		case sdl3::GamepadButton::D_PAD_DOWN:
			return MoveFocusSpatial(0.f, 1.f);
		case sdl3::GamepadButton::D_PAD_LEFT:
			if (in.focus.Valid() && world.HasComponent<RangeInput>(in.focus)) {
				const Range r = *world.GetComponent<Range>(in.focus).Unwrap();
				SetRangeValue(world, in.focus, r.value - (r.step > 0.f ? r.step : r.Span() / 20.f), true);
				return true;
			}
			return MoveFocusSpatial(-1.f, 0.f);
		case sdl3::GamepadButton::D_PAD_RIGHT:
			if (in.focus.Valid() && world.HasComponent<RangeInput>(in.focus)) {
				const Range r = *world.GetComponent<Range>(in.focus).Unwrap();
				SetRangeValue(world, in.focus, r.value + (r.step > 0.f ? r.step : r.Span() / 20.f), true);
				return true;
			}
			return MoveFocusSpatial(1.f, 0.f);
		case sdl3::GamepadButton::SOUTH:
			if (in.focus.Valid()) {
				Activate(in.focus, 0);
				return true;
			}
			return false;
		case sdl3::GamepadButton::EAST: {
			const ecs::Entity top = TopPopup();
			if (top.Valid()) {
				ClosePopupChain(world, top);
				return true;
			}
			return false;
		}
		default:
			return false;
		}
	}

	// ── Boutons ─────────────────────────────────────────────────────────────

	void DispatchGestures(const sdl3::ButtonInput &b, ecs::Entity target, const std::vector<sdl3::ButtonGesture> &gestures,
						  const sdl3::Event &raw) {
		for (const sdl3::ButtonGesture &g : gestures) {
			Trigger t(SourceOf(b.source), b.code, g.gesture, g.param);
			t.mods = b.mods;
			WidgetEvent ev = MakeEvent(t, &raw);
			ev.clicks = g.param;
			ev.mods = b.mods;
			if (b.source == sdl3::ButtonSource::Mouse || b.source == sdl3::ButtonSource::Finger ||
				b.source == sdl3::ButtonSource::Pen)
				ev.pointer = b.position;
			Emit(target, t, ev, true);
		}
	}

	ecs::Entity PressTargetOf(sdl3::ButtonSource s, int code) const {
		for (const Interaction::PressTarget &p : in.pressTargets)
			if (p.source == s && p.code == code)
				return p.target;
		return ecs::Entity{};
	}

	void SetPressTarget(sdl3::ButtonSource s, int code, ecs::Entity target) {
		for (Interaction::PressTarget &p : in.pressTargets)
			if (p.source == s && p.code == code) {
				p.target = target;
				return;
			}
		in.pressTargets.push_back({s, code, target});
	}

	bool HandleButton(const sdl3::ButtonInput &b, const sdl3::Event &ev) {
		const bool pointer = b.source == sdl3::ButtonSource::Mouse || b.source == sdl3::ButtonSource::Finger ||
							 (b.source == sdl3::ButtonSource::Pen && b.code == 0);
		ecs::Entity target{};
		bool consumed = false;
		if (pointer) {
			in.pointer = b.position;
			in.pointerInside = true;
			UpdateHover(&ev);
			target = in.hovered;
			consumed = Over(target) || in.drag != Interaction::Drag::None;
		} else {
			target = in.focus.Valid() && world.IsAlive(in.focus) ? in.focus : ctx.root;
		}
		const bool primary = (b.source == sdl3::ButtonSource::Mouse && b.code == int(sdl3::MouseButton::Left)) ||
							 b.source == sdl3::ButtonSource::Finger || (b.source == sdl3::ButtonSource::Pen && b.code == 0);

		if (b.down) {
			if (pointer) {
				const bool reachable = HandleOutsideClick(target);
				if (!reachable)
					return true; // hors d'une modale : bloqué
				if (target.Valid() && !Reachable(target))
					return true;
			}
			if (target.Valid() && IsDisabledRecursive(world, target))
				return consumed;
			const bool sameTarget = target == in.lastClickTarget;
			const std::vector<sdl3::ButtonGesture> gestures = in.gestures.Feed(b, sameTarget);
			SetPressTarget(b.source, b.code, target);
			in.lastClickTarget = target;
			in.holdFired.clear();
			const int clicks = gestures.empty() ? 1 : gestures.front().param;
			if (pointer && primary) {
				in.pressed = target;
				for (Option<ecs::Entity> cur = target.Valid() ? Some(target) : Option<ecs::Entity>(NONE); cur.IsSome();
					 cur = ecs::ParentOf(world, cur.Unwrap())) {
					SetStateFlag(world, ctx, cur.Unwrap(), State::Active, true);
					if (world.HasComponent<Pressable>(cur.Unwrap()))
						break;
				}
				// Focus : le plus proche focalisable (sinon on le retire).
				const ecs::Entity focusable = NearestWith<Focusable>(world, target);
				if (focusable.Valid())
					SetFocus(world, focusable, false);
				else if (!PopupOf(world, target).Valid())
					ClearFocus(world);
				if (const ecs::Entity editable = NearestWith<EditOnDoubleClick>(world, target); editable.Valid())
					if (clicks == 2 || world.GetComponent<EditOnDoubleClick>(editable).Unwrap()->singleClick) {
						in.drag = Interaction::Drag::None;
						BeginValueEdit(editable);
					}
				(void)StartPointerDrag(target, b.position, clicks == 2);
			}
			if (b.source == sdl3::ButtonSource::Key) {
				Trigger t(InputSource::Key, b.code, b.repeat ? sdl3::ButtonEvent::Repeat : sdl3::ButtonEvent::Press, 0);
				t.mods = b.mods;
				const bool listened = ListenedOnChain(target, t);
				DispatchGestures(b, target, gestures, ev);
				consumed = consumed || listened;
				return consumed;
			}
			if (b.source == sdl3::ButtonSource::Gamepad) {
				const Trigger t(InputSource::GamepadButton, b.code, sdl3::ButtonEvent::Press, 0);
				consumed = ListenedOnChain(target, t);
				DispatchGestures(b, target, gestures, ev);
				consumed = NavigateGamepad(b) || consumed;
				return consumed;
			}
			DispatchGestures(b, target, gestures, ev);
			return consumed;
		}

		// ── Relâchement ─────────────────────────────────────────────────────
		const ecs::Entity pressTarget = PressTargetOf(b.source, b.code);
		const bool dragging = pointer && primary && in.drag != Interaction::Drag::None;
		const bool dragged = dragging && in.dragActive && in.drag != Interaction::Drag::Range &&
							 in.drag != Interaction::Drag::Range2D;
		if (dragging)
			EndDrag(b.position);
		if (pointer && primary) {
			for (Option<ecs::Entity> cur = in.pressed.Valid() && world.IsAlive(in.pressed) ? Some(in.pressed)
																							: Option<ecs::Entity>(NONE);
				 cur.IsSome(); cur = ecs::ParentOf(world, cur.Unwrap()))
				SetStateFlag(world, ctx, cur.Unwrap(), State::Active, false);
			in.pressed = ecs::Entity{};
		}
		// Clic : relâché sur la cible de l'appui (ou un descendant), sans glisser.
		const bool sameTarget = pointer ? (pressTarget.Valid() && target.Valid() &&
										   IsDescendantOrSelf(world, target, pressTarget) && !dragged)
										: pressTarget == target;
		const std::vector<sdl3::ButtonGesture> gestures = in.gestures.Feed(b, sameTarget);
		const ecs::Entity clickTarget = pointer ? target : pressTarget;
		if (clickTarget.Valid() && world.IsAlive(clickTarget) && !IsDisabledRecursive(world, clickTarget)) {
			DispatchGestures(b, clickTarget, gestures, ev);
			const bool clicked = std::any_of(gestures.begin(), gestures.end(),
											 [](const sdl3::ButtonGesture &g) { return g.gesture == sdl3::ButtonEvent::Clic; });
			if (clicked && pointer && primary && world.IsAlive(clickTarget))
				Activate(clickTarget, b.mods);
			if (clicked && b.source == sdl3::ButtonSource::Mouse && b.code == int(sdl3::MouseButton::Right) &&
				world.IsAlive(clickTarget))
				ContextMenu(clickTarget, b.position, ev);
		}
		return consumed;
	}

	void ContextMenu(ecs::Entity target, sdl3::FPoint at, const sdl3::Event &raw) {
		WidgetEvent ev = MakeEvent(Trigger::Of(InputSource::ContextMenu), &raw);
		ev.pointer = at;
		Emit(target, Trigger::Of(InputSource::ContextMenu), ev, true);
		// Menu contextuel déclaré par le builder (PopupTrigger ContextMenu).
		for (Option<ecs::Entity> cur = Some(target); cur.IsSome(); cur = ecs::ParentOf(world, cur.Unwrap()))
			if (auto trig = Copy<PopupTrigger>(world, cur.Unwrap()); trig.IsSome() && trig.Unwrap().mode == PopupOpen::ContextMenu) {
				const ecs::Entity popup = FindPopupChild(cur.Unwrap());
				if (popup.Valid())
					OpenPopupAt(world, popup, at);
				return;
			}
	}

	bool ListenedOnChain(ecs::Entity target, const Trigger &t) const {
		for (Option<ecs::Entity> cur = target.Valid() ? Some(target) : Option<ecs::Entity>(NONE); cur.IsSome();
			 cur = ecs::ParentOf(world, cur.Unwrap()))
			if (auto b = world.GetComponent<Bindings>(cur.Unwrap()); b.IsSome() && b.Unwrap()->Listens(t))
				return true;
		return false;
	}

	// ── Molette ─────────────────────────────────────────────────────────────

	bool HandleWheel(const sdl3::Event &ev) {
		sdl3::FPoint wheel{ev.MouseWheel().x, ev.MouseWheel().y};
		if (ev.MouseWheel().direction == SDL_MOUSEWHEEL_FLIPPED)
			wheel = {-wheel.x, -wheel.y};
		in.pointer = {ev.MouseWheel().mouse_x, ev.MouseWheel().mouse_y};
		in.pointerInside = true;
		UpdateHover(&ev);
		const ecs::Entity target = in.hovered;
		if (!Over(target))
			return false;
		WidgetEvent wev = MakeEvent(Trigger::Wheel(), &ev);
		wev.wheel = wheel;
		wev.target = target;
		DispatchEvent(world, target, wev, true);
		if (IsDisabledRecursive(world, target))
			return true;
		// Comportement par défaut : valeur, sinon défilement du plus proche conteneur.
		const bool shift = (SDL_GetModState() & SDL_KMOD_SHIFT) != 0;
		for (Option<ecs::Entity> cur = Some(target); cur.IsSome(); cur = ecs::ParentOf(world, cur.Unwrap())) {
			const ecs::Entity e = cur.Unwrap();
			if (auto ri = Copy<RangeInput>(world, e); ri.IsSome() && ri.Unwrap().wheel && world.HasComponent<Range>(e) &&
													  !world.HasComponent<Scroll>(e)) {
				const Range r = *world.GetComponent<Range>(e).Unwrap();
				const float step = r.step > 0.f ? r.step : r.Span() / 50.f;
				SetRangeValue(world, e, r.value + (wheel.y != 0.f ? wheel.y : wheel.x) * step, true);
				return true;
			}
			if (auto sc = world.GetComponent<Scroll>(e); sc.IsSome()) {
				Scroll &s = *sc.Unwrap();
				const sdl3::FPoint max = s.MaxOffset();
				float dx = -wheel.x * ctx.wheelSpeed, dy = -wheel.y * ctx.wheelSpeed;
				if (shift || max.y <= 0.f) {
					dx += dy;
					dy = 0.f;
				}
				const sdl3::FPoint before = s.offset;
				s.offset.x += dx;
				s.offset.y += dy;
				s.Clamp();
				if (before.x != s.offset.x || before.y != s.offset.y) {
					if (auto ed = world.GetComponent<TextEditor>(e); ed.IsSome())
						ed.Unwrap()->stickToBottom = s.offset.y >= s.MaxOffset().y - 1.f;
					const float value = dy != 0.f ? s.offset.y : s.offset.x;
					ctx.MarkLayoutDirty();
					WidgetEvent sev = MakeEvent(Trigger::Of(InputSource::Scroll), &ev);
					sev.value = value;
					Emit(e, Trigger::Of(InputSource::Scroll), sev, false);
					return true;
				}
			}
		}
		return true;
	}

	// ── Point d'entrée ──────────────────────────────────────────────────────

	bool Handle(const sdl3::Event &ev) {
		for (const Extension &ext : ctx.extensions)
			if (ext.event && ext.event(world, ctx, ev))
				return true;

		switch (ev.Type()) {
		case SDL_EVENT_MOUSE_MOTION:
		case SDL_EVENT_PEN_MOTION:
		case SDL_EVENT_FINGER_MOTION: {
			sdl3::FPoint p{};
			if (ev.Type() == SDL_EVENT_MOUSE_MOTION)
				p = {ev.MouseMotion().x, ev.MouseMotion().y};
			else if (ev.Type() == SDL_EVENT_PEN_MOTION)
				p = {ev.PenMotion().x, ev.PenMotion().y};
			else {
				int w = 0, h = 0;
				if (SDL_Window *win = SDL_GetWindowFromID(ev.Finger().windowID))
					SDL_GetWindowSize(win, &w, &h);
				p = {ev.Finger().x * float(w), ev.Finger().y * float(h)};
			}
			const sdl3::FPoint delta{p.x - in.pointer.x, p.y - in.pointer.y};
			in.pointer = p;
			in.pointerInside = true;
			if (in.drag != Interaction::Drag::None)
				ContinueDrag(p);
			for (const Interaction::PressTarget &pt : in.pressTargets)
				in.gestures.Move(pt.source, pt.code, p);
			UpdateHover(&ev);
			if (in.hovered.Valid() && ListenedOnChain(in.hovered, Trigger::PointerMove())) {
				WidgetEvent mev = MakeEvent(Trigger::PointerMove(), &ev);
				mev.delta = delta;
				mev.target = in.hovered;
				DispatchEvent(world, in.hovered, mev, true);
			}
			return Over(in.hovered) || in.drag != Interaction::Drag::None;
		}
		case SDL_EVENT_MOUSE_WHEEL:
			return HandleWheel(ev);
		case SDL_EVENT_WINDOW_MOUSE_LEAVE:
			in.pointerInside = false;
			UpdateHover(&ev);
			Broadcast(Trigger::Window(int(ev.Type())), ev);
			return false;
		case SDL_EVENT_WINDOW_FOCUS_LOST:
			if (in.drag != Interaction::Drag::None)
				EndDrag(in.pointer);
			Broadcast(Trigger::Window(int(ev.Type())), ev);
			return false;
		case SDL_EVENT_TEXT_INPUT:
		case SDL_EVENT_TEXT_EDITING: {
			const ecs::Entity focus = in.focus;
			if (!focus.Valid() || !world.IsAlive(focus))
				return false;
			if (!ClaimsKey(ev, focus)) {
				if (ev.IsTextInput()) {
					WidgetEvent tev = MakeEvent(Trigger::Text(), &ev);
					tev.text = String(ev.TextInput().text);
					tev.target = focus;
					DispatchEvent(world, focus, tev, true);
				}
				return false;
			}
			if (ev.IsTextInput())
				(void)EditText(focus, ev);
			return true;
		}
		case SDL_EVENT_DROP_FILE: {
			const sdl3::FPoint p{ev.Drop().x, ev.Drop().y};
			const ecs::Entity hit = HitTest(world, p);
			WidgetEvent dev = MakeEvent(Trigger::DropFile(), &ev);
			dev.pointer = p;
			dev.text = String(ev.Drop().data ? ev.Drop().data : "");
			dev.target = hit.Valid() ? hit : ctx.root;
			DispatchEvent(world, dev.target, dev, true);
			return Over(hit);
		}
		case SDL_EVENT_GAMEPAD_AXIS_MOTION:
		case SDL_EVENT_JOYSTICK_AXIS_MOTION: {
			const bool gamepad = ev.Type() == SDL_EVENT_GAMEPAD_AXIS_MOTION;
			const int axis = gamepad ? ev.GamepadAxis().axis : ev.JoyAxis().axis;
			const int value = gamepad ? ev.GamepadAxis().value : ev.JoyAxis().value;
			Trigger t(gamepad ? InputSource::GamepadAxis : InputSource::JoystickAxis, axis, sdl3::ButtonEvent::Press,
					  int(float(value) / 32767.f * 1000.f));
			const ecs::Entity target = in.focus.Valid() && world.IsAlive(in.focus) ? in.focus : ctx.root;
			WidgetEvent aev = MakeEvent(t, &ev);
			aev.value = float(value) / 32767.f;
			aev.target = target;
			DispatchEvent(world, target, aev, true);
			return false;
		}
		case SDL_EVENT_JOYSTICK_HAT_MOTION: {
			Trigger t(InputSource::JoystickHat, ev.JoyHat().hat, sdl3::ButtonEvent::Press, ev.JoyHat().value);
			const ecs::Entity target = in.focus.Valid() && world.IsAlive(in.focus) ? in.focus : ctx.root;
			WidgetEvent hev = MakeEvent(t, &ev);
			hev.index = ev.JoyHat().value;
			hev.target = target;
			DispatchEvent(world, target, hev, true);
			return false;
		}
		default:
			break;
		}

		if (ev.IsWindow()) {
			Broadcast(Trigger::Window(int(ev.Type())), ev);
			return false;
		}

		if (auto button = sdl3::ReadButton(ev); button.IsSome()) {
			const sdl3::ButtonInput b = button.Unwrap();
			if (b.source == sdl3::ButtonSource::Key) {
				const ecs::Entity focus = in.focus.Valid() && world.IsAlive(in.focus) ? in.focus : ecs::Entity{};
				if (ClaimsKey(ev, focus)) {
					if (ev.IsKeyDown())
						(void)EditText(focus, ev);
					// Les liaisons du champ voient quand même la touche.
					(void)HandleButton(b, ev);
					return true;
				}
				const bool consumed = HandleButton(b, ev);
				const bool navigated = NavigateKey(ev);
				return consumed || navigated;
			}
			return HandleButton(b, ev);
		}
		return false;
	}

	/// Évènement de fenêtre : tous les widgets qui l'écoutent.
	void Broadcast(const Trigger &t, const sdl3::Event &ev) {
		std::vector<ecs::Entity> listeners;
		world.Query<Bindings>([&](ecs::Entity e, Bindings &b) {
			if (b.Listens(t))
				listeners.push_back(e);
		});
		for (ecs::Entity e : listeners) {
			WidgetEvent wev = MakeEvent(t, &ev);
			wev.target = e;
			DispatchEvent(world, e, wev, false);
		}
	}
};

} // namespace

// ============================================================================
// Distribution des évènements
// ============================================================================

void DispatchEvent(ecs::ArchetypeRegistry &world, ecs::Entity target, WidgetEvent event, bool bubble) {
	if (!target.Valid() || !world.IsAlive(target))
		return;
	event.world = &world;
	if (!event.target.Valid())
		event.target = target;
	std::vector<ecs::Entity> chain;
	for (Option<ecs::Entity> cur = Some(target); cur.IsSome(); cur = ecs::ParentOf(world, cur.Unwrap())) {
		chain.push_back(cur.Unwrap());
		if (!bubble)
			break;
	}
	for (ecs::Entity e : chain) {
		if (!world.IsAlive(e))
			break;
		// Copie des rappels : un rappel peut modifier l'arbre (et les liaisons).
		std::vector<EventCallback> calls;
		if (auto b = world.GetComponent<Bindings>(e); b.IsSome())
			for (const Binding &binding : b.Unwrap()->list) {
				if (!binding.enabled || !binding.callback)
					continue;
				for (const Trigger &t : binding.triggers)
					if (t.Matches(event.trigger)) {
						if (t.gesture == sdl3::ButtonEvent::Hold && event.trigger.gesture == sdl3::ButtonEvent::Hold) {
							// Appui long : une seule fois par appui et par liaison.
							if (Context *ctx = ContextOf(world)) {
								std::vector<uint32_t> &fired = ctx->input.holdFired;
								const uint32_t key = (e.id << 12) ^ binding.id;
								if (std::find(fired.begin(), fired.end(), key) != fired.end())
									break;
								fired.push_back(key);
							}
						}
						calls.push_back(binding.callback);
						break;
					}
			}
		event.currentTarget = e;
		for (const EventCallback &cb : calls) {
			cb(event);
			if (event.Stopped())
				return;
		}
	}
}

bool InputSystem::HandleEvent(ecs::ArchetypeRegistry &world, Context &ctx, const sdl3::Event &ev) {
	if (ev.IsConsumed())
		return true;
	if (const uint64_t ms = ev.Timestamp() / 1000000u; ms > ctx.input.timeMs)
		ctx.input.timeMs = ms;
	// La fenêtre de l'interface change de taille : la mise en page suit.
	if (ctx.window && (ev.raw.type == SDL_EVENT_WINDOW_RESIZED || ev.raw.type == SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED) &&
		ev.raw.window.windowID == SDL_GetWindowID(ctx.window->Get())) {
		const sdl3::Point size = ctx.window->GetSize();
		if (size.x > 0 && size.y > 0 && (float(size.x) != ctx.viewport.x || float(size.y) != ctx.viewport.y)) {
			ctx.viewport = {float(size.x), float(size.y)};
			ctx.MarkLayoutDirty();
		}
	}
	Input input(world, ctx);
	return input.Handle(ev);
}

// ============================================================================
// Popups, focus
// ============================================================================

void OpenPopup(ecs::ArchetypeRegistry &world, ecs::Entity popup, ecs::Entity anchor) {
	auto p = world.GetComponent<PopupState>(popup);
	if (p.IsNone())
		return;
	p.Unwrap()->open = true;
	if (anchor.Valid())
		p.Unwrap()->anchor = anchor;
	if (Context *ctx = ContextOf(world)) {
		std::erase(ctx->input.popups, popup);
		ctx->input.popups.push_back(popup);
		ctx->MarkStyleDirty(popup);
		if (anchor.Valid())
			ctx->MarkStyleDirty(anchor);
		if (auto parent = ecs::ParentOf(world, popup); parent.IsSome())
			ctx->MarkStyleDirty(parent.Unwrap());
		ctx->MarkLayoutDirty();
	}
	WidgetEvent ev;
	ev.trigger = Trigger::Of(InputSource::Open);
	DispatchEvent(world, popup, ev, false);
}

void OpenPopupAt(ecs::ArchetypeRegistry &world, ecs::Entity popup, sdl3::FPoint at) {
	if (auto p = world.GetComponent<PopupState>(popup); p.IsSome()) {
		p.Unwrap()->placement = Placement::Point;
		p.Unwrap()->at = at;
	}
	OpenPopup(world, popup, ecs::Entity{});
}

void ClosePopup(ecs::ArchetypeRegistry &world, ecs::Entity popup) {
	auto p = world.GetComponent<PopupState>(popup);
	if (p.IsNone() || !p.Unwrap()->open)
		return;
	p.Unwrap()->open = false;
	const ecs::Entity anchor = p.Unwrap()->anchor;
	if (Context *ctx = ContextOf(world)) {
		std::erase(ctx->input.popups, popup);
		ctx->MarkStyleDirty(popup);
		if (anchor.Valid() && world.IsAlive(anchor))
			ctx->MarkStyleDirty(anchor);
		if (auto parent = ecs::ParentOf(world, popup); parent.IsSome())
			ctx->MarkStyleDirty(parent.Unwrap());
		ctx->MarkLayoutDirty();
		// Le focus ne reste pas dans un popup fermé.
		if (ctx->input.focus.Valid() && IsDescendantOrSelf(world, ctx->input.focus, popup))
			ClearFocus(world);
	}
	WidgetEvent ev;
	ev.trigger = Trigger::Of(InputSource::Close);
	DispatchEvent(world, popup, ev, false);
}

void ClosePopupChain(ecs::ArchetypeRegistry &world, ecs::Entity popup) {
	Context *ctx = ContextOf(world);
	if (!ctx) {
		ClosePopup(world, popup);
		return;
	}
	std::vector<ecs::Entity> &list = ctx->input.popups;
	auto it = std::find(list.begin(), list.end(), popup);
	if (it == list.end()) {
		ClosePopup(world, popup);
		return;
	}
	const std::vector<ecs::Entity> toClose(it, list.end());
	for (auto r = toClose.rbegin(); r != toClose.rend(); ++r)
		ClosePopup(world, *r);
}

void CloseAllPopups(ecs::ArchetypeRegistry &world) {
	if (Context *ctx = ContextOf(world)) {
		const std::vector<ecs::Entity> list = ctx->input.popups;
		for (auto r = list.rbegin(); r != list.rend(); ++r)
			ClosePopup(world, *r);
	}
}

bool IsPopupOpen(const ecs::ArchetypeRegistry &world, ecs::Entity popup) {
	auto p = world.GetComponent<PopupState>(popup);
	return p.IsSome() && p.Unwrap()->open;
}

ecs::Entity PopupOf(const ecs::ArchetypeRegistry &world, ecs::Entity e) {
	for (Option<ecs::Entity> cur = Some(e); cur.IsSome(); cur = ecs::ParentOf(world, cur.Unwrap()))
		if (auto p = world.GetComponent<PopupState>(cur.Unwrap()); p.IsSome() && p.Unwrap()->open)
			return cur.Unwrap();
	return ecs::Entity{};
}

bool HasOpenModal(const ecs::ArchetypeRegistry &world) {
	const Context *ctx = ContextOf(world);
	if (!ctx)
		return false;
	for (ecs::Entity p : ctx->input.popups)
		if (auto popup = world.GetComponent<PopupState>(p); popup.IsSome() && popup.Unwrap()->modal && popup.Unwrap()->open)
			return true;
	return false;
}

void SetFocus(ecs::ArchetypeRegistry &world, ecs::Entity e, bool visible) {
	Context *ctx = ContextOf(world);
	if (!ctx)
		return;
	Interaction &in = ctx->input;
	if (in.focus == e) {
		if (visible != in.focusVisible && e.Valid()) {
			in.focusVisible = visible;
			SetStateFlag(world, *ctx, e, State::FocusVisible, visible);
		}
		return;
	}
	const ecs::Entity old = in.focus;
	in.focus = e;
	in.focusVisible = visible;
	if (old.Valid() && world.IsAlive(old)) {
		SetStateFlag(world, *ctx, old, State::Focus, false);
		SetStateFlag(world, *ctx, old, State::FocusVisible, false);
		// Une valeur en cours d'édition est validée en perdant le focus.
		if (auto edit = world.GetComponent<EditOnDoubleClick>(old); edit.IsSome() && edit.Unwrap()->editing) {
			Input input(world, *ctx);
			input.CommitValueEdit(old, true);
		}
		if (auto ed = world.GetComponent<TextEditor>(old); ed.IsSome())
			ed.Unwrap()->anchor = ed.Unwrap()->cursor;
		WidgetEvent ev;
		ev.trigger = Trigger::Of(InputSource::Blur);
		DispatchEvent(world, old, ev, false);
	}
	if (e.Valid() && world.IsAlive(e)) {
		SetStateFlag(world, *ctx, e, State::Focus, true);
		SetStateFlag(world, *ctx, e, State::FocusVisible, visible);
		if (auto ed = world.GetComponent<TextEditor>(e); ed.IsSome())
			ed.Unwrap()->blink = 0.f;
		WidgetEvent ev;
		ev.trigger = Trigger::Of(InputSource::Focus);
		DispatchEvent(world, e, ev, false);
	}
}

void ClearFocus(ecs::ArchetypeRegistry &world) { SetFocus(world, ecs::Entity{}, false); }

ecs::Entity FocusedWidget(const ecs::ArchetypeRegistry &world) {
	const Context *ctx = ContextOf(world);
	return ctx && world.IsAlive(ctx->input.focus) ? ctx->input.focus : ecs::Entity{};
}

Option<PayloadDrag> ActivePayloadDrag(const ecs::ArchetypeRegistry &world) {
	const Context *ctx = ContextOf(world);
	if (!ctx || ctx->input.drag != Interaction::Drag::Payload || !ctx->input.dragActive ||
		!world.IsAlive(ctx->input.dragEntity))
		return NONE;
	return Some(PayloadDrag{ctx->input.dragEntity, ctx->input.dropHover, ctx->input.pointer});
}

ecs::Entity HitTest(const ecs::ArchetypeRegistry &world, sdl3::FPoint p) {
	const Context *ctx = ContextOf(world);
	if (!ctx)
		return ecs::Entity{};
	for (auto it = ctx->paintOrder.rbegin(); it != ctx->paintOrder.rend(); ++it) {
		const PaintEntry &entry = *it;
		if (!world.IsAlive(entry.entity))
			continue;
		const bool inside = Contains(entry.box, p) && Contains(entry.clip, p);
		if (inside) {
			auto r = world.GetComponent<Rect>(entry.entity);
			const ComputedStyle &cs = StyleOf(world, entry.entity);
			if (r.IsSome() && r.Unwrap()->visible && cs.pointerEvents != PointerEvents::None &&
				cs.visibility != Visibility::Hidden && !IsHiddenRecursive(world, entry.entity))
				return entry.entity;
		}
		if (entry.blocksBelow)
			return entry.entity;
	}
	return ecs::Entity{};
}

// ============================================================================
// Valeurs
// ============================================================================

void SetCheckState(ecs::ArchetypeRegistry &world, ecs::Entity e, bool on, bool notify) {
	if (!world.HasComponent<Check>(e))
		world.AddComponent(e, Check{});
	auto check = world.GetComponent<Check>(e);
	const bool changed = check.Unwrap()->on != on;
	check.Unwrap()->on = on;
	if (Context *ctx = ContextOf(world)) {
		ctx->MarkStyleDirty(e);
		ctx->MarkLayoutDirty();
	}
	// Radio : décoche les autres du groupe.
	if (on)
		if (auto ck = world.GetComponent<Checkable>(e); ck.IsSome() && ck.Unwrap()->mode == CheckMode::Radio) {
			const String group = ck.Unwrap()->group;
			std::vector<ecs::Entity> others;
			if (group.IsEmpty()) {
				if (auto parent = ecs::ParentOf(world, e); parent.IsSome())
					others = ChildList(world, parent.Unwrap());
			} else {
				world.Query<Checkable>([&](ecs::Entity x, Checkable &c) {
					if (c.group == group)
						others.push_back(x);
				});
			}
			for (ecs::Entity o : others)
				if (o != e)
					if (auto oc = world.GetComponent<Check>(o); oc.IsSome() && oc.Unwrap()->on)
						if (auto ock = world.GetComponent<Checkable>(o); ock.IsSome() && ock.Unwrap()->mode == CheckMode::Radio) {
							oc.Unwrap()->on = false;
							MarkStyleDirty(world, o);
							WidgetEvent ev;
							ev.trigger = Trigger::Of(InputSource::Toggle);
							ev.checked = false;
							if (notify)
								DispatchEvent(world, o, ev, false);
						}
		}
	if (changed && notify) {
		WidgetEvent ev;
		ev.trigger = Trigger::Of(InputSource::Toggle);
		ev.checked = on;
		DispatchEvent(world, e, ev, false);
	}
}

void SetRangeValue(ecs::ArchetypeRegistry &world, ecs::Entity e, float value, bool notify) {
	auto r = world.GetComponent<Range>(e);
	if (r.IsNone())
		return;
	const float snapped = r.Unwrap()->Snap(value);
	const bool changed = snapped != r.Unwrap()->value;
	r.Unwrap()->value = snapped;
	// Libellés liés (RangeLabel) : mis à jour tout de suite.
	auto relabel = [&](ecs::Entity x) {
		if (auto rl = world.GetComponent<RangeLabel>(x); rl.IsSome()) {
			if (auto edit = world.GetComponent<EditOnDoubleClick>(x); edit.IsSome() && edit.Unwrap()->editing)
				return;
			const RangeLabel label = *rl.Unwrap();
			const String text = label.format ? label.format(snapped)
											 : label.prefix + String::From(snapped, label.decimals) + label.suffix;
			if (auto tc = world.GetComponent<TextContent>(x); tc.IsSome())
				tc.Unwrap()->value = text;
			else
				world.AddComponent(x, TextContent{text});
		}
	};
	std::vector<ecs::Entity> stack{e};
	while (!stack.empty()) {
		const ecs::Entity x = stack.back();
		stack.pop_back();
		relabel(x);
		for (ecs::Entity c : ChildList(world, x))
			if (!world.HasComponent<Range>(c))
				stack.push_back(c);
	}
	MarkLayoutDirty(world);
	if (changed && notify) {
		WidgetEvent ev;
		ev.trigger = Trigger::Of(InputSource::Change);
		ev.value = snapped;
		DispatchEvent(world, e, ev, false);
	}
}

void SetSelection(ecs::ArchetypeRegistry &world, ecs::Entity e, int index, bool notify) {
	auto sel = world.GetComponent<Selection>(e);
	if (sel.IsNone())
		return;
	const bool changed = sel.Unwrap()->selected != std::vector<int>{index};
	sel.Unwrap()->SelectOnly(index);
	if (Context *ctx = ContextOf(world)) {
		Input input(world, *ctx);
		input.MarkSelectionDirty(e);
		ctx->MarkAllDirty();
	}
	if (changed && notify) {
		WidgetEvent ev;
		ev.trigger = Trigger::Of(InputSource::Select);
		ev.index = index;
		DispatchEvent(world, e, ev, false);
	}
}

void SetTextValue(ecs::ArchetypeRegistry &world, ecs::Entity e, String text, bool notify) {
	if (auto tc = world.GetComponent<TextContent>(e); tc.IsSome()) {
		if (tc.Unwrap()->value == text)
			return;
		tc.Unwrap()->value = text;
	} else {
		world.AddComponent(e, TextContent{text});
	}
	if (auto ed = world.GetComponent<TextEditor>(e); ed.IsSome()) {
		ed.Unwrap()->cursor = std::min(ed.Unwrap()->cursor, text.size());
		ed.Unwrap()->anchor = std::min(ed.Unwrap()->anchor, text.size());
		if (ed.Unwrap()->followTail && ed.Unwrap()->stickToBottom)
			if (auto sc = world.GetComponent<Scroll>(e); sc.IsSome())
				sc.Unwrap()->offset.y = 1.0e9f; // borné à la prochaine mise en page
	}
	MarkLayoutDirty(world);
	if (notify) {
		WidgetEvent ev;
		ev.trigger = Trigger::Of(InputSource::TextChange);
		ev.text = std::move(text);
		DispatchEvent(world, e, ev, false);
	}
}

} // namespace ui
