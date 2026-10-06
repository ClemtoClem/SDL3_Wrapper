#pragma once
/**
 * data::script — interpréteur (parcours d'arbre) du langage embarqué.
 *
 * ── Pas d'exception, y compris pour le flot de contrôle ──────────────────
 * L'implémentation « naturelle » d'un interpréteur d'arbre propage `return`/
 * `break`/`continue` et les erreurs d'exécution par exceptions. Ce dépôt
 * l'interdit (cf. memory/feedback_no_exceptions.md), donc :
 *   - chaque instruction retourne `Result<ExecOutcome, ScriptError>`, où
 *     `ExecOutcome::flow` porte NORMAL/BREAK/CONTINUE/RETURN ;
 *   - chaque expression retourne `Result<Value, ScriptError>`.
 * Tout appelant DOIT tester `IsError()` puis propager — c'est verbeux mais
 * c'est exactement ce que la règle demande, et ça rend chaque point de
 * propagation visible.
 *
 * ── Garde-fous (un éditeur ne doit jamais se figer sur un script) ────────
 * `maxSteps` (budget d'instructions) et `maxCallDepth` (profondeur d'appel)
 * transforment une boucle infinie ou une récursion sans fin en une erreur
 * d'exécution ordinaire, au lieu d'un gel de l'application ou d'un
 * débordement de pile. Les deux sont réglables et remis à zéro à chaque
 * `Run()`.
 *
 * ── Fermetures et cycles ────────────────────────────────────────────────
 * Une fonction capture son environnement par `shared_ptr`, et cet
 * environnement contient la fonction elle-même dès qu'elle y est nommée :
 * c'est un CYCLE de `shared_ptr`, donc une fuite à la sortie du programme
 * (visible sous LeakSanitizer, avec lequel ce dépôt compile). Il n'y a pas
 * de ramasse-miettes ici ; l'interpréteur garde donc un registre
 * `weak_ptr` de tous les environnements créés et vide leurs liaisons dans
 * son destructeur (`BreakEnvironmentCycles`), ce qui casse tous les cycles
 * d'un coup. Limite assumée et documentée : la mémoire d'une fermeture
 * cyclique créée dans une boucle n'est récupérée qu'à la destruction de
 * l'interpréteur, pas avant.
 */
#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <condition_variable>
#include <cstring>
#include <cstdint>
#include <deque>
#include <memory>
#include <mutex>
#include <thread>
#include <utility>
#include <vector>

#include "../../core/core.hpp"
#include "script_owners.hpp"
#include "script_parser.hpp"
#include "script_value.hpp"

namespace data::script {

// ============================================================================
// Environment
// ============================================================================

/// Nature d'une liaison — décide des droits (cf. DeclKind, script_ast.hpp).
enum class BindingKind : uint8_t { VAR, LET, CONST, FUNCTION };

/// Une liaison nom → valeur. `initialized == false` : liaison HISSÉE mais pas
/// encore atteinte (`let`/`const` avant leur ligne, la « zone morte ») — la
/// lire ou l'affecter est une erreur. `host` : posée par l'application
/// (`editor`, `math`…), qu'un script ne peut pas redéclarer.
struct Binding {

	String name;
	Value value;
	BindingKind kind = BindingKind::VAR;
	bool initialized = true;
	bool host = false;
	TypeRef type; ///< type annoncé (`let x:i32`) — vérifié à chaque affectation
};

/// Copie de l'état d'une liaison, prise sous le verrou de sa portée.
struct BindingInfo {

	Value value;
	BindingKind kind = BindingKind::VAR;
	bool initialized = true;
	bool host = false;
	TypeRef type;
};

/// Portée lexicale : une liste de liaisons + un parent. Recherche linéaire
/// (même raison que MapObject, cf. script_value.hpp — une portée compte
/// quelques variables).
///
/// Une portée de FONCTION (corps de fonction, programme, espace de noms)
/// reçoit les `var` ; une portée de BLOC (`{ }`, tour de boucle) reçoit les
/// `let`/`const` (cf. Interpreter::Hoist).
///
/// ── Accès concurrents ──
/// Une fermeture capturée par une fonction `async` partage ses portées avec
/// le fil qui l'exécute : chaque portée a donc son `mutex`, et chaque accès
/// le prend (`lock_guard`) le temps d'une COPIE. Aucune référence vers une
/// liaison ne sort d'ici — c'est ce qui rend une variable partagée entre
/// fils sûre sans que le script ait à le demander. Le parent, lui, est fixé
/// à la construction : le remonter ne demande aucun verrou.
class Environment : public std::enable_shared_from_this<Environment> {

  public:
	Environment() = default;
	explicit Environment(std::shared_ptr<Environment> parent, bool functionScope = false) : m_parent(std::move(parent)), m_functionScope(functionScope) {}

	/// Définit (ou redéfinit) une liaison `var` initialisée — la forme des
	/// valeurs posées par l'hôte et des paramètres.
	void Define(const String& name, Value value);

	/// Crée — ou met à jour — la liaison `name` de CETTE portée (nature et
	/// état d'initialisation ; une valeur existante est gardée).
	void Declare(const String& name, BindingKind kind, bool initialized);

	/// Crée — ou remplace — la liaison `name` de CETTE portée, initialisée à
	/// `value`. `host` : la marque réservée à l'application.
	void Put(const String& name, BindingKind kind, Value value, bool host = false, TypeRef type = nullptr);

	/// Liaison de CETTE portée seulement (copie).
	[[nodiscard]] Option<BindingInfo> Local(const String& name) const;

	/// Liaison visible depuis cette portée (elle-même puis ses parentes).
	[[nodiscard]] Option<BindingInfo> Lookup(const String& name) const;

	/// Valeur visible depuis cette portée ; NONE si absente — jamais
	/// d'erreur ici : c'est l'appelant qui décide si une variable absente est
	/// une erreur (lecture) ou non (test d'existence).
	[[nodiscard]] Option<Value> Find(const String& name) const;

	enum class AssignResult : uint8_t { OK, UNKNOWN, CONSTANT, UNINITIALIZED, CHANGED };

	/// Affecte une variable EXISTANTE (dans cette portée ou une parente).
	/// Jamais de création implicite : une faute de frappe ne crée pas
	/// silencieusement un global (le piège classique de Lua).
	AssignResult Assign(const String& name, Value value);

