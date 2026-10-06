// Définitions de render3d/object3d.hpp
#include "render3d/object3d.hpp"

namespace render3d {

// ── Object3D ─────────────────────────────────────────────────────────────────

math::FMatrix4 Object3D::LocalMatrix() const noexcept {
	return math::ComposeTRS(m_position, m_rotation, m_scale);
}

math::FMatrix4 Object3D::WorldMatrix() const noexcept {
	return m_parent ? m_parent->WorldMatrix() * LocalMatrix() : LocalMatrix();
}

Object3D & Object3D::Add(std::unique_ptr<Object3D> child) {
	child->m_parent = this;
	Object3D &ref = *child;
	m_children.push_back(std::move(child));
	return ref;
}

Object3D * Object3D::FindByName(StringView name) noexcept {
	if (m_name.View() == name)
		return this;
	for (auto &child : m_children) {
		if (Object3D *found = child->FindByName(name))
			return found;
	}
	return nullptr;
}

} // namespace render3d
