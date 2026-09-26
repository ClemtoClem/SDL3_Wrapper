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
#include <chrono>
#include <cmath>
#include <cstdint>
#include <memory>
#include <utility>
#include <vector>

#include "../../core/core.hpp"
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
};

/// Portée lexicale : une liste de liaisons + un parent. Recherche linéaire
/// (même raison que MapObject, cf. script_value.hpp — une portée compte
/// quelques variables).
///
/// Une portée de FONCTION (corps de fonction, programme, espace de noms)
/// reçoit les `var` ; une portée de BLOC (`{ }`, tour de boucle) reçoit les
/// `let`/`const` (cf. Interpreter::Hoist).
class Environment : public std::enable_shared_from_this<Environment> {
public:
	Environment() = default;
	explicit Environment(std::shared_ptr<Environment> parent, bool functionScope = false)
		: m_parent(std::move(parent)), m_functionScope(functionScope) {}

	/// Définit (ou redéfinit) une liaison `var` initialisée — la forme des
	/// valeurs posées par l'hôte et des paramètres.
	void Define(const String &name, Value value) {
		Binding &binding = Declare(name, BindingKind::VAR, true);
		binding.value = std::move(value);
	}

	/// Crée — ou remplace — la liaison `name` de CETTE portée.
	Binding &Declare(const String &name, BindingKind kind, bool initialized) {
		for (Binding &binding : m_bindings) {
			if (binding.name == name) {
				binding.kind = kind;
				binding.initialized = initialized;
				return binding;
			}
		}
		m_bindings.push_back(Binding{name, Value::Nil(), kind, initialized, false});
		return m_bindings.back();
	}

	/// Liaison de CETTE portée seulement.
	[[nodiscard]] Binding *Local(const String &name) noexcept {
		for (Binding &binding : m_bindings)
			if (binding.name == name)
				return &binding;
		return nullptr;
	}

	/// Liaison visible depuis cette portée (elle-même puis ses parentes).
	[[nodiscard]] Binding *Lookup(const String &name) noexcept {
		for (Environment *env = this; env; env = env->m_parent.get())
			if (Binding *binding = env->Local(name))
				return binding;
		return nullptr;
	}

	/// Cherche dans cette portée puis les parentes. `nullptr` si absente —
	/// jamais d'erreur ici : c'est l'appelant qui décide si une variable
	/// absente est une erreur (lecture) ou non (test d'existence).
	[[nodiscard]] Value *Find(const String &name) noexcept {
		Binding *binding = Lookup(name);
		return binding ? &binding->value : nullptr;
	}

	enum class AssignResult : uint8_t { OK, UNKNOWN, CONSTANT, UNINITIALIZED };

	/// Affecte une variable EXISTANTE (dans cette portée ou une parente).
	/// Jamais de création implicite : une faute de frappe ne crée pas
	/// silencieusement un global (le piège classique de Lua).
	AssignResult Assign(const String &name, Value value) {
		Binding *binding = Lookup(name);
		if (!binding)
			return AssignResult::UNKNOWN;
		if (!binding->initialized)
			return AssignResult::UNINITIALIZED;
		if (binding->kind == BindingKind::CONST)
			return AssignResult::CONSTANT;
		binding->value = std::move(value);
		return AssignResult::OK;
	}

	[[nodiscard]] bool IsFunctionScope() const noexcept { return m_functionScope; }

	/// Portée de fonction la plus proche (elle-même si elle en est une) —
	/// là où vivent les `var`.
	[[nodiscard]] Environment *FunctionScope() noexcept {
		Environment *env = this;
		while (env && !env->m_functionScope && env->m_parent)
			env = env->m_parent.get();
		return env;
	}

	[[nodiscard]] const std::vector<Binding> &Bindings() const noexcept { return m_bindings; }

	/// Vide les liaisons — utilisé UNIQUEMENT pour casser les cycles de
	/// fermeture à la destruction de l'interpréteur (cf. en-tête du fichier).
	void ClearBindings() noexcept { m_bindings.clear(); }

private:
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

class Interpreter {
public:
	Interpreter() : m_globals(std::make_shared<Environment>(nullptr, true)) {
		m_environments.push_back(m_globals);
		InstallStandardLibrary();
	}

	Interpreter(const Interpreter &) = delete;
	Interpreter &operator=(const Interpreter &) = delete;

	~Interpreter() { BreakEnvironmentCycles(); }

	// ── Réglages ─────────────────────────────────────────────────────────────

	/// Budget d'instructions par `Run()`/`CallGlobal()` — dépassement =
	/// erreur d'exécution, jamais un gel (cf. en-tête).
	uint64_t maxSteps = 20000000;
	/// Profondeur d'appel maximale — dépassement = erreur, jamais un
	/// débordement de pile.
	size_t maxCallDepth = 200;

	/// Destination de `print`. Si nul, la sortie est accumulée dans
	/// `Output()` (ce dont se servent les tests et la console de l'éditeur).
	std::function<void(const String &)> onPrint;

	[[nodiscard]] const std::vector<String> &Output() const noexcept { return m_output; }
	void ClearOutput() { m_output.clear(); }

	/// Graine du générateur pseudo-aléatoire de `random()` — fixée par
	/// défaut pour que deux exécutions d'un même script donnent EXACTEMENT
	/// la même chose (rapports et captures d'écran reproductibles).
	void SetRandomSeed(uint64_t seed) noexcept { m_randomState = seed ? seed : 0x9E3779B97F4A7C15ull; }

	// ── Globales et fonctions natives ────────────────────────────────────────

	/// Pose une globale de l'HÔTE : un script peut la lire et la réaffecter,
	/// pas la redéclarer par `let`/`const`.
	void SetGlobal(const String &name, Value value) {
		Binding &binding = m_globals->Declare(name, BindingKind::VAR, true);
		binding.value = std::move(value);
		binding.host = true;
	}

	[[nodiscard]] Option<Value> GetGlobal(const String &name) const {
		// Find() est non-const par nature (retourne un slot modifiable) ;
		// cette lecture-ci ne modifie rien, d'où le const_cast local.
		Value *slot = const_cast<Environment &>(*m_globals).Find(name);
		if (!slot)
			return NONE;
		return Some(*slot);
	}

	[[nodiscard]] bool HasGlobal(const String &name) const { return GetGlobal(name).IsSome(); }

	/// Expose une fonction C++ au script. `maxArity < 0` = variadique.
	/// L'arité est vérifiée AVANT l'appel : une native n'a jamais à valider
	/// le nombre de ses arguments elle-même.
	void RegisterNative(const String &name, int minArity, int maxArity, NativeFn fn) {
		auto native = std::make_shared<NativeObject>();
		native->name = name;
		native->fn = std::move(fn);
		native->minArity = minArity;
		native->maxArity = maxArity;
		SetGlobal(name, Value::Native(std::move(native)));
	}

	/// Espace de noms de l'hôte `name` (créé au premier appel) — c'est là que
	/// l'application range son API par domaine (`editor.`, `scene.`, `math.`).
	/// Ses membres sont des CONSTANTES : un script ne peut pas remplacer
	/// `editor.log` par accident.
	[[nodiscard]] std::shared_ptr<NamespaceObject> HostNamespace(const String &name) {
		if (Binding *existing = m_globals->Local(name); existing && existing->value.IsNamespace())
			return existing->value.AsNamespace();
		auto ns = std::make_shared<NamespaceObject>();
		ns->name = name;
		ns->scope = MakeEnvironment(nullptr, true);
		Binding &binding = m_globals->Declare(name, BindingKind::CONST, true);
		binding.value = Value::Namespace(ns);
		binding.host = true;
		return ns;
	}

	/// Pose une constante dans un espace de noms de l'hôte (`math.pi`).
	void RegisterNamespaceConstant(const String &ns, const String &name, Value value) {
		Binding &binding = HostNamespace(ns)->scope->Declare(name, BindingKind::CONST, true);
		binding.value = std::move(value);
		binding.host = true;
	}

	/// Range une native dans un espace de noms de l'hôte (`scene.spawn(...)`).
	void RegisterNamespacedNative(const String &ns, const String &name, int minArity, int maxArity, NativeFn fn) {
		auto native = std::make_shared<NativeObject>();
		native->name = String::Format("%s.%s", ns.CStr(), name.CStr());
		native->fn = std::move(fn);
		native->minArity = minArity;
		native->maxArity = maxArity;
		RegisterNamespaceConstant(ns, name, Value::Native(std::move(native)));
	}

	/// Membre `member` de l'espace de noms `ns` (NONE si l'un ou l'autre
	/// n'existe pas) — lecture côté hôte.
	[[nodiscard]] Option<Value> GetNamespaceMember(const String &ns, const String &member) const {
		Option<Value> space = GetGlobal(ns);
		if (space.IsNone() || !space.Unwrap().IsNamespace() || !space.Unwrap().AsNamespace())
			return NONE;
		const Binding *binding = space.Unwrap().AsNamespace()->scope->Local(member);
		if (!binding || !binding->initialized)
			return NONE;
		return Some(binding->value);
	}

	// ── Exécution ────────────────────────────────────────────────────────────

	/// Compile puis exécute une source. Les définitions (`let`, `fn`) restent
	/// dans les globales : un second `Run()` voit ce que le premier a défini,
	/// et l'hôte peut ensuite appeler les fonctions déclarées via
	/// `CallGlobal` — c'est ce qui permet à un script d'éditeur de déclarer
	/// `on_update(dt)` une fois et d'être piloté image par image ensuite.
	[[nodiscard]] Result<Value, ScriptError> Run(StringView source) {
		auto program = Parser::Compile(source);
		if (program.IsError())
			return Err(program.Error());
		m_program = std::make_shared<Program>(std::move(program).Unwrap());
		return RunProgram(*m_program);
	}