	/// Comme Assign, mais seulement si la variable vaut encore `expected` —
	/// CHANGED sinon (un autre fil l'a modifiée depuis sa lecture) : c'est ce
	/// qui rend `x += 1` sûr entre fils (cf. Interpreter::ExecAssign).
	AssignResult CompareAndAssign(const String& name, const Value& expected, Value value);

	[[nodiscard]] bool IsFunctionScope() const noexcept;

	/// Portée de fonction la plus proche (elle-même si elle en est une) —
	/// là où vivent les `var`.
	[[nodiscard]] Environment* FunctionScope() noexcept;

	/// Copie des liaisons (noms compris).
	[[nodiscard]] std::vector<Binding> Bindings() const;

	/// `var` rencontré à l'exécution, dans CETTE portée (de fonction) :
	/// crée la liaison si besoin, l'initialise, et pose `value` s'il y en a
	/// une. Rend la nature d'une liaison `let`/`const` qui s'y oppose.
	[[nodiscard]] Option<BindingKind> DeclareVar(const String& name, Option<Value> value, TypeRef type = nullptr);

	/// `let`/`const` rencontré à l'exécution : faux si le nom est déjà
	/// déclaré (et initialisé) dans cette portée par une déclaration lexicale.
	[[nodiscard]] bool InitializeLexical(const String& name, BindingKind kind, Value value, TypeRef type = nullptr);

	/// Retire (et rend) les liaisons posées par le SCRIPT, en gardant celles
	/// de l'hôte — la première étape de l'arrêt : les objets du script
	/// meurent (et leurs `deinit` passent) tant que `print`, `std`… existent.
	[[nodiscard]] std::vector<Binding> ExtractScriptBindings();

	/// Vide les liaisons — utilisé UNIQUEMENT pour casser les cycles de
	/// fermeture à la destruction de l'interpréteur (cf. en-tête du fichier).
	void ClearBindings() noexcept;

  private:
	[[nodiscard]] Binding* FindLocked(const String& name) noexcept;

	AssignResult Write(const String& name, const Value* expected, Value value);

	mutable std::mutex m_mutex;
	std::vector<Binding> m_bindings;
	std::shared_ptr<Environment> m_parent;
	bool m_functionScope = false;
};

// ============================================================================
// Flot de contrôle
// ============================================================================

enum class Flow : uint8_t { NORMAL, BREAK, CONTINUE, RETURN };

struct ExecOutcome {

	Flow flow = Flow::NORMAL;
	Value value; ///< valeur de `return` (sinon nil)
};

// ============================================================================
// Interpreter
// ============================================================================

class Interpreter;

/// Bibliothèques installées par le constructeur : l'espace de noms `std` et
/// les fonctions globales (script_std.hpp), l'espace de noms `math`
/// (script_math.hpp) — inclus à la fin de ce fichier.
void InstallStdLibrary(Interpreter& vm);
void InstallMathLibrary(Interpreter& vm);

class Interpreter {
public:
	Interpreter();

	Interpreter(const Interpreter&) = delete;
	Interpreter& operator=(const Interpreter&) = delete;

	/// Arrête et JOINT les fils `async` encore actifs (leurs attentes sont
	/// interrompues, leurs boucles s'arrêtent à l'instruction suivante), fait
	/// passer les `deinit` des objets encore vivants (RAII jusqu'au bout),
	/// puis casse les cycles de fermetures.
	~Interpreter();

	// ── Réglages ─────────────────────────────────────────────────────────────

	/// Budget d'instructions par `Run()`/`CallGlobal()` (et par fil `async`)
	/// — dépassement = erreur d'exécution, jamais un gel (cf. en-tête).
	uint64_t maxSteps = 20000000;
	/// Profondeur d'appel maximale — dépassement = erreur, jamais un
	/// débordement de pile.
	size_t maxCallDepth = 200;
	/// Fils `async` simultanés au plus (chaque appel en crée un).
	size_t maxAsyncTasks = 256;

	/// Destination de `print`. Si nul, la sortie est accumulée dans
	/// `Output()` (ce dont se servent les tests et la console de l'éditeur).
	/// Toujours appelée sur le fil PRINCIPAL : ce qu'un fil `async` affiche
	/// attend le prochain PumpMainThread().
	std::function<void(const String&)> onPrint;
	/// Erreur d'une fonction `async` que personne n'a attendue (`await`) —
	/// sur le fil principal, depuis PumpMainThread(). Sans elle, ces erreurs
	/// vont dans `Output()`.
	std::function<void(const ScriptError&)> onAsyncError;

	/// Lignes affichées. À lire sur le fil principal une fois les fils
	/// `async` terminés (sinon, cf. `onPrint`).
	[[nodiscard]] const std::vector<String>& Output() const noexcept;

	void ClearOutput();

	/// Graine du générateur pseudo-aléatoire de `random()` — fixée par
	/// défaut pour que deux exécutions d'un même script donnent EXACTEMENT
	/// la même chose (rapports et captures d'écran reproductibles).
	void SetRandomSeed(uint64_t seed) noexcept;

	// ── Bases fournies par l'hôte (owners) ───────────────────────────────────

	/// Détruit une instance dont la classe dérive d'une base de l'hôte, selon
	/// la séquence RAII de script_owners.hpp : `on_destroy()` → `deinit()` de
	/// chaque classe (dérivée d'abord) → `OnDeinit` des bases en ordre inverse
	/// → retrait du registre. Idempotent ; sans effet sur une instance qui ne
	/// dérive d'aucune base de l'hôte. Sur le fil principal uniquement.
	void DestroyInstance(const std::shared_ptr<InstanceObject>& instance);

	/// Instances vivantes qui possèdent une base de l'hôte (cf. OwnerRegistry).
	[[nodiscard]] OwnerRegistry& Owners() noexcept { return m_owners; }
	[[nodiscard]] const OwnerRegistry& Owners() const noexcept { return m_owners; }

	// ── Imports ──────────────────────────────────────────────────────────────

	/// Répertoires de recherche des modules, dans l'ordre. Le répertoire du
	/// script importateur est essayé AVANT ceux-ci (imports relatifs) ; un
	/// chemin absolu est essayé tel quel en premier.
	void AddImportPath(const String& directory);

