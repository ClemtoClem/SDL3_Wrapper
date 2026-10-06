// Définitions de ui/interaction.hpp
#include "ui/interaction.hpp"

namespace ui {

void ApplySelectionClick(SelectionState &state, int index, bool ctrl, bool shift, bool multiSelect) {
	if (multiSelect && shift && state.anchor >= 0) {
		state.selected.clear();
		int lo = sdl3::Min(state.anchor, index), hi = sdl3::Max(state.anchor, index);
		for (int i = lo; i <= hi; ++i)
			state.selected.insert(i);
	} else if (multiSelect && ctrl) {
		if (state.selected.count(index))
			state.selected.erase(index);
		else
			state.selected.insert(index);
		state.anchor = index;
	} else {
		state.selected.clear();
		state.selected.insert(index);
		state.anchor = index;
	}
	state.lastClicked = index;
}

ecs::Entity NearestSelectionAncestor(ecs::ArchetypeRegistry &world, ecs::Entity e) {
	ecs::Entity cur = e;
	while (cur.Valid()) {
		if (world.HasComponent<UiSelection>(cur))
			return cur;
		auto p = world.GetComponent<UiParent>(cur);
		if (p.IsNone())
			break;
		cur = p.Unwrap()->parent;
	}
	return ecs::Entity{};
}

void ResolveResizeDrag(UiResizeHandle &h, float mouseCoord) {
	float delta = mouseCoord - h.dragStartMouse;
	if (h.setBefore) {
		float newBefore = sdl3::Clamp(h.dragStartBefore + delta, h.minBefore, h.maxBefore);
		h.setBefore(newBefore);
	}
	if (h.setAfter) {
		// Signe opposé : si le panneau avant grandit de +delta, celui après
		// doit rétrécir d'autant (cas des deux panneaux en Px explicite).
		float newAfter = sdl3::Clamp(h.dragStartAfter - delta, h.minAfter, h.maxAfter);
		h.setAfter(newAfter);
	}
}

void ReorderChild(ecs::ArchetypeRegistry &world, ecs::Entity parent, ecs::Entity child, ecs::Entity target) {
	auto ch = world.GetComponent<UiChildren>(parent);
	if (ch.IsNone())
		return;
	auto &list = ch.Unwrap()->list;
	auto itChild = std::find(list.begin(), list.end(), child);
	if (itChild == list.end())
		return;
	list.erase(itChild);
	auto itTarget = std::find(list.begin(), list.end(), target); // ré-cherché : l'erase a pu décaler `target`
	if (itTarget == list.end()) {
		list.push_back(child); // `target` a disparu entre-temps (despawn concurrent) : replace en fin
		return;
	}
	list.insert(itTarget, child);
}

} // namespace ui