	/// Exécute un programme dans les globales. Ses déclarations de premier
	/// niveau sont HISSÉES d'abord ; les `let`/`const` d'un programme déjà
	/// exécuté dans cet interpréteur sont redéclarables par le suivant (la
	/// console de l'éditeur rejoue un même extrait — comme une console de
	/// navigateur).
	[[nodiscard]] Result<Value, ScriptError> RunProgram(const Program &program) {
		m_steps = 0;
		if (auto hoisted = Hoist(program.statements, m_globals, true, nullptr); hoisted.IsSome())
			return Err(hoisted.Unwrap());
		for (const StmtPtr &stmt : program.statements) {
			if (!stmt)
				continue;
			auto outcome = ExecStmt(*stmt, m_globals);
			if (outcome.IsError())
				return Err(outcome.Error());
			if (outcome.Value().flow == Flow::RETURN)
				return Ok(outcome.Value().value);
		}
		return Ok(Value::Nil());
	}

	/// Appelle une fonction globale déclarée par un script (`on_update`,
	/// `on_start`…). `NONE` si elle n'existe pas — l'hôte distingue ainsi
	/// « pas de rappel déclaré » (normal) d'une vraie erreur d'exécution.
	[[nodiscard]] Option<Result<Value, ScriptError>> CallGlobalIfPresent(const String &name,
																		std::vector<Value> args) {
		Value *slot = m_globals->Find(name);
		if (!slot || !slot->IsCallable())
			return NONE;
		m_steps = 0;
		return Some(CallValue(*slot, std::move(args), 0, 0));
	}

	/// Appelle une valeur appelable (fonction du script ou native).
	[[nodiscard]] Result<Value, ScriptError> CallValue(const Value &callee, std::vector<Value> args, int line,
													   int column) {
		if (callee.IsNative()) {
			const std::shared_ptr<NativeObject> &native = callee.AsNative();
			if (!native || !native->fn)
				return Err(ScriptError(String("fonction native invalide"), line, column));
			int argc = static_cast<int>(args.size());
			if (argc < native->minArity || (native->maxArity >= 0 && argc > native->maxArity))
				return Err(ScriptError(String::Format("`%s` attend %s argument(s), %d fourni(s)",
													  native->name.CStr(), ArityText(*native).CStr(), argc),
									   line, column));
			return native->fn(*this, args);
		}
		if (!callee.IsFunction())
			return Err(ScriptError(String::Format("valeur de type `%s` non appelable", callee.TypeName()), line,
								   column));

		const std::shared_ptr<FunctionObject> &fn = callee.AsFunction();
		if (!fn || !fn->def)
			return Err(ScriptError(String("fonction invalide"), line, column));
		if (args.size() != fn->def->params.size())
			return Err(ScriptError(String::Format("`%s` attend %d argument(s), %d fourni(s)",
												  fn->def->name.IsEmpty() ? "fonction anonyme"
																		  : fn->def->name.CStr(),
												  int(fn->def->params.size()), int(args.size())),
								   line, column));
		if (m_callDepth >= maxCallDepth)
			return Err(ScriptError(String::Format("profondeur d'appel maximale atteinte (%d) — récursion infinie ?",
												  int(maxCallDepth)),
								   line, column));

		auto frame = MakeEnvironment(fn->closure ? fn->closure : m_globals, true);
		for (size_t i = 0; i < fn->def->params.size(); ++i)
			frame->Define(fn->def->params[i], args[i]);
		if (auto hoisted = Hoist(fn->def->body, frame, true, fn->def.get()); hoisted.IsSome())
			return Err(hoisted.Unwrap());

		++m_callDepth;
		for (const StmtPtr &stmt : fn->def->body) {
			if (!stmt)
				continue;
			auto outcome = ExecStmt(*stmt, frame);
			if (outcome.IsError()) {
				--m_callDepth;
				return Err(outcome.Error());
			}
			if (outcome.Value().flow == Flow::RETURN) {
				--m_callDepth;
				return Ok(outcome.Value().value);
			}
			// BREAK/CONTINUE qui remonteraient jusqu'ici sont déjà refusés
			// par ExecStmt (erreur « hors d'une boucle »), rien à faire.
		}
		--m_callDepth;
		return Ok(Value::Nil());
	}

	/// Fabrique d'erreur positionnée — utilisable depuis une native pour
	/// produire un diagnostic homogène avec ceux de l'interpréteur.
	[[nodiscard]] static ScriptError MakeError(String message) { return ScriptError(std::move(message), 0, 0); }

	/// Écrit une ligne sur la sortie du script (`print`, et l'hôte lui-même
	/// quand il veut que sa trace apparaisse au même endroit que celle des
	/// scripts — la console de l'éditeur s'y abonne via `onPrint`).
	void Emit(const String &text) {
		if (onPrint)
			onPrint(text);
		else
			m_output.push_back(text);
	}

	/// xorshift64* — générateur déterministe et sans dépendance, pour que
	/// `SetRandomSeed` rende une exécution scriptée parfaitement rejouable
	/// (rapports et captures d'écran comparables d'un run à l'autre).
	[[nodiscard]] double NextRandom() noexcept {
		m_randomState ^= m_randomState >> 12;
		m_randomState ^= m_randomState << 25;
		m_randomState ^= m_randomState >> 27;
		uint64_t x = m_randomState * 0x2545F4914F6CDD1Dull;
		return double(x >> 11) / double(1ull << 53);
	}

private:
	// ── Environnements ───────────────────────────────────────────────────────

	[[nodiscard]] std::shared_ptr<Environment> MakeEnvironment(std::shared_ptr<Environment> parent,
															   bool functionScope = false) {
		auto env = std::make_shared<Environment>(std::move(parent), functionScope);
		// Purge amortie des entrées mortes : sans elle, le registre grossirait
		// indéfiniment pour un script appelé à chaque image.
		if (m_environments.size() >= m_environmentPurgeThreshold) {
			std::vector<std::weak_ptr<Environment>> alive;
			alive.reserve(m_environments.size());
			for (auto &weak : m_environments)
				if (!weak.expired())
					alive.push_back(weak);
			m_environments = std::move(alive);
			m_environmentPurgeThreshold = m_environments.size() * 2 + 64;
		}
		m_environments.push_back(env);
		return env;
	}

	void BreakEnvironmentCycles() noexcept {
		for (auto &weak : m_environments)
			if (auto env = weak.lock())
				env->ClearBindings();
		m_globals->ClearBindings();
	}

	[[nodiscard]] static String ArityText(const NativeObject &native) {
		if (native.maxArity < 0)
			return String::Format("au moins %d", native.minArity);
		if (native.minArity == native.maxArity)
			return String::From(native.minArity);
		return String::Format("%d à %d", native.minArity, native.maxArity);
	}

	[[nodiscard]] Option<ScriptError> ConsumeStep(int line, int column) {
		if (++m_steps > maxSteps)
			return Some(ScriptError(
				String::Format("budget d'exécution dépassé (%llu instructions) — boucle infinie ?",
							   static_cast<unsigned long long>(maxSteps)),
				line, column));
		return NONE;
	}

	// ── Instructions ─────────────────────────────────────────────────────────

	[[nodiscard]] Result<ExecOutcome, ScriptError> ExecStmt(const Stmt &stmt,
															const std::shared_ptr<Environment> &env) {
		if (auto over = ConsumeStep(stmt.line, stmt.column); over.IsSome())
			return Err(over.Unwrap());

		switch (stmt.kind) {
			case StmtKind::EXPRESSION: {
				const auto &s = static_cast<const ExpressionStmt &>(stmt);
				auto value = Eval(*s.expression, env);
				if (value.IsError())
					return Err(value.Error());
				return Ok(ExecOutcome{});
			}
			case StmtKind::LET:
				return ExecDeclaration(static_cast<const LetStmt &>(stmt), env);
			case StmtKind::ASSIGN:
				return ExecAssign(static_cast<const AssignStmt &>(stmt), env);
			case StmtKind::BLOCK: {
				const auto &s = static_cast<const BlockStmt &>(stmt);
				auto scope = MakeEnvironment(env);
				if (auto hoisted = Hoist(s.statements, scope, false, nullptr); hoisted.IsSome())
					return Err(hoisted.Unwrap());
				for (const StmtPtr &child : s.statements) {
					if (!child)
						continue;
					auto outcome = ExecStmt(*child, scope);
					if (outcome.IsError())
						return outcome;
					if (outcome.Value().flow != Flow::NORMAL)
						return outcome;
				}
				return Ok(ExecOutcome{});
			}
			case StmtKind::IF: {
				const auto &s = static_cast<const IfStmt &>(stmt);
				auto condition = Eval(*s.condition, env);
				if (condition.IsError())
					return Err(condition.Error());
				if (condition.Value().IsTruthy())
					return ExecStmt(*s.thenBranch, env);
				if (s.elseBranch)
					return ExecStmt(*s.elseBranch, env);
				return Ok(ExecOutcome{});
			}
			case StmtKind::WHILE:
				return ExecWhile(static_cast<const WhileStmt &>(stmt), env);
			case StmtKind::FOR_IN:
				return ExecForIn(static_cast<const ForInStmt &>(stmt), env);
			case StmtKind::FUNCTION: {
				const auto &s = static_cast<const FunctionStmt &>(stmt);
				auto fn = std::make_shared<FunctionObject>();
				fn->def = s.def;
				fn->closure = env;
				Binding &binding = env->Declare(s.def->name, BindingKind::FUNCTION, true);
				binding.value = Value::Function(std::move(fn));
				return Ok(ExecOutcome{});
			}
			case StmtKind::NAMESPACE:
				return ExecNamespace(static_cast<const NamespaceStmt &>(stmt), env);
			case StmtKind::RETURN: {
				const auto &s = static_cast<const ReturnStmt &>(stmt);
				ExecOutcome outcome;
				outcome.flow = Flow::RETURN;
				if (s.value) {
					auto value = Eval(*s.value, env);
					if (value.IsError())
						return Err(value.Error());
					outcome.value = value.Unwrap();
				}
				return Ok(std::move(outcome));
			}
			case StmtKind::BREAK:
				return Ok(ExecOutcome{Flow::BREAK, Value::Nil()});
			case StmtKind::CONTINUE:
				return Ok(ExecOutcome{Flow::CONTINUE, Value::Nil()});
		}
		return Err(ScriptError(String("instruction inconnue"), stmt.line, stmt.column));
	}