	/// Copie de la liste (lecture).
	[[nodiscard]] std::vector<String> ImportPaths() const;

	/// Résolveur personnalisé, consulté AVANT tout accès disque. Rend, pour
	/// un spécificateur, une paire (clé de cache, code source) — utile pour
	/// exposer des modules EMBARQUÉS (scripts de l'hôte, ressources,
	/// patches). NONE : l'interpréteur cherche un fichier.
	using ImportResolver = std::function<Option<std::pair<String, String>>(const String&)>;
	void SetImportResolver(ImportResolver resolver);

	/// Exécute (au plus une fois par chemin résolu) le module et rend sa
	/// valeur (cf. ImportExpr). Les erreurs internes sont préfixées du nom
	/// du module, sans écraser leur position source.
	[[nodiscard]] Result<Value, ScriptError> ImportModule(const String& specifier, int line, int column);

	/// Vrai si `specifier` résout vers un module déjà chargé.
	[[nodiscard]] bool IsModuleLoaded(const String& specifier) const;

	/// Chemin canonique d'un spécificateur, ou NONE s'il est introuvable.
	/// Utilisable par l'hôte pour VALIDER une liste d'imports sans exécuter.
	[[nodiscard]] Option<String> ResolveImport(const String& specifier) const;

	// ── Globales et fonctions natives ────────────────────────────────────────

	/// Où une native peut s'exécuter (cf. NativeObject::anyThread).
	enum class NativeThread : uint8_t {
		DEFAULT, ///< l'API de l'hôte : fil principal ; la bibliothèque standard : n'importe lequel
		MAIN,	 ///< toujours sur le fil principal (appel depuis un fil `async` = mis en file)
		ANY,	 ///< sur le fil de l'appelant : la native doit être sûre entre fils
	};

	/// Pose une globale de l'HÔTE : un script peut la lire et la réaffecter,
	/// pas la redéclarer par `let`/`const`.
	void SetGlobal(const String& name, Value value);

	[[nodiscard]] Option<Value> GetGlobal(const String& name) const;

	[[nodiscard]] bool HasGlobal(const String& name) const;

	/// Expose une fonction C++ au script. `maxArity < 0` = variadique.
	/// L'arité est vérifiée AVANT l'appel : une native n'a jamais à valider
	/// le nombre de ses arguments elle-même.
	void RegisterNative(const String& name, int minArity, int maxArity, NativeFn fn,
						NativeThread thread = NativeThread::DEFAULT);

	/// Espace de noms de l'hôte `name` (créé au premier appel) — c'est là que
	/// l'application range son API par domaine (`editor.`, `scene.`, `math.`).
	/// Ses membres sont des CONSTANTES : un script ne peut pas remplacer
	/// `editor.log` par accident.
	/// Un chemin pointé (`std.filesystem`) désigne un espace de noms IMBRIQUÉ
	/// (créé au besoin dans son parent).
	[[nodiscard]] std::shared_ptr<NamespaceObject> HostNamespace(const String& name);

	/**
	 * Déclare un type de l'hôte sous `ns.name` (`std.vector`) : sa valeur est
	 * son constructeur, qui porte aussi ses membres de classe et le nomme
	 * dans les annotations (`let v:std.vector<i32>`) et les tests (`is`).
	 */
	std::shared_ptr<const HostType> RegisterHostType(const String& ns, const String& name,
													 std::shared_ptr<HostType> type);

	/// Pose une constante dans un espace de noms de l'hôte (`math.pi`).
	void RegisterNamespaceConstant(const String& ns, const String& name, Value value);

	/// Range une native dans un espace de noms de l'hôte (`scene.spawn(...)`).
	void RegisterNamespacedNative(const String& ns, const String& name, int minArity, int maxArity, NativeFn fn,
								  NativeThread thread = NativeThread::DEFAULT);

	/// Range dans l'espace de noms `ns` une copie des NATIVES globales de
	/// l'hôte (`print`, `len`…) — sans remplacer un membre déjà présent.
	void ExportGlobalsTo(const String& ns);

	/// Membre `member` de l'espace de noms `ns` (NONE si l'un ou l'autre
	/// n'existe pas) — lecture côté hôte.
	[[nodiscard]] Option<Value> GetNamespaceMember(const String& ns, const String& member) const;

	// ── Exécution ────────────────────────────────────────────────────────────

	/// Compile puis exécute une source. Les définitions (`let`, `fn`) restent
	/// dans les globales : un second `Run()` voit ce que le premier a défini,
	/// et l'hôte peut ensuite appeler les fonctions déclarées via
	/// `CallGlobal` — c'est ce qui permet à un script d'éditeur de déclarer
	/// `on_update(dt)` une fois et d'être piloté image par image ensuite.
	[[nodiscard]] Result<Value, ScriptError> Run(StringView source);

	/// Exécute un programme dans les globales. Ses déclarations de premier
	/// niveau sont HISSÉES d'abord ; les `let`/`const` d'un programme déjà
	/// exécuté dans cet interpréteur sont redéclarables par le suivant (la
	/// console de l'éditeur rejoue un même extrait — comme une console de
	/// navigateur).
	[[nodiscard]] Result<Value, ScriptError> RunProgram(const Program& program);

	/// Appelle une fonction globale déclarée par un script (`on_update`,
	/// `on_start`…). `NONE` si elle n'existe pas — l'hôte distingue ainsi
	/// « pas de rappel déclaré » (normal) d'une vraie erreur d'exécution.
	[[nodiscard]] Option<Result<Value, ScriptError>> CallGlobalIfPresent(const String& name, std::vector<Value> args);

	/// Appelle la méthode de SCRIPT `name` d'une instance si sa classe (ou un
	/// ancêtre) la définit — NONE sinon. C'est ainsi qu'un hôte appelle les
	/// rappels d'un objet (`on_update`…) sans exiger qu'ils existent tous.
	[[nodiscard]] Option<Result<Value, ScriptError>> CallMethodIfPresent(const Value& object, const String& name,
																		 std::vector<Value> args);

	/// Classes déclarées par cet interpréteur qui dérivent (directement ou
	/// non) de la base de l'hôte nommée `qualifiedName` (`game.Scene`) — les
	/// FEUILLES seulement : ni abstraites, ni parentes d'une autre classe
	/// retenue. Dans l'ordre de déclaration.
	[[nodiscard]] std::vector<std::shared_ptr<ClassObject>> ClassesDerivedFrom(const String& qualifiedName) const;

