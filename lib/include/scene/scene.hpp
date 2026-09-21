#pragma once
/**
 * Module `scene::` — hiérarchie de nœuds générique (composition récursive
 * parent/enfants), fondation partagée par l'éditeur, le rendu et la physique.
 *
 * Trois fichiers, du plus élémentaire au plus complet :
 *  - property.hpp : NodeId + valeurs éditables typées (PropertyValue/Map) ;
 *  - node.hpp     : Transform, Component, Node ;
 *  - tree.hpp     : NodePath, NodeTree (hiérarchie, transforms, validation,
 *                   sérialisation) ;
 *  - type_registry.hpp : catalogue des types de nœuds (extensible) ;
 *  - packed.hpp   : scène réutilisable (PackedScene) et instanciation.
 *
 * À ne pas confondre avec : `ui::Scene` (une PAGE d'interface) et le graphe
 * `render3d::Object3D` (la scène de RENDU, pilotée depuis celle-ci).
 */
#include "node.hpp"
#include "packed.hpp"
#include "property.hpp"
#include "tree.hpp"
#include "type_registry.hpp"