	[[nodiscard]] Result<ExecOutcome, ScriptError> ExecAssign(const AssignStmt &stmt,
															  const std::shared_ptr<Environment> &env) {
		auto rhs = Eval(*stmt.value, env);
		if (rhs.IsError())
			return Err(rhs.Error());
		Value value = rhs.Unwrap();

		if (stmt.compound.IsSome()) {
			auto current = Eval(*stmt.target, env);
			if (current.IsError())
				return Err(current.Error());
			auto combined = ApplyBinary(stmt.compound.Unwrap(), current.Value(), value, stmt.line, stmt.column);
			if (combined.IsError())
				return Err(combined.Error());
			value = combined.Unwrap();
		}

		auto assigned = AssignTo(*stmt.target, std::move(value), env);
		if (assigned.IsSome())
			return Err(assigned.Unwrap());
		return Ok(ExecOutcome{});
	}

	/// Écrit `value` dans une cible assignable. Retourne l'erreur éventuelle
	/// (rien à rendre en cas de succès, d'où `Option<ScriptError>` plutôt
	/// qu'un `Result<void, …>` que ce dépôt n'a pas).
	[[nodiscard]] Option<ScriptError> AssignTo(const Expr &target, Value value,
											   const std::shared_ptr<Environment> &env) {
		switch (target.kind) {
			case ExprKind::IDENTIFIER: {
				const auto &e = static_cast<const IdentifierExpr &>(target);
				switch (env->Assign(e.name, std::move(value))) {
					case Environment::AssignResult::OK:
						return NONE;
					case Environment::AssignResult::UNKNOWN:
						return Some(ScriptError(String::Format("variable inconnue `%s` (manque-t-il un `let` ?)",
															   e.name.CStr()),
												target.line, target.column));
					case Environment::AssignResult::CONSTANT: {
						const Binding *binding = env->Lookup(e.name);
						const bool space = binding && binding->value.IsNamespace();
						return Some(ScriptError(String::Format(space ? "`%s` est un espace de noms : réaffectation impossible"
																	 : "`%s` est une constante : réaffectation impossible",
															   e.name.CStr()),
												target.line, target.column));
					}
					case Environment::AssignResult::UNINITIALIZED:
						return Some(DeadZoneError(e.name, target.line, target.column));
				}
				return NONE;
			}
			case ExprKind::INDEX: {
				const auto &e = static_cast<const IndexExpr &>(target);
				auto object = Eval(*e.object, env);
				if (object.IsError())
					return Some(object.Error());
				auto index = Eval(*e.index, env);
				if (index.IsError())
					return Some(index.Error());
				return AssignIndexed(object.Value(), index.Value(), std::move(value), target.line, target.column);
			}
			case ExprKind::MEMBER: {
				const auto &e = static_cast<const MemberExpr &>(target);
				auto object = Eval(*e.object, env);
				if (object.IsError())
					return Some(object.Error());
				if (object.Value().IsNamespace())
					return Some(ReadOnlyNamespaceError(object.Value(), e.name, target.line, target.column));
				if (!object.Value().IsMap() || !object.Value().AsMap())
					return Some(ScriptError(String::Format("`.%s` attend une table, trouvé `%s`", e.name.CStr(),
														   object.Value().TypeName()),
											target.line, target.column));
				object.Value().AsMap()->SetKey(e.name, std::move(value));
				return NONE;
			}
			default:
				break;
		}
		return Some(ScriptError(String("cible d'affectation invalide"), target.line, target.column));
	}

	[[nodiscard]] static Option<ScriptError> AssignIndexed(const Value &object, const Value &index, Value value,
														   int line, int column) {
		if (object.IsNamespace())
			return Some(ReadOnlyNamespaceError(object, index.ToDisplayString(), line, column));
		if (object.IsList() && object.AsList()) {
			if (!index.IsNumber())
				return Some(ScriptError(String::Format("index de liste numérique attendu, trouvé `%s`",
													   index.TypeName()),
										line, column));
			std::vector<Value> &items = object.AsList()->items;
			double raw = index.AsNumber();
			if (raw != std::floor(raw) || raw < 0.0 || raw >= double(items.size()))
				return Some(ScriptError(String::Format("index de liste hors bornes (%s, taille %d)",
													   Value::NumberToString(raw).CStr(), int(items.size())),
										line, column));
			items[static_cast<size_t>(raw)] = std::move(value);
			return NONE;
		}
		if (object.IsMap() && object.AsMap()) {
			object.AsMap()->SetKey(index.ToDisplayString(), std::move(value));
			return NONE;
		}
		return Some(
			ScriptError(String::Format("`[...]` attend une liste ou une table, trouvé `%s`", object.TypeName()),
						line, column));
	}

	[[nodiscard]] Result<ExecOutcome, ScriptError> ExecWhile(const WhileStmt &stmt,
															 const std::shared_ptr<Environment> &env) {
		for (;;) {
			if (auto over = ConsumeStep(stmt.line, stmt.column); over.IsSome())
				return Err(over.Unwrap());
			auto condition = Eval(*stmt.condition, env);
			if (condition.IsError())
				return Err(condition.Error());
			if (!condition.Value().IsTruthy())
				return Ok(ExecOutcome{});

			auto outcome = ExecStmt(*stmt.body, env);
			if (outcome.IsError())
				return outcome;
			if (outcome.Value().flow == Flow::BREAK)
				return Ok(ExecOutcome{});
			if (outcome.Value().flow == Flow::RETURN)
				return outcome;
		}
	}

	[[nodiscard]] Result<ExecOutcome, ScriptError> ExecForIn(const ForInStmt &stmt,
															 const std::shared_ptr<Environment> &env) {
		auto iterable = Eval(*stmt.iterable, env);
		if (iterable.IsError())
			return Err(iterable.Error());

		// La suite itérée est MATÉRIALISÉE avant la boucle : modifier la
		// liste depuis le corps ne peut donc pas invalider l'itération (le
		// piège n°1 d'un `for` sur conteneur vivant).
		std::vector<Value> sequence;
		const Value &source = iterable.Value();
		if (source.IsList() && source.AsList()) {
			sequence = source.AsList()->items;
		} else if (source.IsMap() && source.AsMap()) {
			for (const auto &entry : source.AsMap()->entries)
				sequence.push_back(Value::Str(entry.first));
		} else if (source.IsString()) {
			const String &text = source.AsString();
			for (size_t i = 0; i < text.GetSize(); ++i)
				sequence.push_back(Value::Str(String(text.CStr() + i, 1)));
		} else {
			return Err(ScriptError(String::Format("`for ... in` attend une liste, une table ou une chaîne, "
												  "trouvé `%s`",
												  source.TypeName()),
								   stmt.line, stmt.column));
		}

		for (Value &item : sequence) {
			if (auto over = ConsumeStep(stmt.line, stmt.column); over.IsSome())
				return Err(over.Unwrap());

			auto scope = MakeEnvironment(env);
			scope->Declare(stmt.variable, BindingKind::LET, true).value = item;

			auto outcome = ExecStmt(*stmt.body, scope);
			if (outcome.IsError())
				return outcome;
			if (outcome.Value().flow == Flow::BREAK)
				return Ok(ExecOutcome{});
			if (outcome.Value().flow == Flow::RETURN)
				return outcome;
		}
		return Ok(ExecOutcome{});
	}

	// ── Expressions ──────────────────────────────────────────────────────────

