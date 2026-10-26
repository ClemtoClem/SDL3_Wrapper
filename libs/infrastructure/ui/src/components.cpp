// Définitions de ui/components.hpp
#include <ui/components.hpp>

#include <algorithm>

namespace ui {

const std::vector<ecs::Entity> &ChildList(const ecs::ArchetypeRegistry &world, ecs::Entity e) {
	static const std::vector<ecs::Entity> K_NONE;
	if (auto c = world.GetComponent<ecs::Children>(e); c.IsSome())
		return c.Unwrap()->entities;
	return K_NONE;
}

bool IsDescendantOrSelf(const ecs::ArchetypeRegistry &world, ecs::Entity e, ecs::Entity ancestor) {
	if (!ancestor.Valid())
		return false;
	for (Option<ecs::Entity> cur = Some(e); cur.IsSome(); cur = ecs::ParentOf(world, cur.Unwrap()))
		if (cur.Unwrap() == ancestor)
			return true;
	return false;
}

bool IsHiddenRecursive(const ecs::ArchetypeRegistry &world, ecs::Entity e) {
	return NearestWith<Hidden>(world, e).Valid();
}

bool IsDisabledRecursive(const ecs::ArchetypeRegistry &world, ecs::Entity e) {
	return NearestWith<Disabled>(world, e).Valid();
}

bool Selection::IsSelected(int i) const noexcept {
	return std::binary_search(selected.begin(), selected.end(), i);
}

void Selection::SelectOnly(int i) {
	selected.clear();
	if (i >= 0)
		selected.push_back(i);
	anchor = current = i;
}

void Selection::Click(int i, bool ctrl, bool shift) {
	if (!multiple || (!ctrl && !shift)) {
		if (toggleable && IsSelected(i) && selected.size() == 1) {
			selected.clear();
			current = i;
			return;
		}
		SelectOnly(i);
		return;
	}
	if (shift && anchor >= 0) {
		selected.clear();
		const int lo = std::min(anchor, i), hi = std::max(anchor, i);
		for (int k = lo; k <= hi; ++k)
			selected.push_back(k);
		current = i;
		return;
	}
	// Ctrl : bascule
	auto it = std::lower_bound(selected.begin(), selected.end(), i);
	if (it != selected.end() && *it == i)
		selected.erase(it);
	else
		selected.insert(it, i);
	anchor = current = i;
}

} // namespace ui
