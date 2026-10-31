#pragma once
/**
 * script — la physique rigide 3D (`physics::`) vue depuis le langage
 * `Script`, espace de noms `physics`.
 *
 * Un monde physique de l'hôte (typiquement `physics::World` partagé avec
 * les composants ECS de l'application) est exposé au script :
 *
 *   let monde = physics.world()
 *   monde.gravity = [0, -9.81, 0]
 *
 *   # Un sol statique
 *   monde.spawn_static(physics.box([10, 0.5, 10]), [0, -0.5, 0])
 *   # Une sphère dynamique qui tombe
 *   let balle = monde.spawn(physics.sphere(0.5), [0, 5, 0], {mass: 1.0})
 *   monde.step(1.0 / 60.0)
 *   print(balle.position)
 *
 * Les corps (`physics.body`) sont des poignées `{ monde, entité }` : leurs
 * propriétés (position, velocity, mass…) sont synchronisées avec les
 * composants `RigidBody` du registre ECS sous-jacent.
 */
#include <physics/world.hpp>
#include <script/script_std.hpp>

namespace script {

/// Installe `physics.{world,body,sphere,box,capsule}`. `world` doit rester
/// en vie tant que le script tourne.
void InstallPhysicsLibrary(Interpreter &vm, physics::World &world);

} // namespace script