	[[nodiscard]] Result<Value, ScriptError> Eval(const Expr &expr, const std::shared_ptr<Environment> &env) {
		switch (expr.kind) {
			case ExprKind::NUMBER:
				return Ok(Value::Number(static_cast<const NumberExpr &>(expr).value));
			case ExprKind::STRING_LITERAL:
				return Ok(Value::Str(static_cast<const StringExpr &>(expr).value));
			case ExprKind::BOOLEAN:
				return Ok(Value::Boolean(static_cast<const BooleanExpr &>(expr).value));
			case ExprKind::NIL:
				return Ok(Value::Nil());
			case ExprKind::IDENTIFIER: {
				const auto &e = static_cast<const IdentifierExpr &>(expr);
				if (Binding *binding = env->Lookup(e.name)) {
					if (!binding->initialized)
						return Err(DeadZoneError(e.name, expr.line, expr.column));
					return Ok(binding->value);
				}
				return Err(ScriptError(String::Format("variable inconnue `%s`", e.name.CStr()), expr.line,
									   expr.column));
			}
			case ExprKind::LIST: {
				const auto &e = static_cast<const ListExpr &>(expr);
				auto list = std::make_shared<ListObject>();
				list->items.reserve(e.elements.size());
				for (const ExprPtr &element : e.elements) {
					auto value = Eval(*element, env);
					if (value.IsError())
						return value;
					list->items.push_back(value.Unwrap());
				}
				return Ok(Value::List(std::move(list)));
			}
			case ExprKind::MAP: {
				const auto &e = static_cast<const MapExpr &>(expr);
				auto map = std::make_shared<MapObject>();
				for (const MapEntry &entry : e.entries) {
					auto key = Eval(*entry.key, env);
					if (key.IsError())
						return key;
					auto value = Eval(*entry.value, env);
					if (value.IsError())
						return value;
					map->SetKey(key.Value().ToDisplayString(), value.Unwrap());
				}
				return Ok(Value::Map(std::move(map)));
			}
			case ExprKind::UNARY:
				return EvalUnary(static_cast<const UnaryExpr &>(expr), env);
			case ExprKind::BINARY: {
				const auto &e = static_cast<const BinaryExpr &>(expr);
				auto left = Eval(*e.left, env);
				if (left.IsError())
					return left;
				auto right = Eval(*e.right, env);
				if (right.IsError())
					return right;
				return ApplyBinary(e.op, left.Value(), right.Value(), expr.line, expr.column);
			}
			case ExprKind::LOGICAL: {
				const auto &e = static_cast<const LogicalExpr &>(expr);
				auto left = Eval(*e.left, env);
				if (left.IsError())
					return left;
				// Court-circuit, et la VALEUR est rendue telle quelle (pas
				// convertie en booléen) — idiome `let x = maybe or default`.
				if (e.op == LogicalOp::OR && left.Value().IsTruthy())
					return left;
				if (e.op == LogicalOp::AND && !left.Value().IsTruthy())
					return left;
				return Eval(*e.right, env);
			}
			case ExprKind::CALL: {
				const auto &e = static_cast<const CallExpr &>(expr);
				auto callee = Eval(*e.callee, env);
				if (callee.IsError())
					return callee;
				std::vector<Value> args;
				args.reserve(e.args.size());
				for (const ExprPtr &arg : e.args) {
					auto value = Eval(*arg, env);
					if (value.IsError())
						return value;
					args.push_back(value.Unwrap());
				}
				return CallValue(callee.Value(), std::move(args), expr.line, expr.column);
			}
			case ExprKind::INDEX: {
				const auto &e = static_cast<const IndexExpr &>(expr);
				auto object = Eval(*e.object, env);
				if (object.IsError())
					return object;
				auto index = Eval(*e.index, env);
				if (index.IsError())
					return index;
				return ReadIndexed(object.Value(), index.Value(), expr.line, expr.column);
			}
			case ExprKind::MEMBER: {
				const auto &e = static_cast<const MemberExpr &>(expr);
				auto object = Eval(*e.object, env);
				if (object.IsError())
					return object;
				if (object.Value().IsNamespace())
					return ReadNamespaceMember(object.Value(), e.name, expr.line, expr.column);
				if (!object.Value().IsMap() || !object.Value().AsMap())
					return Err(ScriptError(String::Format("`.%s` attend une table, trouvé `%s`", e.name.CStr(),
														  object.Value().TypeName()),
										   expr.line, expr.column));
				const Value *found = object.Value().AsMap()->Find(e.name);
				return Ok(found ? *found : Value::Nil());
			}
			case ExprKind::FUNCTION: {
				const auto &e = static_cast<const FunctionExpr &>(expr);
				auto fn = std::make_shared<FunctionObject>();
				fn->def = e.def;
				fn->closure = env;
				return Ok(Value::Function(std::move(fn)));
			}
		}
		return Err(ScriptError(String("expression inconnue"), expr.line, expr.column));
	}

	[[nodiscard]] Result<Value, ScriptError> EvalUnary(const UnaryExpr &expr,
													   const std::shared_ptr<Environment> &env) {
		auto operand = Eval(*expr.operand, env);
		if (operand.IsError())
			return operand;
		if (expr.op == UnaryOp::NOT)
			return Ok(Value::Boolean(!operand.Value().IsTruthy()));
		if (!operand.Value().IsNumber())
			return Err(ScriptError(String::Format("`-` attend un nombre, trouvé `%s`", operand.Value().TypeName()),
								   expr.line, expr.column));
		return Ok(Value::Number(-operand.Value().AsNumber()));
	}

	[[nodiscard]] static Result<Value, ScriptError> ReadIndexed(const Value &object, const Value &index, int line,
																int column) {
		if (object.IsNamespace())
			return ReadNamespaceMember(object, index.ToDisplayString(), line, column);
		if (object.IsList() && object.AsList()) {
			if (!index.IsNumber())
				return Err(ScriptError(String::Format("index de liste numérique attendu, trouvé `%s`",
													  index.TypeName()),
									   line, column));
			const std::vector<Value> &items = object.AsList()->items;
			double raw = index.AsNumber();
			// Hors bornes = `nil` (pas une erreur) : un script d'éditeur
			// teste très souvent `if list[i] { ... }` en fin de parcours.
			if (raw != std::floor(raw) || raw < 0.0 || raw >= double(items.size()))
				return Ok(Value::Nil());
			return Ok(items[static_cast<size_t>(raw)]);
		}
		if (object.IsMap() && object.AsMap()) {
			const Value *found = object.AsMap()->Find(index.ToDisplayString());
			return Ok(found ? *found : Value::Nil());
		}
		if (object.IsString()) {
			if (!index.IsNumber())
				return Err(ScriptError(String::Format("index de chaîne numérique attendu, trouvé `%s`",
													  index.TypeName()),
									   line, column));
			const String &text = object.AsString();
			double raw = index.AsNumber();
			if (raw != std::floor(raw) || raw < 0.0 || raw >= double(text.GetSize()))
				return Ok(Value::Nil());
			return Ok(Value::Str(String(text.CStr() + static_cast<size_t>(raw), 1)));
		}
		return Err(ScriptError(
			String::Format("`[...]` attend une liste, une table ou une chaîne, trouvé `%s`", object.TypeName()),
			line, column));
	}

	[[nodiscard]] static Result<Value, ScriptError> ApplyBinary(BinaryOp op, const Value &left, const Value &right,
																int line, int column) {
		switch (op) {
			case BinaryOp::EQUAL:
				return Ok(Value::Boolean(left.Equals(right)));
			case BinaryOp::NOT_EQUAL:
				return Ok(Value::Boolean(!left.Equals(right)));
			case BinaryOp::CONCAT:
				return Ok(Value::Str(left.ToDisplayString() + right.ToDisplayString()));
			default:
				break;
		}

		// `+` concatène si UN des deux opérandes est une chaîne (commodité
		// façon Python/JS) ; `..` reste la forme explicite et toujours sûre.
		if (op == BinaryOp::ADD && (left.IsString() || right.IsString()))
			return Ok(Value::Str(left.ToDisplayString() + right.ToDisplayString()));

		if (!left.IsNumber() || !right.IsNumber())
			return Err(ScriptError(String::Format("opérateur %s inapplicable à `%s` et `%s`", BinaryOpName(op),
												  left.TypeName(), right.TypeName()),
								   line, column));

		double a = left.AsNumber(), b = right.AsNumber();
		switch (op) {
			case BinaryOp::ADD:
				return Ok(Value::Number(a + b));
			case BinaryOp::SUBTRACT:
				return Ok(Value::Number(a - b));
			case BinaryOp::MULTIPLY:
				return Ok(Value::Number(a * b));
			case BinaryOp::DIVIDE:
				if (b == 0.0)
					return Err(ScriptError(String("division par zéro"), line, column));
				return Ok(Value::Number(a / b));
			case BinaryOp::MODULO:
				if (b == 0.0)
					return Err(ScriptError(String("modulo par zéro"), line, column));
				return Ok(Value::Number(std::fmod(a, b)));
			case BinaryOp::LESS:
				return Ok(Value::Boolean(a < b));
			case BinaryOp::LESS_EQUAL:
				return Ok(Value::Boolean(a <= b));
			case BinaryOp::GREATER:
				return Ok(Value::Boolean(a > b));
			case BinaryOp::GREATER_EQUAL:
				return Ok(Value::Boolean(a >= b));
			default:
				break;
		}
		return Err(ScriptError(String("opérateur binaire inconnu"), line, column));
	}

	[[nodiscard]] static const char *BinaryOpName(BinaryOp op) noexcept {
		switch (op) {
			case BinaryOp::ADD:
				return "+";
			case BinaryOp::SUBTRACT:
				return "-";
			case BinaryOp::MULTIPLY:
				return "*";
			case BinaryOp::DIVIDE:
				return "/";
			case BinaryOp::MODULO:
				return "%";
			case BinaryOp::CONCAT:
				return "..";
			case BinaryOp::EQUAL:
				return "==";
			case BinaryOp::NOT_EQUAL:
				return "!=";
			case BinaryOp::LESS:
				return "<";
			case BinaryOp::LESS_EQUAL:
				return "<=";
			case BinaryOp::GREATER:
				return ">";
			case BinaryOp::GREATER_EQUAL:
				return ">=";
		}
		return "?";
	}

	void InstallStandardLibrary();

	// ── Portées : hissage, déclarations, espaces de noms ─────────────────────

	[[nodiscard]] static ScriptError DeadZoneError(const String &name, int line, int column) {
		return ScriptError(String::Format("`%s` est utilisée avant sa déclaration (zone morte d'un `let`/`const`)",
										  name.CStr()),
						   line, column);
	}

	[[nodiscard]] static ScriptError ReadOnlyNamespaceError(const Value &ns, const String &member, int line,
															int column) {
		return ScriptError(String::Format("`%s.%s` : les membres d'un espace de noms se modifient depuis son bloc "
										  "`namespace`, pas du dehors",
										  ns.AsNamespace() ? ns.AsNamespace()->name.CStr() : "?", member.CStr()),
						   line, column);
	}

	/// Lecture `ns.membre` : liaisons vivantes de l'espace de noms, SANS
	/// remonter à ses portées parentes (un membre est ce qu'il déclare).
	[[nodiscard]] static Result<Value, ScriptError> ReadNamespaceMember(const Value &ns, const String &member, int line,
																		int column) {
		const std::shared_ptr<NamespaceObject> &space = ns.AsNamespace();
		Binding *binding = space && space->scope ? space->scope->Local(member) : nullptr;
		if (!binding)
			return Err(ScriptError(String::Format("l'espace de noms `%s` n'a pas de membre `%s`",
												  space ? space->name.CStr() : "?", member.CStr()),
								   line, column));
		if (!binding->initialized)
			return Err(DeadZoneError(member, line, column));
		return Ok(binding->value);
	}

