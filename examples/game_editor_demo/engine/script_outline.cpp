#include "script_outline.hpp"

#include "data/script/script_parser.hpp"
#include "script_owners.hpp"

#include <algorithm>
#include <memory>

namespace game_editor {

using namespace data::script;

namespace {

constexpr int MAX_IMPORT_DEPTH = 8;

/// Une déclaration de classe relevée, avant résolution.
struct Declared {
	ClassDefPtr def;
	String name;   ///< qualifié par les espaces de noms
	String prefix; ///< espace de noms où elle est déclarée (`a.b.`), vide au niveau 0
	size_t mod = 0;
	int line = 0;
};

/// Un module analysé (le script lui-même est le module 0).
struct Module {
	String specifier;
	Program program;
	std::vector<std::pair<String, size_t>> aliases; ///< `const m = import "x"` → (m, module)
};

class Analyzer {
public:
	explicit Analyzer(const ModuleSource& modules) : m_sources(modules) {}

	Option<ScriptError> Load(const String& source) {
		auto program = Parser::Compile(StringView(source.CStr(), source.GetSize()));
		if (program.IsError())
			return Some(program.Error());
		m_modules.push_back(
			std::make_unique<Module>(Module{String(), std::move(program).Unwrap(), {}}));
		Collect(0, m_modules[0]->program.statements, String(), 0);
		return NONE;
	}

	[[nodiscard]] const std::vector<String>& Imports() const noexcept { return m_imports; }

	std::vector<ScriptClassInfo> Resolve() {
		m_infos.assign(m_declared.size(), ScriptClassInfo());
		m_state.assign(m_declared.size(), 0);
		for (size_t i = 0; i < m_declared.size(); ++i)
			ResolveOne(i);
		return m_infos;
	}

	/// Indice de la classe parente de script de chaque classe (-1 : aucune).
	[[nodiscard]] const std::vector<long>& Parents() const noexcept { return m_parent; }

private:
	/// Relève classes et imports d'une liste d'instructions (récursif sur les
	/// espaces de noms ; les imports d'un module sont chargés à la volée).
	void Collect(size_t mod, const std::vector<StmtPtr>& statements, const String& prefix,
				 int depth) {
		for (const StmtPtr& stmt : statements) {
			if (!stmt)
				continue;
			if (stmt->kind == StmtKind::CLASS) {
				const auto& cls = static_cast<const ClassStmt&>(*stmt);
				if (cls.def && !cls.def->isInterface && !cls.def->isEnum)
					m_declared.push_back(
						Declared{cls.def, prefix + cls.def->name, prefix, mod, cls.line});
			} else if (stmt->kind == StmtKind::NAMESPACE) {
				const auto& ns = static_cast<const NamespaceStmt&>(*stmt);
				Collect(mod, ns.body, prefix + ns.name + String("."), depth);
			} else if (stmt->kind == StmtKind::LET) {
				const auto& let = static_cast<const LetStmt&>(*stmt);
				if (!let.initializer || let.initializer->kind != ExprKind::IMPORT)
					continue;
				const auto& imported = static_cast<const ImportExpr&>(*let.initializer);
				if (!imported.specifier || imported.specifier->kind != ExprKind::STRING_LITERAL)
					continue;
				const String specifier = static_cast<const StringExpr&>(*imported.specifier).value;
				if (mod == 0)
					m_imports.push_back(specifier);
				Option<size_t> loaded = LoadModule(specifier, depth + 1);
				if (loaded.IsSome())
					m_modules[mod]->aliases.emplace_back(prefix + let.name, loaded.Unwrap());
			}
		}
	}

	Option<size_t> LoadModule(const String& specifier, int depth) {
		for (size_t i = 1; i < m_modules.size(); ++i)
			if (m_modules[i]->specifier == specifier)
				return Some(i);
		if (depth > MAX_IMPORT_DEPTH || !m_sources)
			return NONE;
		Option<String> source = m_sources(specifier);
		if (source.IsNone())
			return NONE;
		auto program = Parser::Compile(StringView(source.Value().CStr(), source.Value().GetSize()));
		if (program.IsError())
			return NONE; // le module se signalera lui-même ; ici, ses classes restent inconnues
		const size_t index = m_modules.size();
		// Par pointeur : `m_modules` peut grandir pendant la collecte (imports
		// imbriqués) sans déplacer le programme en cours de parcours.
		m_modules.push_back(
			std::make_unique<Module>(Module{specifier, std::move(program).Unwrap(), {}}));
		Collect(index, m_modules[index]->program.statements, String(), depth);
		return Some(index);
	}