	/// Appelle une valeur appelable (fonction du script ou native).
	[[nodiscard]] Result<Value, ScriptError> CallValue(const Value& callee, std::vector<Value> args, int line,
													   int column);

	/// Erreur survenue hors de tout appel du script (rappel d'un bouton
	/// d'interface, destructeur…) : signalée comme une erreur `async`
	/// orpheline (`onAsyncError`, sinon la sortie) — jamais avalée.
	void ReportError(const ScriptError& error);

	/// Jeton vivant tant que l'interpréteur l'est : ce qu'un rappel de l'hôte
	/// (bouton d'interface…) garde pour ne jamais appeler un interpréteur détruit.
	[[nodiscard]] std::weak_ptr<std::atomic<bool>> AliveToken() const;

	/// Valeur destinée à un conteneur typé de l'hôte : vérifiée contre la
	/// valeur `type` (un argument de `std.vector<i32>()`) puis convertie.
	[[nodiscard]] Result<Value, ScriptError> ConformValue(const Value& typeValue, Value value);

	/// Vrai si `value(…)` a un sens : fonction, native, classe, objet doté
	/// d'un `operator ()`, objet de l'hôte appelable.
	[[nodiscard]] bool IsCallableValue(const Value& value);

	/// `value -> sink`, côté réception : ce que fait l'opérateur de flux
	/// quand `sink` reçoit une valeur — `operator <-` d'un objet, crochet
	/// `flowIn` d'un objet de l'hôte (pipe, flux, fichier, conteneur), ajout
	/// à une liste, ou appel d'une fonction (`x -> print`). Rend le puits
	/// (ou le résultat de l'appel), ce qui chaîne `a -> b -> c`.
	[[nodiscard]] Result<Value, ScriptError> FlowInto(const Value& sink, const Value& value, int line = 0,
													  int column = 0);

	/// Texte d'une valeur, `operator str` compris (ce qu'affiche `print`).
	[[nodiscard]] Result<String, ScriptError> Stringify(const Value& value);

	/// Ordre de deux valeurs (<0, 0, >0) : nombres, chaînes, valeurs d'un
	/// même énuméré, ou `operator <` — pour les tris des conteneurs.
	[[nodiscard]] Result<int, ScriptError> CompareValues(const Value& a, const Value& b);

	/// Égalité, `operator ==` compris.
	[[nodiscard]] Result<bool, ScriptError> ValuesEqual(const Value& a, const Value& b);

	/// Ce que parcourt `for (x in valeur)` : éléments d'une liste, clés d'une
	/// table, caractères d'une chaîne, valeurs d'un énuméré, éléments d'un
	/// conteneur de l'hôte, ou ce que rend l'`operator iter` d'un objet.
	[[nodiscard]] Result<std::vector<Value>, ScriptError> Iterate(const Value& source, int line = 0, int column = 0,
																  int depth = 0);

	/// Attend `value` si c'est un Future (sinon la rend telle quelle) : le
	/// `await` du script, utilisable par l'hôte. Sur le fil principal, les
	/// appels que les fils `async` y envoient sont traités pendant l'attente.
	[[nodiscard]] Result<Value, ScriptError> AwaitValue(const Value& value, int line = 0, int column = 0);

	/// À appeler sur le fil PRINCIPAL, une fois par image : exécute les
	/// appels à l'API de l'hôte envoyés par les fils `async`, relaie leurs
	/// `print`, signale leurs erreurs non attendues et récupère les fils
	/// terminés. Sans fonction `async`, ne fait rien.
	void PumpMainThread();

	/// Nombre de fils `async` encore en cours.
	[[nodiscard]] size_t RunningTasks();

	/// Vrai pendant l'arrêt (destruction) : les attentes s'interrompent.
	[[nodiscard]] bool IsStopping() const noexcept;

	/// Fil qui pilote l'interpréteur (celui qui l'a construit par défaut) :
	/// celui où l'API de l'hôte s'exécute et où PumpMainThread() agit.
	void SetMainThread() noexcept;

	[[nodiscard]] bool IsMainThread() const noexcept;

	/// Lance `body` sur un NOUVEAU fil ; rend aussitôt son Future. (Ce que
	/// fait l'appel d'une `async fn` ; exposé pour les natives de l'hôte.)
	[[nodiscard]] Result<Value, ScriptError> SpawnTask(std::function<Result<Value, ScriptError>()> body);

	/// Fabrique d'erreur positionnée — utilisable depuis une native pour
	/// produire un diagnostic homogène avec ceux de l'interpréteur.
	[[nodiscard]] static ScriptError MakeError(String message);

	/// Écrit une ligne sur la sortie du script (`print`, et l'hôte lui-même
	/// quand il veut que sa trace apparaisse au même endroit que celle des
	/// scripts — la console de l'éditeur s'y abonne via `onPrint`). Depuis
	/// un fil `async`, la ligne attend le prochain PumpMainThread().
	void Emit(const String& text);

	/// xorshift64* — générateur déterministe et sans dépendance, pour que
	/// `SetRandomSeed` rende une exécution scriptée parfaitement rejouable
	/// (rapports et captures d'écran comparables d'un run à l'autre).
	[[nodiscard]] double NextRandom() noexcept;

	/// Copie profonde des listes et tables de `values` (les autres valeurs,
	/// objets compris, sont partagées) : ce que reçoit une native de l'hôte
	/// quand d'autres fils tournent — comme les messages entre isolats Dart.
	[[nodiscard]] static std::vector<Value> CopyForHost(const std::vector<Value>& values);

private:
	// ── Fils d'exécution ─────────────────────────────────────────────────────

	/// État propre à un fil : budget d'instructions et profondeur d'appel.
	struct ExecContext {

		uint64_t steps = 0;
		size_t callDepth = 0;
	};

	/// Contexte du fil courant s'il appartient à CET interpréteur (fil
	/// `async`), sinon celui du fil principal.
	static inline thread_local const Interpreter* t_owner = nullptr;
	static inline thread_local ExecContext* t_context = nullptr;
	[[nodiscard]] ExecContext& Ctx() noexcept;

	struct AsyncTask {

		std::thread thread;
		std::shared_ptr<std::atomic<bool>> finished;
	};