	/// Noms des `var` d'une liste d'instructions, blocs imbriqués compris —
	/// mais PAS ceux des fonctions ni des espaces de noms imbriqués, qui sont
	/// leurs propres portées de fonction.
	static void CollectVars(const std::vector<StmtPtr> &statements, std::vector<String> &out) {
		for (const StmtPtr &stmt : statements)
			if (stmt)
				CollectVars(*stmt, out);
	}
	static void CollectVars(const Stmt &stmt, std::vector<String> &out) {
		switch (stmt.kind) {
			case StmtKind::LET: {
				const auto &s = static_cast<const LetStmt &>(stmt);
				if (s.declKind == DeclKind::VAR)
					out.push_back(s.name);
				break;
			}
			case StmtKind::BLOCK:
				CollectVars(static_cast<const BlockStmt &>(stmt).statements, out);
				break;
			case StmtKind::IF: {
				const auto &s = static_cast<const IfStmt &>(stmt);
				if (s.thenBranch)
					CollectVars(*s.thenBranch, out);
				if (s.elseBranch)
					CollectVars(*s.elseBranch, out);
				break;
			}
			case StmtKind::WHILE:
				if (const auto &s = static_cast<const WhileStmt &>(stmt); s.body)
					CollectVars(*s.body, out);
				break;
			case StmtKind::FOR_IN:
				if (const auto &s = static_cast<const ForInStmt &>(stmt); s.body)
					CollectVars(*s.body, out);
				break;
			default:
				break;
		}
	}

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
	[[nodiscard]] Option<ScriptError> Hoist(const std::vector<StmtPtr> &statements,
											const std::shared_ptr<Environment> &env, bool functionBody,
											const FunctionDef *def) {
		for (const StmtPtr &stmt : statements) {
			if (!stmt)
				continue;
			if (stmt->kind == StmtKind::LET) {
				const auto &s = static_cast<const LetStmt &>(*stmt);
				if (s.declKind == DeclKind::VAR)
					continue;
				if (Binding *existing = env->Local(s.name); existing && existing->host)
					return Some(ScriptError(String::Format("`%s` est un nom réservé par l'application : "
														   "choisissez un autre nom",
														   s.name.CStr()),
											s.line, s.column));
				env->Declare(s.name, s.declKind == DeclKind::CONST ? BindingKind::CONST : BindingKind::LET, false);
			} else if (stmt->kind == StmtKind::FUNCTION) {
				const auto &s = static_cast<const FunctionStmt &>(*stmt);
				if (Binding *existing = env->Local(s.def->name);
					existing && existing->host && existing->kind == BindingKind::CONST)
					return Some(ScriptError(String::Format("`%s` est un nom réservé par l'application",
														   s.def->name.CStr()),
											s.line, s.column));
				auto fn = std::make_shared<FunctionObject>();
				fn->def = s.def;
				fn->closure = env;
				Binding &binding = env->Declare(s.def->name, BindingKind::FUNCTION, true);
				binding.value = Value::Function(std::move(fn));
			} else if (stmt->kind == StmtKind::NAMESPACE) {
				const auto &s = static_cast<const NamespaceStmt &>(*stmt);
				Binding *existing = env->Local(s.name);
				if (existing && existing->host && !existing->value.IsNamespace())
					return Some(ScriptError(String::Format("`%s` est un nom réservé par l'application", s.name.CStr()),
											s.line, s.column));
				// Un espace de noms déjà ouvert se ROUVRE (et s'étend) : on ne le
				// remet pas en zone morte.
				if (!existing || !existing->value.IsNamespace())
					env->Declare(s.name, BindingKind::CONST, false);
			}
		}
		if (!functionBody)
			return NONE;

		std::vector<String> localVars;
		const std::vector<String> *vars = &localVars;
		if (def) {
			if (!def->hoistComputed) {
				CollectVars(statements, def->hoistedVars);
				def->hoistComputed = true;
			}
			vars = &def->hoistedVars;
		} else {
			CollectVars(statements, localVars);
		}
		for (const String &name : *vars)
			if (!env->Local(name))
				env->Declare(name, BindingKind::VAR, true); // initialisé à nil
		return NONE;
	}

	/// `var` / `let` / `const` rencontrés à l'exécution.
	[[nodiscard]] Result<ExecOutcome, ScriptError> ExecDeclaration(const LetStmt &s,
																   const std::shared_ptr<Environment> &env) {
		Value initial = Value::Nil();
		if (s.initializer) {
			auto value = Eval(*s.initializer, env);
			if (value.IsError())
				return Err(value.Error());
			initial = value.Unwrap();
		}
		if (s.declKind == DeclKind::VAR) {
			// Vers la portée de FONCTION la plus proche (déjà hissé là).
			Environment *target = env->FunctionScope();
			Binding *binding = target->Local(s.name);
			if (!binding)
				binding = &target->Declare(s.name, BindingKind::VAR, true);
			if (binding->kind == BindingKind::LET || binding->kind == BindingKind::CONST)
				return Err(ScriptError(String::Format("`var %s` : le nom est déjà déclaré par `%s` dans cette fonction",
													  s.name.CStr(), binding->kind == BindingKind::CONST ? "const" : "let"),
									   s.line, s.column));
			// Redéclarer un `var` est permis ; sans initialiseur, il garde sa valeur.
			if (s.initializer)
				binding->value = std::move(initial);
			binding->initialized = true;
			return Ok(ExecOutcome{});
		}
		Binding *binding = env->Local(s.name);
		if (binding && binding->initialized && binding->kind != BindingKind::VAR && !binding->host)
			return Err(ScriptError(String::Format("`%s` est déjà déclarée dans cette portée", s.name.CStr()), s.line,
								   s.column));
		if (!binding)
			binding = &env->Declare(s.name, BindingKind::LET, false);
		binding->kind = s.declKind == DeclKind::CONST ? BindingKind::CONST : BindingKind::LET;
		binding->value = std::move(initial);
		binding->initialized = true;
		return Ok(ExecOutcome{});
	}

	/// `namespace nom { … }` : ouvre (ou rouvre) l'espace de noms puis exécute
	/// son corps dans SA portée — ce qu'il y déclare en devient les membres.
	[[nodiscard]] Result<ExecOutcome, ScriptError> ExecNamespace(const NamespaceStmt &s,
																 const std::shared_ptr<Environment> &env) {
		std::shared_ptr<NamespaceObject> space;
		Binding *binding = env->Local(s.name);
		if (binding && binding->initialized && binding->value.IsNamespace())
			space = binding->value.AsNamespace();
		if (!space) {
			space = std::make_shared<NamespaceObject>();
			space->name = s.name;
			space->scope = MakeEnvironment(env, true);
			Binding &declared = env->Declare(s.name, BindingKind::CONST, true);
			declared.value = Value::Namespace(space);
		}
		if (auto hoisted = Hoist(s.body, space->scope, true, nullptr); hoisted.IsSome())
			return Err(hoisted.Unwrap());
		for (const StmtPtr &child : s.body) {
			if (!child)
				continue;
			auto outcome = ExecStmt(*child, space->scope);
			if (outcome.IsError())
				return outcome;
			if (outcome.Value().flow != Flow::NORMAL)
				return Err(ScriptError(String("`return`/`break`/`continue` interdit dans un espace de noms"),
									   child->line, child->column));
		}
		return Ok(ExecOutcome{});
	}

	// ── Membres ──────────────────────────────────────────────────────────────

