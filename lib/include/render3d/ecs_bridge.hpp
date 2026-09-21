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
inline void SetSceneParent(ecs::ArchetypeRegistry &registry, ecs::Entity child, ecs::Entity parent) {
	registry.AddComponent(child, SceneParent{parent});
	auto children = registry.GetOrAddComponent<SceneChildren>(parent);
	if (children.IsSome())
		children.Unwrap()->list.push_back(child);
}

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
	static void Sync(ecs::ArchetypeRegistry &registry, Object3D &sceneRoot) {
		// 1. Transform : copie SceneTransform -> Object3D, entité courante
		//    uniquement (mutation en place, aucun changement structurel ECS
		//    — cf. la mise en garde sur l'invalidation d'itérateur en tête de
		//    ecs.hpp : ceci ne concerne QUE AddComponent/RemoveComponent/
		//    Despawn pendant l'itération, pas la mutation du Object3D pointé
		//    par SceneNode::node, qui n'est pas un changement d'archétype).
		registry.Query<SceneNode, SceneTransform>([&](ecs::Entity, SceneNode &sceneNode, SceneTransform &transform) {
			if (!sceneNode.node)
				return;
			sceneNode.node->SetPosition(transform.position);
			sceneNode.node->SetRotation(transform.rotation);
			sceneNode.node->SetScale(transform.scale);
		});

		// 2. Re-parentage : détermine le parent Object3D désiré de chaque
		//    entité suivie et déplace le nœud si nécessaire. GetComponent()
		//    sur une AUTRE entité (le parent visé) pendant cette Query() est
		//    une simple lecture sur une ligne d'archétype déjà résolue — pas
		//    un changement structurel, donc sûr même en itération (même
		//    schéma que physics::World::Step(), voir physics/world.hpp).
		registry.Query<SceneNode>([&](ecs::Entity entity, SceneNode &sceneNode) {
			if (!sceneNode.node)
				return;

			Object3D *desiredParent = &sceneRoot;
			if (auto parentComp = registry.GetComponent<SceneParent>(entity); parentComp.IsSome()) {
				Option<RefMut<SceneNode>> parentNode = registry.GetComponent<SceneNode>(parentComp.Value()->parent);
				if (parentNode.IsSome() && parentNode.Value()->node)
					desiredParent = parentNode.Value()->node;
			}

			if (sceneNode.node->Parent() == desiredParent)
				return;

			Object3D *currentParent = sceneNode.node->Parent();
			if (!currentParent)
				return; // best-effort : pas de parent actuel, on saute ce cycle

			std::unique_ptr<Object3D> owned = currentParent->RemoveChild(sceneNode.node);
			if (owned)
				desiredParent->Add(std::move(owned));
		});
	}
};

} // namespace render3d
