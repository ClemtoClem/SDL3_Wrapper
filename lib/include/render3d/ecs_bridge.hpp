#pragma once
/**
 * @file ecs_bridge.hpp
 * @brief Pont ECS -> render3d::Object3D (M28 du plan) : synchronise CHAQUE
 * frame un sous-graphe de scène `Object3D` à partir de composants ECS, à
 * SENS UNIQUE (ECS -> Object3D, jamais l'inverse).
 *
 * @details
 * Décision de scoping déjà actée : les entités ECS sont la source de vérité
 * (identité stable pour la sélection/l'undo d'un futur éditeur) — les nœuds
 * `Object3D` sont de simples "acteurs" pilotés depuis l'ECS, jamais modifiés
 * directement en dehors de SceneSyncSystem::Sync().
 *
 * Mirroir exact du même problème déjà résolu pour l'arbre ECS propre à
 * `ui::` — voir `UiParent`/`UiChildren` (ui/components.hpp:230-238) et
 * `SetParent()`/`DespawnTree()` (ui/components.hpp:1022-1038). Même forme
 * ici avec `SceneParent`/`SceneChildren`/`SetSceneParent()`.
 *
 * `SceneNode::node` est un pointeur NON-propriétaire : ce module ne
 * construit ni ne détruit jamais de `Object3D` — il ne fait que
 * transformer/re-parenter des nœuds déjà possédés ailleurs (même esprit que
 * `physics::World` tenant un `ecs::ArchetypeRegistry&` non-propriétaire, ou
 * `ui::Viewport3D` tenant un `Object3D*` non-propriétaire).
 */
#include "../core/core.hpp"
#include "../ecs/ecs.hpp"
#include "../math/math.hpp"
#include "object3d.hpp"

#include <vector>

namespace render3d {

// ============================================================================
// Composants
// ============================================================================

/// Transform ECS-autoritaire d'un nœud de scène — copiée vers l'Object3D
/// correspondant à chaque SceneSyncSystem::Sync() (jamais l'inverse).
struct SceneTransform {
	math::FVector3 position;
	math::FQuaternion rotation = math::FQuaternion::Identity();
	math::FVector3 scale{1.f, 1.f, 1.f};
};

/// Lie une entité ECS au `Object3D*` qu'elle pilote. Non-propriétaire :
/// l'Object3D est possédé par la chaîne de unique_ptr existante du graphe de
/// scène de l'application — ce module ne le construit ni ne le détruit.
struct SceneNode {
	Object3D *node = nullptr;
};

// ============================================================================
// Hiérarchie — miroir de UiParent/UiChildren (ui/components.hpp)
// ============================================================================

/// Lie une entité de scène à son parent (dans l'ECS, pas dans l'Object3D —
/// c'est SceneSyncSystem::Sync() qui répercute ce lien sur le graphe réel).
/// Sans lui, l'entité est rattachée directement à la racine de scène.
struct SceneParent {
	ecs::Entity parent{};
};

/// Liste ordonnée des enfants (dans l'ECS) — maintenue par SetSceneParent().
struct SceneChildren {
	std::vector<ecs::Entity> list;
};

/// Fait de `child` un enfant (ECS) de `parent` (maintient SceneParent +
/// SceneChildren). Le déplacement réel du Object3D correspondant n'a lieu
/// qu'au prochain SceneSyncSystem::Sync().
void SetSceneParent(ecs::ArchetypeRegistry &registry, ecs::Entity child, ecs::Entity parent);

// ============================================================================
// SceneSyncSystem
// ============================================================================

/// Synchronise chaque frame un sous-graphe `Object3D` à partir des
/// composants ECS ci-dessus. Deux passes indépendantes (transform, puis
/// re-parentage) — indépendantes car le transform est un état local par
/// nœud tandis que le re-parentage n'affecte que la composition de
/// WorldMatrix() (voir object3d.hpp:54-65) : aucun état lu/écrit en commun,
/// l'ordre entre les deux passes n'a donc pas d'importance.
class SceneSyncSystem {
public:
	/// `sceneRoot` reçoit tout nœud suivi (SceneNode non-null) qui n'a pas de
	/// SceneParent résolu — garantit qu'aucun nœud suivi ne finit orphelin.
	static void Sync(ecs::ArchetypeRegistry &registry, Object3D &sceneRoot);
};

} // namespace render3d