	/// Classe nommée `path` vue depuis le module `mod` (préfixe d'espace
	/// de noms courant `prefix`), ou -1.
	long FindClass(size_t mod, const String& prefix, const String& path) const {
		auto local = [&](const String& name) -> long {
			for (size_t i = 0; i < m_declared.size(); ++i)
				if (m_declared[i].mod == mod && m_declared[i].name == name)
					return long(i);
			return -1;
		};
		if (!prefix.IsEmpty())
			if (long found = local(prefix + path); found >= 0)
				return found;
		if (long found = local(path); found >= 0)
			return found;
		// `alias.Classe` : une classe d'un module importé.
		for (const auto& [alias, target] : m_modules[mod]->aliases) {
			const String head = alias + String(".");
			if (!path.StartsWith(head.View()))
				continue;
			const String rest = path.Substr(head.GetSize());
			for (size_t i = 0; i < m_declared.size(); ++i)
				if (m_declared[i].mod == target && m_declared[i].name == rest)
					return long(i);
		}
		return -1;
	}

	void ResolveOne(size_t i) {
		if (m_state[i] != 0)
			return; // 1 : en cours (cycle d'héritage, refusé à l'exécution), 2 : fait
		m_state[i] = 1;
		if (m_parent.size() < m_declared.size())
			m_parent.assign(m_declared.size(), -1);
		const Declared& d = m_declared[i];
		ScriptClassInfo info;
		info.name = d.name;
		info.module = m_modules[d.mod]->specifier;
		info.line = d.line;
		info.isAbstract = d.def->isAbstract;
		for (const MethodDef& method : d.def->methods) {
			const String& name = method.def ? method.def->name : String();
			if (!method.isStatic && (name == "init" || name == "deinit" || name.StartsWith("on_")))
				info.hooks.push_back(name);
		}
		std::vector<String> own;
		for (const TypePath& path : d.def->bases) {
			const String joined = JoinTypePath(path);
			// Une classe locale masque une base du moteur du même nom.
			const long cls = FindClass(d.mod, d.prefix, joined);
			if (cls >= 0 && size_t(cls) != i) {
				ResolveOne(size_t(cls));
				m_parent[i] = cls;
				info.parent = m_declared[size_t(cls)].name;
				continue;
			}
			if (Option<String> base = engine_base::ShortName(joined); base.IsSome()) {
				own.push_back(base.Unwrap());
				continue;
			}
			info.unresolved.push_back(joined);
		}
		if (m_parent[i] >= 0)
			info.engineBases = m_infos[size_t(m_parent[i])].engineBases;
		for (const String& base : own)
			if (std::find(info.engineBases.begin(), info.engineBases.end(), base) ==
				info.engineBases.end())
				info.engineBases.push_back(base);
		m_infos[i] = std::move(info);
		m_state[i] = 2;
	}