	/// Appel à l'API de l'hôte envoyé par un fil `async` au fil principal.
	struct MainThreadCall {

		std::shared_ptr<NativeObject> native;
		std::vector<Value> args;
		std::mutex mutex;
		std::condition_variable finished;
		bool done = false;
		bool failed = false;
		Value value;
		ScriptError error;
	};

	struct OrphanError {

		std::weak_ptr<FutureObject> future;
		ScriptError error;
		int age = 0;
	};

	[[nodiscard]] std::shared_ptr<NativeObject> MakeNative(const String& name, int minArity, int maxArity, NativeFn fn,
														   NativeThread thread) const;

	[[nodiscard]] Result<Value, ScriptError> CallOnMainThread(const std::shared_ptr<NativeObject>& native,
															  std::vector<Value> args, int line, int column);

	/// Copie profonde bornée (une structure qui se contient elle-même
	/// s'arrête à 64 niveaux au lieu de boucler).
	[[nodiscard]] static Value DeepCopy(const Value& value, int depth);

	void NoteFailure(const std::shared_ptr<FutureObject>& future, const ScriptError& error);

	/// Une erreur `async` que personne n'a attendue au bout de deux passages
	/// (ou dont le Future a disparu) est signalée — jamais avalée.
	void ReportOrphanErrors();

	void ReapFinishedTasks();

	void StopAsyncTasks();

	/// Corps d'une fonction du script, exécuté sur le fil courant.
	[[nodiscard]] Result<Value, ScriptError> CallFunctionBody(const std::shared_ptr<FunctionObject>& fn,
															  std::vector<Value> args, int line, int column);

	// ── Environnements ───────────────────────────────────────────────────────

	[[nodiscard]] std::shared_ptr<Environment> MakeEnvironment(std::shared_ptr<Environment> parent,
															   bool functionScope = false);

	void BreakEnvironmentCycles() noexcept;

	// ── RAII : destructeurs `deinit` ─────────────────────────────────────────
	//
	// Une instance dont la classe a un `deinit` et qui perd sa DERNIÈRE
	// référence est mise en file par son destructeur C++ (cf.
	// ~InstanceObject) ; la file est vidée au début de l'instruction
	// suivante — donc à la sortie d'un bloc `{ }`, d'une fonction, d'un tour
	// de boucle ou à l'écrasement d'une variable, de façon déterministe.
	// Le `deinit` de la classe passe d'abord, puis ceux des parents (C++).

	static inline thread_local bool t_finalizing = false;

	void RunPendingFinalizers();

	void RunDeinit(const std::shared_ptr<InstanceObject>& ghost);

	/// Erreur qu'aucun appelant ne peut recevoir (destructeur) : signalée
	/// comme une erreur `async` orpheline — jamais avalée.
	void ReportDetachedError(const ScriptError& error);

	/// Arrêt : retire les liaisons du SCRIPT (celles de l'hôte restent), ce
	/// qui détruit ses objets, puis fait passer leurs `deinit`. La sortie va
	/// dans `Output()` : l'hôte, qui détruit l'interpréteur, n'écoute plus.
	void FinalizeAtExit();

	[[nodiscard]] static String ArityText(const NativeObject& native);

	[[nodiscard]] Option<ScriptError> ConsumeStep(int line, int column);

	// ── Instructions ─────────────────────────────────────────────────────────

	[[nodiscard]] Result<ExecOutcome, ScriptError> ExecStmt(const Stmt& stmt, const std::shared_ptr<Environment>& env);

	[[nodiscard]] Result<ExecOutcome, ScriptError> ExecAssign(const AssignStmt& stmt,
															  const std::shared_ptr<Environment>& env);

	/**
	 * `cible op= valeur` SANS perte de mise à jour entre fils : lecture de la
	 * valeur courante, calcul (hors de tout verrou), puis écriture seulement
	 * si personne ne l'a changée entre-temps — sinon on recommence. Deux fils
	 * qui font `compteur += 1` mille fois chacun obtiennent bien 2000.
	 * L'objet d'une cible `a.b` / `a[i]` n'est évalué qu'UNE fois.
	 */
	[[nodiscard]] Result<ExecOutcome, ScriptError> ExecCompoundAssign(const AssignStmt& stmt, const Value& operand,
																	  const std::shared_ptr<Environment>& env);

	/// Lecture `obj.nom` (table, objet, classe, espace de noms, Future).
	[[nodiscard]] Result<Value, ScriptError> ReadMember(const Value& object, const String& name, int line, int column);

	/**
	 * Méthodes des listes et des chaînes — `liste.append(x)`,
	 * `texte.upper()` — : les fonctions de la bibliothèque standard, le
	 * receveur en premier argument.
	 *   liste : append push pop insert remove clear index_of contains len
	 *           is_empty reverse sort join
	 *   chaîne : len is_empty upper lower trim split sub find contains
	 *            replace starts_with ends_with
	 */
	[[nodiscard]] Result<Value, ScriptError> ReadBuiltinMethod(const Value& receiver, const String& name, int line,
															   int column);

	/// Membre d'un objet de l'hôte : méthode (liée à l'objet) ou propriété.
	[[nodiscard]] Result<Value, ScriptError> ReadHostMember(const std::shared_ptr<HostObject>& host, const String& name,
															int line, int column);

	/// Écriture `obj.nom = v` (table, objet ou classe).
	[[nodiscard]] Option<ScriptError> AssignObjectOrMap(const Value& object, const String& name, Value value, int line,
														int column);

	/// Écrit `value` dans une cible assignable. Retourne l'erreur éventuelle
	/// (rien à rendre en cas de succès, d'où `Option<ScriptError>` plutôt
	/// qu'un `Result<void, …>` que ce dépôt n'a pas).
	[[nodiscard]] Option<ScriptError> AssignTo(const Expr& target, Value value,
											   const std::shared_ptr<Environment>& env);

	[[nodiscard]] Option<ScriptError> AssignIndexed(const Value& object, const Value& index, Value value, int line,
													int column);

	[[nodiscard]] Result<ExecOutcome, ScriptError> ExecWhile(const WhileStmt& stmt,
															 const std::shared_ptr<Environment>& env);

	[[nodiscard]] Result<ExecOutcome, ScriptError> ExecForIn(const ForInStmt& stmt,
															 const std::shared_ptr<Environment>& env);

