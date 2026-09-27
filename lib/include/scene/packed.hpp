#pragma once
/**
 * scene::PackedScene — une hiérarchie RÉUTILISABLE, et son instanciation.
 *
 *     Car.scene                     RaceTrack
 *     ├── Body                       ├── Car01   ─┐
 *     ├── Wheels                     ├── Car02   ─┼─ trois instances de
 *     │   ├── FrontLeft              └── Car03   ─┘  la MÊME définition
 *     │   └── …
 *     └── Camera
 *
 * ── Ce qu'une instance est, et ce qu'elle n'est pas ──────────────────────
 * Instancier COPIE l'arbre de la scène source dans l'arbre cible, avec des
 * identifiants neufs et les références internes re-câblées (exactement
 * NodeTree::Duplicate, déjà éprouvé). Ce n'est donc pas un lien vivant : une
 * modification ultérieure du fichier source ne se propage pas d'elle-même aux
 * instances déjà posées.
 *
 * C'est un choix, pas un oubli. Un lien vivant impose de savoir, pour chaque
 * valeur d'une instance, si elle vient de la source ou si l'utilisateur l'a
 * redéfinie localement — c'est le « système d'override » que la spécification
 * range elle-même dans les suites possibles. La racine de chaque instance
 * garde donc une trace de sa provenance (composant `SceneInstance`, ci-dessous)
 * pour que l'éditeur l'affiche, et pour qu'un tel système puisse être ajouté
 * plus tard sans changer le format : les instances existantes sauront déjà
 * d'où elles viennent.
 */
#include "tree.hpp"

namespace scene {

/// Types de composants connus de la bibliothèque elle-même (tous les autres
/// sont déclarés par l'application, cf. type_registry.hpp).
namespace component_type {
/// Posé sur la RACINE d'une instance : mémorise la scène d'origine.
/// Propriétés : `source` (RESOURCE — chemin du fichier .scene).
inline constexpr const char* SCENE_INSTANCE = "SceneInstance";
} // namespace component_type

class PackedScene {
public:
	PackedScene() = default;

	/// Emballe le sous-arbre `root` (de `tree`) en scène réutilisable. La
	/// racine de la scène emballée est une COPIE de `root` : son transform
	/// local est conservé et sert de transform par défaut à chaque instance.
	[[nodiscard]] static PackedScene FromSubtree(const NodeTree& tree, NodeId root);

	[[nodiscard]] bool IsEmpty() const noexcept;
	[[nodiscard]] const NodeTree& Tree() const noexcept { return m_tree; }
	[[nodiscard]] NodeTree& Tree() noexcept { return m_tree; }

	/// Chemin d'où vient cette scène (posé par l'application au chargement) —
	/// recopié dans le composant `SceneInstance` de chaque instance.
	[[nodiscard]] const String& Source() const noexcept { return m_source; }
	void SetSource(String source) { m_source = std::move(source); }

	/**
	 * Instancie la scène sous `parent` dans `target`. Le nœud racine créé
	 * reçoit `name` (ou le nom de la scène si `name` est vide), un nom rendu
	 * unique entre ses frères, et le composant `SceneInstance`.
	 *
	 * Rend l'identifiant de la racine de l'instance, invalide si `parent`
	 * n'existe pas.
	 */
	NodeId InstantiateInto(NodeTree& target, NodeId parent, const String& name = String(),
						   size_t index = NODE_APPEND) const;

	/// Vrai si ce nœud est la racine d'une instance de scène.
	[[nodiscard]] static bool IsInstanceRoot(const Node& node) noexcept;

	/// Chemin de la scène dont ce nœud est une instance (vide sinon).
	[[nodiscard]] static String InstanceSource(const Node& node);

	// ── Sérialisation ────────────────────────────────────────────────────────

	[[nodiscard]] data::NodePtr ToJson() const;

	[[nodiscard]] static Result<PackedScene, String> FromJson(const data::NodePtr& json);

	[[nodiscard]] String EncodeJson() const;

	[[nodiscard]] static Result<PackedScene, String> DecodeJson(const String& text);

private:
	static void CopyChildren(const NodeTree& from, NodeId fromNode, NodeTree& to, NodeId toNode);

	/// Après emballage, les références internes pointent encore sur les
	/// identifiants de l'arbre D'ORIGINE : on les redirige vers les copies.
	/// Une référence qui sortait du sous-arbre est effacée (elle désignerait
	/// un nœud absent de la scène emballée, donc une référence cassée dès la
	/// première instanciation).
	void RemapInternalRefs(const NodeTree& from, NodeId fromRoot);

	NodeTree m_tree{String("Scene")};
	String m_source;
};

} // namespace scene
