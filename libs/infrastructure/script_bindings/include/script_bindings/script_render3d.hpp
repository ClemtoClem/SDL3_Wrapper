#pragma once
/**
 * script — rendu 3D (`render3d::`) vu depuis le langage `Script`,
 * espace de noms `render3d`.
 *
 * Le script ne crée JAMAIS de Canvas ni de racine de scène — l'hôte les
 * possède et les passe à `InstallRender3DLibrary`. Il reçoit des poignées
 * (`render3d.node`) qu'il peut positionner, habiller, rattacher :
 *
 *   let scène = render3d.scene()                 # racine de l'hôte
 *   let cube = scène.create_shape(
 *       render3d.mesh.box(1, 1, 1),
 *       render3d.material({color: [0.5, 0.3, 0.1], roughness: 0.6}))
 *   cube.position = [0, 1, 0]
 *   cube.rotation = render3d.quat_from_euler(0, 0.5, 0)
 *   scène.add(cube)
 *
 *   # Plus tard, pour la scène déjà peuplée :
 *   for (n in scène.root.children()) { print(n.name) }
 *   scène.root.find("héros").position = [2, 0, -3]
 *
 * Les meshes et matériaux sont des objets script ordinaires (partageables
 * entre plusieurs shapes). Les nœuds sont des références à des `Object3D*`
 * qui restent valides tant que leur parent (ou la racine) existe.
 */
#include <render3d/canvas.hpp>
#include <render3d/mesh.hpp>
#include <render3d/object3d.hpp>
#include <script/script_std.hpp>

namespace script {

/// Installe `render3d.{mesh,material,camera,scene,node}`. `canvas` (optionnel)
/// permet à un script d'appeler `render3d.render(camera)`. `sceneRoot` est
/// le nœud racine, possédé par l'hôte.
void InstallRender3DLibrary(Interpreter &vm, render3d::Canvas *canvas = nullptr,
                            render3d::Object3D *sceneRoot = nullptr);

} // namespace script