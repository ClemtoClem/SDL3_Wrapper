#pragma once
/**
 * Composants ECS des entités du jeu voxel. Une entité est une composition
 * quelconque de ces composants (et de ceux des mods : `Properties` porte
 * les propriétés libres de sa définition JSON).
 */
#include <array>
#include <vector>

#include <core/core.hpp>
#include <dataio/node.hpp>
#include <ecs/ecs.hpp>
#include <math/math.hpp>

#include "content.hpp"

namespace voxel {

/// Position (pieds de l'entité, au centre de sa base) et orientation.
struct Transform {
	math::FVector3 position{0.f, 0.f, 0.f};
	math::FVector3 previous{0.f, 0.f, 0.f}; ///< avant le dernier pas fixe (interpolation)
	float yaw = 0.f, pitch = 0.f;           ///< radians
	[[nodiscard]] math::FVector3 Forward() const noexcept;
};

struct Velocity {
	math::FVector3 linear{0.f, 0.f, 0.f};
};

/// Volume de collision : boîte centrée en x/z, base aux pieds.
struct Collider {
	math::FVector3 size{0.6f, 1.8f, 0.6f};
	bool collidesWithEntities = true;
	bool pushable = true;
	[[nodiscard]] Box WorldBox(const math::FVector3 &feet) const noexcept { return Box::AtFeet(feet, size); }
};

struct PhysicsBody {
	float mass = 1.f, gravityScale = 1.f, friction = 0.6f, bounciness = 0.f, drag = 0.f;
	float stepHeight = 0.6f, jumpSpeed = 8.4f, maxSpeed = 4.3f;
	bool swims = true, flies = false, noclip = false;
	/// Sens de la gravité : 1 vers le bas, -1 vers le haut (marcher au plafond).
	float gravityDirection = 1.f;
	// État (écrit par la physique)
	bool onGround = false, inFluid = false, onClimbable = false, hitWall = false, hitCeiling = false;
	bool inSinking = false; ///< dans des sables mouvants
	bool trapped = false;   ///< la tête dans un bloc plein (sable tombé dessus…) : immobilisé
	float fluidViscosity = 0.f;
	math::FVector3 current{0.f, 0.f, 0.f}; ///< courant du liquide traversé (blocs/s ; y < 0 : chute)
	float groundSlipperiness = 0.f;
	BlockId groundBlock = AIR;
	float fallDistance = 0.f;
	float lastImpactSpeed = 0.f; ///< vitesse verticale à l'atterrissage (dégâts de chute)
	/// Cellule où le corps a réveillé les liquides pour la dernière fois
	/// (cf. FluidSimulation::Disturb) ; `stirred` faux hors d'un liquide.
	BlockPos stirredCell{0, 0, 0};
	bool stirred = false;
};

/// Intention de déplacement (joueur ou IA) : la physique la convertit en
/// accélération selon le sol (glace, fluide…).
struct MoveIntent {
	math::FVector3 wish{0.f, 0.f, 0.f}; ///< direction horizontale souhaitée (longueur ≤ 1)
	float speed = 4.3f;
	bool jump = false, sneak = false, sprint = false;
	bool descend = false;               ///< vol / nage : descendre
};

/// Identité d'une entité définie par le contenu.
struct EntityKind {
	String id;       ///< EntityDef (créature, entité) ou "voxel:player", "voxel:item", "voxel:falling_block"
	String kind;
	const EntityDef *def = nullptr;
};

struct Health {
	float current = 10.f, max = 10.f;
	float invulnerable = 0.f; ///< secondes restantes
	ecs::Entity lastAttacker{}; ///< pour l'expérience gagnée par le vainqueur
	float hurtTime = 0.f;       ///< secondes depuis le dernier coup (affichage)
	/// Cause du dernier dégât : « suffocation », « drowning », « fall »,
	/// « void », « fire », « starvation », « attack »… (mort : réapparition).
	String lastCause;
	[[nodiscard]] bool IsDead() const noexcept { return current <= 0.f; }
};

/// Ressources de survie d'un joueur.
struct Survival {
	float hunger = 20.f, saturation = 5.f, exhaustion = 0.f; ///< faim 0..20
	float stamina = 1.f;                                     ///< endurance 0..1 (course)
	float air = 10.f, maxAir = 10.f;                         ///< secondes de souffle sous l'eau
	int xp = 0;                                              ///< expérience totale
	float regenTimer = 0.f, starveTimer = 0.f, drownTimer = 0.f;
	bool suffocating = false;                                ///< tête dans un bloc plein / du sable (HUD)
};

/// Effet de statut (poison, régénération, vitesse…) : décrits par la
/// configuration des mods (« effects »).
struct StatusEffect {
	String id;
	float remaining = 0.f; ///< secondes
	int level = 1;
	float timer = 0.f;
};
struct StatusEffects {
	std::vector<StatusEffect> list;
	[[nodiscard]] const StatusEffect *Find(const String &id) const;
	[[nodiscard]] int Level(const String &id) const;
};


/// Apparence (forme, textures, matériau) — reprise de la définition, mais
/// modifiable par les scripts entité par entité.
struct Visual {
	ShapeDesc shape;
	Appearance look;
	BlockId block = AIR; ///< entité qui ressemble à un bloc (bloc qui tombe, objet posé)
	float scale = 1.f;
	bool visible = true;
	/// Modèle articulé (prioritaire sur `shape`) et skin choisi.
	RigRef rig;
	String skin;
};

/// Propriétés libres (JSON de la définition + état des scripts).
struct Properties {
	NodePtr values;
};

/// Sons de l'entité (événement → SoundDef).
struct SoundEmitter {
	SoundSet sounds;
	float idleTimer = 0.f;
};

struct BehaviorState {
	String id;
	NodePtr params;
	float timer = 0.f;
	math::FVector3 target{0.f, 0.f, 0.f};
	bool hasTarget = false;
};

/// Comportements composés (exécutés dans l'ordre à chaque pas).
struct Behaviors {
	std::vector<BehaviorState> list;
};

struct ItemStack {
	String item;
	int count = 0;
	int wear = 0;          ///< usure (outils)
	NodePtr meta;          ///< données libres
	[[nodiscard]] bool IsEmpty() const noexcept { return count <= 0 || item.IsEmpty(); }
};

/// Équipement porté : armure (tête, torse, jambes, pieds).
enum class ArmorSlot : uint8_t { HEAD, CHEST, LEGS, FEET };
inline constexpr int ARMOR_SLOTS = 4;
struct Equipment {
	std::array<ItemStack, ARMOR_SLOTS> armor;
};

/// Objet au sol, ramassable.
struct DroppedItem {
	ItemStack stack;
	float pickupDelay = 0.5f;
	float age = 0.f;
};

/// Bloc qui tombe (sable…) : redevient un bloc à l'atterrissage.
struct FallingBlock {
	BlockId block = AIR;
	uint8_t param = 0;
};

struct Player {
	String name = "Joueur";
	int selectedSlot = 0;
	float reach = 5.f;
	float breakProgress = 0.f;
	BlockPos breaking{0, -100000, 0};
	/// Dimension où se trouve le joueur, et d'où il vient (portail de retour).
	String dimension;
	String returnDimension;
	math::FVector3 returnPosition{0.f, 0.f, 0.f};
	float portalTime = 0.f; ///< secondes passées dans un portail
	bool portalCooldown = false; ///< arrivé par un portail : il faut en sortir avant d'en reprendre un
	/// Dernier lit utilisé : point de réapparition (tant qu'il existe).
	bool hasBed = false;
	BlockPos bed{0, 0, 0};
	String bedDimension;
};

/// Entité enregistrée avec le monde (sinon : éphémère).
struct Persistent {
	Uuid uuid;
};

/// Rattachement à la sauvegarde d'un joueur, d'un coffre…
struct Named {
	String name;
};

} // namespace voxel