	std::shared_ptr<Environment> m_globals;
	std::shared_ptr<Program> m_program; ///< garde l'AST du dernier Run() en vie
	std::vector<std::weak_ptr<Environment>> m_environments;
	size_t m_environmentPurgeThreshold = 256;
	std::vector<String> m_output;
	uint64_t m_steps = 0;
	size_t m_callDepth = 0;
	uint64_t m_randomState = 0x9E3779B97F4A7C15ull;
};


// ============================================================================
// Bibliothèque standard — helpers d'arguments
// ============================================================================

namespace detail {

/// L'arité étant déjà validée par `CallValue`, ces helpers ne vérifient QUE
/// le type : un argument obligatoire est forcément présent au moment où une
/// native tourne.
[[nodiscard]] inline Result<double, ScriptError> ArgNumber(const std::vector<Value> &args, size_t index,
														   const char *fnName) {
	if (index >= args.size() || !args[index].IsNumber())
		return Err(Interpreter::MakeError(String::Format("`%s` : argument %d doit être un nombre, trouvé `%s`",
														 fnName, int(index + 1),
														 index < args.size() ? args[index].TypeName() : "rien")));
	return Ok(args[index].AsNumber());
}

[[nodiscard]] inline Result<String, ScriptError> ArgString(const std::vector<Value> &args, size_t index,
														   const char *fnName) {
	if (index >= args.size() || !args[index].IsString())
		return Err(Interpreter::MakeError(String::Format("`%s` : argument %d doit être une chaîne, trouvé `%s`",
														 fnName, int(index + 1),
														 index < args.size() ? args[index].TypeName() : "rien")));
	return Ok(args[index].AsString());
}

[[nodiscard]] inline Result<std::shared_ptr<ListObject>, ScriptError>
ArgList(const std::vector<Value> &args, size_t index, const char *fnName) {
	if (index >= args.size() || !args[index].IsList() || !args[index].AsList())
		return Err(Interpreter::MakeError(String::Format("`%s` : argument %d doit être une liste, trouvé `%s`",
														 fnName, int(index + 1),
														 index < args.size() ? args[index].TypeName() : "rien")));
	return Ok(args[index].AsList());
}

[[nodiscard]] inline Result<std::shared_ptr<MapObject>, ScriptError> ArgMap(const std::vector<Value> &args,
																			size_t index, const char *fnName) {
	if (index >= args.size() || !args[index].IsMap() || !args[index].AsMap())
		return Err(Interpreter::MakeError(String::Format("`%s` : argument %d doit être une table, trouvé `%s`",
														 fnName, int(index + 1),
														 index < args.size() ? args[index].TypeName() : "rien")));
	return Ok(args[index].AsMap());
}

/// Indice de liste depuis une valeur de script : entier, dans les bornes.
[[nodiscard]] inline Result<size_t, ScriptError> ToIndex(double raw, size_t size, const char *fnName) {
	if (raw != std::floor(raw) || raw < 0.0 || raw >= double(size))
		return Err(Interpreter::MakeError(String::Format("`%s` : indice %s hors bornes (taille %d)", fnName,
														 Value::NumberToString(raw).CStr(), int(size))));
	return Ok(static_cast<size_t>(raw));
}

} // namespace detail

// ============================================================================
// Bibliothèque standard — installation
// ============================================================================

inline void Interpreter::InstallStandardLibrary() {
	// ── Général ─────────────────────────────────────────────────────────────

	RegisterNative("print", 0, -1, [](Interpreter &vm, std::vector<Value> &args) -> Result<Value, ScriptError> {
		String line;
		for (size_t i = 0; i < args.size(); ++i) {
			if (i > 0)
				line.Concat(" ");
			line.Concat(args[i].ToDisplayString());
		}
		vm.Emit(line);
		return Ok(Value::Nil());
	});

	RegisterNative("type", 1, 1, [](Interpreter &, std::vector<Value> &args) -> Result<Value, ScriptError> {
		return Ok(Value::Str(String(args[0].TypeName())));
	});

	// Le seul moyen pour un script de signaler lui-même un échec — utilisé
	// par les scénarios de test de l'éditeur.
	RegisterNative("assert", 1, 2, [](Interpreter &, std::vector<Value> &args) -> Result<Value, ScriptError> {
		if (args[0].IsTruthy())
			return Ok(args[0]);
		String message = args.size() > 1 ? args[1].ToDisplayString() : String("assertion échouée");
		return Err(Interpreter::MakeError(std::move(message)));
	});

	RegisterNative("len", 1, 1, [](Interpreter &, std::vector<Value> &args) -> Result<Value, ScriptError> {
		const Value &v = args[0];
		if (v.IsList() && v.AsList())
			return Ok(Value::Number(double(v.AsList()->items.size())));
		if (v.IsMap() && v.AsMap())
			return Ok(Value::Number(double(v.AsMap()->entries.size())));
		if (v.IsString())
			return Ok(Value::Number(double(v.AsString().GetSize())));
		return Err(Interpreter::MakeError(
			String::Format("`len` attend une liste, une table ou une chaîne, trouvé `%s`", v.TypeName())));
	});

	RegisterNative("str", 1, 1, [](Interpreter &, std::vector<Value> &args) -> Result<Value, ScriptError> {
		return Ok(Value::Str(args[0].ToDisplayString()));
	});

	// `num` rend `nil` (et non une erreur) sur une chaîne non numérique :
	// c'est ce qui permet `let v = num(saisie) or defaut`.
	RegisterNative("num", 1, 1, [](Interpreter &, std::vector<Value> &args) -> Result<Value, ScriptError> {
		const Value &v = args[0];
		if (v.IsNumber())
			return Ok(v);
		if (v.IsBoolean())
			return Ok(Value::Number(v.AsBoolean() ? 1.0 : 0.0));
		if (v.IsString()) {
			Option<double> parsed = v.AsString().Trim().TryParseDouble();
			if (parsed.IsNone())
				return Ok(Value::Nil());
			return Ok(Value::Number(parsed.Unwrap()));
		}
		return Ok(Value::Nil());
	});

	RegisterNative("int", 1, 1, [](Interpreter &, std::vector<Value> &args) -> Result<Value, ScriptError> {
		auto n = detail::ArgNumber(args, 0, "int");
		if (n.IsError())
			return Err(n.Error());
		return Ok(Value::Number(std::trunc(n.Unwrap())));
	});

	// Horloge monotone, en secondes depuis la création de l'interpréteur —
	// std::chrono plutôt que sdl3::GetTicksMS pour que data:: reste
	// indépendant de sdl3:: (aucune autre partie du module n'en dépend).
	RegisterNative("clock", 0, 0, [](Interpreter &, std::vector<Value> &) -> Result<Value, ScriptError> {
		static const std::chrono::steady_clock::time_point START = std::chrono::steady_clock::now();
		auto elapsed = std::chrono::steady_clock::now() - START;
		return Ok(Value::Number(std::chrono::duration<double>(elapsed).count()));
	});

	// ── Mathématiques : l'espace de noms `math` ────────────────────────────
	// Fonctions ET constantes rangées sous `math.` (`math.sin`, `math.pi`)
	// plutôt que dans les globales, où des noms d'une lettre (`e`, `c`, `g`)
	// entraient en collision avec les variables des scripts.

	auto unaryMath = [this](const char *name, double (*fn)(double)) {
		const String qualified = String::Format("math.%s", name);
		RegisterNamespacedNative(String("math"), String(name), 1, 1,
								 [qualified, fn](Interpreter &, std::vector<Value> &args) -> Result<Value, ScriptError> {
									 auto n = detail::ArgNumber(args, 0, qualified.CStr());
									 if (n.IsError())
										 return Err(n.Error());
									 return Ok(Value::Number(fn(n.Unwrap())));
								 });
	};
	unaryMath("abs", [](double v) { return std::fabs(v); });
	unaryMath("floor", [](double v) { return std::floor(v); });
	unaryMath("ceil", [](double v) { return std::ceil(v); });
	unaryMath("round", [](double v) { return std::round(v); });
	unaryMath("sqrt", [](double v) { return v > 0.0 ? std::sqrt(v) : 0.0; });
	unaryMath("sin", [](double v) { return std::sin(v); });
	unaryMath("cos", [](double v) { return std::cos(v); });
	unaryMath("tan", [](double v) { return std::tan(v); });
	unaryMath("asin", [](double v) { return std::asin(v); });
	unaryMath("acos", [](double v) { return std::acos(v); });
	unaryMath("atan", [](double v) { return std::atan(v); });
	unaryMath("exp", [](double v) { return std::exp(v); });
	unaryMath("log", [](double v) { return v > 0.0 ? std::log(v) : 0.0; });
	unaryMath("sign", [](double v) { return v > 0.0 ? 1.0 : (v < 0.0 ? -1.0 : 0.0); });
	unaryMath("rad", [](double v) { return v * 3.14159265358979323846 / 180.0; });
	unaryMath("deg", [](double v) { return v * 180.0 / 3.14159265358979323846; });

	// Constantes : des VALEURS (`math.pi`), non des fonctions à appeler.
	const std::pair<const char *, double> mathConstants[] = {
		{"pi", 3.14159265358979323846},
		{"e", 2.71828182845904523536},
		{"phi", 1.61803398874989484820},   // nombre d'or
		{"sqrt2", 1.41421356237309504880}, // racine de 2
		{"sqrt3", 1.73205080756887729352}, // racine de 3
		{"tau", 6.28318530717958647692},   // 2π (tour complet)
		{"pi_2", 1.57079632679489661923},  // π/2 (angle droit)
		{"pi_4", 0.78539816339744830962},  // π/4 (45°)
		{"inv_pi", 0.31830988618379067154}, // 1/π
		{"ln2", 0.69314718055994530941},
		{"ln10", 2.30258509299404568402},
		{"euler_mascheroni", 0.57721566490153286060}, // constante γ
		{"catalan", 0.91596559417721901505},
		{"inf", HUGE_VAL},
	};
	for (const auto &[name, value] : mathConstants)
		RegisterNamespaceConstant(String("math"), String(name), Value::Number(value));

	// Constantes physiques : un espace de noms à part — ce ne sont pas des
	// mathématiques, et `physics.c` se lit mieux qu'un `c` global.
	const std::pair<const char *, double> physicsConstants[] = {
		{"c", 299792458.0},          // vitesse de la lumière dans le vide (m/s)
		{"g", 9.80665},              // pesanteur terrestre standard (m/s²)
		{"R", 8.314462618},          // constante des gaz parfaits (J mol⁻¹ K⁻¹)
		{"N_A", 6.02214076e23},      // nombre d'Avogadro (mol⁻¹)
		{"k_B", 1.380649e-23},       // constante de Boltzmann (J/K)
		{"std_atm", 101325.0},       // pression atmosphérique standard (Pa)
		{"h", 6.62607015e-34},       // constante de Planck (J s)
		{"hbar", 1.054571817e-34},   // constante de Planck réduite (h / 2π)
		{"e_charge", 1.602176634e-19}, // charge élémentaire (C)
		{"m_e", 9.1093837015e-31},   // masse de l'électron au repos (kg)
	};
	for (const auto &[name, value] : physicsConstants)
		RegisterNamespaceConstant(String("physics"), String(name), Value::Number(value));

	RegisterNamespacedNative(String("math"), String("atan2"), 2, 2, [](Interpreter &, std::vector<Value> &args) -> Result<Value, ScriptError> {
		auto y = detail::ArgNumber(args, 0, "math.atan2");
		if (y.IsError())
			return Err(y.Error());
		auto x = detail::ArgNumber(args, 1, "math.atan2");
		if (x.IsError())
			return Err(x.Error());
		return Ok(Value::Number(std::atan2(y.Unwrap(), x.Unwrap())));
	});

	RegisterNamespacedNative(String("math"), String("pow"), 2, 2, [](Interpreter &, std::vector<Value> &args) -> Result<Value, ScriptError> {
		auto base = detail::ArgNumber(args, 0, "math.pow");
		if (base.IsError())
			return Err(base.Error());
		auto exponent = detail::ArgNumber(args, 1, "math.pow");
		if (exponent.IsError())
			return Err(exponent.Error());
		return Ok(Value::Number(std::pow(base.Unwrap(), exponent.Unwrap())));
	});

	RegisterNamespacedNative(String("math"), String("min"), 1, -1, [](Interpreter &, std::vector<Value> &args) -> Result<Value, ScriptError> {
		double best = 0.0;
		for (size_t i = 0; i < args.size(); ++i) {
			auto n = detail::ArgNumber(args, i, "math.min");
			if (n.IsError())
				return Err(n.Error());
			double v = n.Unwrap();
			if (i == 0 || v < best)
				best = v;
		}
		return Ok(Value::Number(best));
	});

	RegisterNamespacedNative(String("math"), String("max"), 1, -1, [](Interpreter &, std::vector<Value> &args) -> Result<Value, ScriptError> {
		double best = 0.0;
		for (size_t i = 0; i < args.size(); ++i) {
			auto n = detail::ArgNumber(args, i, "math.max");
			if (n.IsError())
				return Err(n.Error());
			double v = n.Unwrap();
			if (i == 0 || v > best)
				best = v;
		}
		return Ok(Value::Number(best));
	});

	RegisterNamespacedNative(String("math"), String("clamp"), 3, 3, [](Interpreter &, std::vector<Value> &args) -> Result<Value, ScriptError> {
		auto value = detail::ArgNumber(args, 0, "math.clamp");
		if (value.IsError())
			return Err(value.Error());
		auto low = detail::ArgNumber(args, 1, "math.clamp");
		if (low.IsError())
			return Err(low.Error());
		auto high = detail::ArgNumber(args, 2, "math.clamp");
		if (high.IsError())
			return Err(high.Error());
		double v = value.Unwrap(), lo = low.Unwrap(), hi = high.Unwrap();
		return Ok(Value::Number(v < lo ? lo : (v > hi ? hi : v)));
	});

	RegisterNamespacedNative(String("math"), String("lerp"), 3, 3, [](Interpreter &, std::vector<Value> &args) -> Result<Value, ScriptError> {
		auto from = detail::ArgNumber(args, 0, "math.lerp");
		if (from.IsError())
			return Err(from.Error());
		auto to = detail::ArgNumber(args, 1, "math.lerp");
		if (to.IsError())
			return Err(to.Error());
		auto t = detail::ArgNumber(args, 2, "math.lerp");
		if (t.IsError())
			return Err(t.Error());
		double a = from.Unwrap(), b = to.Unwrap(), k = t.Unwrap();
		return Ok(Value::Number(a + (b - a) * k));
	});

	RegisterNamespacedNative(String("math"), String("random"), 0, 0, [](Interpreter &vm, std::vector<Value> &) -> Result<Value, ScriptError> {
		return Ok(Value::Number(vm.NextRandom()));
	});

	RegisterNamespacedNative(String("math"), String("random_int"), 2, 2, [](Interpreter &vm, std::vector<Value> &args) -> Result<Value, ScriptError> {
		auto low = detail::ArgNumber(args, 0, "math.random_int");
		if (low.IsError())
			return Err(low.Error());
		auto high = detail::ArgNumber(args, 1, "math.random_int");
		if (high.IsError())
			return Err(high.Error());
		double lo = std::floor(low.Unwrap()), hi = std::floor(high.Unwrap());
		if (hi < lo)
			return Err(Interpreter::MakeError(String("`math.random_int` : borne haute inférieure à la borne basse")));
		double span = hi - lo + 1.0;
		return Ok(Value::Number(lo + std::floor(vm.NextRandom() * span)));
	});

	// ── Listes ──────────────────────────────────────────────────────────────

	RegisterNative("list", 0, -1, [](Interpreter &, std::vector<Value> &args) -> Result<Value, ScriptError> {
		auto list = std::make_shared<ListObject>();
		list->items = args;
		return Ok(Value::List(std::move(list)));
	});

	RegisterNative("push", 2, -1, [](Interpreter &, std::vector<Value> &args) -> Result<Value, ScriptError> {
		auto list = detail::ArgList(args, 0, "push");
		if (list.IsError())
			return Err(list.Error());
		for (size_t i = 1; i < args.size(); ++i)
			list.Value()->items.push_back(args[i]);
		return Ok(args[0]);
	});

	RegisterNative("pop", 1, 1, [](Interpreter &, std::vector<Value> &args) -> Result<Value, ScriptError> {
		auto list = detail::ArgList(args, 0, "pop");
		if (list.IsError())
			return Err(list.Error());
		std::vector<Value> &items = list.Value()->items;
		if (items.empty())
			return Ok(Value::Nil());
		Value last = items.back();
		items.pop_back();
		return Ok(last);
	});

	RegisterNative("insert", 3, 3, [](Interpreter &, std::vector<Value> &args) -> Result<Value, ScriptError> {
		auto list = detail::ArgList(args, 0, "insert");
		if (list.IsError())
			return Err(list.Error());
		auto rawIndex = detail::ArgNumber(args, 1, "insert");
		if (rawIndex.IsError())
			return Err(rawIndex.Error());
		std::vector<Value> &items = list.Value()->items;
		double raw = rawIndex.Unwrap();
		// Insertion EN FIN autorisée (indice == taille), contrairement à une
		// lecture — d'où le contrôle local plutôt que detail::ToIndex.
		if (raw != std::floor(raw) || raw < 0.0 || raw > double(items.size()))
			return Err(Interpreter::MakeError(String::Format("`insert` : indice %s hors bornes (taille %d)",
															 Value::NumberToString(raw).CStr(), int(items.size()))));
		items.insert(items.begin() + static_cast<ptrdiff_t>(raw), args[2]);
		return Ok(args[0]);
	});

	RegisterNative("remove", 2, 2, [](Interpreter &, std::vector<Value> &args) -> Result<Value, ScriptError> {
		auto list = detail::ArgList(args, 0, "remove");
		if (list.IsError())
			return Err(list.Error());
		auto rawIndex = detail::ArgNumber(args, 1, "remove");
		if (rawIndex.IsError())
			return Err(rawIndex.Error());
		std::vector<Value> &items = list.Value()->items;
		auto index = detail::ToIndex(rawIndex.Unwrap(), items.size(), "remove");
		if (index.IsError())
			return Err(index.Error());
		Value removed = items[index.Value()];
		items.erase(items.begin() + static_cast<ptrdiff_t>(index.Value()));
		return Ok(removed);
	});

	RegisterNative("clear", 1, 1, [](Interpreter &, std::vector<Value> &args) -> Result<Value, ScriptError> {
		if (args[0].IsList() && args[0].AsList()) {
			args[0].AsList()->items.clear();
			return Ok(args[0]);
		}
		if (args[0].IsMap() && args[0].AsMap()) {
			args[0].AsMap()->entries.clear();
			return Ok(args[0]);
		}
		return Err(Interpreter::MakeError(
			String::Format("`clear` attend une liste ou une table, trouvé `%s`", args[0].TypeName())));
	});

	RegisterNative("index_of", 2, 2, [](Interpreter &, std::vector<Value> &args) -> Result<Value, ScriptError> {
		auto list = detail::ArgList(args, 0, "index_of");
		if (list.IsError())
			return Err(list.Error());
		const std::vector<Value> &items = list.Value()->items;
		for (size_t i = 0; i < items.size(); ++i)
			if (items[i].Equals(args[1]))
				return Ok(Value::Number(double(i)));
		return Ok(Value::Number(-1.0));
	});

	RegisterNative("reverse", 1, 1, [](Interpreter &, std::vector<Value> &args) -> Result<Value, ScriptError> {
		auto list = detail::ArgList(args, 0, "reverse");
		if (list.IsError())
			return Err(list.Error());
		std::vector<Value> &items = list.Value()->items;
		for (size_t i = 0, j = items.size(); i + 1 < j; ++i, --j) {
			Value tmp = items[i];
			items[i] = items[j - 1];
			items[j - 1] = tmp;
		}
		return Ok(args[0]);
	});

	// Tri numérique croissant, en place. Tri par insertion : O(n^2) mais
	// sans comparateur scripté (donc sans rappel pouvant échouer au milieu
	// d'un std::sort, ce qui casserait l'invariant de tri sans moyen de
	// remonter l'erreur proprement).
	RegisterNative("sort_numbers", 1, 1, [](Interpreter &, std::vector<Value> &args) -> Result<Value, ScriptError> {
		auto list = detail::ArgList(args, 0, "sort_numbers");
		if (list.IsError())
			return Err(list.Error());
		std::vector<Value> &items = list.Value()->items;
		for (const Value &item : items)
			if (!item.IsNumber())
				return Err(Interpreter::MakeError(
					String::Format("`sort_numbers` : élément `%s` non numérique", item.TypeName())));
		for (size_t i = 1; i < items.size(); ++i) {
			Value key = items[i];
			size_t j = i;
			while (j > 0 && items[j - 1].AsNumber() > key.AsNumber()) {
				items[j] = items[j - 1];
				--j;
			}
			items[j] = key;
		}
		return Ok(args[0]);
	});

	RegisterNative("range", 1, 3, [](Interpreter &, std::vector<Value> &args) -> Result<Value, ScriptError> {
		double start = 0.0, stop = 0.0, step = 1.0;
		auto first = detail::ArgNumber(args, 0, "range");
		if (first.IsError())
			return Err(first.Error());
		if (args.size() == 1) {
			stop = first.Unwrap();
		} else {
			start = first.Unwrap();
			auto second = detail::ArgNumber(args, 1, "range");
			if (second.IsError())
				return Err(second.Error());
			stop = second.Unwrap();
			if (args.size() > 2) {
				auto third = detail::ArgNumber(args, 2, "range");
				if (third.IsError())
					return Err(third.Error());
				step = third.Unwrap();
			}
		}
		if (step == 0.0)
			return Err(Interpreter::MakeError(String("`range` : pas nul")));

		auto list = std::make_shared<ListObject>();
		// Borne dure : `range` matérialise sa liste, une borne farfelue
		// mangerait toute la mémoire avant que maxSteps ne s'en aperçoive.
		constexpr size_t MAX_RANGE = 1000000;
		for (double v = start; (step > 0.0 ? v < stop : v > stop); v += step) {
			if (list->items.size() >= MAX_RANGE)
				return Err(Interpreter::MakeError(String::Format("`range` : plus de %d éléments", int(MAX_RANGE))));
			list->items.push_back(Value::Number(v));
		}
		return Ok(Value::List(std::move(list)));
	});

	// ── Tables ──────────────────────────────────────────────────────────────

	RegisterNative("map", 0, 0, [](Interpreter &, std::vector<Value> &) -> Result<Value, ScriptError> {
		return Ok(Value::EmptyMap());
	});

	RegisterNative("keys", 1, 1, [](Interpreter &, std::vector<Value> &args) -> Result<Value, ScriptError> {
		// Espace de noms : ses membres initialisés (ce qu'on peut lire).
		if (args[0].IsNamespace() && args[0].AsNamespace()) {
			auto members = std::make_shared<ListObject>();
			for (const Binding &binding : args[0].AsNamespace()->scope->Bindings())
				if (binding.initialized)
					members->items.push_back(Value::Str(binding.name));
			return Ok(Value::List(std::move(members)));
		}
		auto map = detail::ArgMap(args, 0, "keys");
		if (map.IsError())
			return Err(map.Error());
		auto list = std::make_shared<ListObject>();
		for (const auto &entry : map.Value()->entries)
			list->items.push_back(Value::Str(entry.first));
		return Ok(Value::List(std::move(list)));
	});

	RegisterNative("values", 1, 1, [](Interpreter &, std::vector<Value> &args) -> Result<Value, ScriptError> {
		auto map = detail::ArgMap(args, 0, "values");
		if (map.IsError())
			return Err(map.Error());
		auto list = std::make_shared<ListObject>();
		for (const auto &entry : map.Value()->entries)
			list->items.push_back(entry.second);
		return Ok(Value::List(std::move(list)));
	});

	RegisterNative("has", 2, 2, [](Interpreter &, std::vector<Value> &args) -> Result<Value, ScriptError> {
		if (args[0].IsNamespace() && args[0].AsNamespace()) {
			const Binding *member = args[0].AsNamespace()->scope->Local(args[1].ToDisplayString());
			return Ok(Value::Boolean(member && member->initialized));
		}
		auto map = detail::ArgMap(args, 0, "has");
		if (map.IsError())
			return Err(map.Error());
		return Ok(Value::Boolean(map.Value()->Find(args[1].ToDisplayString()) != nullptr));
	});

	RegisterNative("erase", 2, 2, [](Interpreter &, std::vector<Value> &args) -> Result<Value, ScriptError> {
		auto map = detail::ArgMap(args, 0, "erase");
		if (map.IsError())
			return Err(map.Error());
		return Ok(Value::Boolean(map.Value()->RemoveKey(args[1].ToDisplayString())));
	});

	// ── Chaînes ─────────────────────────────────────────────────────────────

	RegisterNative("upper", 1, 1, [](Interpreter &, std::vector<Value> &args) -> Result<Value, ScriptError> {
		auto text = detail::ArgString(args, 0, "upper");
		if (text.IsError())
			return Err(text.Error());
		return Ok(Value::Str(text.Value().ToUpper()));
	});

	RegisterNative("lower", 1, 1, [](Interpreter &, std::vector<Value> &args) -> Result<Value, ScriptError> {
		auto text = detail::ArgString(args, 0, "lower");
		if (text.IsError())
			return Err(text.Error());
		return Ok(Value::Str(text.Value().ToLower()));
	});

	RegisterNative("trim", 1, 1, [](Interpreter &, std::vector<Value> &args) -> Result<Value, ScriptError> {
		auto text = detail::ArgString(args, 0, "trim");
		if (text.IsError())
			return Err(text.Error());
		return Ok(Value::Str(text.Value().Trim()));
	});

	RegisterNative("sub", 2, 3, [](Interpreter &, std::vector<Value> &args) -> Result<Value, ScriptError> {
		auto text = detail::ArgString(args, 0, "sub");
		if (text.IsError())
			return Err(text.Error());
		auto start = detail::ArgNumber(args, 1, "sub");
		if (start.IsError())
			return Err(start.Error());
		const String &s = text.Value();
		double rawStart = std::floor(start.Unwrap());
		if (rawStart < 0.0)
			rawStart = 0.0;
		if (rawStart > double(s.GetSize()))
			return Ok(Value::Str(String()));
		size_t begin = static_cast<size_t>(rawStart);
		size_t count = s.GetSize() - begin;
		if (args.size() > 2) {
			auto length = detail::ArgNumber(args, 2, "sub");
			if (length.IsError())
				return Err(length.Error());
			double rawLength = std::floor(length.Unwrap());
			if (rawLength < 0.0)
				rawLength = 0.0;
			if (rawLength < double(count))
				count = static_cast<size_t>(rawLength);
		}
		return Ok(Value::Str(s.Substr(begin, count)));
	});

	// -1 si absent (jamais `nil`) : le résultat reste toujours comparable
	// numériquement, ce qui évite un `if idx != nil and idx >= 0`.
	RegisterNative("find", 2, 2, [](Interpreter &, std::vector<Value> &args) -> Result<Value, ScriptError> {
		auto text = detail::ArgString(args, 0, "find");
		if (text.IsError())
			return Err(text.Error());
		auto needle = detail::ArgString(args, 1, "find");
		if (needle.IsError())
			return Err(needle.Error());
		size_t found = text.Value().Find(needle.Value());
		if (found == String::NPOS)
			return Ok(Value::Number(-1.0));
		return Ok(Value::Number(double(found)));
	});

	RegisterNative("replace", 3, 3, [](Interpreter &, std::vector<Value> &args) -> Result<Value, ScriptError> {
		auto text = detail::ArgString(args, 0, "replace");
		if (text.IsError())
			return Err(text.Error());
		auto from = detail::ArgString(args, 1, "replace");
		if (from.IsError())
			return Err(from.Error());
		auto to = detail::ArgString(args, 2, "replace");
		if (to.IsError())
			return Err(to.Error());
		if (from.Value().IsEmpty())
			return Ok(args[0]);
		return Ok(Value::Str(text.Value().Replace(from.Value().View(), to.Value().View())));
	});

	RegisterNative("starts_with", 2, 2, [](Interpreter &, std::vector<Value> &args) -> Result<Value, ScriptError> {
		auto text = detail::ArgString(args, 0, "starts_with");
		if (text.IsError())
			return Err(text.Error());
		auto prefix = detail::ArgString(args, 1, "starts_with");
		if (prefix.IsError())
			return Err(prefix.Error());
		return Ok(Value::Boolean(text.Value().StartsWith(prefix.Value().View())));
	});

	RegisterNative("ends_with", 2, 2, [](Interpreter &, std::vector<Value> &args) -> Result<Value, ScriptError> {
		auto text = detail::ArgString(args, 0, "ends_with");
		if (text.IsError())
			return Err(text.Error());
		auto suffix = detail::ArgString(args, 1, "ends_with");
		if (suffix.IsError())
			return Err(suffix.Error());
		return Ok(Value::Boolean(text.Value().EndsWith(suffix.Value().View())));
	});

	RegisterNative("split", 2, 2, [](Interpreter &, std::vector<Value> &args) -> Result<Value, ScriptError> {
		auto text = detail::ArgString(args, 0, "split");
		if (text.IsError())
			return Err(text.Error());
		auto separator = detail::ArgString(args, 1, "split");
		if (separator.IsError())
			return Err(separator.Error());
		auto list = std::make_shared<ListObject>();
		if (separator.Value().IsEmpty()) {
			const String &s = text.Value();
			for (size_t i = 0; i < s.GetSize(); ++i)
				list->items.push_back(Value::Str(String(s.CStr() + i, 1)));
		} else {
			for (const String &part : text.Value().Split(separator.Value().View()))
				list->items.push_back(Value::Str(part));
		}
		return Ok(Value::List(std::move(list)));
	});

	RegisterNative("join", 2, 2, [](Interpreter &, std::vector<Value> &args) -> Result<Value, ScriptError> {
		auto list = detail::ArgList(args, 0, "join");
		if (list.IsError())
			return Err(list.Error());
		auto separator = detail::ArgString(args, 1, "join");
		if (separator.IsError())
			return Err(separator.Error());
		String out;
		const std::vector<Value> &items = list.Value()->items;
		for (size_t i = 0; i < items.size(); ++i) {
			if (i > 0)
				out.Concat(separator.Value());
			out.Concat(items[i].ToDisplayString());
		}
		return Ok(Value::Str(std::move(out)));
	});

	// `format("pos {} / {}", x, total)` — marqueurs positionnels `{}`,
	// délibérément PAS un printf : un `%d` face à une chaîne serait un
	// comportement indéfini, alors qu'ici tout argument est affichable.
	RegisterNative("format", 1, -1, [](Interpreter &, std::vector<Value> &args) -> Result<Value, ScriptError> {
		auto pattern = detail::ArgString(args, 0, "format");
		if (pattern.IsError())
			return Err(pattern.Error());
		const String &fmt = pattern.Value();
		String out;
		size_t nextArg = 1;
		for (size_t i = 0; i < fmt.GetSize(); ++i) {
			char c = fmt.CharAt(i);
			if (c == '{' && i + 1 < fmt.GetSize() && fmt.CharAt(i + 1) == '}') {
				out.Concat(nextArg < args.size() ? args[nextArg].ToDisplayString() : String("nil"));
				++nextArg;
				++i;
				continue;
			}
			out.PushBack(c);
		}
		return Ok(Value::Str(std::move(out)));
	});
}

} // namespace data::script
