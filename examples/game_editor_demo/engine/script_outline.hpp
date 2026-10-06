#pragma once
/**
 * game_editor — ce qu'un script DÉFINIT, lu sans l'exécuter.
 *
 * Le moteur instancie la classe d'un script dérivée de `Scene` (script de
 * scène) ou de `Behaviour` (script de nœud) : un script qui n'en définit pas,
 * ou plusieurs, n'est refusé qu'au lancement du mode Jeu. L'éditeur, lui,
 * veut le savoir en ÉDITION — dans l'inspecteur, le navigateur de
 * ressources, l'éditeur de code — sans exécuter le moindre code (un script
 * de scène peut créer des objets ou changer de scène dès son premier niveau).
 *
 * L'analyse est donc STATIQUE : le script est analysé syntaxiquement, ses
 * classes relevées (y compris dans `namespace { }`), et chaque base de
 * `extends` résolue : classe du même fichier, base du moteur (`Mesh3D`,
 * `game.Mesh3D`), ou classe d'un module importé (`const m = import "m"` puis
 * `extends m.Base`), le module étant lui-même analysé. Les règles appliquées
 * sont celles du moteur (cf. Interpreter::ClassesDerivedFrom) : seules
 * comptent les classes FEUILLES — ni abstraites, ni parentes d'une autre
 * classe retenue —, celles des modules importés comprises.
 */
#include "core/core.hpp"
#include "data/script/script_lexer.hpp"

#include <functional>
#include <vector>

namespace game_editor {

/// Une classe d'un script, bases résolues.
struct ScriptClassInfo {
	String name;   ///< qualifié par son espace de noms (`racing.Bolide`)
	String module; ///< module d'origine (vide : le script analysé)
	int line = 0;
	bool isAbstract = false;
	String parent;					 ///< classe parente de script (vide : aucune, ou introuvable)
	std::vector<String> engineBases; ///< bases du moteur, héritées comprises, ordre de construction
	std::vector<String> unresolved; ///< bases citées que l'analyse n'a pas su résoudre
	std::vector<String> hooks; ///< rappels définis : init, on_start, on_update, on_destroy, deinit…

	[[nodiscard]] bool Derives(const char* shortBase) const;
	/// « Torche (Behaviour, Light3D) ».
	[[nodiscard]] String Signature() const;
};

/// Comment l'éditeur utilise le script.
enum class ScriptUse : uint8_t {
	SCENE,	 ///< script de jeu d'une scène : doit définir UNE classe Scene
	LIBRARY, ///< script de la bibliothèque : une Behaviour (attachable) ou un module
};

/// Ce que le moteur fera du script.
enum class ScriptRole : uint8_t {
	EMPTY,	   ///< rien à exécuter
	SCENE,	   ///< une classe Scene instanciée au lancement
	BEHAVIOUR, ///< une classe Behaviour, une instance par nœud porteur
	MODULE, ///< des déclarations à importer (`import "nom"`) — attachable à un nœud seulement si
			///< `attachProblem` est vide
	INVALID, ///< script de scène refusé au lancement (cf. `problem`)
};

struct ScriptOutline {
	Option<data::script::ScriptError> error; ///< erreur de syntaxe (le reste est alors vide)
	std::vector<ScriptClassInfo> classes;	 ///< celles du script, puis celles de ses modules
	std::vector<String> imports;			 ///< modules importés (directement)
	ScriptRole role = ScriptRole::EMPTY;
	String mainClass; ///< la classe que le moteur instanciera
	String problem;	  ///< ce que le moteur refuserait ; vide si rien
	/// Script de la bibliothèque : pourquoi le moteur refuserait de l'ATTACHER à
	/// un nœud (aucune Behaviour, ou plusieurs) ; vide s'il s'attache. Un
	/// module importé n'a pas à s'attacher : ce n'est pas une erreur en soi.
	String attachProblem;

	[[nodiscard]] const ScriptClassInfo* Find(const String& name) const;
	/// Une ligne lisible : « comportement Torche (Behaviour, Light3D) »,
	/// « module : 3 classes », « erreur ligne 4 : … ».
	[[nodiscard]] String Summary() const;
};

[[nodiscard]] const char* ScriptRoleName(ScriptRole role) noexcept;

/// Source d'un module importé (`import "nom"`), NONE s'il est introuvable.
using ModuleSource = std::function<Option<String>(const String& specifier)>;

[[nodiscard]] ScriptOutline OutlineScript(const String& source, ScriptUse use,
										  const ModuleSource& modules);

} // namespace game_editor
