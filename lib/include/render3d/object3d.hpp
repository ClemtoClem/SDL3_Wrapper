#pragma once
#include <algorithm>
#include <functional>
#include <memory>
#include <vector>

#include "../core/core.hpp"
#include "../math/math.hpp"

namespace render3d {

// Avant-déclaration seulement : OnDraw() en a besoin comme paramètre, mais
// object3d.hpp n'a pas besoin de la définition complète de Canvas (qui,
// inversement, inclut object3d.hpp pour DrawObject()) — voir shape.hpp pour
// le premier Object3D concret qui appelle réellement des méthodes de Canvas.
class Canvas;

/// Nœud de graphe de scène (three.js Object3D) : transform local
/// (position/rotation quaternion/scale) composé via math::ComposeTRS,
/// hiérarchie parent/enfants avec propagation de la matrice monde. Les
/// nœuds sont possédés par leur parent (Add() prend un unique_ptr) — non
/// copiable/déplaçable : un déplacement invaliderait le pointeur `parent`
/// que les enfants existants tiennent vers `this`.
class Object3D {
	math::FVector3 m_position{0.f, 0.f, 0.f};
	math::FQuaternion m_rotation = math::FQuaternion::Identity();
	math::FVector3 m_scale{1.f, 1.f, 1.f};

	Object3D *m_parent = nullptr;
	std::vector<std::unique_ptr<Object3D>> m_children;

	bool m_visible = true;
	String m_name;

public:
	Object3D() = default;
	virtual ~Object3D() = default;

	Object3D(const Object3D &) = delete;
	Object3D &operator=(const Object3D &) = delete;
	Object3D(Object3D &&) = delete;
	Object3D &operator=(Object3D &&) = delete;

	// ── Transform local ──────────────────────────────────────────────────────

	[[nodiscard]] const math::FVector3 &Position() const noexcept { return m_position; }
	void SetPosition(const math::FVector3 &position) noexcept { m_position = position; }

	[[nodiscard]] const math::FQuaternion &Rotation() const noexcept { return m_rotation; }
	void SetRotation(const math::FQuaternion &rotation) noexcept { m_rotation = rotation; }

	[[nodiscard]] const math::FVector3 &Scale() const noexcept { return m_scale; }
	void SetScale(const math::FVector3 &scale) noexcept { m_scale = scale; }

	/// Matrice locale (par rapport au parent), recomposée à chaque appel —
	/// pas de mise en cache : un graphe de cette taille n'en a pas besoin, et
	/// ça élimine toute une classe de bugs d'invalidation de cache.
	[[nodiscard]] math::FMatrix4 LocalMatrix() const noexcept;

	/// Matrice monde : remonte la chaîne de parents à chaque appel (même
	/// remarque que LocalMatrix()).
	[[nodiscard]] math::FMatrix4 WorldMatrix() const noexcept;

	// ── Hiérarchie ────────────────────────────────────────────────────────────

	[[nodiscard]] Object3D *Parent() const noexcept { return m_parent; }
	[[nodiscard]] const std::vector<std::unique_ptr<Object3D>> &Children() const noexcept { return m_children; }

	/// Prend possession de `child` ; retourne une référence pour chaînage.
	Object3D &Add(std::unique_ptr<Object3D> child);

	/// Retire `child` de ce nœud (recherche par identité de pointeur) et rend
	/// à l'appelant l'unique_ptr propriétaire — nullptr si `child` n'est pas
	/// un enfant direct de `this`. Symétrique de Add() : permet de déplacer
	/// un nœud déjà possédé vers un autre parent sans allocation ni
	/// destruction (voir render3d::SceneSyncSystem::Sync, ecs_bridge.hpp).
	std::unique_ptr<Object3D> RemoveChild(Object3D *child) noexcept {
		auto it = std::find_if(m_children.begin(), m_children.end(),
								[child](const std::unique_ptr<Object3D> &c) { return c.get() == child; });
		if (it == m_children.end())
			return nullptr;
		std::unique_ptr<Object3D> removed = std::move(*it);
		m_children.erase(it);
		removed->m_parent = nullptr;
		return removed;
	}

	// ── Divers ────────────────────────────────────────────────────────────────

	[[nodiscard]] bool IsVisible() const noexcept { return m_visible; }
	void SetVisible(bool visible) noexcept { m_visible = visible; }

	[[nodiscard]] const String &Name() const noexcept { return m_name; }
	void SetName(String name) noexcept { m_name = std::move(name); }

	/// Parcours en profondeur ; saute les sous-arbres invisibles (comme
	/// Canvas::DrawObject, qui s'appuie dessus).
	void Traverse(const std::function<void(Object3D &)> &visitor) {
		if (!m_visible)
			return;
		visitor(*this);
		for (auto &child : m_children)
			child->Traverse(visitor);
	}

	/// Recherche par nom dans ce nœud et tous ses descendants (nullptr si
	/// aucun ne correspond). Volontairement PAS implémenté via Traverse() :
	/// Traverse() saute les sous-arbres invisibles (voir sa propre doc
	/// ci-dessus), ce qui convient au parcours d'ordre de dessin mais pas à
	/// une recherche par nom — c'est une opération structurelle (ex.
	/// résoudre la cible d'une piste d'animation, voir animation.hpp) qui
	/// doit trouver un nœud quelle que soit sa visibilité courante : un os
	/// animé peut très bien être rendu invisible pour une raison sans
	/// rapport, et le rater silencieusement ici serait un bug réel et
	/// difficile à repérer.
	[[nodiscard]] Object3D *FindByName(StringView name) noexcept;

	/// Point d'extension : ne fait rien par défaut (Object3D/Group purs ne
	/// dessinent rien) — voir Shape::OnDraw() pour l'implémentation réelle.
	virtual void OnDraw(Canvas &canvas) { (void)canvas; }
};

} // namespace render3d
