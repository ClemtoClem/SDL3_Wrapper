#pragma once
/**
 * game_editor — les classes de BASE du moteur, dont dérivent les scripts de jeu.
 *
 * Le moteur définit en C++ des types de base (des « owners » au sens de
 * data/script/script_owners.hpp) ; un script de jeu DÉFINIT des classes qui
 * en dérivent, et le moteur les instancie et les appelle :
 *
 *   ┌──────────────┬──────────────────────────────────────────────────────┐
 *   │ Scene        │ contrôleur d'une scène : le script de scène définit  │
 *   │              │ UNE classe qui en dérive, instanciée au lancement.   │
 *   │ Behaviour    │ comportement d'un nœud : un script de la bibliothèque│
 *   │              │ définit UNE classe qui en dérive ; une instance par  │
 *   │              │ nœud qui porte le script.                            │
 *   │ Node3D       │ un nœud vide (ou décrit par une table) CRÉÉ par      │
 *   │              │ l'objet, retiré à sa destruction.                    │
 *   │ Mesh3D       │ un objet visible créé par l'objet (forme, modèle,    │
 *   │              │ matériau), retiré à sa destruction.                  │
 *   │ Node2D       │ un nœud 2D créé par l'objet (options de             │
 *   │              │ `node2d.spawn`), méthodes de `node2d.*`.             │
 *   │ PhysicsBody  │ le corps rigide du nœud porté par une base déclarée  │
 *   │              │ AVANT (Node3D, Mesh3D ou Behaviour).                 │
 *   │ Light3D      │ la lumière de ce même nœud.                          │
 *   │ Gameplay     │ abonnements à des évènements (`on`, `emit`).         │
 *   │ SceneAsset   │ un fichier `.scene` réutilisable ; ses instances     │
 *   │              │ sont retirées à la destruction.                      │
 *   │ ObjectAsset  │ un OBJET du projet (`.object`) : ses instances ne    │
 *   │              │ sont que des références qui suivent l'objet ;        │
 *   │              │ `define(nœud)` en fait le contenu de l'objet.        │
 *   └──────────────┴──────────────────────────────────────────────────────┘
 *
 * Rappels appelés par le moteur pendant une partie (méthodes facultatives) :
 *   Scene     : on_start(), on_trigger(zone, autre, évènement),
 *               on_trigger_exit(zone, autre, évènement), on_mouse_mode(mode)
 *   Behaviour : on_start() — une fois toutes les Behaviour de la scène (ou
 *               de la pièce instanciée) construites —, on_trigger(autre,
 *               évènement), on_trigger_exit(autre, évènement)
 *   tout objet vivant (Scene, Behaviour, Mesh3D, Node2D…) : on_update(dt)
 *               à chaque image, dans l'ordre de création
 *   tout objet : on_destroy() puis deinit() à sa destruction — `destroy()`,
 *               retrait de son nœud, ou fin de la scène jouée (séquence RAII
 *               de data/script/script_owners.hpp).
 *
 * Construction : `super.init(…)` passe les MÊMES arguments à chaque base ;
 * celles du moteur lisent la première chaîne comme un nom et la première
 * table comme des options (mêmes clés que `scene.spawn` : shape, size, pos,
 * rot, scale, color, material, body, mass…, plus `light: {…}`).
 *
 * Les bases sont dans l'espace de noms `game` et, par commodité, aussi
 * globales : `class Torche extends Behaviour, Light3D { … }`.
 */
#include "data/script.hpp"
#include "data/script/script_owners.hpp"
#include "scene/tree.hpp"

#include <memory>

namespace game_editor {

class Runtime;

/// Noms qualifiés des bases (pour reconnaître les classes dérivées).
namespace engine_base {
inline constexpr const char* SCENE = "game.Scene";
inline constexpr const char* BEHAVIOUR = "game.Behaviour";

/// Toutes les bases, par leur nom court (aussi global dans les scripts).
inline constexpr const char* ALL[] = {"Scene",	 "Behaviour",	"Node3D",	"Mesh3D",	 "Node2D",
									  "PhysicsBody", "Light3D", "Gameplay", "SceneAsset", "ObjectAsset"};

/// Le nom court d'une base du moteur (`Mesh3D` ou `game.Mesh3D`), ou NONE.
[[nodiscard]] Option<String> ShortName(const String& name);
} // namespace engine_base

/// Partie C++ commune aux bases qui PORTENT un nœud de la scène (Node3D,
/// Mesh3D, Node2D, Behaviour). Le nœud est désigné par son identifiant et par
/// l'arbre où il vit : si la scène change, le nœud est considéré disparu
/// (un identifiant ne désigne jamais un nœud d'une AUTRE scène).
struct NodeOwner : data::script::OwnerObject {
	Runtime* runtime = nullptr;
	scene::NodeId node;
	const scene::NodeTree* tree = nullptr;
	/// Le nœud a été créé par cette base : il est retiré à sa destruction
	/// (Node3D, Mesh3D, Node2D). Une Behaviour, elle, s'attache à un nœud
	/// existant, qui lui survit.
	bool ownsNode = false;

	/// Le nœud, ou nullptr s'il n'existe plus (retiré, autre scène).
	[[nodiscard]] scene::Node* Node() const;
	/// Nom (ou chemin s'il est ambigu) — la forme qu'emploie l'API `object.*`.
	[[nodiscard]] String Handle() const;

	void OnDeinit(data::script::Interpreter& vm) override;

	/// Une autre base porteuse de nœud est-elle déjà initialisée sur la même
	/// instance ? (Deux nœuds pour un même objet serait ambigu.)
	[[nodiscard]] Option<data::script::ScriptError> RejectSecondNode() const;
};

/// Installe les bases du moteur dans `vm` (espace de noms `game` + globales).
void InstallEngineBases(data::script::Interpreter& vm, Runtime& runtime);

} // namespace game_editor
