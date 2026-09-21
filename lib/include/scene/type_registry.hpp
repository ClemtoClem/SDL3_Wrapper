#pragma once
/**
 * scene::NodeTypeRegistry — le catalogue des TYPES de nœuds.
 *
 * ── Pourquoi un registre et pas une hiérarchie de classes ────────────────
 * La question posée par la spécification (« les types doivent-ils être des
 * classes, des composants, des fabriques, des identifiants ? ») se tranche en
 * regardant ce que le projet fait déjà : tout ce qui est éditable y est un
 * DESCRIPTEUR sérialisable, et la scène vivante est reconstruite depuis lui.
 * Une hiérarchie `class MeshInstance : public Node3D` ajouterait un second
 * modèle d'objets — non copiable trivialement (donc pas d'instantané
 * d'annulation), non sérialisable sans table de types maison, et fermé : un
 * utilisateur ne pourrait pas déclarer son propre type sans recompiler la
 * bibliothèque.
 *
 * Le type est donc une CHAÎNE portée par le nœud, et ce registre dit ce que
 * cette chaîne signifie : quels composants poser à la création, quelles
 * propriétés attendre, quel libellé afficher. Trois conséquences utiles :
 *
 *  - un type INCONNU (fichier venu d'une version plus récente, greffon
 *    absent) se charge, se sauvegarde et s'affiche quand même — dégradé,
 *    jamais perdu, comme le reste du format ;
 *  - l'éditeur construit son menu « Ajouter un nœud » à partir du registre,
 *    sans connaître un seul type à la compilation ;
 *  - l'inspecteur sait quelles propriétés proposer pour un type donné.
 *
 * Le registre est FACULTATIF : `NodeTree` fonctionne entièrement sans lui.
 */
#include <functional>
#include <vector>

#include "tree.hpp"

namespace scene {

/// Description d'un type de nœud.
struct NodeTypeInfo {
	String name;        ///< identifiant technique, celui écrit dans le fichier ("MeshInstance")
	String label;       ///< libellé affiché ("Maillage")
	String category;    ///< regroupement dans le menu de création ("3D", "Physique", "Audio"…)
	String icon;        ///< repère textuel court pour l'outliner ("▣")
	/// Composants posés d'office à la création (un MeshInstance naît avec un
	/// composant de maillage vide plutôt qu'avec rien).
	std::vector<Component> defaultComponents;
	/// Propriétés proposées d'office, avec leur valeur par défaut.
	PropertyMap defaultProperties;
	/// Un nœud de ce type peut-il recevoir des enfants ? (une feuille pure,
	/// comme un point d'ancrage, peut dire non — l'outliner refuse alors le
	/// dépôt au lieu de laisser construire une scène absurde).
	bool acceptsChildren = true;
};

class NodeTypeRegistry {
public:
	/// Registre pré-rempli avec les deux types de la bibliothèque.
	[[nodiscard]] static NodeTypeRegistry WithBuiltins() {
		NodeTypeRegistry registry;
		NodeTypeInfo node;
		node.name = String(node_type::NODE);
		node.label = String("Groupe");
		node.category = String("Structure");
		node.icon = String("▤");
		registry.Register(std::move(node));

		NodeTypeInfo node3d;
		node3d.name = String(node_type::NODE3D);
		node3d.label = String("Nœud 3D");
		node3d.category = String("Structure");
		node3d.icon = String("◇");
		registry.Register(std::move(node3d));
		return registry;
	}

	/// Déclare (ou remplace) un type.
	void Register(NodeTypeInfo info) {
		for (NodeTypeInfo &existing : m_types)
			if (existing.name == info.name) {
				existing = std::move(info);
				return;
			}
		m_types.push_back(std::move(info));
	}

	[[nodiscard]] const NodeTypeInfo *Find(const String &name) const noexcept {
		for (const NodeTypeInfo &info : m_types)
			if (info.name == name)
				return &info;
		return nullptr;
	}

	[[nodiscard]] bool Knows(const String &name) const noexcept { return Find(name) != nullptr; }
	[[nodiscard]] const std::vector<NodeTypeInfo> &Types() const noexcept { return m_types; }

	/// Libellé d'affichage (repli sur le nom technique pour un type inconnu,
	/// qui reste donc lisible dans l'outliner).
	[[nodiscard]] String LabelOf(const String &name) const {
		const NodeTypeInfo *info = Find(name);
		return info && !info->label.IsEmpty() ? info->label : name;
	}

	[[nodiscard]] String IconOf(const String &name) const {
		const NodeTypeInfo *info = Find(name);
		return info ? info->icon : String("?");
	}

	/// Nœud neuf conforme au type : composants et propriétés par défaut déjà
	/// en place. Un type inconnu donne un nœud nu portant quand même ce type
	/// (cf. en-tête : jamais perdre l'information).
	[[nodiscard]] Node Make(const String &type, String name) const {
		Node node;
		node.type = type;
		node.name = std::move(name);
		if (const NodeTypeInfo *info = Find(type)) {
			node.components = info->defaultComponents;
			node.properties = info->defaultProperties;
		}
		return node;
	}

	/// Crée directement dans un arbre (raccourci de l'éditeur).
	NodeId Create(NodeTree &tree, NodeId parent, const String &type, const String &name) const {
		return tree.Add(parent, Make(type, tree.UniqueChildName(parent, name)));
	}

	/// Prédicat prêt à passer à NodeTree::Validate.
	[[nodiscard]] std::function<bool(const String &)> TypeChecker() const {
		return [this](const String &type) { return Knows(type); };
	}

private:
	std::vector<NodeTypeInfo> m_types;
};

} // namespace scene
