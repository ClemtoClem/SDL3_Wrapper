#pragma once
/**
 * game_editor — OBJETS : arborescences de nœuds réutilisables (`.object`).
 *
 * Un objet est un document à part entière (`SceneDesc` de genre OBJECT,
 * fichier `objects/<Nom>.object`) : un arbre de nœuds, sans réglages du
 * monde (caméra, ambiance, ciel), sans calque 2D ni script de jeu — ce qui
 * reste propre à la SCÈNE, élément final qui les assemble.
 *
 * Une INSTANCE d'objet, dans une scène ou dans un autre objet, est un nœud
 * qui porte le composant `ObjectInstance { source = nom de l'objet }` et son
 * propre transform (position, rotation, taille). Son contenu n'est PAS
 * enregistré : il est GÉNÉRÉ à partir de l'objet (copie des enfants de la
 * racine de l'objet, identifiants neufs, références internes re-câblées),
 * chaque nœud généré portant la propriété `object_generated`. Modifier
 * l'objet puis re-développer ses instances (ExpandProject) propage donc la
 * modification à toutes les scènes, sans rien dupliquer sur le disque.
 *
 * Un objet peut contenir des instances d'autres objets : ils sont développés
 * dans l'ordre des dépendances ; un cycle (A contient B qui contient A) est
 * signalé et l'instance fautive reste vide.
 *
 * Sans dépendance au runtime : testable sans fenêtre.
 */
#include "core/core.hpp"
#include "project.hpp"
#include "scene/scene.hpp"

#include <vector>

namespace game_editor::objects {

/// Composant d'une instance : `source` = nom de l'objet.
inline constexpr const char* INSTANCE_COMPONENT = "ObjectInstance";
/// Propriété des nœuds générés à partir d'un objet (jamais enregistrés).
inline constexpr const char* GENERATED_PROPERTY = "object_generated";

[[nodiscard]] bool IsInstance(const scene::Node& node) noexcept;

/// Nom de l'objet dont `node` est une instance (vide sinon).
[[nodiscard]] String SourceOf(const scene::Node& node);

[[nodiscard]] bool IsGenerated(const scene::Node& node) noexcept;

/// Fait de `node` une instance de `objectName` (le contenu reste à développer).
void MakeInstance(scene::Node& node, const String& objectName);

/// Pour un nœud généré : l'instance (non générée) qui le contient — le nœud
/// que l'éditeur sélectionne et déplace. Pour tout autre nœud : lui-même.
[[nodiscard]] scene::NodeId EditableAncestor(const scene::NodeTree& tree, scene::NodeId id);

/// Copie de `tree` SANS les nœuds générés : ce qui s'enregistre.
[[nodiscard]] scene::NodeTree StripGenerated(const scene::NodeTree& tree);

/// Noms des objets instanciés DIRECTEMENT dans `tree` (instances non
/// générées), sans doublon.
[[nodiscard]] std::vector<String> DirectDependencies(const scene::NodeTree& tree);

/// Vrai si `tree` instancie `objectName`, directement ou à travers d'autres
/// objets du projet.
[[nodiscard]] bool DependsOn(const Project& project, const scene::NodeTree& tree,
							 const String& objectName);

/// Bilan d'un développement.
struct ExpandReport {
	int instances = 0; ///< instances (re)développées
	std::vector<String> errors;
};

/**
 * Re-développe les instances non générées de `tree` à partir des objets de
 * `project` tels qu'ils sont (déjà développés eux-mêmes : cf. ExpandProject).
 * `owner` : nom du document développé (objet ou scène), pour refuser qu'un
 * objet s'instancie lui-même.
 */
ExpandReport ExpandInstances(scene::NodeTree& tree, const Project& project, const String& owner);

/// Re-développe la seule instance `id` (les autres nœuds de `tree` ne
/// bougent pas : leurs identifiants restent valides).
ExpandReport ExpandInstance(scene::NodeTree& tree, scene::NodeId id, const Project& project,
							const String& owner);

/// Développe tous les objets (dépendances d'abord), puis toutes les scènes.
ExpandReport ExpandProject(Project& project);

/// Renomme les références `from` → `to` dans tous les documents (instances
/// non générées) ; rend le nombre d'instances touchées.
int RenameReferences(Project& project, const String& from, const String& to);

} // namespace game_editor::objects