	// ── Expressions ──────────────────────────────────────────────────────────

	[[nodiscard]] Result<Value, ScriptError> Eval(const Expr& expr, const std::shared_ptr<Environment>& env);

	[[nodiscard]] Result<Value, ScriptError> EvalUnary(const UnaryExpr& expr, const std::shared_ptr<Environment>& env);

	[[nodiscard]] Result<Value, ScriptError> ReadIndexed(const Value& object, const Value& index, int line, int column);

	// ── Surcharge d'opérateurs et flux ───────────────────────────────────────
	//
	// `operator +(o)` (d'instance : `this` à gauche) ou `static operator +(a, b)`
	// (l'un des deux opérandes est de la classe). Cherchés dans cet ordre :
	// méthode de l'opérande gauche, `static` de la classe de gauche puis de
	// droite, crochet `binary` d'un objet de l'hôte. À défaut de `!=`, `>`,
	// `<=`, `>=`, on dérive de `==` et `<`.

	/// Erreur d'un crochet de l'hôte (sans position) : placée sur l'appel.
	[[nodiscard]] static Result<Value, ScriptError> Positioned(Result<Value, ScriptError> result, int line, int column);

	/// `operator <symbole>` d'instance de `value`, lié à elle.
	[[nodiscard]] Option<Value> InstanceOperator(const Value& value, const char* symbol);

	/// `static operator <symbole>` de la classe de `value`.
	[[nodiscard]] Option<Value> StaticOperator(const Value& value, const char* symbol);

	[[nodiscard]] Option<Result<Value, ScriptError>> TryBinaryOverload(const char* symbol, BinaryOp op, const Value& a,
																	   const Value& b, int line, int column);

	/// Ordre de deux valeurs d'un même énuméré (rang de déclaration).
	[[nodiscard]] static Option<int> EnumOrder(const Value& a, const Value& b);

	/// Opérateur binaire complet : flux, surcharges, puis ApplyBinary.
	[[nodiscard]] Result<Value, ScriptError> EvalBinaryOp(BinaryOp op, const Value& left, const Value& right, int line,
														  int column);

	/// `source -> sink` : `operator ->` de la source, crochet `flowOut`
	/// d'une source de l'hôte (un pipe se CONNECTE, un conteneur ou un flux
	/// ÉMET ses éléments), sinon la source est une simple valeur, livrée au
	/// puits (cf. FlowInto).
	[[nodiscard]] Result<Value, ScriptError> Flow(const Value& source, const Value& sink, int line, int column);

	/// `a <-> b` : liaison dans les deux sens (`operator <->`, crochet `link`).
	[[nodiscard]] Result<Value, ScriptError> Link(const Value& a, const Value& b, int line, int column);

	/// `f<i32>(…)`, `Boîte<f32>(…)`, `std.vector<u8>()` : appel avec des
	/// arguments de type explicites.
	[[nodiscard]] Result<Value, ScriptError> CallGeneric(const Value& callee, const std::vector<TypeRef>& typeArgs,
														 std::vector<Value> args,
														 const std::shared_ptr<Environment>& env, int line, int column);

	[[nodiscard]] static Result<Value, ScriptError> ApplyBinary(BinaryOp op, const Value& left, const Value& right,
																int line, int column);

	[[nodiscard]] static const char* BinaryOpName(BinaryOp op) noexcept;

	// ── Portées : hissage, déclarations, espaces de noms ─────────────────────

	[[nodiscard]] static ScriptError DeadZoneError(const String& name, int line, int column);

	[[nodiscard]] static ScriptError ReadOnlyNamespaceError(const Value& ns, const String& member, int line,
															int column);

	/// Lecture `ns.membre` : liaisons vivantes de l'espace de noms, SANS
	/// remonter à ses portées parentes (un membre est ce qu'il déclare).
	[[nodiscard]] static Result<Value, ScriptError> ReadNamespaceMember(const Value& ns, const String& member, int line,
																		int column);

	/// Noms des `var` d'une liste d'instructions, blocs imbriqués compris —
	/// mais PAS ceux des fonctions ni des espaces de noms imbriqués, qui sont
	/// leurs propres portées de fonction.
	static void CollectVars(const std::vector<StmtPtr>& statements, std::vector<String>& out);

	static void CollectVars(const Stmt& stmt, std::vector<String>& out);

	/**
	 * Hissage des déclarations de `statements` dans la portée `env`, AVANT
	 * leur exécution :
	 *  - `let`/`const` du niveau courant : liaison créée NON initialisée (la
	 *    lire avant sa ligne = erreur de zone morte) ;
	 *  - `fn` du niveau courant : définie tout de suite (appelable avant sa
	 *    ligne, comme en JS) ;
	 *  - si `env` est une portée de FONCTION : tous les `var` du corps, blocs
	 *    imbriqués compris, initialisés à `nil`.
	 * `def` (facultatif) garde en cache la liste des `var` d'une fonction.
	 */
	[[nodiscard]] Option<ScriptError> Hoist(const std::vector<StmtPtr>& statements,
											const std::shared_ptr<Environment>& env, bool functionBody,
											const FunctionDef* def);

	/// `var` / `let` / `const` rencontrés à l'exécution.
	[[nodiscard]] Result<ExecOutcome, ScriptError> ExecDeclaration(const LetStmt& s,
																   const std::shared_ptr<Environment>& env);

	/// `namespace nom { … }` : ouvre (ou rouvre) l'espace de noms puis exécute
	/// son corps dans SA portée — ce qu'il y déclare en devient les membres.
	[[nodiscard]] Result<ExecOutcome, ScriptError> ExecNamespace(const NamespaceStmt& s,
																 const std::shared_ptr<Environment>& env);

	// ── Types annoncés ───────────────────────────────────────────────────────
	//
	// `let x:i32`, `fn f(a:f64):list<i32>`, `let champ:Forme?` : le type est
	// VÉRIFIÉ à l'exécution — à la déclaration, à chaque affectation, à
	// chaque appel (arguments et résultat). Une variable typée sans valeur
	// initiale reçoit celle de son type (0, "", false, liste ou table vide).
	// Un type numérique (i8 … f64) CONVERTIT le nombre reçu (`let x:u8 = 3`
	// range un u8) s'il y tient sans perte ; les éléments d'un conteneur
	// (`list<i32>`) sont seulement vérifiés.

