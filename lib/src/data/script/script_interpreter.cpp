// Définitions de data/script/script_interpreter.hpp
#include "data/script/script_interpreter.hpp"
#include <filesystem>
#include <fstream>
#include <iterator>

namespace
{

	/// Lit un fichier de module : chemin tel quel, puis (sans extension)
	/// `<chemin>.script`. Rend (chemin effectif, contenu).
	Option<std::pair<std::filesystem::path, std::string>> ReadModuleFile(const std::filesystem::path &path)
	{
		auto tryFile = [](const std::filesystem::path &p)
			-> Option<std::pair<std::filesystem::path, std::string>>
		{
			std::error_code ec;
			if (!std::filesystem::is_regular_file(p, ec) || ec)
				return NONE;
			std::ifstream in(p, std::ios::binary);
			if (!in)
				return NONE;
			std::string text((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
			return Some(std::make_pair(p, std::move(text)));
		};
		if (auto got = tryFile(path); got.IsSome())
			return got;
		if (!path.has_extension())
		{
			std::filesystem::path p = path;
			p += ".script";
			if (auto got = tryFile(p); got.IsSome())
				return got;
		}
		return NONE;
	}

	/// Clé de cache : chemin canonique (symlinks résolus, séparateurs unifiés).
	String CanonicalModuleKey(const std::filesystem::path &path)
	{
		std::error_code ec;
		const auto canonical = std::filesystem::weakly_canonical(path, ec);
		return String((ec ? path : canonical).generic_string().c_str());
	}

} // namespace

namespace data::script
{

	// ── Environment ──────────────────────────────────────────────────────────────

	void Environment::Define(const String &name, Value value)
	{
		Put(name, BindingKind::VAR, std::move(value));
	}

	void Environment::Declare(const String &name, BindingKind kind, bool initialized)
	{
		std::lock_guard<std::mutex> lock(m_mutex);
		if (Binding *binding = FindLocked(name))
		{
			binding->kind = kind;
			binding->initialized = initialized;
			return;
		}
		m_bindings.push_back(Binding{name, Value::Nil(), kind, initialized, false, nullptr});
	}

	void Environment::Put(const String &name, BindingKind kind, Value value, bool host, TypeRef type)
	{
		std::lock_guard<std::mutex> lock(m_mutex);
		Binding *binding = FindLocked(name);
		if (!binding)
		{
			m_bindings.push_back(Binding{name, Value::Nil(), kind, true, false, nullptr});
			binding = &m_bindings.back();
		}
		binding->kind = kind;
		binding->value = std::move(value);
		binding->initialized = true;
		binding->host = binding->host || host;
		binding->type = std::move(type);
	}

	Option<BindingInfo> Environment::Local(const String &name) const
	{
		std::lock_guard<std::mutex> lock(m_mutex);
		for (const Binding &binding : m_bindings)
			if (binding.name == name)
				return Some(BindingInfo{binding.value, binding.kind, binding.initialized, binding.host, binding.type});
		return NONE;
	}

	Option<BindingInfo> Environment::Lookup(const String &name) const
	{
		for (const Environment *env = this; env; env = env->m_parent.get())
			if (Option<BindingInfo> binding = env->Local(name); binding.IsSome())
				return binding;
		return NONE;
	}

	Option<Value> Environment::Find(const String &name) const
	{
		Option<BindingInfo> binding = Lookup(name);
		if (binding.IsNone())
			return NONE;
		return Some(std::move(binding).Unwrap().value);
	}

	Environment::AssignResult Environment::Assign(const String &name, Value value)
	{
		return Write(name, nullptr, std::move(value));
	}

	Environment::AssignResult Environment::CompareAndAssign(const String &name, const Value &expected, Value value)
	{
		return Write(name, &expected, std::move(value));
	}

	bool Environment::IsFunctionScope() const noexcept
	{
		return m_functionScope;
	}

	Environment *Environment::FunctionScope() noexcept
	{
		Environment *env = this;
		while (env && !env->m_functionScope && env->m_parent)
			env = env->m_parent.get();
		return env;
	}

	std::vector<Binding> Environment::Bindings() const
	{
		std::lock_guard<std::mutex> lock(m_mutex);
		return m_bindings;
	}

	Option<BindingKind> Environment::DeclareVar(const String &name, Option<Value> value, TypeRef type)
	{
		std::lock_guard<std::mutex> lock(m_mutex);
		Binding *binding = FindLocked(name);
		if (!binding)
		{
			m_bindings.push_back(Binding{name, Value::Nil(), BindingKind::VAR, true, false, nullptr});
			binding = &m_bindings.back();
		}
		if (binding->kind == BindingKind::LET || binding->kind == BindingKind::CONST)
			return Some(binding->kind);
		if (value.IsSome())
			binding->value = std::move(value).Unwrap();
		if (type)
			binding->type = std::move(type);
		binding->initialized = true;
		return NONE;
	}

	bool Environment::InitializeLexical(const String &name, BindingKind kind, Value value, TypeRef type)
	{
		std::lock_guard<std::mutex> lock(m_mutex);
		Binding *binding = FindLocked(name);
		if (binding && binding->initialized && binding->kind != BindingKind::VAR && !binding->host)
			return false;
		if (!binding)
		{
			m_bindings.push_back(Binding{name, Value::Nil(), kind, false, false, nullptr});
			binding = &m_bindings.back();
		}
		binding->kind = kind;
		binding->value = std::move(value);
		binding->initialized = true;
		binding->type = std::move(type);
		return true;
	}

	std::vector<Binding> Environment::ExtractScriptBindings()
	{
		std::lock_guard<std::mutex> lock(m_mutex);
		std::vector<Binding> released;
		for (size_t i = 0; i < m_bindings.size();)
		{
			if (m_bindings[i].host)
			{
				++i;
				continue;
			}
			released.push_back(std::move(m_bindings[i]));
			m_bindings.erase(m_bindings.begin() + ptrdiff_t(i));
		}
		return released;
	}

	void Environment::ClearBindings() noexcept
	{
		std::vector<Binding> released;
		{
			std::lock_guard<std::mutex> lock(m_mutex);
			released.swap(m_bindings);
		}
		// Détruites HORS du verrou : libérer une valeur peut libérer d'autres
		// portées, dont on prendrait alors le verrou.
	}

	Binding *Environment::FindLocked(const String &name) noexcept
	{
		for (Binding &binding : m_bindings)
			if (binding.name == name)
				return &binding;
		return nullptr;
	}

	Environment::AssignResult Environment::Write(const String &name, const Value *expected, Value value)
	{
		for (Environment *env = this; env; env = env->m_parent.get())
		{
			std::lock_guard<std::mutex> lock(env->m_mutex);
			Binding *binding = env->FindLocked(name);
			if (!binding)
				continue;
			if (!binding->initialized)
				return AssignResult::UNINITIALIZED;
			if (binding->kind == BindingKind::CONST)
				return AssignResult::CONSTANT;
			if (expected && !binding->value.Equals(*expected))
				return AssignResult::CHANGED;
			binding->value = std::move(value);
			return AssignResult::OK;
		}
		return AssignResult::UNKNOWN;
	}

	// ── Interpreter ──────────────────────────────────────────────────────────────

	Interpreter::Interpreter() : m_globals(std::make_shared<Environment>(nullptr, true)), m_mainThread(std::this_thread::get_id()),
								 m_alive(std::make_shared<std::atomic<bool>>(true))
	{
		m_environments.push_back(m_globals);
		m_installingStandardLibrary = true;
		InstallStdLibrary(*this);
		InstallMathLibrary(*this);
		m_installingStandardLibrary = false;
	}

	Interpreter::~Interpreter()
	{
		StopAsyncTasks();
		// Les ressources des bases de l'hôte partent pendant que
		// l'interpréteur fonctionne encore : leur `on_destroy`/`deinit` de
		// script s'exécutent normalement (cf. script_owners.hpp).
		m_owners.DestroyAll(*this);
		FinalizeAtExit();
		m_alive->store(false);
		BreakEnvironmentCycles();
	}

	const std::vector<String> &Interpreter::Output() const noexcept
	{
		return m_output;
	}

	void Interpreter::ClearOutput()
	{
		std::lock_guard<std::mutex> lock(m_outputMutex);
		m_output.clear();
	}

	void Interpreter::SetRandomSeed(uint64_t seed) noexcept
	{
		std::lock_guard<std::mutex> lock(m_randomMutex);
		m_randomState = seed ? seed : 0x9E3779B97F4A7C15ull;
	}

	void Interpreter::SetGlobal(const String &name, Value value)
	{
		m_globals->Put(name, BindingKind::VAR, std::move(value), true);
	}

	Option<Value> Interpreter::GetGlobal(const String &name) const
	{
		return m_globals->Find(name);
	}

	bool Interpreter::HasGlobal(const String &name) const
	{
		return GetGlobal(name).IsSome();
	}

	void Interpreter::RegisterNative(const String &name, int minArity, int maxArity, NativeFn fn, NativeThread thread)
	{
		SetGlobal(name, Value::Native(MakeNative(name, minArity, maxArity, std::move(fn), thread)));
	}

	std::shared_ptr<NamespaceObject> Interpreter::HostNamespace(const String &name)
	{
		std::shared_ptr<Environment> parent = m_globals;
		std::shared_ptr<NamespaceObject> ns;
		String qualified;
		for (const String &part : name.Split(StringView(".")))
		{
			qualified = qualified.IsEmpty() ? part : qualified + String(".") + part;
			if (Option<BindingInfo> existing = parent->Local(part);
				existing.IsSome() && existing.Value().value.IsNamespace())
			{
				ns = existing.Value().value.AsNamespace();
			}
			else
			{
				ns = std::make_shared<NamespaceObject>();
				ns->name = qualified;
				ns->scope = MakeEnvironment(nullptr, true);
				parent->Put(part, BindingKind::CONST, Value::Namespace(ns), true);
			}
			parent = ns->scope;
		}
		return ns;
	}

	std::shared_ptr<const HostType> Interpreter::RegisterHostType(const String &ns, const String &name,
																  std::shared_ptr<HostType> type)
	{
		const String qualified = String::Format("%s.%s", ns.CStr(), name.CStr());
		if (type->name.IsEmpty())
			type->name = qualified;
		std::shared_ptr<const HostType> shared = type;
		auto native = MakeNative(
			qualified, type->minArity, type->maxArity,
			[shared](Interpreter &vm, std::vector<Value> &args) -> Result<Value, ScriptError>
			{
				if (!shared->construct)
					return Err(MakeError(String::Format("`%s` ne se construit pas directement", shared->name.CStr())));
				return shared->construct(vm, args, {});
			},
			NativeThread::ANY);
		native->hostType = shared;
		RegisterNamespaceConstant(ns, name, Value::Native(std::move(native)));
		return shared;
	}

	void Interpreter::RegisterNamespaceConstant(const String &ns, const String &name, Value value)
	{
		HostNamespace(ns)->scope->Put(name, BindingKind::CONST, std::move(value), true);
	}

	void Interpreter::RegisterNamespacedNative(const String &ns, const String &name, int minArity, int maxArity, NativeFn fn,
											   NativeThread thread)
	{
		RegisterNamespaceConstant(ns, name,
								  Value::Native(MakeNative(String::Format("%s.%s", ns.CStr(), name.CStr()), minArity,
														   maxArity, std::move(fn), thread)));
	}

	void Interpreter::ExportGlobalsTo(const String &ns)
	{
		std::shared_ptr<NamespaceObject> space = HostNamespace(ns);
		for (const Binding &binding : m_globals->Bindings())
		{
			if (!binding.host || !binding.value.IsNative() || space->scope->Local(binding.name).IsSome())
				continue;
			space->scope->Put(binding.name, BindingKind::CONST, binding.value, true);
		}
	}

	Option<Value> Interpreter::GetNamespaceMember(const String &ns, const String &member) const
	{
		Option<Value> space = GetGlobal(ns);
		if (space.IsNone() || !space.Unwrap().IsNamespace() || !space.Unwrap().AsNamespace())
			return NONE;
		Option<BindingInfo> binding = space.Unwrap().AsNamespace()->scope->Local(member);
		if (binding.IsNone() || !binding.Value().initialized)
			return NONE;
		return Some(binding.Value().value);
	}

	Result<Value, ScriptError> Interpreter::Run(StringView source)
	{
		auto program = Parser::Compile(source);
		if (program.IsError())
			return Err(program.Error());
		m_program = std::make_shared<Program>(std::move(program).Unwrap());
		return RunProgram(*m_program);
	}

	Result<Value, ScriptError> Interpreter::RunProgram(const Program &program)
	{
		Ctx().steps = 0;
		if (auto hoisted = Hoist(program.statements, m_globals, true, nullptr); hoisted.IsSome())
			return Err(hoisted.Unwrap());
		for (const StmtPtr &stmt : program.statements)
		{
			if (!stmt)
				continue;
			auto outcome = ExecStmt(*stmt, m_globals);
			if (outcome.IsError())
				return Err(outcome.Error());
			if (outcome.Value().flow == Flow::RETURN)
			{
				Value result = outcome.Value().value;
				outcome = Ok(ExecOutcome{});
				RunPendingFinalizers();
				return Ok(std::move(result));
			}
		}
		RunPendingFinalizers();
		return Ok(Value::Nil());
	}

	Option<Result<Value, ScriptError>> Interpreter::CallGlobalIfPresent(const String &name, std::vector<Value> args)
	{
		Option<Value> slot = m_globals->Find(name);
		if (slot.IsNone() || !slot.Value().IsCallable())
			return NONE;
		Ctx().steps = 0;
		auto result = CallValue(slot.Value(), std::move(args), 0, 0);
		RunPendingFinalizers();
		return Some(std::move(result));
	}

	Option<Result<Value, ScriptError>> Interpreter::CallMethodIfPresent(const Value &object, const String &name,
																		std::vector<Value> args)
	{
		if (!object.IsInstance() || !object.AsInstance())
			return NONE;
		auto [method, definer] = FindMethod(object.AsInstance()->klass, name, false);
		if (!method || method->isAbstract)
			return NONE;
		Ctx().steps = 0;
		auto result = CallValue(BindMethod(object, definer, method->def), std::move(args), 0, 0);
		RunPendingFinalizers();
		return Some(std::move(result));
	}

	std::vector<std::shared_ptr<ClassObject>> Interpreter::ClassesDerivedFrom(const String &qualifiedName) const
	{
		std::vector<std::shared_ptr<ClassObject>> matches;
		{
			std::lock_guard<std::mutex> lock(m_environmentsMutex);
			for (const auto &weak : m_classes)
			{
				std::shared_ptr<ClassObject> klass = weak.lock();
				if (!klass || klass->IsAbstract() || klass->IsInterface())
					continue;
				for (const auto &base : klass->owners)
					if (base && base->name == qualifiedName)
					{
						matches.push_back(std::move(klass));
						break;
					}
			}
		}
		std::vector<std::shared_ptr<ClassObject>> leaves;
		for (const auto &candidate : matches)
		{
			bool isParent = false;
			for (const auto &other : matches)
				for (auto up = other->superclass; up && !isParent; up = up->superclass)
					isParent = up == candidate;
			if (!isParent)
				leaves.push_back(candidate);
		}
		return leaves;
	}

	Result<Value, ScriptError> Interpreter::CallValue(const Value &callee, std::vector<Value> args, int line, int column)
	{
		// Appeler une classe l'instancie (façon Python/Dart ; `new` est facultatif).
		if (callee.IsClass())
			return Instantiate(callee.AsClass(), std::move(args), line, column);
		// Objet appelable : `operator ()` d'une classe, crochet d'un type de l'hôte.
		if (callee.IsInstance() && callee.AsInstance())
			if (Option<Value> method = InstanceOperator(callee, "()"); method.IsSome())
				return CallValue(method.Value(), std::move(args), line, column);
		if (callee.IsHost() && callee.AsHost() && callee.AsHost()->type->call)
			return Positioned(callee.AsHost()->type->call(*this, callee.AsHost(), args), line, column);
		if (callee.IsNative())
		{
			const std::shared_ptr<NativeObject> &native = callee.AsNative();
			if (!native || !native->fn)
				return Err(ScriptError(String("fonction native invalide"), line, column));
			int argc = static_cast<int>(args.size());
			if (argc < native->minArity || (native->maxArity >= 0 && argc > native->maxArity))
				return Err(ScriptError(String::Format("`%s` attend %s argument(s), %d fourni(s)", native->name.CStr(),
													  ArityText(*native).CStr(), argc),
									   line, column));
			if (!native->anyThread)
			{
				// L'API de l'hôte n'est pas sûre entre fils : depuis un fil
				// `async`, l'appel part sur le fil principal ; et ses
				// arguments sont des COPIES dès que des fils tournent (un
				// autre fil ne les modifiera pas sous ses pieds).
				if (!IsMainThread())
					return CallOnMainThread(native, CopyForHost(args), line, column);
				if (m_concurrent.load())
					args = CopyForHost(args);
			}
			return native->fn(*this, args);
		}
		if (!callee.IsFunction())
			return Err(ScriptError(String::Format("valeur de type `%s` non appelable", callee.TypeName()), line, column));

		const std::shared_ptr<FunctionObject> &fn = callee.AsFunction();
		if (!fn || !fn->def)
			return Err(ScriptError(String("fonction invalide"), line, column));
		if (args.size() != fn->def->params.size())
			return Err(ScriptError(String::Format("`%s` attend %d argument(s), %d fourni(s)",
												  fn->def->name.IsEmpty() ? "fonction anonyme" : fn->def->name.CStr(),
												  int(fn->def->params.size()), int(args.size())),
								   line, column));
		// `async fn` : son propre fil, et un Future rendu tout de suite.
		if (fn->def->isAsync)
		{
			std::shared_ptr<FunctionObject> target = fn;
			return SpawnTask([this, target, args = std::move(args), line, column]() mutable
							 { return CallFunctionBody(target, std::move(args), line, column); });
		}
		return CallFunctionBody(fn, std::move(args), line, column);
	}

	void Interpreter::ReportError(const ScriptError &error)
	{
		ReportDetachedError(error);
	}

	std::weak_ptr<std::atomic<bool>> Interpreter::AliveToken() const
	{
		return m_alive;
	}

	Result<Value, ScriptError> Interpreter::ConformValue(const Value &typeValue, Value value)
	{
		return Conform(typeValue, std::move(value));
	}

	bool Interpreter::IsCallableValue(const Value &value)
	{
		if (value.IsCallable() || value.IsClass())
			return true;
		if (value.IsInstance())
			return InstanceOperator(value, "()").IsSome();
		return value.IsHost() && value.AsHost() && value.AsHost()->type->call != nullptr;
	}

	Result<Value, ScriptError> Interpreter::FlowInto(const Value &sink, const Value &value, int line, int column)
	{
		if (Option<Value> method = InstanceOperator(sink, "<-"); method.IsSome())
			return CallValue(method.Value(), {value}, line, column);
		if (Option<Value> method = StaticOperator(sink, "<-"); method.IsSome())
			return CallValue(method.Value(), {sink, value}, line, column);
		if (sink.IsHost() && sink.AsHost() && sink.AsHost()->type->flowIn)
			return Positioned(sink.AsHost()->type->flowIn(*this, sink.AsHost(), value), line, column);
		if (sink.IsList() && sink.AsList())
		{
			sink.AsList()->Push(value);
			return Ok(sink);
		}
		if (IsCallableValue(sink))
			return CallValue(sink, {value}, line, column);
		if (sink.IsNumber() && value.IsNumber())
			return Err(ScriptError(String("`<-` / `->` entre deux nombres : pour comparer à un nombre négatif, écrivez "
										  "`x < -1` (avec une espace)"),
								   line, column));
		return Err(ScriptError(String::Format("`->` : une valeur `%s` ne reçoit pas de flux (attendu une fonction, un "
											  "pipe, un flux, une liste, un conteneur ou un objet doté d'un `operator "
											  "<-`)",
											  sink.TypeName()),
							   line, column));
	}

	Result<String, ScriptError> Interpreter::Stringify(const Value &value)
	{
		if (Option<Value> method = InstanceOperator(value, " str"); method.IsSome())
		{
			auto text = CallValue(method.Value(), {}, 0, 0);
			if (text.IsError())
				return Err(text.Error());
			return Ok(text.Value().ToDisplayString());
		}
		return Ok(value.ToDisplayString());
	}

	Result<int, ScriptError> Interpreter::CompareValues(const Value &a, const Value &b)
	{
		if (a.IsNumber() && b.IsNumber())
		{
			const int order = numeric::Compare(a, b);
			return Ok(order == 2 ? 0 : order);
		}
		if (a.IsString() && b.IsString())
		{
			const int order = std::strcmp(a.AsString().CStr(), b.AsString().CStr());
			return Ok(order < 0 ? -1 : (order > 0 ? 1 : 0));
		}
		if (Option<int> order = EnumOrder(a, b); order.IsSome())
			return Ok(order.Unwrap());
		for (int pass = 0; pass < 2; ++pass)
		{
			const Value &left = pass == 0 ? a : b;
			const Value &right = pass == 0 ? b : a;
			auto less = TryBinaryOverload("<", BinaryOp::LESS, left, right, 0, 0);
			if (less.IsNone())
				break;
			auto result = std::move(less).Unwrap();
			if (result.IsError())
				return Err(result.Error());
			if (result.Value().IsTruthy())
				return Ok(pass == 0 ? -1 : 1);
			if (pass == 1)
				return Ok(0);
		}
		return Err(MakeError(
			String::Format("valeurs incomparables : `%s` et `%s` (définissez `operator <`)", a.TypeName(), b.TypeName())));
	}

	Result<bool, ScriptError> Interpreter::ValuesEqual(const Value &a, const Value &b)
	{
		if (a.IsInstance() || b.IsInstance() || a.IsHost() || b.IsHost())
		{
			auto equal = TryBinaryOverload("==", BinaryOp::EQUAL, a, b, 0, 0);
			if (equal.IsSome())
			{
				auto result = std::move(equal).Unwrap();
				if (result.IsError())
					return Err(result.Error());
				return Ok(result.Value().IsTruthy());
			}
		}
		return Ok(a.Equals(b));
	}

	Result<std::vector<Value>, ScriptError> Interpreter::Iterate(const Value &source, int line, int column, int depth)
	{
		std::vector<Value> sequence;
		if (source.IsList() && source.AsList())
		{
			sequence = source.AsList()->Snapshot();
		}
		else if (source.IsMap() && source.AsMap())
		{
			for (const auto &entry : source.AsMap()->Snapshot())
				sequence.push_back(Value::Str(entry.first));
		}
		else if (source.IsString())
		{
			const String &text = source.AsString();
			for (size_t i = 0; i < text.GetSize(); ++i)
				sequence.push_back(Value::Str(String(text.CStr() + i, 1)));
		}
		else if (source.IsHost() && source.AsHost() && source.AsHost()->type->items)
		{
			sequence = source.AsHost()->type->items(*source.AsHost());
		}
		else if (source.IsClass() && source.AsClass() && source.AsClass()->def->isEnum)
		{
			sequence = source.AsClass()->enumValues;
		}
		else if (Option<Value> method = InstanceOperator(source, " iter"); method.IsSome() && depth < 4)
		{
			auto produced = CallValue(method.Value(), {}, line, column);
			if (produced.IsError())
				return Err(produced.Error());
			return Iterate(produced.Value(), line, column, depth + 1);
		}
		else
		{
			return Err(ScriptError(String::Format("`for ... in` attend une liste, une table, une chaîne, un énuméré, un "
												  "conteneur ou un objet doté d'un `operator iter`, trouvé `%s`",
												  source.TypeName()),
								   line, column));
		}
		return Ok(std::move(sequence));
	}

	Result<Value, ScriptError> Interpreter::AwaitValue(const Value &value, int line, int column)
	{
		if (!value.IsFuture() || !value.AsFuture())
			return Ok(value);
		const std::shared_ptr<FutureObject> &future = value.AsFuture();
		const bool main = IsMainThread();
		std::unique_lock<std::mutex> lock(future->mutex);
		while (future->state == FutureObject::State::PENDING)
		{
			if (m_stopping.load())
				return Err(ScriptError(String("attente interrompue : l'interpréteur s'arrête"), line, column));
			if (main)
			{
				lock.unlock();
				PumpMainThread();
				lock.lock();
				if (future->state != FutureObject::State::PENDING)
					break;
			}
			future->ready.wait_for(lock, std::chrono::milliseconds(main ? 1 : 50));
		}
		future->observed = true;
		if (future->state == FutureObject::State::FAILED)
			return Err(future->error);
		return Ok(future->value);
	}

	void Interpreter::PumpMainThread()
	{
		if (!IsMainThread())
			return;
		RunPendingFinalizers();
		std::deque<std::shared_ptr<MainThreadCall>> calls;
		{
			std::lock_guard<std::mutex> lock(m_mainQueueMutex);
			calls.swap(m_mainQueue);
		}
		for (const std::shared_ptr<MainThreadCall> &call : calls)
		{
			Result<Value, ScriptError> result = call->native->fn(*this, call->args);
			{
				std::lock_guard<std::mutex> lock(call->mutex);
				// Champs simples plutôt qu'un Option<Result> : GCC -O3 y voit à
				// tort une lecture non initialisée (-Wmaybe-uninitialized).
				call->failed = result.IsError();
				if (call->failed)
					call->error = result.Error();
				else
					call->value = result.Value();
				call->done = true;
			}
			call->finished.notify_all();
		}

		std::vector<String> prints;
		{
			std::lock_guard<std::mutex> lock(m_outputMutex);
			prints.swap(m_pendingPrints);
		}
		for (const String &line : prints)
			Emit(line);

		ReportOrphanErrors();
		ReapFinishedTasks();
	}

	size_t Interpreter::RunningTasks()
	{
		ReapFinishedTasks();
		std::lock_guard<std::mutex> lock(m_tasksMutex);
		return m_tasks.size();
	}

	bool Interpreter::IsStopping() const noexcept
	{
		return m_stopping.load();
	}

	void Interpreter::SetMainThread() noexcept
	{
		m_mainThread = std::this_thread::get_id();
	}

	bool Interpreter::IsMainThread() const noexcept
	{
		return std::this_thread::get_id() == m_mainThread;
	}

	Result<Value, ScriptError> Interpreter::SpawnTask(std::function<Result<Value, ScriptError>()> body)
	{
		ReapFinishedTasks();
		auto future = std::make_shared<FutureObject>();
		std::lock_guard<std::mutex> lock(m_tasksMutex);
		if (m_stopping.load())
			return Err(MakeError(String("l'interpréteur s'arrête : plus de nouvelle tâche async")));
		if (m_tasks.size() >= maxAsyncTasks)
			return Err(MakeError(String::Format("trop de fonctions async simultanées (%d au plus)", int(maxAsyncTasks))));
		m_concurrent.store(true);
		auto finished = std::make_shared<std::atomic<bool>>(false);
		std::thread thread([this, body = std::move(body), future, finished]() mutable
						   {
	ExecContext context;
	t_owner = this;
	t_context = &context;
	Result<Value, ScriptError> result = body();
	RunPendingFinalizers();
	if (result.IsError())
		NoteFailure(future, result.Error());
	future->Complete(std::move(result));
	t_owner = nullptr;
	t_context = nullptr;
	finished->store(true); });
		m_tasks.push_back(AsyncTask{std::move(thread), std::move(finished)});
		return Ok(Value::Future(std::move(future)));
	}

	ScriptError Interpreter::MakeError(String message)
	{
		return ScriptError(std::move(message), 0, 0);
	}

	void Interpreter::Emit(const String &text)
	{
		if (IsMainThread() && onPrint)
		{
			onPrint(text);
			return;
		}
		std::lock_guard<std::mutex> lock(m_outputMutex);
		if (onPrint)
			m_pendingPrints.push_back(text);
		else
			m_output.push_back(text);
	}

	double Interpreter::NextRandom() noexcept
	{
		std::lock_guard<std::mutex> lock(m_randomMutex);
		m_randomState ^= m_randomState >> 12;
		m_randomState ^= m_randomState << 25;
		m_randomState ^= m_randomState >> 27;
		uint64_t x = m_randomState * 0x2545F4914F6CDD1Dull;
		return double(x >> 11) / double(1ull << 53);
	}

	void Interpreter::DestroyInstance(const std::shared_ptr<InstanceObject> &instance)
	{
		if (!instance || instance->destroyed || !IsOwned(*instance))
			return;
		// Posé AVANT tout appel de script : un `destroy()` réentrant (depuis
		// `on_destroy`, ou une fin de scène pendant la destruction) ne fait rien.
		instance->destroyed = true;
		const Value self = Value::Instance(instance);

		// 1) `on_destroy` : le script libère ses PAIRES (références à d'autres
		//    ressources, abonnements) pendant que ses bases existent encore.
		if (auto [method, definer] = FindMethod(instance->klass, String("on_destroy"), false);
			method && !method->isAbstract)
		{
			auto result = CallValue(BindMethod(self, definer, method->def), {}, method->def->line, method->def->column);
			if (result.IsError())
			{
				ScriptError error = result.Error();
				error.message = String::Format("`%s.on_destroy` : %s", definer->Name().CStr(), error.message.CStr());
				ReportDetachedError(error);
			}
		}
		// 2) `deinit` de chaque classe, dérivée d'abord ; `finalized` empêche
		//    le destructeur de l'instance de le rappeler plus tard.
		if (!instance->finalized)
		{
			instance->finalized = true;
			if (instance->klass->hasDeinit)
				RunDeinit(instance);
		}
		// 3) Bases de l'hôte en ordre inverse, puis retrait du registre (qui
		//    relâche sa référence : l'instance redevient un objet ordinaire).
		DeinitOwners(*this, instance);
	}

	void Interpreter::AddImportPath(const String &directory)
	{
		std::lock_guard<std::mutex> lock(m_importMutex);
		m_importPaths.push_back(directory);
	}

	std::vector<String> Interpreter::ImportPaths() const
	{
		std::lock_guard<std::mutex> lock(m_importMutex);
		return m_importPaths;
	}

	void Interpreter::SetImportResolver(ImportResolver resolver)
	{
		std::lock_guard<std::mutex> lock(m_importMutex);
		m_importResolver = std::move(resolver);
	}

	void Interpreter::PopImportStack() noexcept
	{
		std::lock_guard<std::mutex> lock(m_importMutex);
		if (!m_importStack.empty())
			m_importStack.pop_back();
	}

	Option<std::pair<String, String>> Interpreter::LoadModuleSource(const String &specifier) const
	{
		ImportResolver resolver;
		{
			std::lock_guard<std::mutex> lock(m_importMutex);
			resolver = m_importResolver;
		}
		if (resolver)
			if (auto resolved = resolver(specifier); resolved.IsSome())
				return resolved;

		const std::filesystem::path direct(specifier.CStr());
		if (direct.is_absolute())
		{
			if (auto got = ReadModuleFile(direct); got.IsSome())
				return Some(std::make_pair(CanonicalModuleKey(got.Value().first),
										   String(got.Value().second.c_str(), got.Value().second.size())));
			return NONE;
		}

		std::vector<std::filesystem::path> dirs;
		{
			std::lock_guard<std::mutex> lock(m_importMutex);
			if (!m_importStack.empty())
				dirs.push_back(std::filesystem::path(m_importStack.back().CStr()).parent_path());
			for (const String &dir : m_importPaths)
				dirs.push_back(std::filesystem::path(dir.CStr()));
		}
		for (const auto &dir : dirs)
		{
			if (auto got = ReadModuleFile(dir / direct); got.IsSome())
				return Some(std::make_pair(CanonicalModuleKey(got.Value().first),
										   String(got.Value().second.c_str(), got.Value().second.size())));
		}
		return NONE;
	}

	Option<String> Interpreter::ResolveImport(const String &specifier) const
	{
		auto loaded = LoadModuleSource(specifier);
		if (loaded.IsNone())
			return NONE;
		return Some(std::move(loaded).Unwrap().first);
	}

	bool Interpreter::IsModuleLoaded(const String &specifier) const
	{
		auto resolved = ResolveImport(specifier);
		if (resolved.IsNone())
			return false;
		std::lock_guard<std::mutex> lock(m_importMutex);
		for (const auto &entry : m_importCache)
			if (entry.first == resolved.Value())
				return true;
		return false;
	}

	Result<Value, ScriptError> Interpreter::ImportModule(const String &specifier, int line, int column)
	{
		if (specifier.IsEmpty())
			return Err(ScriptError(String("`import` : spécificateur vide"), line, column));

		auto loaded = LoadModuleSource(specifier);
		if (loaded.IsNone())
			return Err(ScriptError(
				String::Format("module introuvable : `%s` (essayé : résolveur, chemin absolu, répertoire du script, "
							   "répertoires d'import ; extension implicite : .script)",
							   specifier.CStr()),
				line, column));
		const String key = loaded.Unwrap().first;
		const String source = loaded.Unwrap().second;

		// Cache + détection de cycle (sous un seul verrou).
		{
			std::lock_guard<std::mutex> lock(m_importMutex);
			for (const auto &entry : m_importCache)
				if (entry.first == key)
					return Ok(entry.second);
			for (const String &active : m_importStack)
				if (active == key)
					return Err(ScriptError(
						String::Format("import cyclique : `%s` est déjà en cours de chargement", specifier.CStr()),
						line, column));
			m_importStack.push_back(key);
		}
		struct PopGuard
		{
			Interpreter *self;
			~PopGuard() { self->PopImportStack(); }
		} pop{this};

		auto program = Parser::Compile(StringView(source.CStr(), source.GetSize()));
		if (program.IsError())
		{
			ScriptError error = program.Error();
			error.message = String::Format("module `%s` : %s", specifier.CStr(), error.message.CStr());
			return Err(std::move(error));
		}

		// Portée de FONCTION propre au module (les `var` y restent) ; parentée
		// à la portée globale, donc `std`, `math`… restent visibles.
		const auto scope = MakeEnvironment(m_globals, /*functionScope=*/true);
		if (auto hoisted = Hoist(program.Value().statements, scope, /*functionBody=*/true, nullptr);
			hoisted.IsSome())
		{
			ScriptError error = hoisted.Unwrap();
			error.message = String::Format("module `%s` : %s", specifier.CStr(), error.message.CStr());
			return Err(std::move(error));
		}

		Value result = Value::Nil();
		bool hasReturn = false;
		for (const StmtPtr &stmt : program.Value().statements)
		{
			if (!stmt)
				continue;
			auto outcome = ExecStmt(*stmt, scope);
			if (outcome.IsError())
			{
				ScriptError error = outcome.Error();
				error.message = String::Format("module `%s` : %s", specifier.CStr(), error.message.CStr());
				return Err(std::move(error));
			}
			if (outcome.Value().flow == Flow::RETURN)
			{
				result = outcome.Value().value;
				hasReturn = true;
				break;
			}
		}

		// Pas de `return` : les déclarations de premier niveau deviennent les
		// MEMBRES d'un espace de noms — `let m = import "x"` puis `m.foo`.
		if (!hasReturn)
		{
			auto space = std::make_shared<NamespaceObject>();
			space->name = specifier;
			space->scope = scope;
			result = Value::Namespace(std::move(space));
		}

		{
			std::lock_guard<std::mutex> lock(m_importMutex);
			m_importCache.emplace_back(key, result);
		}
		return Ok(result);
	}

	std::vector<Value> Interpreter::CopyForHost(const std::vector<Value> &values)
	{
		std::vector<Value> out;
		out.reserve(values.size());
		for (const Value &value : values)
			out.push_back(DeepCopy(value, 0));
		return out;
	}

	Interpreter::ExecContext &Interpreter::Ctx() noexcept
	{
		return t_owner == this && t_context ? *t_context : m_mainContext;
	}

	std::shared_ptr<NativeObject> Interpreter::MakeNative(const String &name, int minArity, int maxArity, NativeFn fn,
														  NativeThread thread) const
	{
		auto native = std::make_shared<NativeObject>();
		native->name = name;
		native->fn = std::move(fn);
		native->minArity = minArity;
		native->maxArity = maxArity;
		native->anyThread = thread == NativeThread::ANY || (thread == NativeThread::DEFAULT && m_installingStandardLibrary);
		return native;
	}

	Result<Value, ScriptError> Interpreter::CallOnMainThread(const std::shared_ptr<NativeObject> &native,
															 std::vector<Value> args, int line, int column)
	{
		auto call = std::make_shared<MainThreadCall>();
		call->native = native;
		call->args = std::move(args);
		{
			std::lock_guard<std::mutex> lock(m_mainQueueMutex);
			if (m_stopping.load())
				return Err(ScriptError(String("l'interpréteur s'arrête"), line, column));
			m_mainQueue.push_back(call);
		}
		std::unique_lock<std::mutex> lock(call->mutex);
		while (!call->done)
		{
			if (m_stopping.load())
				return Err(
					ScriptError(String::Format("`%s` : l'interpréteur s'arrête", native->name.CStr()), line, column));
			call->finished.wait_for(lock, std::chrono::milliseconds(20));
		}
		if (call->failed)
			return Err(call->error);
		return Ok(call->value);
	}

	Value Interpreter::DeepCopy(const Value &value, int depth)
	{
		if (depth > 64)
			return value;
		if (value.IsList() && value.AsList())
		{
			std::vector<Value> items = value.AsList()->Snapshot();
			for (Value &item : items)
				item = DeepCopy(item, depth + 1);
			return Value::List(std::make_shared<ListObject>(std::move(items)));
		}
		if (value.IsMap() && value.AsMap())
		{
			auto copy = std::make_shared<MapObject>();
			for (auto &entry : value.AsMap()->Snapshot())
				copy->entries.emplace_back(entry.first, DeepCopy(entry.second, depth + 1));
			return Value::Map(std::move(copy));
		}
		return value;
	}

	void Interpreter::NoteFailure(const std::shared_ptr<FutureObject> &future, const ScriptError &error)
	{
		std::lock_guard<std::mutex> lock(m_outputMutex);
		m_orphans.push_back(OrphanError{future, error, 0});
	}

	void Interpreter::ReportOrphanErrors()
	{
		std::vector<ScriptError> report;
		{
			std::lock_guard<std::mutex> lock(m_outputMutex);
			for (size_t i = 0; i < m_orphans.size();)
			{
				OrphanError &orphan = m_orphans[i];
				bool observed = false;
				if (auto future = orphan.future.lock())
				{
					std::lock_guard<std::mutex> futureLock(future->mutex);
					observed = future->observed;
				}
				if (observed)
				{
					m_orphans.erase(m_orphans.begin() + ptrdiff_t(i));
					continue;
				}
				if (orphan.future.expired() || ++orphan.age >= 2)
				{
					report.push_back(orphan.error);
					m_orphans.erase(m_orphans.begin() + ptrdiff_t(i));
					continue;
				}
				++i;
			}
		}
		for (const ScriptError &error : report)
		{
			if (onAsyncError)
				onAsyncError(error);
			else
				Emit(String::Format("erreur async non attendue : %s", error.Format().CStr()));
		}
	}

	void Interpreter::ReapFinishedTasks()
	{
		std::vector<std::thread> done;
		{
			std::lock_guard<std::mutex> lock(m_tasksMutex);
			for (size_t i = 0; i < m_tasks.size();)
			{
				if (m_tasks[i].finished->load())
				{
					done.push_back(std::move(m_tasks[i].thread));
					m_tasks.erase(m_tasks.begin() + ptrdiff_t(i));
				}
				else
				{
					++i;
				}
			}
		}
		for (std::thread &thread : done)
			if (thread.joinable())
				thread.join();
	}

	void Interpreter::StopAsyncTasks()
	{
		m_stopping.store(true);
		std::vector<AsyncTask> tasks;
		{
			std::lock_guard<std::mutex> lock(m_tasksMutex);
			tasks.swap(m_tasks);
		}
		for (AsyncTask &task : tasks)
			if (task.thread.joinable())
				task.thread.join();
		// Appels restés en file : leurs fils sont partis, rien à exécuter.
		std::lock_guard<std::mutex> lock(m_mainQueueMutex);
		m_mainQueue.clear();
	}

	Result<Value, ScriptError> Interpreter::CallFunctionBody(const std::shared_ptr<FunctionObject> &fn,
															 std::vector<Value> args, int line, int column)
	{
		ExecContext &context = Ctx();
		if (context.callDepth >= maxCallDepth)
			return Err(ScriptError(
				String::Format("profondeur d'appel maximale atteinte (%d) — récursion infinie ?", int(maxCallDepth)), line,
				column));

		const FunctionDef &def = *fn->def;
		const std::shared_ptr<Environment> &closure = fn->closure ? fn->closure : m_globals;
		auto frame = MakeEnvironment(closure, true);
		// Fonction générique : `T`… liés pour l'appel (donnés ou libres).
		for (size_t i = 0; i < def.typeParams.size(); ++i)
			frame->Put(def.typeParams[i].name, BindingKind::CONST,
					   i < fn->typeArgs.size() ? fn->typeArgs[i] : FreshTypeParam(def.typeParams[i], closure));
		const std::shared_ptr<Environment> &typeScope = def.typeParams.empty() ? closure : frame;
		for (size_t i = 0; i < def.params.size(); ++i)
		{
			const TypeRef &type = i < def.paramTypes.size() ? def.paramTypes[i] : TypeRef();
			if (auto bad = CheckTyped("le paramètre", def.params[i], args[i], type, typeScope, line, column);
				bad.IsSome())
			{
				ScriptError error = bad.Unwrap();
				error.message = String::Format(
					"appel de `%s` : %s", def.name.IsEmpty() ? "fonction anonyme" : def.name.CStr(), error.message.CStr());
				return Err(std::move(error));
			}
			frame->Put(def.params[i], BindingKind::VAR, args[i], false, type);
		}
		if (auto hoisted = Hoist(def.body, frame, true, fn->def.get()); hoisted.IsSome())
			return Err(hoisted.Unwrap());

		// Type du résultat : pour une `async fn … : future<T>`, c'est T.
		TypeRef resultType = def.returnType;
		if (resultType && def.isAsync && resultType->IsSimple("future"))
			resultType = resultType->args.empty() ? TypeRef() : resultType->args[0];
		auto checkResult = [&](Value result) -> Result<Value, ScriptError>
		{
			if (resultType)
			{
				if (Option<String> bad = TypeMismatch(result, *resultType, typeScope); bad.IsSome())
					return Err(ScriptError(String::Format("`%s` doit rendre `%s` : %s",
														  def.name.IsEmpty() ? "fonction anonyme" : def.name.CStr(),
														  resultType->ToString().CStr(), bad.Value().CStr()),
										   def.line, def.column));
				CoerceNumber(result, *resultType, typeScope);
			}
			return Ok(std::move(result));
		};

		++context.callDepth;
		for (const StmtPtr &stmt : def.body)
		{
			if (!stmt)
				continue;
			auto outcome = ExecStmt(*stmt, frame);
			if (outcome.IsError())
			{
				--context.callDepth;
				return Err(outcome.Error());
			}
			if (outcome.Value().flow == Flow::RETURN)
			{
				--context.callDepth;
				return checkResult(outcome.Value().value);
			}
			// BREAK/CONTINUE qui remonteraient jusqu'ici sont déjà refusés
			// par ExecStmt (erreur « hors d'une boucle »), rien à faire.
		}
		--context.callDepth;
		return checkResult(Value::Nil());
	}

	std::shared_ptr<Environment> Interpreter::MakeEnvironment(std::shared_ptr<Environment> parent, bool functionScope)
	{
		auto env = std::make_shared<Environment>(std::move(parent), functionScope);
		std::lock_guard<std::mutex> lock(m_environmentsMutex);
		// Purge amortie des entrées mortes : sans elle, le registre grossirait
		// indéfiniment pour un script appelé à chaque image.
		if (m_environments.size() >= m_environmentPurgeThreshold)
		{
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

	void Interpreter::BreakEnvironmentCycles() noexcept
	{
		std::vector<std::weak_ptr<Environment>> environments;
		{
			std::lock_guard<std::mutex> lock(m_environmentsMutex);
			environments = m_environments;
		}

		for (auto &weak : environments)
			if (auto env = weak.lock())
				env->ClearBindings();
		m_globals->ClearBindings();

		// Champs de classe et valeurs d'énumérés : une classe et ses
		// instances se citent mutuellement (autre cycle de `shared_ptr`).
		std::vector<std::weak_ptr<ClassObject>> classes;
		{
			std::lock_guard<std::mutex> lock(m_environmentsMutex);
			classes.swap(m_classes);
		}

		for (auto &weak : classes)
		{
			if (auto klass = weak.lock())
			{
				std::vector<FieldSlot> statics;
				{
					std::lock_guard<std::mutex> lock(klass->statics.mutex);
					statics.swap(klass->statics.slots);
				}
				std::vector<Value> values;
				values.swap(klass->enumValues);
			}
		}
		
		{
			std::lock_guard<std::mutex> lock(m_importMutex);
			m_importCache.clear();
			m_importStack.clear();
			m_importPaths.clear();
			m_importResolver = nullptr;
		}
	}

	void Interpreter::RunPendingFinalizers()
	{
		FinalizerQueue &queue = PendingFinalizers();
		if (queue.pending.empty() || t_finalizing)
			return;
		t_finalizing = true;
		std::vector<std::shared_ptr<InstanceObject>> foreign; // d'un autre interpréteur du même fil
		for (int round = 0; round < 100000 && !queue.pending.empty(); ++round)
		{
			std::vector<std::shared_ptr<InstanceObject>> batch;
			batch.swap(queue.pending);
			for (std::shared_ptr<InstanceObject> &ghost : batch)
			{
				const std::shared_ptr<ClassObject> klass = ghost->klass;
				if (!klass->ownerAlive || !klass->ownerAlive->load())
					continue;
				if (klass->owner != this)
				{
					foreign.push_back(std::move(ghost));
					continue;
				}
				RunDeinit(ghost);
				ghost.reset(); // ses champs meurent ici : d'autres `deinit` peuvent suivre
			}
		}
		for (auto &ghost : foreign)
			queue.pending.push_back(std::move(ghost));
		t_finalizing = false;
	}

	void Interpreter::RunDeinit(const std::shared_ptr<InstanceObject> &ghost)
	{
		const Value self = Value::Instance(ghost);
		for (std::shared_ptr<ClassObject> cls = ghost->klass; cls; cls = cls->superclass)
		{
			const MethodDef *method = cls->def->FindMethod(String("deinit"), false);
			if (!method)
				continue;
			auto result = CallValue(BindMethod(self, cls, method->def), {}, method->def->line, method->def->column);
			if (result.IsError())
			{
				ScriptError error = result.Error();
				error.message = String::Format("destructeur `%s.deinit` : %s", cls->Name().CStr(), error.message.CStr());
				ReportDetachedError(error);
			}
		}
	}

	void Interpreter::ReportDetachedError(const ScriptError &error)
	{
		if (IsMainThread())
		{
			if (onAsyncError)
				onAsyncError(error);
			else
				Emit(String::Format("erreur : %s", error.Format().CStr()));
			return;
		}
		std::lock_guard<std::mutex> lock(m_outputMutex);
		m_orphans.push_back(OrphanError{std::weak_ptr<FutureObject>(), error, 0});
	}

	void Interpreter::FinalizeAtExit()
	{
		m_teardown.store(true);
		onPrint = nullptr;
		onAsyncError = nullptr;
		for (int round = 0; round < 8; ++round)
		{
			std::vector<std::weak_ptr<Environment>> environments;
			{
				std::lock_guard<std::mutex> lock(m_environmentsMutex);
				environments = m_environments;
			}
			bool released = false;
			for (auto &weak : environments)
			{
				if (auto env = weak.lock())
				{
					std::vector<Binding> bindings = env->ExtractScriptBindings();
					released = released || !bindings.empty();
				}
			}
			std::vector<Binding> globals = m_globals->ExtractScriptBindings();
			released = released || !globals.empty();
			globals.clear();
			m_program.reset();
			RunPendingFinalizers();
			if (!released)
				break;
		}
		m_teardown.store(false);
	}

	String Interpreter::ArityText(const NativeObject &native)
	{
		if (native.maxArity < 0)
			return String::Format("au moins %d", native.minArity);
		if (native.minArity == native.maxArity)
			return String::From(native.minArity);
		return String::Format("%d à %d", native.minArity, native.maxArity);
	}

	Option<ScriptError> Interpreter::ConsumeStep(int line, int column)
	{
		if (++Ctx().steps > maxSteps)
			return Some(ScriptError(String::Format("budget d'exécution dépassé (%llu instructions) — boucle infinie ?",
												   static_cast<unsigned long long>(maxSteps)),
									line, column));
		if (m_stopping.load(std::memory_order_relaxed) && !m_teardown.load(std::memory_order_relaxed))
			return Some(ScriptError(String("l'interpréteur s'arrête"), line, column));
		return NONE;
	}

	Result<ExecOutcome, ScriptError> Interpreter::ExecStmt(const Stmt &stmt, const std::shared_ptr<Environment> &env)
	{
		if (!PendingFinalizers().pending.empty())
			RunPendingFinalizers();
		if (auto over = ConsumeStep(stmt.line, stmt.column); over.IsSome())
			return Err(over.Unwrap());

		switch (stmt.kind)
		{
		case StmtKind::EXPRESSION:
		{
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
		case StmtKind::BLOCK:
		{
			const auto &s = static_cast<const BlockStmt &>(stmt);
			auto scope = MakeEnvironment(env);
			if (auto hoisted = Hoist(s.statements, scope, false, nullptr); hoisted.IsSome())
				return Err(hoisted.Unwrap());
			for (const StmtPtr &child : s.statements)
			{
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
		case StmtKind::IF:
		{
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
		case StmtKind::FUNCTION:
		{
			const auto &s = static_cast<const FunctionStmt &>(stmt);
			auto fn = std::make_shared<FunctionObject>();
			fn->def = s.def;
			fn->closure = env;
			env->Put(s.def->name, BindingKind::FUNCTION, Value::Function(std::move(fn)));
			return Ok(ExecOutcome{});
		}
		case StmtKind::NAMESPACE:
			return ExecNamespace(static_cast<const NamespaceStmt &>(stmt), env);
		case StmtKind::CLASS:
			return ExecClass(static_cast<const ClassStmt &>(stmt), env);
		case StmtKind::RETURN:
		{
			const auto &s = static_cast<const ReturnStmt &>(stmt);
			ExecOutcome outcome;
			outcome.flow = Flow::RETURN;
			if (s.value)
			{
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

	Result<ExecOutcome, ScriptError> Interpreter::ExecAssign(const AssignStmt &stmt,
															 const std::shared_ptr<Environment> &env)
	{
		auto rhs = Eval(*stmt.value, env);
		if (rhs.IsError())
			return Err(rhs.Error());
		Value value = rhs.Unwrap();

		if (stmt.compound.IsSome())
			return ExecCompoundAssign(stmt, value, env);

		auto assigned = AssignTo(*stmt.target, std::move(value), env);
		if (assigned.IsSome())
			return Err(assigned.Unwrap());
		return Ok(ExecOutcome{});
	}

	Result<ExecOutcome, ScriptError> Interpreter::ExecCompoundAssign(const AssignStmt &stmt, const Value &operand,
																	 const std::shared_ptr<Environment> &env)
	{
		const BinaryOp op = stmt.compound.Unwrap();
		const Expr &target = *stmt.target;
		constexpr int MAX_RETRIES = 100000;
		auto combine = [&](const Value &current)
		{ return EvalBinaryOp(op, current, operand, stmt.line, stmt.column); };

		if (target.kind == ExprKind::IDENTIFIER)
		{
			const String &name = static_cast<const IdentifierExpr &>(target).name;
			for (int attempt = 0; attempt < MAX_RETRIES; ++attempt)
			{
				auto current = Eval(target, env);
				if (current.IsError())
					return Err(current.Error());
				auto combined = combine(current.Value());
				if (combined.IsError())
					return Err(combined.Error());
				if (Option<BindingInfo> info = env->Lookup(name); info.IsSome() && info.Value().type)
					if (auto bad = CheckTyped("la variable", name, combined.Value(), info.Value().type, env, stmt.line,
											  stmt.column);
						bad.IsSome())
						return Err(bad.Unwrap());
				const Environment::AssignResult written =
					env->CompareAndAssign(name, current.Value(), std::move(combined).Unwrap());
				if (written == Environment::AssignResult::CHANGED)
					continue;
				if (written == Environment::AssignResult::OK)
					return Ok(ExecOutcome{});
				// Constante, zone morte… : le message habituel d'AssignTo.
				auto error = AssignTo(target, Value::Nil(), env);
				return Err(error.IsSome() ? error.Unwrap()
										  : ScriptError(String("affectation impossible"), stmt.line, stmt.column));
			}
			return Err(ScriptError(String::Format("`%s` : trop de conflits d'écriture entre fils", name.CStr()), stmt.line,
								   stmt.column));
		}

		// `a.b op= v` / `a[i] op= v` : l'objet, puis la clé, une seule fois.
		const bool member = target.kind == ExprKind::MEMBER;
		const Expr &objectExpr =
			member ? *static_cast<const MemberExpr &>(target).object : *static_cast<const IndexExpr &>(target).object;
		auto object = Eval(objectExpr, env);
		if (object.IsError())
			return Err(object.Error());
		Value key = member ? Value::Str(static_cast<const MemberExpr &>(target).name) : Value::Nil();
		if (!member)
		{
			auto index = Eval(*static_cast<const IndexExpr &>(target).index, env);
			if (index.IsError())
				return Err(index.Error());
			key = index.Unwrap();
		}
		const Value &obj = object.Value();
		for (int attempt = 0; attempt < MAX_RETRIES; ++attempt)
		{
			auto current = member ? ReadMember(obj, key.AsString(), target.line, target.column)
								  : ReadIndexed(obj, key, target.line, target.column);
			if (current.IsError())
				return Err(current.Error());
			auto combined = combine(current.Value());
			if (combined.IsError())
				return Err(combined.Error());
			Value next = std::move(combined).Unwrap();
			bool written = false;
			bool plain = false; // pas de comparaison possible : écriture ordinaire
			if (obj.IsList() && obj.AsList() && key.IsNumber() && key.AsNumber() >= 0.0)
				written = obj.AsList()->CompareAndSet(static_cast<size_t>(key.AsNumber()), current.Value(), next);
			else if (obj.IsMap() && obj.AsMap())
				written = obj.AsMap()->CompareAndSet(key.ToDisplayString(), current.Value(), next);
			else if (obj.IsInstance() && obj.AsInstance() && obj.AsInstance()->fields.Has(key.ToDisplayString()))
			{
				if (auto bad = CheckTyped("le champ", key.ToDisplayString(), next,
										  obj.AsInstance()->fields.TypeOf(key.ToDisplayString()),
										  InstanceTypeScope(obj.AsInstance()), stmt.line, stmt.column);
					bad.IsSome())
					return Err(bad.Unwrap());
				const auto result = obj.AsInstance()->fields.CompareAndSet(key.ToDisplayString(), current.Value(), next);
				if (result == FieldSet::WriteResult::CONSTANT)
					plain = true; // AssignObjectMember rendra l'erreur « constant »
				written = result == FieldSet::WriteResult::OK;
			}
			else
				plain = true;
			if (plain)
			{
				auto error = member ? AssignObjectOrMap(obj, key.AsString(), std::move(next), target.line, target.column)
									: AssignIndexed(obj, key, std::move(next), target.line, target.column);
				if (error.IsSome())
					return Err(error.Unwrap());
				return Ok(ExecOutcome{});
			}
			if (written)
				return Ok(ExecOutcome{});
		}
		return Err(ScriptError(String("trop de conflits d'écriture entre fils"), stmt.line, stmt.column));
	}

	Result<Value, ScriptError> Interpreter::ReadMember(const Value &object, const String &name, int line, int column)
	{
		if (object.IsNamespace())
			return ReadNamespaceMember(object, name, line, column);
		if (object.IsHost() && object.AsHost())
			return ReadHostMember(object.AsHost(), name, line, column);
		if (object.IsNative() && object.AsNative() && object.AsNative()->hostType)
		{
			const std::shared_ptr<const HostType> &type = object.AsNative()->hostType;
			if (const Value *member = type->FindStatic(name))
				return Ok(*member);
			if (name == "name")
				return Ok(Value::Str(type->name));
			return Err(ScriptError(String::Format("`%s` n'a pas de membre de classe `%s`", type->name.CStr(), name.CStr()),
								   line, column));
		}
		if (object.IsInstance())
			return ReadInstanceMember(object, name, line, column);
		if (object.IsClass())
			return ReadClassMember(object.AsClass(), name, line, column);
		if (object.IsFuture())
			return ReadFutureMember(object, name, line, column);
		if (object.IsList() || object.IsString())
			return ReadBuiltinMethod(object, name, line, column);
		if (!object.IsMap() || !object.AsMap())
			return Err(ScriptError(
				String::Format("`.%s` attend une table ou un objet, trouvé `%s`", name.CStr(), object.TypeName()), line,
				column));
		Option<Value> found = object.AsMap()->Get(name);
		return Ok(found.IsSome() ? std::move(found).Unwrap() : Value::Nil());
	}

	Result<Value, ScriptError> Interpreter::ReadBuiltinMethod(const Value &receiver, const String &name, int line,
															  int column)
	{
		struct Alias
		{
			const char *method;
			const char *global;
		};
		static constexpr Alias LIST_METHODS[] = {
			{"append", "push"},
			{"push", "push"},
			{"pop", "pop"},
			{"insert", "insert"},
			{"remove", "remove"},
			{"clear", "clear"},
			{"index_of", "index_of"},
			{"len", "len"},
			{"reverse", "reverse"},
			{"sort", "sort_numbers"},
			{"join", "join"},
		};
		static constexpr Alias STRING_METHODS[] = {
			{"len", "len"},
			{"upper", "upper"},
			{"lower", "lower"},
			{"trim", "trim"},
			{"split", "split"},
			{"sub", "sub"},
			{"find", "find"},
			{"replace", "replace"},
			{"starts_with", "starts_with"},
			{"ends_with", "ends_with"},
		};
		auto native = std::make_shared<NativeObject>();
		native->anyThread = true;
		native->name = String::Format("%s.%s", receiver.TypeName(), name.CStr());
		const Value self = receiver;
		if (name == "is_empty")
		{
			native->minArity = native->maxArity = 0;
			native->fn = [self](Interpreter &, std::vector<Value> &) -> Result<Value, ScriptError>
			{
				return Ok(Value::Boolean(self.IsList() ? self.AsList()->Size() == 0 : self.AsString().IsEmpty()));
			};
			return Ok(Value::Native(std::move(native)));
		}
		if (name == "contains")
		{
			native->minArity = native->maxArity = 1;
			native->fn = [self](Interpreter &, std::vector<Value> &args) -> Result<Value, ScriptError>
			{
				if (self.IsString())
					return Ok(Value::Boolean(self.AsString().Find(args[0].ToDisplayString()) != String::NPOS));
				for (const Value &item : self.AsList()->Snapshot())
					if (item.Equals(args[0]))
						return Ok(Value::Boolean(true));
				return Ok(Value::Boolean(false));
			};
			return Ok(Value::Native(std::move(native)));
		}
		const char *global = nullptr;
		if (receiver.IsList())
		{
			for (const Alias &alias : LIST_METHODS)
				if (name == alias.method)
					global = alias.global;
		}
		else
		{
			for (const Alias &alias : STRING_METHODS)
				if (name == alias.method)
					global = alias.global;
		}
		Option<Value> function = global ? GetGlobal(String(global)) : Option<Value>(NONE);
		if (function.IsNone() || !function.Value().IsNative())
			return Err(ScriptError(
				String::Format("une valeur `%s` n'a pas de méthode `%s`", receiver.TypeName(), name.CStr()), line, column));
		const std::shared_ptr<NativeObject> target = function.Value().AsNative();
		native->minArity = target->minArity > 0 ? target->minArity - 1 : 0;
		native->maxArity = target->maxArity < 0 ? -1 : (target->maxArity > 0 ? target->maxArity - 1 : 0);
		// `append` : exactement un élément (push en accepte plusieurs).
		if (name == "append")
			native->minArity = native->maxArity = 1;
		native->fn = [self, target](Interpreter &vm, std::vector<Value> &args) -> Result<Value, ScriptError>
		{
			std::vector<Value> full;
			full.reserve(args.size() + 1);
			full.push_back(self);
			for (Value &arg : args)
				full.push_back(std::move(arg));
			return vm.CallValue(Value::Native(target), std::move(full), 0, 0);
		};
		return Ok(Value::Native(std::move(native)));
	}

	Result<Value, ScriptError> Interpreter::ReadHostMember(const std::shared_ptr<HostObject> &host, const String &name,
														   int line, int column)
	{
		const HostType &type = *host->type;
		if (const HostMethod *method = type.FindMethod(name))
		{
			auto native = std::make_shared<NativeObject>();
			native->name = String::Format("%s.%s", type.name.CStr(), name.CStr());
			native->minArity = method->minArity;
			native->maxArity = method->maxArity;
			native->anyThread = !type.mainThreadOnly;
			auto fn = method->fn;
			native->fn = [host, fn](Interpreter &vm, std::vector<Value> &args) -> Result<Value, ScriptError>
			{
				return fn(vm, host, args);
			};
			return Ok(Value::Native(std::move(native)));
		}
		if (type.get)
			if (Option<Value> property = type.get(*host, name); property.IsSome())
				return Ok(std::move(property).Unwrap());
		String known;
		for (const HostMethod &method : type.methods)
		{
			known.Concat(known.IsEmpty() ? "" : ", ");
			known.Concat(method.name);
		}
		return Err(ScriptError(
			String::Format("un `%s` n'a pas de membre `%s` (méthodes : %s)", type.name.CStr(), name.CStr(), known.CStr()),
			line, column));
	}

	Option<ScriptError> Interpreter::AssignObjectOrMap(const Value &object, const String &name, Value value, int line,
													   int column)
	{
		if (object.IsNamespace())
			return Some(ReadOnlyNamespaceError(object, name, line, column));
		if (object.IsHost() && object.AsHost())
		{
			const HostType &type = *object.AsHost()->type;
			auto written = type.set ? type.set(*this, object.AsHost(), name, value) : Result<bool, ScriptError>(Ok(false));
			if (written.IsError())
				return Some(ScriptError(written.Error().message, line, column));
			if (!written.Value())
				return Some(ScriptError(
					String::Format("un `%s` n'a pas de propriété modifiable `%s`", type.name.CStr(), name.CStr()), line,
					column));
			return NONE;
		}
		if (object.IsInstance() || object.IsClass())
			return AssignObjectMember(object, name, std::move(value), line, column);
		if (!object.IsMap() || !object.AsMap())
			return Some(ScriptError(
				String::Format("`.%s` attend une table ou un objet, trouvé `%s`", name.CStr(), object.TypeName()), line,
				column));
		object.AsMap()->SetKey(name, std::move(value));
		return NONE;
	}

	Option<ScriptError> Interpreter::AssignTo(const Expr &target, Value value, const std::shared_ptr<Environment> &env)
	{
		switch (target.kind)
		{
		case ExprKind::IDENTIFIER:
		{
			const auto &e = static_cast<const IdentifierExpr &>(target);
			if (Option<BindingInfo> info = env->Lookup(e.name); info.IsSome() && info.Value().type)
				if (auto bad =
						CheckTyped("la variable", e.name, value, info.Value().type, env, target.line, target.column);
					bad.IsSome())
					return bad;
			switch (env->Assign(e.name, std::move(value)))
			{
			case Environment::AssignResult::OK:
			case Environment::AssignResult::CHANGED:
				return NONE;
			case Environment::AssignResult::UNKNOWN:
				return Some(
					ScriptError(String::Format("variable inconnue `%s` (manque-t-il un `let` ?)", e.name.CStr()),
								target.line, target.column));
			case Environment::AssignResult::CONSTANT:
			{
				Option<BindingInfo> binding = env->Lookup(e.name);
				const bool space = binding.IsSome() && binding.Value().value.IsNamespace();
				return Some(
					ScriptError(String::Format(space ? "`%s` est un espace de noms : réaffectation impossible"
													 : "`%s` est une constante : réaffectation impossible",
											   e.name.CStr()),
								target.line, target.column));
			}
			case Environment::AssignResult::UNINITIALIZED:
				return Some(DeadZoneError(e.name, target.line, target.column));
			}
			return NONE;
		}
		case ExprKind::INDEX:
		{
			const auto &e = static_cast<const IndexExpr &>(target);
			auto object = Eval(*e.object, env);
			if (object.IsError())
				return Some(object.Error());
			auto index = Eval(*e.index, env);
			if (index.IsError())
				return Some(index.Error());
			return AssignIndexed(object.Value(), index.Value(), std::move(value), target.line, target.column);
		}
		case ExprKind::MEMBER:
		{
			const auto &e = static_cast<const MemberExpr &>(target);
			auto object = Eval(*e.object, env);
			if (object.IsError())
				return Some(object.Error());
			return AssignObjectOrMap(object.Value(), e.name, std::move(value), target.line, target.column);
		}
		default:
			break;
		}
		return Some(ScriptError(String("cible d'affectation invalide"), target.line, target.column));
	}

	Option<ScriptError> Interpreter::AssignIndexed(const Value &object, const Value &index, Value value, int line,
												   int column)
	{
		if (Option<Value> method = InstanceOperator(object, "[]="); method.IsSome())
		{
			auto result = CallValue(method.Value(), {index, std::move(value)}, line, column);
			return result.IsError() ? Some(result.Error()) : Option<ScriptError>(NONE);
		}
		if (object.IsHost() && object.AsHost())
		{
			if (!object.AsHost()->type->setIndex)
				return Some(ScriptError(
					String::Format("un `%s` ne s'indexe pas en écriture (`[...] =`)", object.TypeName()), line, column));
			if (auto error = object.AsHost()->type->setIndex(*this, object.AsHost(), index, std::move(value));
				error.IsSome())
			{
				ScriptError positioned = error.Unwrap();
				if (positioned.line == 0)
				{
					positioned.line = line;
					positioned.column = column;
				}
				return Some(positioned);
			}
			return NONE;
		}
		if (object.IsInstance())
			return AssignObjectMember(object, index.ToDisplayString(), std::move(value), line, column);
		if (object.IsNamespace())
			return Some(ReadOnlyNamespaceError(object, index.ToDisplayString(), line, column));
		if (object.IsList() && object.AsList())
		{
			if (!index.IsNumber())
				return Some(ScriptError(String::Format("index de liste numérique attendu, trouvé `%s`", index.TypeName()),
										line, column));
			ListObject &list = *object.AsList();
			std::lock_guard<std::mutex> lock(list.mutex);
			std::vector<Value> &items = list.items;
			double raw = index.AsNumber();
			if (raw != std::floor(raw) || raw < 0.0 || raw >= double(items.size()))
				return Some(ScriptError(String::Format("index de liste hors bornes (%s, taille %d)",
													   Value::NumberToString(raw).CStr(), int(items.size())),
										line, column));
			items[static_cast<size_t>(raw)] = std::move(value);
			return NONE;
		}
		if (object.IsMap() && object.AsMap())
		{
			object.AsMap()->SetKey(index.ToDisplayString(), std::move(value));
			return NONE;
		}
		return Some(ScriptError(String::Format("`[...]` attend une liste ou une table, trouvé `%s`", object.TypeName()),
								line, column));
	}

	Result<ExecOutcome, ScriptError> Interpreter::ExecWhile(const WhileStmt &stmt, const std::shared_ptr<Environment> &env)
	{
		// `do { … } while (…)` : le premier tour passe sans tester.
		bool skipTest = stmt.checkAfter;
		for (;;)
		{
			if (auto over = ConsumeStep(stmt.line, stmt.column); over.IsSome())
				return Err(over.Unwrap());
			if (!skipTest)
			{
				auto condition = Eval(*stmt.condition, env);
				if (condition.IsError())
					return Err(condition.Error());
				if (!condition.Value().IsTruthy())
					return Ok(ExecOutcome{});
			}
			skipTest = false;

			auto outcome = ExecStmt(*stmt.body, env);
			if (outcome.IsError())
				return outcome;
			if (outcome.Value().flow == Flow::BREAK)
				return Ok(ExecOutcome{});
			if (outcome.Value().flow == Flow::RETURN)
				return outcome;
		}
	}

	Result<ExecOutcome, ScriptError> Interpreter::ExecForIn(const ForInStmt &stmt, const std::shared_ptr<Environment> &env)
	{
		auto iterable = Eval(*stmt.iterable, env);
		if (iterable.IsError())
			return Err(iterable.Error());

		// La suite itérée est MATÉRIALISÉE avant la boucle : modifier la
		// liste depuis le corps ne peut donc pas invalider l'itération (le
		// piège n°1 d'un `for` sur conteneur vivant).
		auto materialized = Iterate(iterable.Value(), stmt.line, stmt.column);
		if (materialized.IsError())
			return Err(materialized.Error());
		std::vector<Value> sequence = std::move(materialized).Unwrap();

		for (Value &item : sequence)
		{
			if (auto over = ConsumeStep(stmt.line, stmt.column); over.IsSome())
				return Err(over.Unwrap());

			auto scope = MakeEnvironment(env);
			if (auto bad = CheckTyped("la variable de boucle", stmt.variable, item, stmt.type, env, stmt.line, stmt.column);
				bad.IsSome())
				return Err(bad.Unwrap());
			scope->Put(stmt.variable, BindingKind::LET, item, false, stmt.type);

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

	Result<Value, ScriptError> Interpreter::Eval(const Expr &expr, const std::shared_ptr<Environment> &env)
	{
		switch (expr.kind) {
			case ExprKind::NUMBER:
			{
				const auto &number = static_cast<const NumberExpr &>(expr);
				if (!number.isInteger)
					return Ok(Value::Number(number.value));
				if (number.integer <= uint64_t(INT64_MAX))
					return Ok(Value::Int(static_cast<int64_t>(number.integer)));
				return Ok(Value::UInt(number.integer));
			}
			case ExprKind::STRING_LITERAL:
				return Ok(Value::Str(static_cast<const StringExpr &>(expr).value));
			case ExprKind::BOOLEAN:
				return Ok(Value::Boolean(static_cast<const BooleanExpr &>(expr).value));
			case ExprKind::NIL:
				return Ok(Value::Nil());
			case ExprKind::IDENTIFIER:
			{
				const auto &e = static_cast<const IdentifierExpr &>(expr);
				if (Option<BindingInfo> binding = env->Lookup(e.name); binding.IsSome())
				{
					if (!binding.Value().initialized)
						return Err(DeadZoneError(e.name, expr.line, expr.column));
					return Ok(std::move(binding).Unwrap().value);
				}
				return Err(ScriptError(String::Format("variable inconnue `%s`", e.name.CStr()), expr.line, expr.column));
			}
			case ExprKind::LIST:
			{
				const auto &e = static_cast<const ListExpr &>(expr);
				auto list = std::make_shared<ListObject>();
				list->items.reserve(e.elements.size());
				for (const ExprPtr &element : e.elements)
				{
					auto value = Eval(*element, env);
					if (value.IsError())
						return value;
					list->items.push_back(value.Unwrap());
				}
				return Ok(Value::List(std::move(list)));
			}
			case ExprKind::MAP:
			{
				const auto &e = static_cast<const MapExpr &>(expr);
				auto map = std::make_shared<MapObject>();
				for (const MapEntry &entry : e.entries)
				{
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
			case ExprKind::BINARY:
			{
				const auto &e = static_cast<const BinaryExpr &>(expr);
				auto left = Eval(*e.left, env);
				if (left.IsError())
					return left;
				auto right = Eval(*e.right, env);
				if (right.IsError())
					return right;
				return EvalBinaryOp(e.op, left.Value(), right.Value(), expr.line, expr.column);
			}
			case ExprKind::LOGICAL:
			{
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
			case ExprKind::CALL:
			{
				const auto &e = static_cast<const CallExpr &>(expr);
				auto callee = Eval(*e.callee, env);
				if (callee.IsError())
					return callee;
				const bool hostType =
					callee.Value().IsNative() && callee.Value().AsNative() && callee.Value().AsNative()->hostType;
				if (e.isNew && !callee.Value().IsClass() && !hostType)
					return Err(
						ScriptError(String::Format("`new` attend une classe, trouvé `%s`", callee.Value().TypeName()),
									expr.line, expr.column));
				std::vector<Value> args;
				args.reserve(e.args.size());
				for (const ExprPtr &arg : e.args)
				{
					auto value = Eval(*arg, env);
					if (value.IsError())
						return value;
					args.push_back(value.Unwrap());
				}
				if (!e.typeArgs.empty())
					return CallGeneric(callee.Value(), e.typeArgs, std::move(args), env, expr.line, expr.column);
				return CallValue(callee.Value(), std::move(args), expr.line, expr.column);
			}
			case ExprKind::INDEX:
			{
				const auto &e = static_cast<const IndexExpr &>(expr);
				auto object = Eval(*e.object, env);
				if (object.IsError())
					return object;
				auto index = Eval(*e.index, env);
				if (index.IsError())
					return index;
				return ReadIndexed(object.Value(), index.Value(), expr.line, expr.column);
			}
			case ExprKind::MEMBER:
			{
				const auto &e = static_cast<const MemberExpr &>(expr);
				auto object = Eval(*e.object, env);
				if (object.IsError())
					return object;
				return ReadMember(object.Value(), e.name, expr.line, expr.column);
			}
			case ExprKind::FUNCTION:
			{
				const auto &e = static_cast<const FunctionExpr &>(expr);
				auto fn = std::make_shared<FunctionObject>();
				fn->def = e.def;
				fn->closure = env;
				return Ok(Value::Function(std::move(fn)));
			}
			case ExprKind::THIS:
			{
				Option<Value> self = env->Find(String(THIS_NAME));
				if (self.IsNone() || !self.Value().IsInstance())
					return Err(ScriptError(String("`this` hors d'une méthode d'instance"), expr.line, expr.column));
				return Ok(std::move(self).Unwrap());
			}
			case ExprKind::SUPER:
				return EvalSuper(static_cast<const SuperExpr &>(expr), env);
			case ExprKind::AWAIT:
			{
				const auto &e = static_cast<const AwaitExpr &>(expr);
				auto operand = Eval(*e.operand, env);
				if (operand.IsError())
					return operand;
				return AwaitValue(operand.Value(), expr.line, expr.column);
			}
			case ExprKind::IMPORT: {
				const auto& e = static_cast<const ImportExpr&>(expr);
				auto operand = Eval(*e.specifier, env);
				if (operand.IsError())
					return operand;
				if (!operand.Value().IsString())
					return Err(ScriptError(
						String::Format("`import` attend une chaîne (chemin de module), trouvé `%s`",
									operand.Value().TypeName()),
						expr.line, expr.column));
				return ImportModule(operand.Value().AsString(), expr.line, expr.column);
			}
		}
		return Err(ScriptError(String("expression inconnue"), expr.line, expr.column));
	}

	Result<Value, ScriptError> Interpreter::EvalUnary(const UnaryExpr &expr, const std::shared_ptr<Environment> &env)
	{
		auto operand = Eval(*expr.operand, env);
		if (operand.IsError())
			return operand;
		if (expr.op == UnaryOp::NOT)
			return Ok(Value::Boolean(!operand.Value().IsTruthy()));
		// `-objet` : `operator -()` (renommé `operator neg`), ou crochet de l'hôte.
		const Value &value = operand.Value();
		if (Option<Value> method = InstanceOperator(value, " neg"); method.IsSome())
			return CallValue(method.Value(), {}, expr.line, expr.column);
		if (Option<Value> method = StaticOperator(value, " neg"); method.IsSome())
			return CallValue(method.Value(), {value}, expr.line, expr.column);
		if (value.IsHost() && value.AsHost() && value.AsHost()->type->negate)
			if (auto negated = value.AsHost()->type->negate(*this, value.AsHost()); negated.IsSome())
				return Positioned(std::move(negated).Unwrap(), expr.line, expr.column);
		if (!operand.Value().IsNumber())
			return Err(ScriptError(String::Format("`-` attend un nombre, trouvé `%s`", operand.Value().TypeName()),
								   expr.line, expr.column));
		auto negated = numeric::Negate(operand.Value());
		if (negated.IsError())
			return Err(ScriptError(negated.Error(), expr.line, expr.column));
		return Ok(std::move(negated).Unwrap());
	}

	Result<Value, ScriptError> Interpreter::ReadIndexed(const Value &object, const Value &index, int line, int column)
	{
		if (Option<Value> method = InstanceOperator(object, "[]"); method.IsSome())
			return CallValue(method.Value(), {index}, line, column);
		if (object.IsHost() && object.AsHost())
		{
			if (!object.AsHost()->type->index)
				return Err(
					ScriptError(String::Format("un `%s` ne s'indexe pas (`[...]`)", object.TypeName()), line, column));
			return Positioned(object.AsHost()->type->index(*this, object.AsHost(), index), line, column);
		}
		if (object.IsInstance() && object.AsInstance())
		{
			// `obj["champ"]` : les CHAMPS seulement (nom calculé).
			Option<Value> field = object.AsInstance()->fields.Get(index.ToDisplayString());
			return Ok(field.IsSome() ? std::move(field).Unwrap() : Value::Nil());
		}
		if (object.IsNamespace())
			return ReadNamespaceMember(object, index.ToDisplayString(), line, column);
		if (object.IsList() && object.AsList())
		{
			if (!index.IsNumber())
				return Err(ScriptError(String::Format("index de liste numérique attendu, trouvé `%s`", index.TypeName()),
									   line, column));
			double raw = index.AsNumber();
			// Hors bornes = `nil` (pas une erreur) : un script d'éditeur
			// teste très souvent `if list[i] { ... }` en fin de parcours.
			if (raw != std::floor(raw) || raw < 0.0)
				return Ok(Value::Nil());
			Option<Value> item = object.AsList()->At(static_cast<size_t>(raw));
			return Ok(item.IsSome() ? std::move(item).Unwrap() : Value::Nil());
		}
		if (object.IsMap() && object.AsMap())
		{
			Option<Value> found = object.AsMap()->Get(index.ToDisplayString());
			return Ok(found.IsSome() ? std::move(found).Unwrap() : Value::Nil());
		}
		if (object.IsString())
		{
			if (!index.IsNumber())
				return Err(ScriptError(String::Format("index de chaîne numérique attendu, trouvé `%s`", index.TypeName()),
									   line, column));
			const String &text = object.AsString();
			double raw = index.AsNumber();
			if (raw != std::floor(raw) || raw < 0.0 || raw >= double(text.GetSize()))
				return Ok(Value::Nil());
			return Ok(Value::Str(String(text.CStr() + static_cast<size_t>(raw), 1)));
		}
		return Err(
			ScriptError(String::Format("`[...]` attend une liste, une table ou une chaîne, trouvé `%s`", object.TypeName()),
						line, column));
	}

	Result<Value, ScriptError> Interpreter::Positioned(Result<Value, ScriptError> result, int line, int column)
	{
		if (result.IsOk() || result.Error().line != 0)
			return result;
		ScriptError error = result.Error();
		error.line = line;
		error.column = column;
		return Err(std::move(error));
	}

	Option<Value> Interpreter::InstanceOperator(const Value &value, const char *symbol)
	{
		if (!value.IsInstance() || !value.AsInstance())
			return NONE;
		auto [method, owner] = FindMethod(value.AsInstance()->klass, String("operator") + String(symbol), false);
		if (!method || method->isAbstract)
			return NONE;
		return Some(BindMethod(value, owner, method->def));
	}

	Option<Value> Interpreter::StaticOperator(const Value &value, const char *symbol)
	{
		if (!value.IsInstance() || !value.AsInstance())
			return NONE;
		auto [method, owner] = FindMethod(value.AsInstance()->klass, String("operator") + String(symbol), true);
		if (!method)
			return NONE;
		return Some(BindMethod(Value::Nil(), owner, method->def));
	}

	Option<Result<Value, ScriptError>> Interpreter::TryBinaryOverload(const char *symbol, BinaryOp op, const Value &a,
																	  const Value &b, int line, int column)
	{
		if (Option<Value> method = InstanceOperator(a, symbol); method.IsSome())
			return Some(CallValue(method.Value(), {b}, line, column));
		if (Option<Value> method = StaticOperator(a, symbol); method.IsSome())
			return Some(CallValue(method.Value(), {a, b}, line, column));
		if (Option<Value> method = StaticOperator(b, symbol); method.IsSome())
			return Some(CallValue(method.Value(), {a, b}, line, column));
		for (const Value *side : {&a, &b})
			if (side->IsHost() && side->AsHost() && side->AsHost()->type->binary)
				if (auto result = side->AsHost()->type->binary(*this, op, a, b); result.IsSome())
					return Some(Positioned(std::move(result).Unwrap(), line, column));
		return NONE;
	}

	Option<int> Interpreter::EnumOrder(const Value &a, const Value &b)
	{
		if (!a.IsInstance() || !b.IsInstance() || !a.AsInstance() || !b.AsInstance())
			return NONE;
		const auto &klass = a.AsInstance()->klass;
		if (!klass->def->isEnum || klass != b.AsInstance()->klass)
			return NONE;
		const int64_t x = a.AsInstance()->fields.Get(String("ordinal")).UnwrapOr(Value::Int(0)).AsInt64();
		const int64_t y = b.AsInstance()->fields.Get(String("ordinal")).UnwrapOr(Value::Int(0)).AsInt64();
		return Some(x < y ? -1 : (x > y ? 1 : 0));
	}

	Result<Value, ScriptError> Interpreter::EvalBinaryOp(BinaryOp op, const Value &left, const Value &right, int line,
														 int column)
	{
		switch (op)
		{
		case BinaryOp::FLOW_RIGHT:
			return Flow(left, right, line, column);
		case BinaryOp::FLOW_LEFT:
			return Flow(right, left, line, column);
		case BinaryOp::FLOW_BOTH:
			return Link(left, right, line, column);
		case BinaryOp::IS:
		case BinaryOp::AS:
			return ApplyBinary(op, left, right, line, column);
		default:
			break;
		}
		if (left.IsInstance() || right.IsInstance() || left.IsHost() || right.IsHost())
		{
			if (auto result = TryBinaryOverload(BinaryOpName(op), op, left, right, line, column); result.IsSome())
				return std::move(result).Unwrap();
			// Dérivées : `!=` de `==`, `>` `<=` `>=` de `<`.
			auto derived = [&](const char *symbol, BinaryOp base, const Value &a, const Value &b,
							   bool negate) -> Option<Result<Value, ScriptError>>
			{
				auto result = TryBinaryOverload(symbol, base, a, b, line, column);
				if (result.IsNone())
					return NONE;
				auto value = std::move(result).Unwrap();
				if (value.IsError() || !negate)
					return Some(std::move(value));
				return Some(Result<Value, ScriptError>(Ok(Value::Boolean(!value.Value().IsTruthy()))));
			};
			Option<Result<Value, ScriptError>> fallback = NONE;
			if (op == BinaryOp::NOT_EQUAL)
				fallback = derived("==", BinaryOp::EQUAL, left, right, true);
			else if (op == BinaryOp::GREATER)
				fallback = derived("<", BinaryOp::LESS, right, left, false);
			else if (op == BinaryOp::LESS_EQUAL)
				fallback = derived("<", BinaryOp::LESS, right, left, true);
			else if (op == BinaryOp::GREATER_EQUAL)
				fallback = derived("<", BinaryOp::LESS, left, right, true);
			if (fallback.IsSome())
				return std::move(fallback).Unwrap();
			// Valeurs d'un même énuméré : ordonnées par rang de déclaration.
			if (Option<int> order = EnumOrder(left, right); order.IsSome())
			{
				const int o = order.Unwrap();
				switch (op)
				{
				case BinaryOp::LESS:
					return Ok(Value::Boolean(o < 0));
				case BinaryOp::LESS_EQUAL:
					return Ok(Value::Boolean(o <= 0));
				case BinaryOp::GREATER:
					return Ok(Value::Boolean(o > 0));
				case BinaryOp::GREATER_EQUAL:
					return Ok(Value::Boolean(o >= 0));
				default:
					break;
				}
			}
			// Concaténation : le texte d'un objet passe par son `operator str`.
			if (op == BinaryOp::CONCAT || (op == BinaryOp::ADD && (left.IsString() || right.IsString())))
			{
				auto a = Stringify(left);
				if (a.IsError())
					return Err(a.Error());
				auto b = Stringify(right);
				if (b.IsError())
					return Err(b.Error());
				return Ok(Value::Str(a.Value() + b.Value()));
			}
		}
		return ApplyBinary(op, left, right, line, column);
	}

	Result<Value, ScriptError> Interpreter::Flow(const Value &source, const Value &sink, int line, int column)
	{
		if (Option<Value> method = InstanceOperator(source, "->"); method.IsSome())
			return CallValue(method.Value(), {sink}, line, column);
		if (Option<Value> method = StaticOperator(source, "->"); method.IsSome())
			return CallValue(method.Value(), {source, sink}, line, column);
		if (source.IsHost() && source.AsHost() && source.AsHost()->type->flowOut)
			return Positioned(source.AsHost()->type->flowOut(*this, source.AsHost(), sink), line, column);
		return FlowInto(sink, source, line, column);
	}

	Result<Value, ScriptError> Interpreter::Link(const Value &a, const Value &b, int line, int column)
	{
		if (Option<Value> method = InstanceOperator(a, "<->"); method.IsSome())
			return CallValue(method.Value(), {b}, line, column);
		if (Option<Value> method = StaticOperator(a, "<->"); method.IsSome())
			return CallValue(method.Value(), {a, b}, line, column);
		if (Option<Value> method = InstanceOperator(b, "<->"); method.IsSome())
			return CallValue(method.Value(), {a}, line, column);
		if (Option<Value> method = StaticOperator(b, "<->"); method.IsSome())
			return CallValue(method.Value(), {a, b}, line, column);
		if (a.IsHost() && a.AsHost() && a.AsHost()->type->link)
			return Positioned(a.AsHost()->type->link(*this, a.AsHost(), b), line, column);
		if (b.IsHost() && b.AsHost() && b.AsHost()->type->link)
			return Positioned(b.AsHost()->type->link(*this, b.AsHost(), a), line, column);
		return Err(ScriptError(String::Format("`<->` relie deux pipes (ou des objets dotés d'un `operator <->`), pas "
											  "`%s` et `%s`",
											  a.TypeName(), b.TypeName()),
							   line, column));
	}

	Result<Value, ScriptError> Interpreter::CallGeneric(const Value &callee, const std::vector<TypeRef> &typeArgs,
														std::vector<Value> args,
														const std::shared_ptr<Environment> &env, int line, int column)
	{
		std::vector<Value> types = MakeTypeValues(typeArgs, env);
		if (callee.IsFunction() && callee.AsFunction())
		{
			const FunctionDef &def = *callee.AsFunction()->def;
			if (types.size() > def.typeParams.size())
				return Err(ScriptError(String::Format("`%s` attend %d argument(s) de type, %d fourni(s)",
													  def.name.IsEmpty() ? "fonction anonyme" : def.name.CStr(),
													  int(def.typeParams.size()), int(types.size())),
									   line, column));
			auto specialized = std::make_shared<FunctionObject>(*callee.AsFunction());
			specialized->typeArgs = std::move(types);
			return CallValue(Value::Function(std::move(specialized)), std::move(args), line, column);
		}
		if (callee.IsClass())
			return Instantiate(callee.AsClass(), std::move(args), line, column, std::move(types));
		if (callee.IsNative() && callee.AsNative() && callee.AsNative()->hostType)
		{
			const std::shared_ptr<const HostType> &type = callee.AsNative()->hostType;
			const int argc = int(args.size());
			if (argc < type->minArity || (type->maxArity >= 0 && argc > type->maxArity))
				return Err(ScriptError(String::Format("`%s` attend %s argument(s), %d fourni(s)", type->name.CStr(),
													  ArityText(*callee.AsNative()).CStr(), argc),
									   line, column));
			if (!type->construct)
				return Err(
					ScriptError(String::Format("`%s` ne se construit pas directement", type->name.CStr()), line, column));
			return Positioned(type->construct(*this, args, types), line, column);
		}
		return Err(ScriptError(
			String::Format("une valeur `%s` n'est pas générique : pas d'arguments de type `<…>`", callee.TypeName()), line,
			column));
	}

	Result<Value, ScriptError> Interpreter::ApplyBinary(BinaryOp op, const Value &left, const Value &right,
														int line, int column)
	{
		switch (op)
		{
		case BinaryOp::EQUAL:
			return Ok(Value::Boolean(left.Equals(right)));
		case BinaryOp::NOT_EQUAL:
			return Ok(Value::Boolean(!left.Equals(right)));
		case BinaryOp::CONCAT:
			return Ok(Value::Str(left.ToDisplayString() + right.ToDisplayString()));
		case BinaryOp::IS:
		case BinaryOp::AS:
		{
			// Type de l'hôte : `v is std.vector` — ou, pour une base de l'hôte,
			// une instance de script qui en dérive (`ennemi is Mesh3D`).
			if (right.IsNative() && right.AsNative() && right.AsNative()->hostType)
			{
				const std::shared_ptr<const HostType> &wanted = right.AsNative()->hostType;
				const bool isHost = (left.IsHost() && left.AsHost() && left.AsHost()->type == wanted) ||
									(wanted->isOwner && left.IsInstance() && left.AsInstance() &&
									 FindOwner(*left.AsInstance(), *wanted) != nullptr);
				if (op == BinaryOp::IS)
					return Ok(Value::Boolean(isHost));
				if (!isHost)
					return Err(ScriptError(String::Format("transtypage impossible : `%s` n'est pas un `%s`",
														  left.TypeName(), right.AsNative()->hostType->name.CStr()),
										   line, column));
				return Ok(left);
			}
			if (!right.IsClass() || !right.AsClass())
				return Err(ScriptError(String::Format("`%s` attend une classe ou une interface à droite, trouvé `%s`",
													  BinaryOpName(op), right.TypeName()),
									   line, column));
			const bool match = left.IsInstance() && left.AsInstance() && left.AsInstance()->klass &&
							   left.AsInstance()->klass->IsSubtypeOf(*right.AsClass());
			if (op == BinaryOp::IS)
				return Ok(Value::Boolean(match));
			if (!match)
				return Err(ScriptError(
					String::Format("transtypage impossible : `%s` n'est pas un `%s`",
								   left.IsInstance() ? left.AsInstance()->klass->Name().CStr() : left.TypeName(),
								   right.AsClass()->Name().CStr()),
					line, column));
			return Ok(left);
		}
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

		// Arithmétique typée (i8 … u64, f32, f64) : cf. data::script::numeric.
		auto arithmetic = [&](numeric::Op nop) -> Result<Value, ScriptError>
		{
			auto result = numeric::Apply(nop, left, right);
			if (result.IsError())
				return Err(ScriptError(result.Error(), line, column));
			return Ok(std::move(result).Unwrap());
		};
		const int order = numeric::Compare(left, right); // 2 = NaN : non ordonné
		switch (op)
		{
		case BinaryOp::ADD:
			return arithmetic(numeric::Op::ADD);
		case BinaryOp::SUBTRACT:
			return arithmetic(numeric::Op::SUBTRACT);
		case BinaryOp::MULTIPLY:
			return arithmetic(numeric::Op::MULTIPLY);
		case BinaryOp::DIVIDE:
			return arithmetic(numeric::Op::DIVIDE);
		case BinaryOp::MODULO:
			return arithmetic(numeric::Op::MODULO);
		case BinaryOp::LESS:
			return Ok(Value::Boolean(order == -1));
		case BinaryOp::LESS_EQUAL:
			return Ok(Value::Boolean(order == -1 || order == 0));
		case BinaryOp::GREATER:
			return Ok(Value::Boolean(order == 1));
		case BinaryOp::GREATER_EQUAL:
			return Ok(Value::Boolean(order == 1 || order == 0));
		default:
			break;
		}
		return Err(ScriptError(String("opérateur binaire inconnu"), line, column));
	}

	const char *Interpreter::BinaryOpName(BinaryOp op) noexcept
	{
		switch (op)
		{
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
		case BinaryOp::IS:
			return "is";
		case BinaryOp::AS:
			return "as";
		case BinaryOp::FLOW_RIGHT:
			return "->";
		case BinaryOp::FLOW_LEFT:
			return "<-";
		case BinaryOp::FLOW_BOTH:
			return "<->";
		}
		return "?";
	}

	ScriptError Interpreter::DeadZoneError(const String &name, int line, int column)
	{
		return ScriptError(
			String::Format("`%s` est utilisée avant sa déclaration (zone morte d'un `let`/`const`)", name.CStr()), line,
			column);
	}

	ScriptError Interpreter::ReadOnlyNamespaceError(const Value &ns, const String &member, int line, int column)
	{
		return ScriptError(String::Format("`%s.%s` : les membres d'un espace de noms se modifient depuis son bloc "
										  "`namespace`, pas du dehors",
										  ns.AsNamespace() ? ns.AsNamespace()->name.CStr() : "?", member.CStr()),
						   line, column);
	}

	Result<Value, ScriptError> Interpreter::ReadNamespaceMember(const Value &ns, const String &member, int line, int column)
	{
		const std::shared_ptr<NamespaceObject> &space = ns.AsNamespace();
		Option<BindingInfo> binding = space && space->scope ? space->scope->Local(member) : NONE;
		if (binding.IsNone())
			return Err(ScriptError(String::Format("l'espace de noms `%s` n'a pas de membre `%s`",
												  space ? space->name.CStr() : "?", member.CStr()),
								   line, column));
		if (!binding.Value().initialized)
			return Err(DeadZoneError(member, line, column));
		return Ok(std::move(binding).Unwrap().value);
	}

	void Interpreter::CollectVars(const std::vector<StmtPtr> &statements, std::vector<String> &out)
	{
		for (const StmtPtr &stmt : statements)
			if (stmt)
				CollectVars(*stmt, out);
	}

	void Interpreter::CollectVars(const Stmt &stmt, std::vector<String> &out)
	{
		switch (stmt.kind)
		{
		case StmtKind::LET:
		{
			const auto &s = static_cast<const LetStmt &>(stmt);
			if (s.declKind == DeclKind::VAR)
				out.push_back(s.name);
			break;
		}
		case StmtKind::BLOCK:
			CollectVars(static_cast<const BlockStmt &>(stmt).statements, out);
			break;
		case StmtKind::IF:
		{
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

	Option<ScriptError> Interpreter::Hoist(const std::vector<StmtPtr> &statements,
										   const std::shared_ptr<Environment> &env, bool functionBody,
										   const FunctionDef *def)
	{
		for (const StmtPtr &stmt : statements)
		{
			if (!stmt)
				continue;
			if (stmt->kind == StmtKind::LET)
			{
				const auto &s = static_cast<const LetStmt &>(*stmt);
				if (s.declKind == DeclKind::VAR)
					continue;
				if (Option<BindingInfo> existing = env->Local(s.name); existing.IsSome() && existing.Value().host)
					return Some(ScriptError(String::Format("`%s` est un nom réservé par l'application : "
														   "choisissez un autre nom",
														   s.name.CStr()),
											s.line, s.column));
				env->Declare(s.name, s.declKind == DeclKind::CONST ? BindingKind::CONST : BindingKind::LET, false);
			}
			else if (stmt->kind == StmtKind::FUNCTION)
			{
				const auto &s = static_cast<const FunctionStmt &>(*stmt);
				if (Option<BindingInfo> existing = env->Local(s.def->name);
					existing.IsSome() && existing.Value().host && existing.Value().kind == BindingKind::CONST)
					return Some(ScriptError(String::Format("`%s` est un nom réservé par l'application", s.def->name.CStr()),
											s.line, s.column));
				auto fn = std::make_shared<FunctionObject>();
				fn->def = s.def;
				fn->closure = env;
				env->Put(s.def->name, BindingKind::FUNCTION, Value::Function(std::move(fn)));
			}
			else if (stmt->kind == StmtKind::CLASS)
			{
				// Comme un `const` : en zone morte jusqu'à sa déclaration.
				const auto &s = static_cast<const ClassStmt &>(*stmt);
				if (Option<BindingInfo> existing = env->Local(s.def->name); existing.IsSome() && existing.Value().host)
					return Some(ScriptError(String::Format("`%s` est un nom réservé par l'application", s.def->name.CStr()),
											s.line, s.column));
				env->Declare(s.def->name, BindingKind::CONST, false);
			}
			else if (stmt->kind == StmtKind::NAMESPACE)
			{
				const auto &s = static_cast<const NamespaceStmt &>(*stmt);
				Option<BindingInfo> existing = env->Local(s.name);
				const bool isNamespace = existing.IsSome() && existing.Value().value.IsNamespace();
				if (existing.IsSome() && existing.Value().host && !isNamespace)
					return Some(ScriptError(String::Format("`%s` est un nom réservé par l'application", s.name.CStr()),
											s.line, s.column));
				// Un espace de noms déjà ouvert se ROUVRE (et s'étend) : on ne le
				// remet pas en zone morte.
				if (!isNamespace)
					env->Declare(s.name, BindingKind::CONST, false);
			}
		}
		if (!functionBody)
			return NONE;

		std::vector<String> localVars;
		const std::vector<String> *vars = &localVars;
		if (def)
		{
			// Calculée une seule fois, même si plusieurs fils appellent la
			// fonction pour la première fois ensemble.
			std::call_once(def->hoistOnce, [&]
						   { CollectVars(statements, def->hoistedVars); });
			vars = &def->hoistedVars;
		}
		else
		{
			CollectVars(statements, localVars);
		}
		for (const String &name : *vars)
			if (env->Local(name).IsNone())
				env->Declare(name, BindingKind::VAR, true); // initialisé à nil
		return NONE;
	}

	Result<ExecOutcome, ScriptError> Interpreter::ExecDeclaration(const LetStmt &s, const std::shared_ptr<Environment> &env)
	{
		Value initial = Value::Nil();
		if (s.initializer)
		{
			auto value = Eval(*s.initializer, env);
			if (value.IsError())
				return Err(value.Error());
			initial = value.Unwrap();
			if (auto bad = CheckTyped("la variable", s.name, initial, s.type, env, s.line, s.column); bad.IsSome())
				return Err(bad.Unwrap());
		}
		else if (s.type)
		{
			initial = DefaultFor(*s.type);
		}
		if (s.declKind == DeclKind::VAR)
		{
			// Vers la portée de FONCTION la plus proche (déjà hissé là).
			Environment *target = env->FunctionScope();
			// Redéclarer un `var` est permis ; sans initialiseur, il garde sa valeur.
			Option<BindingKind> conflict = target->DeclareVar(
				s.name, (s.initializer || s.type) ? Some(std::move(initial)) : Option<Value>(NONE), s.type);
			if (conflict.IsSome())
				return Err(
					ScriptError(String::Format("`var %s` : le nom est déjà déclaré par `%s` dans cette fonction",
											   s.name.CStr(), conflict.Value() == BindingKind::CONST ? "const" : "let"),
								s.line, s.column));
			return Ok(ExecOutcome{});
		}
		const BindingKind kind = s.declKind == DeclKind::CONST ? BindingKind::CONST : BindingKind::LET;
		if (!env->InitializeLexical(s.name, kind, std::move(initial), s.type))
			return Err(
				ScriptError(String::Format("`%s` est déjà déclarée dans cette portée", s.name.CStr()), s.line, s.column));
		return Ok(ExecOutcome{});
	}

	Result<ExecOutcome, ScriptError> Interpreter::ExecNamespace(const NamespaceStmt &s,
																const std::shared_ptr<Environment> &env)
	{
		std::shared_ptr<NamespaceObject> space;
		if (Option<BindingInfo> binding = env->Local(s.name);
			binding.IsSome() && binding.Value().initialized && binding.Value().value.IsNamespace())
			space = binding.Value().value.AsNamespace();
		if (!space)
		{
			space = std::make_shared<NamespaceObject>();
			space->name = s.name;
			space->scope = MakeEnvironment(env, true);
			env->Put(s.name, BindingKind::CONST, Value::Namespace(space));
		}
		if (auto hoisted = Hoist(s.body, space->scope, true, nullptr); hoisted.IsSome())
			return Err(hoisted.Unwrap());
		for (const StmtPtr &child : s.body)
		{
			if (!child)
				continue;
			auto outcome = ExecStmt(*child, space->scope);
			if (outcome.IsError())
				return outcome;
			if (outcome.Value().flow != Flow::NORMAL)
				return Err(ScriptError(String("`return`/`break`/`continue` interdit dans un espace de noms"), child->line,
									   child->column));
		}
		return Ok(ExecOutcome{});
	}

	String Interpreter::DescribeForType(const Value &value)
	{
		if (value.IsNumber())
			return String::Format("le nombre %s", value.ToDisplayString().CStr());
		if (value.IsInstance() && value.AsInstance())
			return String::Format("un objet `%s`", value.AsInstance()->klass->Name().CStr());
		if (value.IsString())
			return String("une chaîne");
		return String::Format("une valeur `%s`", value.TypeName());
	}

	Option<String> Interpreter::TypeMismatch(const Value &value, const TypeExpr &type,
											 const std::shared_ptr<Environment> &env, int depth)
	{
		if (value.IsNil() && type.nullable)
			return NONE;
		auto refuse = [&](const char *expected)
		{
			return Some(String::Format("attendu %s, reçu %s", expected, DescribeForType(value).CStr()));
		};
		if (type.path.size() == 1)
		{
			const String &name = type.path[0];
			if (name == "any")
				return NONE;
			if (name == "nil" || name == "void")
				return value.IsNil() ? Option<String>(NONE) : refuse("nil");
			if (Option<NumberType> numberType = NumberTypeFromName(name.View()); numberType.IsSome())
			{
				if (!value.IsNumber())
					return refuse(name.CStr());
				auto converted = numeric::ConvertExact(value, numberType.Value());
				if (converted.IsError())
					return Some(converted.Error());
				return NONE;
			}
			if (name == "num")
				return value.IsNumber() ? Option<String>(NONE) : refuse("num");
			if (name == "bool")
				return value.IsBoolean() ? Option<String>(NONE) : refuse("bool");
			if (name == "string" || name == "str")
				return value.IsString() ? Option<String>(NONE) : refuse("string");
			if (name == "fn")
				return value.IsCallable() || value.IsClass() ? Option<String>(NONE) : refuse("une fonction");
			if (name == "future")
				return value.IsFuture() ? Option<String>(NONE) : refuse("future");
			if (name == "list")
			{
				if (!value.IsList() || !value.AsList())
					return refuse("list");
				if (type.args.empty() || !type.args[0] || depth > 16)
					return NONE;
				const std::vector<Value> items = value.AsList()->Snapshot();
				for (size_t i = 0; i < items.size(); ++i)
					if (Option<String> bad = TypeMismatch(items[i], *type.args[0], env, depth + 1); bad.IsSome())
						return Some(String::Format("élément %d : %s", int(i), bad.Value().CStr()));
				return NONE;
			}
			if (name == "map")
			{
				if (!value.IsMap() || !value.AsMap())
					return refuse("map");
				// map<V> ou map<K, V> (les clés sont toujours des chaînes).
				const TypeRef &valueType = type.args.empty() ? TypeRef() : type.args.back();
				if (!valueType || depth > 16)
					return NONE;
				for (const auto &entry : value.AsMap()->Snapshot())
					if (Option<String> bad = TypeMismatch(entry.second, *valueType, env, depth + 1); bad.IsSome())
						return Some(String::Format("clé `%s` : %s", entry.first.CStr(), bad.Value().CStr()));
				return NONE;
			}
		}
		// Un paramètre de type (`T`), une classe ou une interface, un type de
		// l'hôte (`std.vector`).
		auto resolved = ResolveTypeValue(type.path, env, type.line, type.column);
		if (resolved.IsError())
			return Some(String::Format("type inconnu `%s`", type.ToString().CStr()));
		const Value &target = resolved.Value();
		if (target.IsType() && target.AsType() && type.path.size() == 1)
			return CheckTypeParam(value, *target.AsType(), type.nullable, depth);
		if (target.IsNative() && target.AsNative() && target.AsNative()->hostType)
		{
			const std::shared_ptr<const HostType> &host = target.AsNative()->hostType;
			if (!value.IsHost() || !value.AsHost() || value.AsHost()->type != host)
				return Some(String::Format("attendu un `%s`, reçu %s", host->name.CStr(), DescribeForType(value).CStr()));
			return TypeArgsMismatch(value.AsHost()->typeArgs, type, env);
		}
		if (!target.IsClass())
			return Some(String::Format("`%s` n'est pas un type (c'est `%s`)", type.ToString().CStr(), target.TypeName()));
		const std::shared_ptr<ClassObject> &klass = target.AsClass();
		if (value.IsInstance() && value.AsInstance() && value.AsInstance()->klass->IsSubtypeOf(*klass))
		{
			if (type.args.empty())
				return NONE;
			const std::vector<Value> *args = value.AsInstance()->TypeArgsOf(klass.get());
			return args ? TypeArgsMismatch(*args, type, env) : Option<String>(NONE);
		}
		return Some(
			String::Format("attendu un objet `%s`, reçu %s", type.ToString().CStr(), DescribeForType(value).CStr()));
	}

	Value Interpreter::FreshTypeParam(const TypeParam &param, const std::shared_ptr<Environment> &scope)
	{
		auto type = std::make_shared<TypeObject>();
		type->name = param.name;
		type->bound = param.bound;
		type->boundScope = scope;
		return Value::Type(std::move(type));
	}

	Value Interpreter::MakeTypeValue(const TypeRef &type, const std::shared_ptr<Environment> &scope, const String &name)
	{
		if (type && type->path.size() == 1 && type->args.empty() && !type->nullable)
			if (Option<Value> existing = scope->Find(type->path[0]); existing.IsSome() && existing.Value().IsType())
				return existing.Unwrap();
		auto made = std::make_shared<TypeObject>();
		made->name = name;
		made->type = type;
		made->scope = scope;
		return Value::Type(std::move(made));
	}

	String Interpreter::BoundTypeName(const TypeObject &type)
	{
		std::lock_guard<std::mutex> lock(type.mutex);
		if (type.type)
			return type.type->ToString();
		if (type.klass)
			return type.klass->Name();
		if (type.hostType)
			return type.hostType->name;
		return String();
	}

	Option<String> Interpreter::TypeArgsMismatch(const std::vector<Value> &actual, const TypeExpr &type,
												 const std::shared_ptr<Environment> &env)
	{
		for (size_t i = 0; i < type.args.size() && i < actual.size(); ++i)
		{
			if (!type.args[i] || !actual[i].IsType() || !actual[i].AsType())
				continue;
			const String have = BoundTypeName(*actual[i].AsType());
			if (have.IsEmpty())
				continue;
			String want = type.args[i]->ToString();
			if (type.args[i]->path.size() == 1)
				if (Option<Value> param = env->Find(type.args[i]->path[0]); param.IsSome() && param.Value().IsType())
				{
					want = BoundTypeName(*param.Value().AsType());
					if (want.IsEmpty())
						continue;
				}
			if (want != have)
				return Some(String::Format("attendu `%s`, reçu un `%s` dont l'argument de type %d est `%s`",
										   type.ToString().CStr(), JoinTypePath(type.path).CStr(), int(i + 1),
										   have.CStr()));
		}
		return NONE;
	}

	Option<String> Interpreter::CheckTypeParam(const Value &value, TypeObject &param, bool nullable, int depth)
	{
		if (value.IsNil() && nullable)
			return NONE;
		TypeRef bound, fixed;
		std::shared_ptr<Environment> boundScope, scope;
		std::shared_ptr<ClassObject> klass;
		std::shared_ptr<const HostType> hostType;
		{
			std::lock_guard<std::mutex> lock(param.mutex);
			bound = param.bound;
			boundScope = param.boundScope;
			fixed = param.type;
			scope = param.scope;
			klass = param.klass;
			hostType = param.hostType;
		}
		if (bound && boundScope)
			if (Option<String> bad = TypeMismatch(value, *bound, boundScope, depth + 1); bad.IsSome())
				return Some(String::Format("`%s extends %s` : %s", param.name.CStr(), bound->ToString().CStr(),
										   bad.Value().CStr()));
		if (fixed && scope)
			return TypeMismatch(value, *fixed, scope, depth + 1);
		if (klass)
		{
			if (value.IsInstance() && value.AsInstance() && value.AsInstance()->klass->IsSubtypeOf(*klass))
				return NONE;
			return Some(String::Format("attendu un objet `%s` (%s), reçu %s", klass->Name().CStr(), param.name.CStr(),
									   DescribeForType(value).CStr()));
		}
		if (hostType)
		{
			if (value.IsHost() && value.AsHost() && value.AsHost()->type == hostType)
				return NONE;
			return Some(String::Format("attendu un `%s` (%s), reçu %s", hostType->name.CStr(), param.name.CStr(),
									   DescribeForType(value).CStr()));
		}
		// Libre : la valeur le fixe (nil ne fixe rien).
		if (value.IsNil())
			return NONE;
		std::lock_guard<std::mutex> lock(param.mutex);
		if (param.type || param.klass || param.hostType)
			return NONE; // fixé entre-temps par un autre fil : accepté (course bénigne)
		if (value.IsInstance() && value.AsInstance())
		{
			param.klass = value.AsInstance()->klass;
		}
		else if (value.IsHost() && value.AsHost())
		{
			param.hostType = value.AsHost()->type;
		}
		else
		{
			auto inferred = std::make_shared<TypeExpr>();
			inferred->path.push_back(String(value.IsCallable() || value.IsClass() ? "fn" : value.TypeName()));
			param.type = std::move(inferred);
			param.scope = m_globals;
		}
		return NONE;
	}

	Value Interpreter::DefaultFor(const TypeExpr &type)
	{
		if (type.nullable || type.path.size() != 1)
			return Value::Nil();
		const String &name = type.path[0];
		if (Option<NumberType> numberType = NumberTypeFromName(name.View()); numberType.IsSome())
			return IsIntegerType(numberType.Value()) ? numeric::ConvertExact(Value::Int(0), numberType.Value()).Unwrap()
													 : Value::Float(0.0, numberType.Value());
		if (name == "num")
			return Value::Int(0);
		if (name == "bool")
			return Value::Boolean(false);
		if (name == "string" || name == "str")
			return Value::Str(String());
		if (name == "list")
			return Value::EmptyList();
		if (name == "map")
			return Value::EmptyMap();
		return Value::Nil();
	}

	void Interpreter::CoerceNumber(Value &value, const TypeExpr &type, const std::shared_ptr<Environment> &env, int depth)
	{
		if (!value.IsNumber() || type.path.size() != 1 || depth > 8)
			return;
		if (Option<NumberType> numberType = NumberTypeFromName(type.path[0].View()); numberType.IsSome())
		{
			if (auto converted = numeric::ConvertExact(value, numberType.Value()); converted.IsOk())
				value = std::move(converted).Unwrap();
			return;
		}
		Option<Value> param = env ? env->Find(type.path[0]) : Option<Value>(NONE);
		if (param.IsNone() || !param.Value().IsType() || !param.Value().AsType())
			return;
		const std::shared_ptr<TypeObject> &bound = param.Value().AsType();
		TypeRef fixed;
		std::shared_ptr<Environment> scope;
		{
			std::lock_guard<std::mutex> lock(bound->mutex);
			fixed = bound->type;
			scope = bound->scope;
		}
		if (fixed)
			CoerceNumber(value, *fixed, scope, depth + 1);
	}

	Result<Value, ScriptError> Interpreter::Conform(const Value &typeValue, Value value)
	{
		if (!typeValue.IsType() || !typeValue.AsType())
			return Ok(std::move(value));
		TypeObject &param = *typeValue.AsType();
		if (Option<String> bad = CheckTypeParam(value, param, false, 0); bad.IsSome())
			return Err(MakeError(bad.Unwrap()));
		TypeRef fixed;
		std::shared_ptr<Environment> scope;
		{
			std::lock_guard<std::mutex> lock(param.mutex);
			fixed = param.type;
			scope = param.scope;
		}
		if (fixed)
			CoerceNumber(value, *fixed, scope);
		return Ok(std::move(value));
	}

	std::vector<Value> Interpreter::MakeTypeValues(const std::vector<TypeRef> &types,
												   const std::shared_ptr<Environment> &env)
	{
		std::vector<Value> out;
		out.reserve(types.size());
		for (const TypeRef &type : types)
			out.push_back(MakeTypeValue(type, env, type ? type->ToString() : String("?")));
		return out;
	}

	Option<ScriptError> Interpreter::CheckTyped(const char *what, const String &name, Value &value,
												const TypeRef &type, const std::shared_ptr<Environment> &env, int line,
												int column)
	{
		if (!type)
			return NONE;
		Option<String> bad = TypeMismatch(value, *type, env);
		if (bad.IsNone())
		{
			CoerceNumber(value, *type, env);
			return NONE;
		}
		return Some(ScriptError(
			String::Format("%s `%s` est de type `%s` : %s", what, name.CStr(), type->ToString().CStr(), bad.Value().CStr()),
			line, column));
	}

	std::shared_ptr<Environment> Interpreter::BindEnvironment(const Value &self, const std::shared_ptr<ClassObject> &owner)
	{
		auto env = MakeEnvironment(owner->closure ? owner->closure : m_globals);
		env->Put(String(THIS_NAME), BindingKind::CONST, self);
		env->Put(String(OWNER_NAME), BindingKind::CONST, Value::Class(owner));
		BindClassTypeParams(env, self, owner);
		return env;
	}

	void Interpreter::BindClassTypeParams(const std::shared_ptr<Environment> &env, const Value &self,
										  const std::shared_ptr<ClassObject> &owner)
	{
		const std::vector<TypeParam> &params = owner->def->typeParams;
		if (params.empty())
			return;
		const std::vector<Value> *args =
			self.IsInstance() && self.AsInstance() ? self.AsInstance()->TypeArgsOf(owner.get()) : nullptr;
		for (size_t i = 0; i < params.size(); ++i)
			env->Put(params[i].name, BindingKind::CONST,
					 args && i < args->size() ? (*args)[i] : FreshTypeParam(params[i], owner->closure));
	}

	Value Interpreter::BindMethod(const Value &self, const std::shared_ptr<ClassObject> &owner, const FunctionDefPtr &def)
	{
		auto fn = std::make_shared<FunctionObject>();
		fn->def = def;
		if (!self.IsNil())
		{
			fn->closure = BindEnvironment(self, owner);
		}
		else if (!owner->def->typeParams.empty())
		{
			fn->closure = MakeEnvironment(owner->closure ? owner->closure : m_globals);
			BindClassTypeParams(fn->closure, self, owner);
		}
		else
		{
			fn->closure = owner->closure ? owner->closure : m_globals;
		}
		return Value::Function(std::move(fn));
	}

	std::shared_ptr<Environment> Interpreter::InstanceTypeScope(const std::shared_ptr<InstanceObject> &instance)
	{
		const std::shared_ptr<ClassObject> &klass = instance->klass;
		if (instance->typeArgs.empty())
			return klass->closure ? klass->closure : m_globals;
		auto env = MakeEnvironment(klass->closure ? klass->closure : m_globals);
		for (auto it = instance->typeArgs.rbegin(); it != instance->typeArgs.rend(); ++it)
		{
			const std::vector<TypeParam> &params = it->first->def->typeParams;
			for (size_t i = 0; i < params.size() && i < it->second.size(); ++i)
				env->Put(params[i].name, BindingKind::CONST, it->second[i]);
		}
		return env;
	}

	std::pair<const MethodDef *, std::shared_ptr<ClassObject>> Interpreter::FindMethod(std::shared_ptr<ClassObject> from, const String &name, bool wantStatic)
	{
		for (; from; from = from->superclass)
			if (const MethodDef *method = from->def->FindMethod(name, wantStatic))
				return {method, from};
		return {nullptr, nullptr};
	}

	Result<Value, ScriptError> Interpreter::ReadInstanceMember(const Value &object, const String &name, int line, int column)
	{
		const std::shared_ptr<InstanceObject> &instance = object.AsInstance();
		if (Option<Value> field = instance->fields.Get(name); field.IsSome())
			return Ok(std::move(field).Unwrap());
		auto [method, owner] = FindMethod(instance->klass, name, false);
		if (method)
		{
			if (method->isAbstract)
				return Err(
					ScriptError(String::Format("`%s.%s` est abstraite", owner->Name().CStr(), name.CStr()), line, column));
			return Ok(BindMethod(object, owner, method->def));
		}
		// Bases de l'hôte : leurs méthodes et propriétés, après celles du
		// script (une classe de script peut donc en REDÉFINIR une).
		if (!instance->owners.empty())
		{
			if (std::shared_ptr<OwnerObject> base = OwnerWithMethod(*instance, name))
				return ReadHostMember(base, name, line, column);
			if (Option<Value> property = ReadOwnerProperty(*instance, name); property.IsSome())
				return Ok(std::move(property).Unwrap());
		}
		// `destroy` intrinsèque : présente dès qu'une base de l'hôte est
		// déclarée (sans qu'aucune classe n'ait à l'écrire).
		if (name == "destroy" && IsOwned(*instance))
		{
			std::weak_ptr<InstanceObject> weak = instance;
			return Ok(Value::Native(MakeNative(
				String::Format("%s.destroy", instance->klass->Name().CStr()), 0, 0,
				[weak](Interpreter &vm, std::vector<Value> &) -> Result<Value, ScriptError>
				{
					std::shared_ptr<InstanceObject> alive = weak.lock();
					const bool first = alive && !alive->destroyed;
					if (alive)
						vm.DestroyInstance(alive);
					return Ok(Value::Boolean(first));
				},
				NativeThread::MAIN)));
		}
		if (name == "is_destroyed" && IsOwned(*instance))
		{
			std::weak_ptr<InstanceObject> weak = instance;
			return Ok(Value::Native(MakeNative(
				String::Format("%s.is_destroyed", instance->klass->Name().CStr()), 0, 0,
				[weak](Interpreter &, std::vector<Value> &) -> Result<Value, ScriptError>
				{
					std::shared_ptr<InstanceObject> alive = weak.lock();
					return Ok(Value::Boolean(!alive || alive->destroyed));
				},
				NativeThread::ANY)));
		}
		return Err(
			ScriptError(String::Format("`%s` n'a ni champ ni méthode `%s`", instance->klass->Name().CStr(), name.CStr()),
						line, column));
	}

	Result<Value, ScriptError> Interpreter::ReadClassMember(const std::shared_ptr<ClassObject> &klass,
															const String &name, int line, int column)
	{
		for (std::shared_ptr<ClassObject> cls = klass; cls; cls = cls->superclass)
			if (Option<Value> field = cls->statics.Get(name); field.IsSome())
				return Ok(std::move(field).Unwrap());
		auto [method, owner] = FindMethod(klass, name, true);
		if (method && method->isFactory && owner != klass)
			method = nullptr; // un constructeur nommé ne s'hérite pas (il rendrait le mauvais type)
		if (method)
		{
			Value fn = BindMethod(Value::Nil(), owner, method->def);
			if (!method->isFactory)
				return Ok(fn);
			// `factory` : l'appel DOIT rendre une instance de la classe.
			auto native = std::make_shared<NativeObject>();
			native->name = String::Format("%s.%s", klass->Name().CStr(), name.CStr());
			native->minArity = native->maxArity = int(method->def->params.size());
			std::shared_ptr<ClassObject> expected = klass;
			native->fn = [fn, expected, line, column](Interpreter &vm,
													  std::vector<Value> &args) -> Result<Value, ScriptError>
			{
				auto result = vm.CallValue(fn, std::move(args), line, column);
				if (result.IsError())
					return result;
				const Value &made = result.Value();
				if (!made.IsInstance() || !made.AsInstance()->klass->IsSubtypeOf(*expected))
					return Err(ScriptError(String::Format("le constructeur `factory` `%s.%s` doit rendre une instance "
														  "de `%s`, pas `%s`",
														  expected->Name().CStr(), fn.AsFunction()->def->name.CStr(),
														  expected->Name().CStr(), made.TypeName()),
										   line, column));
				return result;
			};
			return Ok(Value::Native(std::move(native)));
		}
		if (name == "name")
			return Ok(Value::Str(klass->Name()));
		if (klass->def->isEnum && (name == "values" || name == "from" || name == "from_name"))
		{
			// `Couleur.values()`, `Couleur.from(5)`, `Couleur.from_name("ROUGE")`
			// (nil si aucune valeur ne correspond).
			auto native = std::make_shared<NativeObject>();
			native->name = String::Format("%s.%s", klass->Name().CStr(), name.CStr());
			native->anyThread = true;
			native->minArity = native->maxArity = name == "values" ? 0 : 1;
			const std::vector<Value> values = klass->enumValues;
			const String member = name == "from" ? String("value") : String("name");
			if (name == "values")
				native->fn = [values](Interpreter &, std::vector<Value> &) -> Result<Value, ScriptError>
				{
					return Ok(Value::List(std::make_shared<ListObject>(values)));
				};
			else
				native->fn = [values, member](Interpreter &, std::vector<Value> &args) -> Result<Value, ScriptError>
				{
					for (const Value &item : values)
					{
						Option<Value> field = item.AsInstance()->fields.Get(member);
						if (field.IsSome() && field.Value().Equals(args[0]))
							return Ok(item);
					}
					return Ok(Value::Nil());
				};
			return Ok(Value::Native(std::move(native)));
		}
		return Err(ScriptError(String::Format("`%s` n'a pas de membre de classe `%s` (les membres `static` se lisent "
											  "sur la classe, les autres sur une instance)",
											  klass->Name().CStr(), name.CStr()),
							   line, column));
	}

	Result<Value, ScriptError> Interpreter::ReadFutureMember(const Value &object, const String &name, int line, int column)
	{
		const std::shared_ptr<FutureObject> future = object.AsFuture();
		if (!future)
			return Err(ScriptError(String("Future invalide"), line, column));
		auto state = [&]
		{ return future->GetState(); };
		if (name == "done")
			return Ok(Value::Boolean(state() != FutureObject::State::PENDING));
		if (name == "failed")
			return Ok(Value::Boolean(state() == FutureObject::State::FAILED));
		if (name == "value" || name == "error")
		{
			std::lock_guard<std::mutex> lock(future->mutex);
			if (name == "value")
				return Ok(future->state == FutureObject::State::DONE ? future->value : Value::Nil());
			if (future->state != FutureObject::State::FAILED)
				return Ok(Value::Nil());
			future->observed = true; // lue : l'erreur n'est plus orpheline
			return Ok(Value::Str(future->error.message));
		}
		auto native = std::make_shared<NativeObject>();
		native->anyThread = true;
		if (name == "wait")
		{
			native->name = String("future.wait");
			native->minArity = native->maxArity = 0;
			native->fn = [future](Interpreter &vm, std::vector<Value> &) -> Result<Value, ScriptError>
			{
				return vm.AwaitValue(Value::Future(future));
			};
			return Ok(Value::Native(std::move(native)));
		}
		if (name == "then")
		{
			native->name = String("future.then");
			native->minArity = native->maxArity = 1;
			native->fn = [future](Interpreter &vm, std::vector<Value> &args) -> Result<Value, ScriptError>
			{
				Value callback = args[0];
				if (!callback.IsCallable())
					return Err(MakeError(String::Format("`then` attend une fonction, trouvé `%s`", callback.TypeName())));
				return vm.SpawnTask([&vm, future, callback]() -> Result<Value, ScriptError>
									{
										auto result = vm.AwaitValue(Value::Future(future));
										if (result.IsError())
											return result;
										auto next = vm.CallValue(callback, {result.Value()}, 0, 0);
										if (next.IsError())
											return next;
										return vm.AwaitValue(next.Value()); // un rappel `async` s'aplatit (comme Dart)
									});
			};
			return Ok(Value::Native(std::move(native)));
		}
		return Err(ScriptError(String::Format("un Future n'a pas de membre `%s` (done, failed, value, error, then, "
											  "wait)",
											  name.CStr()),
							   line, column));
	}

	Option<ScriptError> Interpreter::AssignObjectMember(const Value &object, const String &name, Value value, int line,
														int column)
	{
		auto constantError = [&](const String &owner)
		{
			return Some(
				ScriptError(String::Format("`%s.%s` est constant : réaffectation impossible", owner.CStr(), name.CStr()),
							line, column));
		};
		if (object.IsInstance())
		{
			const std::shared_ptr<InstanceObject> &instance = object.AsInstance();
			if (TypeRef fieldType = instance->fields.TypeOf(name))
				if (auto bad = CheckTyped("le champ", name, value, fieldType, InstanceTypeScope(instance), line, column);
					bad.IsSome())
					return bad;
			// Propriété modifiable d'une base de l'hôte (`this.visible = false`),
			// si aucun champ du script ne porte ce nom.
			if (!instance->fields.Has(name))
				for (const std::shared_ptr<OwnerObject> &base : instance->owners)
				{
					if (!base || !base->type->set || !base->initialized || base->destroyed)
						continue;
					auto written = base->type->set(*this, base, name, value);
					if (written.IsError())
						return Some(ScriptError(written.Error().message, line, column));
					if (written.Value())
						return NONE;
				}
			const bool isMethod = !instance->fields.Has(name) && FindMethod(instance->klass, name, false).first;
			if (isMethod)
				return Some(ScriptError(String::Format("`%s` est une méthode de `%s` : on ne l'écrase pas par un "
													   "champ",
													   name.CStr(), instance->klass->Name().CStr()),
										line, column));
			if (instance->fields.Set(name, std::move(value), true) == FieldSet::WriteResult::CONSTANT)
				return constantError(instance->klass->Name());
			return NONE;
		}
		const std::shared_ptr<ClassObject> &klass = object.AsClass();
		for (std::shared_ptr<ClassObject> cls = klass; cls; cls = cls->superclass)
		{
			if (auto bad = CheckTyped("le champ", name, value, cls->statics.TypeOf(name), cls->closure, line, column);
				bad.IsSome())
				return bad;
			switch (cls->statics.Set(name, value, false))
			{
			case FieldSet::WriteResult::CONSTANT:
				return constantError(cls->Name());
			case FieldSet::WriteResult::MISSING:
				continue;
			default:
				return NONE;
			}
		}
		(void)klass->statics.Set(name, std::move(value), true);
		return NONE;
	}

	Result<Value, ScriptError> Interpreter::EvalSuper(const SuperExpr &expr, const std::shared_ptr<Environment> &env)
	{
		Option<Value> self = env->Find(String(THIS_NAME));
		Option<Value> owner = env->Find(String(OWNER_NAME));
		if (self.IsNone() || owner.IsNone() || !owner.Value().IsClass() || !self.Value().IsInstance())
			return Err(ScriptError(String("`super` hors d'une méthode d'instance"), expr.line, expr.column));
		const std::shared_ptr<ClassObject> current = owner.Value().AsClass();
		const std::shared_ptr<InstanceObject> instance = self.Value().AsInstance();

		// `super.init(…)` : construit TOUTES les bases de `current` — la classe
		// parente de script d'abord (qui chaîne elle-même les siennes), puis
		// les bases de l'hôte que `current` déclare, dans l'ordre de `extends`.
		// Les mêmes arguments vont à chacune.
		if (expr.method == "init")
		{
			const auto parentInit = current->superclass ? FindMethod(current->superclass, String("init"), false)
														: std::pair<const MethodDef *, std::shared_ptr<ClassObject>>{};
			if (!parentInit.first && current->owners.empty())
				return Ok(Value::Native(MakeNative(
					String("super.init"), 0, 0,
					[](Interpreter &, std::vector<Value> &) -> Result<Value, ScriptError> { return Ok(Value::Nil()); },
					NativeThread::ANY)));
			std::weak_ptr<InstanceObject> weak = instance;
			std::shared_ptr<ClassObject> level = current;
			const int line = expr.line, column = expr.column;
			return Ok(Value::Native(MakeNative(
				String("super.init"), 0, -1,
				[weak, level, line, column](Interpreter &vm, std::vector<Value> &args) -> Result<Value, ScriptError>
				{
					std::shared_ptr<InstanceObject> alive = weak.lock();
					if (!alive)
						return Ok(Value::Nil());
					if (level->superclass)
					{
						auto [method, definer] = vm.FindMethod(level->superclass, String("init"), false);
						if (method && !method->isAbstract)
						{
							std::vector<Value> copy = args;
							auto result = vm.CallValue(vm.BindMethod(Value::Instance(alive), definer, method->def),
													   std::move(copy), line, column);
							if (result.IsError())
								return result;
						}
					}
					if (auto error = InitOwners(vm, alive, *level, args); error.IsSome())
					{
						ScriptError failure = error.Unwrap();
						if (failure.line == 0)
						{
							failure.line = line;
							failure.column = column;
						}
						return Err(std::move(failure));
					}
					return Ok(Value::Nil());
				},
				NativeThread::DEFAULT)));
		}

		// Méthode ordinaire : celle de l'ancêtre de script, sinon celle d'une
		// base de l'hôte (une classe qui REDÉFINIT `set_mesh` appelle ainsi
		// l'originale avec `super.set_mesh(…)`).
		auto [method, definer] = FindMethod(current->superclass, expr.method, false);
		if (method && !method->isAbstract)
			return Ok(BindMethod(self.Value(), definer, method->def));
		for (const auto &type : current->owners)
			if (type->FindMethod(expr.method))
				if (std::shared_ptr<OwnerObject> base = FindOwner(*instance, *type))
					return ReadHostMember(base, expr.method, expr.line, expr.column);
		return Err(ScriptError(String::Format("`super.%s` : méthode introuvable", expr.method.CStr()), expr.line,
							   expr.column));
	}

	Result<Value, ScriptError> Interpreter::Instantiate(const std::shared_ptr<ClassObject> &klass,
														std::vector<Value> args, int line, int column,
														std::vector<Value> typeArgs)
	{
		if (!klass)
			return Err(ScriptError(String("classe invalide"), line, column));
		if (klass->def->isEnum)
			return Err(ScriptError(String::Format("`%s` est un type énuméré : ses valeurs existent déjà (`%s.%s`)",
												  klass->Name().CStr(), klass->Name().CStr(),
												  klass->def->enumEntries.front().name.CStr()),
								   line, column));
		if (typeArgs.size() > klass->def->typeParams.size())
			return Err(ScriptError(String::Format("`%s` attend %d argument(s) de type, %d fourni(s)", klass->Name().CStr(),
												  int(klass->def->typeParams.size()), int(typeArgs.size())),
								   line, column));
		if (klass->IsInterface())
			return Err(ScriptError(String::Format("`%s` est une interface : on n'en crée pas d'instance — "
												  "instanciez une classe qui l'implémente",
												  klass->Name().CStr()),
								   line, column));
		if (klass->IsAbstract())
			return Err(ScriptError(
				String::Format("`%s` est abstraite : instanciez une classe dérivée", klass->Name().CStr()), line, column));
		auto instance = std::make_shared<InstanceObject>();
		instance->klass = klass;
		ResolveInstanceTypeArgs(*instance, std::move(typeArgs));
		AttachOwners(instance); // bases de l'hôte : créées, pas encore initialisées
		Value self = Value::Instance(instance);

		std::vector<std::shared_ptr<ClassObject>> chain;
		for (std::shared_ptr<ClassObject> cls = klass; cls; cls = cls->superclass)
			chain.insert(chain.begin(), cls);
		for (const std::shared_ptr<ClassObject> &cls : chain)
		{
			std::shared_ptr<Environment> fieldEnv;
			for (const FieldDef &field : cls->def->fields)
			{
				if (field.isStatic)
					continue;
				Value initial = field.type ? DefaultFor(*field.type) : Value::Nil();
				if (field.initializer)
				{
					if (!fieldEnv)
						fieldEnv = BindEnvironment(self, cls);
					auto value = Eval(*field.initializer, fieldEnv);
					if (value.IsError())
						return value;
					initial = value.Unwrap();
					if (auto bad =
							CheckTyped("le champ", field.name, initial, field.type,
									   cls->def->typeParams.empty() ? cls->closure : fieldEnv, field.line, field.column);
						bad.IsSome())
						return Err(bad.Unwrap());
				}
				// (Redéclaré par une classe dérivée : sa valeur remplace celle du parent.)
				instance->fields.Initialize(field.name, std::move(initial), field.declKind == DeclKind::CONST, field.type);
			}
		}

		auto [init, owner] = FindMethod(klass, String("init"), false);
		if (init)
		{
			auto result = CallValue(BindMethod(self, owner, init->def), std::move(args), line, column);
			if (result.IsError())
			{
				// Construction ratée : ce qui a pu être initialisé est libéré
				// (sans `on_destroy` ni `deinit` : l'objet n'est jamais né).
				instance->finalized = true;
				DeinitOwners(*this, instance);
				return result;
			}
		}
		else if (klass->owners.empty() && !args.empty())
		{
			return Err(ScriptError(String::Format("`%s` n'a pas de constructeur `init` : %d argument(s) inattendu(s)",
												  klass->Name().CStr(), int(args.size())),
								   line, column));
		}
		// Bases de l'hôte que `init` n'a pas construites (pas de `super.init`,
		// ou pas d'`init` du tout : les arguments leur reviennent alors) —
		// le constructeur par défaut des bases C++.
		if (!klass->owners.empty())
		{
			std::vector<Value> rest = init ? std::vector<Value>{} : std::move(args);
			if (auto error = InitOwners(*this, instance, *klass, rest); error.IsSome())
			{
				instance->finalized = true;
				DeinitOwners(*this, instance);
				ScriptError failure = error.Unwrap();
				if (failure.line == 0)
				{
					failure.line = line;
					failure.column = column;
				}
				return Err(std::move(failure));
			}
		}
		return Ok(self);
	}

	void Interpreter::ResolveInstanceTypeArgs(InstanceObject &instance, std::vector<Value> current)
	{
		for (std::shared_ptr<ClassObject> cls = instance.klass; cls; cls = cls->superclass)
		{
			const std::vector<TypeParam> &params = cls->def->typeParams;
			for (size_t i = current.size(); i < params.size(); ++i)
				current.push_back(FreshTypeParam(params[i], cls->closure));
			if (!params.empty())
				instance.typeArgs.emplace_back(cls.get(), current);
			std::vector<Value> next;
			if (cls->superclass && !cls->def->superTypeArgs.empty())
			{
				auto scope = MakeEnvironment(cls->closure ? cls->closure : m_globals);
				for (size_t i = 0; i < params.size(); ++i)
					scope->Put(params[i].name, BindingKind::CONST, current[i]);
				for (const TypeRef &arg : cls->def->superTypeArgs)
					next.push_back(MakeTypeValue(arg, scope, String()));
			}
			current = std::move(next);
		}
	}

	Result<std::shared_ptr<ClassObject>, ScriptError> Interpreter::ResolveTypePath(const TypePath &path, const std::shared_ptr<Environment> &env, int line, int column)
	{
		auto value = ResolveTypeValue(path, env, line, column);
		if (value.IsError())
			return Err(value.Error());
		if (!value.Value().IsClass())
			return Err(ScriptError(String::Format("`%s` n'est pas une classe ni une interface (c'est `%s`)",
												  JoinTypePath(path).CStr(), value.Value().TypeName()),
								   line, column));
		return Ok(value.Value().AsClass());
	}

	Result<Value, ScriptError> Interpreter::ResolveTypeValue(const TypePath &path, const std::shared_ptr<Environment> &env, int line, int column)
	{
		Option<BindingInfo> binding = env->Lookup(path.front());
		if (binding.IsNone())
			return Err(ScriptError(String::Format("type inconnu `%s`", JoinTypePath(path).CStr()), line, column));
		if (!binding.Value().initialized)
			return Err(DeadZoneError(path.front(), line, column));
		Value value = binding.Value().value;
		for (size_t i = 1; i < path.size(); ++i)
		{
			if (!value.IsNamespace())
				return Err(
					ScriptError(String::Format("`%s` n'est pas un espace de noms", path[i - 1].CStr()), line, column));
			auto member = ReadNamespaceMember(value, path[i], line, column);
			if (member.IsError())
				return Err(member.Error());
			value = member.Unwrap();
		}
		return Ok(value);
	}

	void Interpreter::CollectInterfaces(const std::shared_ptr<ClassObject> &klass,
										std::vector<std::shared_ptr<ClassObject>> &out)
	{
		for (std::shared_ptr<ClassObject> cls = klass; cls; cls = cls->superclass)
		{
			for (const auto &iface : cls->interfaces)
			{
				if (std::find(out.begin(), out.end(), iface) != out.end())
					continue;
				out.push_back(iface);
				CollectInterfaces(iface, out);
			}
		}
	}

	Result<ExecOutcome, ScriptError> Interpreter::ExecClass(const ClassStmt &stmt, const std::shared_ptr<Environment> &env)
	{
		const ClassDef &def = *stmt.def;
		auto fail = [&](String message)
		{
			return Result<ExecOutcome, ScriptError>(Err(ScriptError(std::move(message), stmt.line, stmt.column)));
		};

		auto klass = std::make_shared<ClassObject>();
		klass->def = stmt.def;
		klass->closure = env;
		klass->owner = this;
		klass->ownerAlive = m_alive;
		{
			std::lock_guard<std::mutex> lock(m_environmentsMutex);
			m_classes.push_back(klass);
		}
		// `extends A, B, C` : au plus une classe de script, et des bases de
		// l'hôte (owners) ; les owners du parent passent à la classe AVANT les
		// siens (ordre de construction : du plus ancien au plus dérivé).
		std::vector<std::shared_ptr<const HostType>> declaredOwners;
		for (size_t i = 0; i < def.bases.size(); ++i)
		{
			const TypePath &path = def.bases[i];
			auto resolved = ResolveTypeValue(path, env, stmt.line, stmt.column);
			if (resolved.IsError())
				return Err(resolved.Error());
			const Value &base = resolved.Value();
			if (base.IsClass() && base.AsClass())
			{
				if (klass->superclass)
					return fail(String::Format("`%s` : deux classes parentes de script (`%s` et `%s`) — une seule, "
											   "les autres contrats vont dans `implements`",
											   def.name.CStr(), klass->superclass->Name().CStr(),
											   base.AsClass()->Name().CStr()));
				if (base.AsClass()->IsInterface())
					return fail(String::Format("`%s` étend l'interface `%s` : une interface s'IMPLÉMENTE "
											   "(`implements`)",
											   def.name.CStr(), base.AsClass()->Name().CStr()));
				klass->superclass = base.AsClass();
				if (!def.superTypeArgs.empty() && def.superTypeArgsBase != i)
					return fail(String::Format("`%s` : arguments de type donnés à une base qui n'est pas la classe "
											   "parente",
											   def.name.CStr()));
				if (def.superTypeArgs.size() > klass->superclass->def->typeParams.size())
					return fail(String::Format("`%s` passe %d argument(s) de type à `%s`, qui en attend %d",
											   def.name.CStr(), int(def.superTypeArgs.size()),
											   klass->superclass->Name().CStr(),
											   int(klass->superclass->def->typeParams.size())));
				continue;
			}
			const std::shared_ptr<const HostType> host =
				base.IsNative() && base.AsNative() ? base.AsNative()->hostType : nullptr;
			if (!host)
				return fail(String::Format("`%s` : `%s` n'est ni une classe ni une base de l'hôte (c'est `%s`)",
										   def.name.CStr(), JoinTypePath(path).CStr(), base.TypeName()));
			if (!host->isOwner)
				return fail(String::Format("`%s` : `%s` est un type de l'hôte qui ne se dérive pas (seules les "
										   "bases marquées owner le peuvent)",
										   def.name.CStr(), host->name.CStr()));
			if (!def.superTypeArgs.empty() && def.superTypeArgsBase == i)
				return fail(String::Format("`%s` : la base de l'hôte `%s` n'est pas générique", def.name.CStr(),
										   host->name.CStr()));
			if (std::find(declaredOwners.begin(), declaredOwners.end(), host) != declaredOwners.end())
				return fail(String::Format("`%s` : base `%s` citée deux fois", def.name.CStr(), host->name.CStr()));
			declaredOwners.push_back(host);
		}
		if (klass->superclass)
			klass->owners = klass->superclass->owners;
		for (const auto &host : declaredOwners)
		{
			if (std::find(klass->owners.begin(), klass->owners.end(), host) != klass->owners.end())
				return fail(String::Format("`%s` : `%s` est déjà une base de `%s`", def.name.CStr(),
										   host->name.CStr(), klass->superclass->Name().CStr()));
			klass->owners.push_back(host);
		}
		klass->hasDeinit =
			def.FindMethod(String("deinit"), false) != nullptr || (klass->superclass && klass->superclass->hasDeinit);
		for (const TypePath &path : def.interfaces)
		{
			auto iface = ResolveTypePath(path, env, stmt.line, stmt.column);
			if (iface.IsError())
				return Err(iface.Error());
			if (!iface.Value()->IsInterface())
				return fail(String::Format(def.isInterface ? "l'interface `%s` ne peut étendre que des interfaces, "
															 "pas la classe `%s`"
														   : "`%s` : `%s` est une classe — héritez-en avec `extends`",
										   def.name.CStr(), iface.Value()->Name().CStr()));
			klass->interfaces.push_back(iface.Unwrap());
		}

		std::vector<std::shared_ptr<ClassObject>> interfaces;
		CollectInterfaces(klass, interfaces);

		// `override` : doit remplacer une méthode d'un ancêtre ou d'une interface.
		for (const MethodDef &method : def.methods)
		{
			if (!method.isOverride)
				continue;
			bool found = FindMethod(klass->superclass, method.def->name, false).first != nullptr;
			for (const auto &iface : interfaces)
				found = found || iface->def->FindMethod(method.def->name, false) != nullptr;
			if (!found)
				return fail(String::Format("`%s.%s` est marquée `override` mais ne remplace aucune méthode héritée",
										   def.name.CStr(), method.def->name.CStr()));
		}

		// Classe concrète : tout ce qui est abstrait doit être fourni.
		if (!def.isAbstract)
		{
			auto implemented = [&](const String &name) -> const MethodDef *
			{
				const MethodDef *method = FindMethod(klass, name, false).first;
				return method && !method->isAbstract ? method : nullptr;
			};
			for (std::shared_ptr<ClassObject> cls = klass->superclass; cls; cls = cls->superclass)
				for (const MethodDef &method : cls->def->methods)
					if (method.isAbstract && !method.isStatic && !implemented(method.def->name))
						return fail(String::Format("la classe `%s` doit implémenter la méthode abstraite `%s` "
												   "(héritée de `%s`)",
												   def.name.CStr(), method.def->name.CStr(), cls->Name().CStr()));
			for (const auto &iface : interfaces)
			{
				for (const MethodDef &signature : iface->def->methods)
				{
					const MethodDef *method = implemented(signature.def->name);
					if (!method)
						return fail(String::Format("la classe `%s` doit implémenter `%s(%d)` de l'interface `%s`",
												   def.name.CStr(), signature.def->name.CStr(),
												   int(signature.def->params.size()), iface->Name().CStr()));
					if (method->def->params.size() != signature.def->params.size())
						return fail(String::Format("`%s.%s` prend %d paramètre(s), l'interface `%s` en attend %d",
												   def.name.CStr(), signature.def->name.CStr(),
												   int(method->def->params.size()), iface->Name().CStr(),
												   int(signature.def->params.size())));
				}
			}
		}

		// Déclarée AVANT ses champs de classe : un initialiseur `static` peut
		// nommer sa propre classe (`static let défaut = Point(0, 0)`).
		env->Put(def.name, BindingKind::CONST, Value::Class(klass));

		for (const FieldDef &field : def.fields)
		{
			if (!field.isStatic)
				continue;
			Value initial = field.type ? DefaultFor(*field.type) : Value::Nil();
			if (field.initializer)
			{
				auto value = Eval(*field.initializer, env);
				if (value.IsError())
					return Err(value.Error());
				initial = value.Unwrap();
				if (auto bad = CheckTyped("le champ", field.name, initial, field.type, env, field.line, field.column);
					bad.IsSome())
					return Err(bad.Unwrap());
			}
			klass->statics.Initialize(field.name, std::move(initial), field.declKind == DeclKind::CONST, field.type);
		}

		// Type énuméré : une instance par valeur, constante de la classe.
		Value previous;
		for (const EnumEntry &entry : def.enumEntries)
		{
			Value value = Value::Int(0);
			if (entry.value)
			{
				auto evaluated = Eval(*entry.value, env);
				if (evaluated.IsError())
					return Err(evaluated.Error());
				value = evaluated.Unwrap();
			}
			else if (previous.IsInteger())
			{
				auto next = numeric::Apply(numeric::Op::ADD, previous, Value::Int(1));
				if (next.IsError())
					return Err(ScriptError(next.Error(), entry.line, entry.column));
				value = next.Unwrap();
			}
			else if (!previous.IsNil())
			{
				return Err(ScriptError(String::Format("`%s.%s` : la valeur précédente n'est pas un entier, donnez-en "
													  "une explicitement",
													  def.name.CStr(), entry.name.CStr()),
									   entry.line, entry.column));
			}
			previous = value;
			auto instance = std::make_shared<InstanceObject>();
			instance->klass = klass;
			instance->fields.Initialize(String("name"), Value::Str(entry.name), true);
			instance->fields.Initialize(String("value"), std::move(value), true);
			instance->fields.Initialize(String("ordinal"), Value::Int(int64_t(klass->enumValues.size())), true);
			Value item = Value::Instance(std::move(instance));
			klass->statics.Initialize(entry.name, item, true);
			klass->enumValues.push_back(std::move(item));
		}
		return Ok(ExecOutcome{});
	}

	namespace detail
	{

		Result<double, ScriptError> ArgNumber(const std::vector<Value> &args, size_t index, const char *fnName)
		{
			if (index >= args.size() || !args[index].IsNumber())
				return Err(Interpreter::MakeError(String::Format("`%s` : argument %d doit être un nombre, trouvé `%s`", fnName,
																 int(index + 1),
																 index < args.size() ? args[index].TypeName() : "rien")));
			return Ok(args[index].AsNumber());
		}

		Result<String, ScriptError> ArgString(const std::vector<Value> &args, size_t index, const char *fnName)
		{
			if (index >= args.size() || !args[index].IsString())
				return Err(Interpreter::MakeError(String::Format("`%s` : argument %d doit être une chaîne, trouvé `%s`", fnName,
																 int(index + 1),
																 index < args.size() ? args[index].TypeName() : "rien")));
			return Ok(args[index].AsString());
		}

		Result<std::shared_ptr<ListObject>, ScriptError> ArgList(const std::vector<Value> &args,
																 size_t index, const char *fnName)
		{
			if (index >= args.size() || !args[index].IsList() || !args[index].AsList())
				return Err(Interpreter::MakeError(String::Format("`%s` : argument %d doit être une liste, trouvé `%s`", fnName,
																 int(index + 1),
																 index < args.size() ? args[index].TypeName() : "rien")));
			return Ok(args[index].AsList());
		}

		Result<std::shared_ptr<MapObject>, ScriptError> ArgMap(const std::vector<Value> &args, size_t index, const char *fnName)
		{
			if (index >= args.size() || !args[index].IsMap() || !args[index].AsMap())
				return Err(Interpreter::MakeError(String::Format("`%s` : argument %d doit être une table, trouvé `%s`", fnName,
																 int(index + 1),
																 index < args.size() ? args[index].TypeName() : "rien")));
			return Ok(args[index].AsMap());
		}

		Result<size_t, ScriptError> ToIndex(double raw, size_t size, const char *fnName)
		{
			if (raw != std::floor(raw) || raw < 0.0 || raw >= double(size))
				return Err(Interpreter::MakeError(String::Format("`%s` : indice %s hors bornes (taille %d)", fnName,
																 Value::NumberToString(raw).CStr(), int(size))));
			return Ok(static_cast<size_t>(raw));
		}

	} // namespace detail

} // namespace data::script
