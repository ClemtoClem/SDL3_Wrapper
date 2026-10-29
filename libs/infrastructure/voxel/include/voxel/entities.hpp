#pragma once
/**
 * Entités du jeu : fabriques (depuis un EntityDef ou pour les entités du
 * moteur : joueur, objet au sol, bloc qui tombe) et COMPORTEMENTS.
 *
 * Une entité n'est qu'une composition de composants (components.hpp) : la
 * fabrique pose ceux que décrit la définition — forme, collision, rendu,
 * physique, sons, santé, propriétés libres, comportements — et rien
 * d'autre ; un mod peut en ajouter ou en retirer par script.
 *
 * Comportements intégrés (paramètres dans la définition) :
 *   wander {radius, speed, pause}       errer au hasard
 *   follow {item, range, speed}         suivre le joueur (qui tient `item`)
 *   flee   {range, speed}               fuir le joueur
 *   hop    {interval}                   sauts réguliers
 *   float                               remonter à la surface de l'eau
 *   look_at_player {range}              s'orienter vers le joueur
 *   idle                                rien
 * Un comportement de MOD (BehaviorDef "script") est exécuté par la
 * fonction `ScriptBehavior` fournie par l'hôte de scripts.
 */
#include <functional>

#include <ecs/ecs.hpp>

#include "components.hpp"
#include "inventory.hpp"
#include "world.hpp"

namespace voxel {

/// Son à jouer (événement ECS lu par le moteur audio de l'application).
struct SoundEvent {
	String sound;                          ///< SoundDef
	math::FVector3 position{0.f, 0.f, 0.f};
	float volume = 1.f, pitch = 1.f;
	bool positional = true;
	int variant = 0; ///< variante du SoundDef (fichier tiré au hasard)
	/// Canal (ambiance, musique) : un son à la fois, l'ancien fondu en sortie
	/// et le nouveau en entrée ; vide : son libre.
	String channel;
	float fadeIn = 0.f, fadeOut = 0.f;
	bool loop = false;
	bool stop = false; ///< éteindre le canal (`sound` vide)
};

/// Message de jeu (chat, console, notifications).
struct GameMessage {
	String text;
	String channel = "game";
};

/// Le joueur a changé de dimension (le monde affiché a été remplacé).
struct DimensionChanged {
	ecs::Entity player;
	String from, to;
};

struct EntityDied {
	ecs::Entity entity;
	String kind;
	math::FVector3 position{0.f, 0.f, 0.f};
};

inline constexpr const char *PLAYER_KIND = "voxel:player";
inline constexpr const char *ITEM_KIND = "voxel:item";
inline constexpr const char *FALLING_BLOCK_KIND = "voxel:falling_block";
inline constexpr const char *STRUCTURE_KIND = "voxel:structure";

[[nodiscard]] ecs::Entity SpawnFromDef(ecs::ArchetypeRegistry &registry, const EntityDef &def, const math::FVector3 &feet);
[[nodiscard]] ecs::Entity SpawnPlayer(ecs::ArchetypeRegistry &registry, const String &name, const math::FVector3 &feet);
[[nodiscard]] ecs::Entity SpawnDroppedItem(ecs::ArchetypeRegistry &registry, const ContentRegistry &content, ItemStack stack,
										   const math::FVector3 &position, const math::FVector3 &velocity = {0.f, 3.f, 0.f});
/// Assemblage de blocs mobile (cf. Visual::structure, Carrier) : coin minimal
/// en `corner`, plancher à `floor` (relatif) ; jamais enregistré.
[[nodiscard]] ecs::Entity SpawnStructure(ecs::ArchetypeRegistry &registry, std::shared_ptr<const Schematic> structure,
										 const math::FVector3 &corner, float floor);
[[nodiscard]] ecs::Entity SpawnFallingBlock(ecs::ArchetypeRegistry &registry, const ContentRegistry &content, BlockId block,
											uint8_t param, const BlockPos &from);

/// Exécution d'un comportement scripté : (entité, état, pas) ; faux si la
/// fonction n'existe pas.
using ScriptBehavior = std::function<bool(ecs::Entity entity, BehaviorState &state, float dt)>;

/// Un pas d'IA : les comportements écrivent la MoveIntent et l'orientation.
void StepBehaviors(ecs::ArchetypeRegistry &registry, const World &world, float dt, uint32_t seed,
				   const ScriptBehavior &scriptBehavior = {});

/// Joueurs les plus proches (pour l'IA et les scripts).
[[nodiscard]] Option<ecs::Entity> NearestPlayer(ecs::ArchetypeRegistry &registry, const math::FVector3 &from, float range);

} // namespace voxel