	/// Ce que la valeur EST, pour un message d'erreur de type.
	[[nodiscard]] static String DescribeForType(const Value& value);

	/// NONE si `value` est du type `type`, sinon la raison.
	[[nodiscard]] Option<String> TypeMismatch(const Value& value, const TypeExpr& type,
											  const std::shared_ptr<Environment>& env, int depth = 0);

	// ── Génériques ───────────────────────────────────────────────────────────

	/// Nouveau type libre pour le paramètre `param` (déduit au premier usage).
	[[nodiscard]] static Value FreshTypeParam(const TypeParam& param, const std::shared_ptr<Environment>& scope);

	/// Type donné par une annotation : `T` déjà lié dans `scope` est repris
	/// tel quel (il partage alors sa déduction), sinon un type fixé.
	[[nodiscard]] static Value MakeTypeValue(const TypeRef& type, const std::shared_ptr<Environment>& scope,
											 const String& name);

	/// Nom lisible de ce qu'un type lié désigne (`i32`, `Forme`, `?` libre).
	[[nodiscard]] static String BoundTypeName(const TypeObject& type);

	/// `Boîte<i32>` face à une instance dont les arguments sont connus :
	/// chacun doit désigner le même type (un argument libre accepte tout).
	[[nodiscard]] Option<String> TypeArgsMismatch(const std::vector<Value>& actual, const TypeExpr& type,
												  const std::shared_ptr<Environment>& env);

	/// Valeur face à un paramètre de type : vérifiée contre le type lié, ou
	/// — s'il est encore libre — elle le FIXE (déduction au premier usage).
	[[nodiscard]] Option<String> CheckTypeParam(const Value& value, TypeObject& param, bool nullable, int depth);

	/// Valeur d'une variable typée déclarée sans valeur.
	[[nodiscard]] static Value DefaultFor(const TypeExpr& type);

	/// Un nombre destiné à un type numérique simple (`i32`, `f32?`, ou `T`
	/// lié à `u8`…) est converti vers ce type — à appeler APRÈS TypeMismatch
	/// (conversion sûre).
	void CoerceNumber(Value& value, const TypeExpr& type, const std::shared_ptr<Environment>& env, int depth = 0);

	/// Valeur destinée à un conteneur typé (`std.vector<u8>`) : vérifiée
	/// contre le type lié `typeValue`, puis convertie (nombres). Pour les
	/// objets de l'hôte (cf. HostObject::typeArgs).
	[[nodiscard]] Result<Value, ScriptError> Conform(const Value& typeValue, Value value);

	/// Valeurs `type` d'une liste d'annotations (`std.vector<i32>()`), dans
	/// la portée de l'appel.
	[[nodiscard]] std::vector<Value> MakeTypeValues(const std::vector<TypeRef>& types,
													const std::shared_ptr<Environment>& env);

	/// Erreur « `nom` est de type T : raison » ; sinon `value` est convertie
	/// vers le type numérique annoncé.
	[[nodiscard]] Option<ScriptError> CheckTyped(const char* what, const String& name, Value& value,
												 const TypeRef& type, const std::shared_ptr<Environment>& env, int line,
												 int column);

	// ── Classes, interfaces, instances ───────────────────────────────────────
	//
	// Une méthode LIÉE est une fonction ordinaire dont la fermeture est une
	// petite portée qui porte `this` (l'instance) et, sous un nom qu'aucun
	// script ne peut écrire, la classe qui DÉFINIT la méthode — c'est d'elle
	// que part `super`, et non de la classe de l'instance (sinon un `super`
	// dans une classe intermédiaire rappellerait sa propre méthode).

	static constexpr const char* THIS_NAME = "this";
	static constexpr const char* OWNER_NAME = "\x01classe";

	/// Portée d'une méthode liée : `this` + la classe propriétaire (+ ses
	/// paramètres de type, pour une classe générique).
	[[nodiscard]] std::shared_ptr<Environment> BindEnvironment(const Value& self,
															   const std::shared_ptr<ClassObject>& owner);

	/// `T`, `U`… d'une classe générique dans `env` : ceux de l'instance, ou
	/// des types libres (méthode de classe, instance sans arguments connus).
	void BindClassTypeParams(const std::shared_ptr<Environment>& env, const Value& self,
							 const std::shared_ptr<ClassObject>& owner);

	[[nodiscard]] Value BindMethod(const Value& self, const std::shared_ptr<ClassObject>& owner,
								   const FunctionDefPtr& def);

	/// Portée où résoudre les types des champs d'une instance : celle de sa
	/// classe, plus les paramètres de type de chaque niveau générique.
	[[nodiscard]] std::shared_ptr<Environment> InstanceTypeScope(const std::shared_ptr<InstanceObject>& instance);

	/// Méthode `name` (d'instance ou de classe) en remontant les parents à
	/// partir de `from` ; rend aussi la classe qui la définit.
	[[nodiscard]] static std::pair<const MethodDef*, std::shared_ptr<ClassObject>>
	FindMethod(std::shared_ptr<ClassObject> from, const String& name, bool wantStatic);

	[[nodiscard]] Result<Value, ScriptError> ReadInstanceMember(const Value& object, const String& name, int line,
																int column);

	/// `Classe.membre` : champ de classe, méthode de classe ou constructeur
	/// `factory` (hérités, sauf les `factory`), ou `Classe.name`.
	[[nodiscard]] Result<Value, ScriptError> ReadClassMember(const std::shared_ptr<ClassObject>& klass,
															 const String& name, int line, int column);

	/**
	 * Membres d'un Future (façon Dart) :
	 *   `.done`   vrai une fois terminé (valeur ou erreur) ;
	 *   `.failed` vrai s'il s'est terminé en erreur ;
	 *   `.value`  la valeur (nil tant qu'il n'est pas terminé) ;
	 *   `.error`  le message d'erreur (nil sinon) ;
	 *   `.then(fn)` un NOUVEAU Future : `fn(valeur)` une fois celui-ci terminé ;
	 *   `.wait()` attend, comme `await` (utilisable hors d'une fonction `async`).
	 */
	[[nodiscard]] Result<Value, ScriptError> ReadFutureMember(const Value& object, const String& name, int line,
															  int column);

