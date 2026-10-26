// AnimationSystem — tout ce qui dépend du temps et non d'un évènement
#include <ui/systems.hpp>

#include <algorithm>
#include <cmath>

namespace ui {

namespace {

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

/// Entrée de menu à sous-menu survolée → déploiement après un court délai ;
/// entrée simple → repli des sous-menus ouverts depuis son popup ; barre de
/// menus avec un menu ouvert → bascule immédiate.
void NavigateMenus(ecs::ArchetypeRegistry &world, Context &ctx, float dt) {
	Interaction &in = ctx.input;
	const ecs::Entity hovered = in.hovered;
	if (!hovered.Valid() || !world.IsAlive(hovered) || in.drag != Interaction::Drag::None) {
		in.menuHover = ecs::Entity{};
		return;
	}
	// Barre de menus : un menu est ouvert, on survole une autre entrée.
	const ecs::Entity entry = NearestWith<PopupTrigger>(world, hovered);
	if (entry.Valid()) {
		auto bar = ecs::ParentOf(world, entry);
		if (bar.IsSome() && world.HasComponent<MenuStrip>(bar.Unwrap())) {
			ecs::Entity openEntry{};
			for (ecs::Entity sibling : ChildList(world, bar.Unwrap()))
				for (ecs::Entity c : ChildList(world, sibling))
					if (IsPopupOpen(world, c))
						openEntry = sibling;
			if (openEntry.Valid() && openEntry != entry) {
				for (ecs::Entity c : ChildList(world, openEntry))
					if (IsPopupOpen(world, c))
						ClosePopupChain(world, c);
				for (ecs::Entity c : ChildList(world, entry))
					if (world.HasComponent<PopupState>(c)) {
						if (auto t = world.GetComponent<PopupTrigger>(entry); t.IsSome())
							world.GetComponent<PopupState>(c).Unwrap()->placement = t.Unwrap()->placement;
						OpenPopup(world, c, entry);
						break;
					}
			}
			in.menuHover = ecs::Entity{};
			return;
		}
	}
	// Entrées dans un popup ouvert.
	const ecs::Entity popup = PopupOf(world, hovered);
	if (!popup.Valid())
		return;
	ecs::Entity item = hovered;
	for (Option<ecs::Entity> cur = Some(hovered); cur.IsSome(); cur = ecs::ParentOf(world, cur.Unwrap())) {
		if (cur.Unwrap() == popup)
			break;
		if (world.HasComponent<Pressable>(cur.Unwrap()) || world.HasComponent<PopupTrigger>(cur.Unwrap())) {
			item = cur.Unwrap();
			break;
		}
	}
	if (item != in.menuHover) {
		in.menuHover = item;
		in.menuHoverTime = 0.f;
	}
	const float before = in.menuHoverTime;
	in.menuHoverTime += dt;
	if (before >= ctx.submenuDelay || in.menuHoverTime < ctx.submenuDelay)
		return;
	// Sous-menus ouverts depuis ce popup (popups ouverts après lui).
	auto it = std::find(in.popups.begin(), in.popups.end(), popup);
	std::vector<ecs::Entity> after;
	if (it != in.popups.end())
		after.assign(it + 1, in.popups.end());
	auto trig = world.GetComponent<PopupTrigger>(item);
	ecs::Entity submenu{};
	if (trig.IsSome() && trig.Unwrap()->mode == PopupOpen::Hover)
		for (ecs::Entity c : ChildList(world, item))
			if (world.HasComponent<PopupState>(c))
				submenu = c;
	for (ecs::Entity p : after)
		if (p != submenu && world.IsAlive(p))
			if (!submenu.Valid() || !IsDescendantOrSelf(world, p, submenu))
				ClosePopupChain(world, p);
	if (submenu.Valid() && !IsPopupOpen(world, submenu)) {
		world.GetComponent<PopupState>(submenu).Unwrap()->placement = trig.Unwrap()->placement;
		OpenPopup(world, submenu, item);
	}
}

} // namespace

void AnimationSystem::Run(ecs::ArchetypeRegistry &world, Context &ctx, float dt) {
	Interaction &in = ctx.input;
	in.timeMs += uint64_t(std::max(0.f, dt) * 1000.f);
	for (Extension &ext : ctx.extensions)
		if (ext.tick)
			ext.tick(world, ctx, dt);

	// ── Appuis longs ────────────────────────────────────────────────────────
	// Horloge de l'interface : avancée par dt, recalée sur l'horodatage des
	// évènements (celui que reçoit sdl3::ButtonGestureTracker).
	const uint64_t now = in.timeMs;
	for (const sdl3::ButtonGestureTracker::Held &h : in.gestures.Update(now)) {
		if (h.moved || h.heldMs < 100)
			continue;
		ecs::Entity target{};
		for (const Interaction::PressTarget &p : in.pressTargets)
			if (p.source == h.source && p.code == h.code)
				target = p.target;
		if (!target.Valid() || !world.IsAlive(target))
			continue;
		WidgetEvent ev;
		ev.world = &world;
		ev.trigger = Trigger(SourceOf(h.source), h.code, sdl3::ButtonEvent::Hold, int(h.heldMs));
		ev.pointer = in.pointer;
		ev.target = target;
		DispatchEvent(world, target, ev, true);
	}

	// ── Infobulles ──────────────────────────────────────────────────────────
	// Le survol n'est recalculé qu'aux mouvements du pointeur : un widget
	// masqué (ou recouvert) sous un pointeur immobile ne garde pas sa bulle.
	const bool stillUnderPointer = in.hovered.Valid() && HitTest(world, in.pointer) == in.hovered;
	const ecs::Entity tipOwner = stillUnderPointer ? NearestWith<Tooltip>(world, in.hovered) : ecs::Entity{};
	if (tipOwner.Valid() && in.drag == Interaction::Drag::None && !in.pressed.Valid()) {
		in.hoverTime += dt;
		if (in.hoverTime >= ctx.tooltipDelay && !in.tooltipVisible) {
			in.tooltipVisible = true;
			in.tooltipText = world.GetComponent<Tooltip>(tipOwner).Unwrap()->text;
			in.tooltipPos = {in.pointer.x + 12.f, in.pointer.y + 18.f};
		}
	} else {
		in.tooltipVisible = false;
		in.hoverTime = 0.f;
	}

	NavigateMenus(world, ctx, dt);

	// ── Interrupteurs (Check animé) ─────────────────────────────────────────
	bool moved = false;
	world.Query<Check>([&](ecs::Entity e, Check &c) {
		const float target = c.on ? 1.f : 0.f;
		if (c.anim == target)
			return;
		float duration = 0.12f;
		if (auto cs = world.GetComponent<ComputedStyle>(e); cs.IsSome() && cs.Unwrap()->transitionDuration > 0.f)
			duration = cs.Unwrap()->transitionDuration;
		const float step = dt / duration;
		c.anim = c.anim < target ? std::min(target, c.anim + step) : std::max(target, c.anim - step);
		moved = true;
	});
	if (moved)
		ctx.MarkLayoutDirty();

	world.Query<Spin>([&](ecs::Entity, Spin &s) { s.angle = std::fmod(s.angle + s.speed * dt, 360.f); });

	// ── Défilement automatique du texte ─────────────────────────────────────
	world.Query<Marquee, ComputedStyle>([&](ecs::Entity, Marquee &m, ComputedStyle &cs) {
		if (cs.textOverflow != TextOverflow::Marquee || m.overflow <= 0.f) {
			m.offset = 0.f;
			return;
		}
		if (m.hold > 0.f) {
			m.hold -= dt;
			return;
		}
		m.offset += (m.forward ? 1.f : -1.f) * cs.marqueeSpeed * dt;
		if (m.offset >= m.overflow) {
			m.offset = m.overflow;
			m.forward = false;
			m.hold = cs.marqueePause;
		} else if (m.offset <= 0.f) {
			m.offset = 0.f;
			m.forward = true;
			m.hold = cs.marqueePause;
		}
	});

	// ── Éditeurs : curseur, suivi de la fin (journaux) ──────────────────────
	world.Query<TextEditor>([&](ecs::Entity e, TextEditor &ed) {
		ed.blink = std::fmod(ed.blink + dt, 1.f);
		if (ed.followTail && ed.stickToBottom)
			if (auto sc = world.GetComponent<Scroll>(e); sc.IsSome()) {
				const float max = sc.Unwrap()->MaxOffset().y;
				if (sc.Unwrap()->offset.y < max)
					sc.Unwrap()->offset.y = max;
			}
	});
}

} // namespace ui
