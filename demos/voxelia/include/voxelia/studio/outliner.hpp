#pragma once
/**
 * World Outliner et inspecteur d'entités — MODÈLES lus directement dans le
 * jeu vivant (voxel::Game::Registry), sans fenêtre : le panneau du studio ne
 * fait qu'afficher ce qu'ils rendent (et les tests les vérifient sur une vraie
 * partie).
 *
 * À n'appeler que lorsque le fil principal a la main sur le jeu (entre
 * Session::EndSimulation et Session::BeginSimulation) : la simulation, sur le
 * fil « voxel-update », écrit dans ces mêmes composants.
 */
#include <vector>

#include <voxel/game.hpp>

namespace voxelia::studio {

enum class EntityCategory : uint8_t { Player, Creature, Item, FallingBlock, Carrier, Other };

[[nodiscard]] const char *EntityCategoryId(EntityCategory category) noexcept;    ///< "player", "creature"…
[[nodiscard]] const char *EntityCategoryLabel(EntityCategory category) noexcept; ///< « Joueurs », « Créatures »…

struct OutlinerEntry {
	ecs::Entity entity{};
	String id;   ///< EntityKind::id (« core:crawler », « voxel:item »…)
	String name; ///< nom affiché (Named, Player, définition, sinon id)
	EntityCategory category = EntityCategory::Other;
	String dimension;
	voxel::ChunkPos chunk{};
	math::FVector3 position{0.f, 0.f, 0.f};
	float distance = 0.f; ///< à `from` (caméra de l'éditeur)
	bool dead = false;
};

struct OutlinerFilter {
	String text{};                          ///< sous-chaîne du nom ou de l'id (casse ignorée)
	Option<EntityCategory> category = NONE; ///< NONE : toutes
	float maxDistance = 0.f;                ///< 0 : sans limite
	bool aliveOnly = false;
};

/// Entités du monde chargé qui passent le filtre, par catégorie, puis chunk,
/// puis distance.
[[nodiscard]] std::vector<OutlinerEntry> CollectEntities(voxel::Game &game, const OutlinerFilter &filter,
														 const math::FVector3 &from);

struct InspectorRow {
	String label;
	String value;
};

struct InspectorSection {
	String title; ///< « Transform », « Physique », « Santé », « Survie », « IA »…
	std::vector<InspectorRow> rows;
};

/// Propriétés en direct d'une entité, une section par composant présent.
/// Vide si l'entité n'existe plus.
[[nodiscard]] std::vector<InspectorSection> InspectEntity(voxel::Game &game, ecs::Entity entity);

} // namespace voxelia::studio