	/// `objet.champ = v` (un champ nouveau se crée, comme en Python) ou
	/// `Classe.champ = v` ; un champ `const` refuse.
	[[nodiscard]] Option<ScriptError> AssignObjectMember(const Value& object, const String& name, Value value, int line,
														 int column);

	/// `super.m` : la méthode `m` des PARENTS de la classe qui définit la
	/// méthode en cours, liée au même `this`.
	[[nodiscard]] Result<Value, ScriptError> EvalSuper(const SuperExpr& expr, const std::shared_ptr<Environment>& env);

	/// `Classe(args)` / `new Classe(args)` : champs (des ancêtres d'abord),
	/// puis le constructeur `init` le plus dérivé.
	[[nodiscard]] Result<Value, ScriptError> Instantiate(const std::shared_ptr<ClassObject>& klass,
														 std::vector<Value> args, int line, int column,
														 std::vector<Value> typeArgs = {});

	/// Arguments de type de chaque niveau générique de la hiérarchie : ceux
	/// donnés (`Boîte<i32>()`), puis ceux que chaque classe passe à son
	/// parent (`extends Boîte<T>`), des types libres sinon.
	void ResolveInstanceTypeArgs(InstanceObject& instance, std::vector<Value> current);

	/// Classe nommée par `path` (`Forme` ou `geo.Forme`).
	[[nodiscard]] Result<std::shared_ptr<ClassObject>, ScriptError>
	ResolveTypePath(const TypePath& path, const std::shared_ptr<Environment>& env, int line, int column);

	/// Valeur nommée par un chemin de type : classe, type de l'hôte
	/// (constructeur `std.vector`) ou paramètre de type (`T`).
	[[nodiscard]] Result<Value, ScriptError>
	ResolveTypeValue(const TypePath& path, const std::shared_ptr<Environment>& env, int line, int column);

	/// Toutes les interfaces que `klass` doit honorer : les siennes, celles de
	/// ses ancêtres, et les interfaces que celles-ci étendent.
	static void CollectInterfaces(const std::shared_ptr<ClassObject>& klass,
								  std::vector<std::shared_ptr<ClassObject>>& out);

	/**
	 * `class` / `interface` : résout parent et interfaces, VÉRIFIE le contrat
	 * — `override` qui ne remplace rien, méthode abstraite ou d'interface non
	 * fournie par une classe concrète, arité différente de l'interface —
	 * puis déclare la classe et évalue ses champs `static`.
	 */
	[[nodiscard]] Result<ExecOutcome, ScriptError> ExecClass(const ClassStmt& stmt,
															 const std::shared_ptr<Environment>& env);

	// ── Imports ──────────────────────────────────────────────────────────────

	/// Résolution effective : résolveur → absolu → relatif → répertoires.
	[[nodiscard]] Option<std::pair<String, String>> LoadModuleSource(const String& specifier) const;

	/// Retire le module courant de la pile (RAII dans ImportModule).
	void PopImportStack() noexcept;

	// ── Membres ──────────────────────────────────────────────────────────────

	std::shared_ptr<Environment> m_globals;
	std::shared_ptr<Program> m_program; ///< garde l'AST du dernier Run() en vie
	mutable std::mutex m_environmentsMutex;
	std::vector<std::weak_ptr<Environment>> m_environments;
	size_t m_environmentPurgeThreshold = 256;
	std::mutex m_outputMutex; ///< sortie, `print` en attente et erreurs orphelines
	std::vector<String> m_output;
	std::vector<String> m_pendingPrints;
	std::vector<OrphanError> m_orphans;
	std::mutex m_randomMutex;
	uint64_t m_randomState = 0x9E3779B97F4A7C15ull;
	ExecContext m_mainContext;
	std::thread::id m_mainThread;
	bool m_installingStandardLibrary = false;
	/// Fils `async` : vivants, file d'appels vers le fil principal, arrêt.
	std::mutex m_tasksMutex;
	std::vector<AsyncTask> m_tasks;
	std::mutex m_mainQueueMutex;
	std::deque<std::shared_ptr<MainThreadCall>> m_mainQueue;
	std::atomic<bool> m_stopping{false};
	std::atomic<bool> m_concurrent{false}; ///< au moins une tâche async lancée
	/// Vivant tant que l'interpréteur l'est : les classes s'y réfèrent pour
	/// savoir si leurs `deinit` peuvent encore passer.
	std::shared_ptr<std::atomic<bool>> m_alive;
	std::atomic<bool> m_teardown{false};			   ///< `deinit` de fin : l'exécution reste permise
	std::vector<std::weak_ptr<ClassObject>> m_classes; ///< sous m_environmentsMutex

	mutable std::mutex m_importMutex;
	std::vector<String> m_importPaths;
	ImportResolver m_importResolver;
	/// Chemin canonique → valeur du module (rendue à l'identique).
	std::vector<std::pair<String, Value>> m_importCache;
	/// Chaîne des imports en cours (détection de cycle).
	std::vector<String> m_importStack;

	OwnerRegistry m_owners;
};

// ============================================================================
// Bibliothèque standard — helpers d'arguments
// ============================================================================

namespace detail {

/// L'arité étant déjà validée par `CallValue`, ces helpers ne vérifient QUE
/// le type : un argument obligatoire est forcément présent au moment où une
/// native tourne.
[[nodiscard]] Result<double, ScriptError> ArgNumber(const std::vector<Value>& args, size_t index,
														   const char* fnName);

[[nodiscard]] Result<String, ScriptError> ArgString(const std::vector<Value>& args, size_t index,
														   const char* fnName);

[[nodiscard]] Result<std::shared_ptr<ListObject>, ScriptError> ArgList(const std::vector<Value>& args,
																			  size_t index, const char* fnName);

[[nodiscard]] Result<std::shared_ptr<MapObject>, ScriptError> ArgMap(const std::vector<Value>& args,
																			size_t index, const char* fnName);

/// Indice de liste depuis une valeur de script : entier, dans les bornes.
[[nodiscard]] Result<size_t, ScriptError> ToIndex(double raw, size_t size, const char* fnName);

} // namespace detail

} // namespace data::script

// Bibliothèques installées par le constructeur (cf. InstallStdLibrary /
// InstallMathLibrary, déclarées plus haut) — après l'interpréteur, qu'elles
// utilisent.
#include "script_std.hpp"
#include "script_math.hpp"
