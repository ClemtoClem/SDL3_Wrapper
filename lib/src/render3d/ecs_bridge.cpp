// Définitions de render3d/ecs_bridge.hpp
#include "render3d/ecs_bridge.hpp"

namespace render3d {

void SetSceneParent(ecs::ArchetypeRegistry &registry, ecs::Entity child, ecs::Entity parent) {
	registry.AddComponent(child, SceneParent{parent});
	auto children = registry.GetOrAddComponent<SceneChildren>(parent);
	if (children.IsSome())
		children.Unwrap()->list.push_back(child);
}

// ── SceneSyncSystem ──────────────────────────────────────────────────────────

void SceneSyncSystem::Sync(ecs::ArchetypeRegistry &registry, Object3D &sceneRoot) {
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

} // namespace render3d