	const ModuleSource& m_sources;
	std::vector<std::unique_ptr<Module>> m_modules;
	std::vector<Declared> m_declared;
	std::vector<String> m_imports;
	std::vector<ScriptClassInfo> m_infos;
	std::vector<int> m_state;
	std::vector<long> m_parent;
};

/// Feuilles dérivées de `base` : ni abstraites, ni parentes d'une autre
/// candidate (règle de Interpreter::ClassesDerivedFrom).
std::vector<size_t> Leaves(const std::vector<ScriptClassInfo>& classes,
						   const std::vector<long>& parents, const char* base) {
	std::vector<size_t> candidates;
	for (size_t i = 0; i < classes.size(); ++i)
		if (!classes[i].isAbstract && classes[i].Derives(base))
			candidates.push_back(i);
	std::vector<size_t> leaves;
	for (size_t c : candidates) {
		bool isParent = false;
		for (size_t other : candidates)
			for (long up = parents[other]; up >= 0 && !isParent; up = parents[size_t(up)])
				isParent = size_t(up) == c;
		if (!isParent)
			leaves.push_back(c);
	}
	return leaves;
}

String JoinNames(const std::vector<ScriptClassInfo>& classes, const std::vector<size_t>& indices) {
	String out;
	for (size_t i : indices) {
		out.Concat(out.IsEmpty() ? "" : ", ");
		out.Concat(classes[i].name);
	}
	return out;
}

} // namespace

// ── ScriptClassInfo / ScriptOutline ──────────────────────────────────────────

bool ScriptClassInfo::Derives(const char* shortBase) const {
	for (const String& base : engineBases)
		if (base == shortBase)
			return true;
	return false;
}

String ScriptClassInfo::Signature() const {
	String bases = parent;
	for (const String& base : engineBases) {
		bases.Concat(bases.IsEmpty() ? "" : ", ");
		bases.Concat(base);
	}
	return bases.IsEmpty() ? name : String::Format("%s (%s)", name.CStr(), bases.CStr());
}

const ScriptClassInfo* ScriptOutline::Find(const String& name) const {
	for (const ScriptClassInfo& info : classes)
		if (info.name == name)
			return &info;
	return nullptr;
}

const char* ScriptRoleName(ScriptRole role) noexcept {
	switch (role) {
	case ScriptRole::EMPTY:
		return "vide";
	case ScriptRole::SCENE:
		return "scène";
	case ScriptRole::BEHAVIOUR:
		return "comportement";
	case ScriptRole::MODULE:
		return "module";
	case ScriptRole::INVALID:
		return "invalide";
	}
	return "?";
}

String ScriptOutline::Summary() const {
	if (error.IsSome())
		return String::Format("erreur ligne %d : %s", error.Value().line,
							  error.Value().message.CStr());
	if (!problem.IsEmpty())
		return problem;
	size_t own = 0;
	for (const ScriptClassInfo& info : classes)
		own += info.module.IsEmpty() ? 1 : 0;
	if (role == ScriptRole::SCENE || role == ScriptRole::BEHAVIOUR) {
		const ScriptClassInfo* main = Find(mainClass);
		String text = String::Format("%s %s", ScriptRoleName(role),
									 main ? main->Signature().CStr() : mainClass.CStr());
		if (main && main->module.IsEmpty() && own > 1)
			text.Concat(String::Format(" + %d autre%s classe%s", int(own - 1), own > 2 ? "s" : "",
									   own > 2 ? "s" : ""));
		return text;
	}
	if (role == ScriptRole::MODULE) {
		size_t behaviours = 0;
		for (const ScriptClassInfo& info : classes)
			behaviours +=
				info.module.IsEmpty() && !info.isAbstract && info.Derives("Behaviour") ? 1 : 0;
		String text = String::Format("module : %d classe%s", int(own), own > 1 ? "s" : "");
		if (behaviours > 0)
			text.Concat(String::Format(" dont %d Behaviour", int(behaviours)));
		return text;
	}
	return String("vide");
}

ScriptOutline OutlineScript(const String& source, ScriptUse use, const ModuleSource& modules) {
	ScriptOutline outline;
	if (source.Trim().IsEmpty())
		return outline;
	Analyzer analyzer(modules);
	if (auto error = analyzer.Load(source); error.IsSome()) {
		outline.error = error;
		outline.role = ScriptRole::INVALID;
		return outline;
	}
	outline.classes = analyzer.Resolve();
	outline.imports = analyzer.Imports();
	const std::vector<long>& parents = analyzer.Parents();

	const std::vector<size_t> scenes = Leaves(outline.classes, parents, "Scene");
	const std::vector<size_t> behaviours = Leaves(outline.classes, parents, "Behaviour");
	if (use == ScriptUse::SCENE) {
		if (scenes.size() == 1) {
			outline.role = ScriptRole::SCENE;
			outline.mainClass = outline.classes[scenes.front()].name;
		} else {
			outline.role = ScriptRole::INVALID;
			outline.problem =
				scenes.empty()
					? String("aucune classe dérivée de Scene : le moteur n'aura rien à instancier "
							 "(class MaScène extends Scene { … })")
					: String::Format(
						  "plusieurs classes dérivées de Scene (%s) : une seule est instanciée "
						  "— rendez les autres `abstract`",
						  JoinNames(outline.classes, scenes).CStr());
		}
		return outline;
	}
	if (behaviours.size() == 1) {
		outline.role = ScriptRole::BEHAVIOUR;
		outline.mainClass = outline.classes[behaviours.front()].name;
		return outline;
	}
	outline.role = ScriptRole::MODULE;
	outline.attachProblem =
		behaviours.empty()
			? String("aucune classe dérivée de Behaviour : rien ne s'attachera au nœud")
			: String::Format(
				  "plusieurs classes dérivées de Behaviour (%s) : le moteur n'en attache aucune — "
				  "rendez les autres `abstract`, ou importez ce module depuis un script qui n'en "
				  "définit qu'une",
				  JoinNames(outline.classes, behaviours).CStr());
	return outline;
}

} // namespace game_editor
