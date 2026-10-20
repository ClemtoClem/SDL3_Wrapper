#pragma once
/**
 * Physique adaptée aux mondes voxel.
 *
 *  - Volumes de collision des blocs : cube plein, nodebox/boîtes FIXED,
 *    taille quelconque, orientés par le param du bloc (quarts de tour). Un
 *    chunk NON CHARGÉ compte comme plein (on ne tombe pas dans le vide en
 *    attendant le terrain).
 *  - Déplacement d'une boîte : axe par axe (Y, puis X et Z), chaque axe
 *    rogné contre les boîtes rencontrées — jamais de traversée, quelle que
 *    soit la vitesse ; montée automatique des marches (`stepHeight`).
 *  - Corps : gravité, sol (glissance des surfaces : la glace glisse),
 *    fluides (viscosité, poussée, nage), échelles, rebond, frottement de
 *    l'air ; hauteur de chute et vitesse d'impact (dégâts de chute).
 *  - Contacts entre entités : les boîtes qui se chevauchent se repoussent
 *    (si « pushable ») et émettent EntityContact.
 *  - Raycasts : blocs (DDA, précis jusqu'aux sous-boîtes des nodebox,
 *    fluides inclus ou non) et entités.
 */
#include <functional>

#include <ecs/ecs.hpp>

#include "components.hpp"
#include "world.hpp"

namespace voxel {

struct PhysicsSettings {
	float gravity = 28.f;          ///< blocs/s²
	float terminalVelocity = 60.f;
	float airControl = 0.25f;      ///< part de contrôle en l'air
	float groundAcceleration = 14.f;
	float fluidBuoyancy = 0.85f;   ///< part de la gravité compensée dans un fluide
};

/// Boîte d'un bloc orientée (param & 3 = quarts de tour autour de Y).
[[nodiscard]] Box RotateBlockBox(const Box &box, uint8_t param) noexcept;

/// Boîtes de collision MONDE des blocs solides qui touchent `region`.
void CollectBlockBoxes(const World &world, const Box &region, std::vector<Box> &out);
/// Boîtes de SÉLECTION (forme visible, solides ou non) d'un bloc, monde.
[[nodiscard]] std::vector<Box> SelectionBoxes(const World &world, const BlockPos &p);

struct MoveResult {
	math::FVector3 moved{0.f, 0.f, 0.f};
	bool hitX = false, hitY = false, hitZ = false;
	bool landed = false; ///< bloqué en descendant
	bool stepped = false;
};

/// Déplace `box` de `delta` sans traverser les blocs solides ; `box` est
/// mise à jour. Montée de marche si `stepHeight` > 0 et `onGround`.
MoveResult MoveBox(const World &world, Box &box, const math::FVector3 &delta, float stepHeight = 0.f, bool onGround = false);

struct BlockHit {
	BlockPos block;
	BlockPos previous;        ///< bloc d'où vient le rayon (où poser)
	Face face = Face::TOP;    ///< face touchée
	math::FVector3 point{0.f, 0.f, 0.f};
	float distance = 0.f;
};

/// Premier bloc touché (forme de sélection). `includeFluids` : l'eau arrête
/// le rayon (seau). `filter` : faux pour ignorer un bloc.
[[nodiscard]] Option<BlockHit> RaycastBlocks(const World &world, const math::FVector3 &origin, const math::FVector3 &direction,
											 float maxDistance, bool includeFluids = false,
											 const std::function<bool(const BlockDef &)> &filter = {});

struct EntityHit {
	ecs::Entity entity;
	float distance = 0.f;
	math::FVector3 point{0.f, 0.f, 0.f};
};
[[nodiscard]] Option<EntityHit> RaycastEntities(ecs::ArchetypeRegistry &registry, const math::FVector3 &origin,
												const math::FVector3 &direction, float maxDistance,
												ecs::Entity ignore = ecs::Entity{});
/// Intersection rayon/boîte (méthode des dalles) : distance d'entrée.
[[nodiscard]] Option<float> RayBox(const math::FVector3 &origin, const math::FVector3 &direction, const Box &box,
								   Face *face = nullptr);

/// Contact entre deux entités (événement ECS).
struct EntityContact {
	ecs::Entity a, b;
};
/// Une entité touche un bloc non solide qu'elle traverse (fluide, plante…)
/// ou atterrit sur un bloc (événement ECS, pour les scripts « on_touch »).
struct BlockContact {
	ecs::Entity entity;
	BlockPos block;
	BlockId id = AIR;
	bool landed = false;
	float impactSpeed = 0.f;
};

/// Un pas de physique sur toutes les entités (Transform, Velocity,
/// Collider, PhysicsBody [, MoveIntent]) puis les contacts entre entités.
void StepPhysics(ecs::ArchetypeRegistry &registry, const World &world, float dt, const PhysicsSettings &settings = {});

} // namespace voxel
