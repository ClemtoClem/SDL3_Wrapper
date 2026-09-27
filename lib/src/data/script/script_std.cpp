// Définitions de data/script/script_std.hpp — fichier généré par splitter.py : le code
// vient tel quel de l'en-tête (seules les signatures sont réécrites).

// L'en-tête n'est pas autonome : il compte sur ce qu'inclut son module.
#include "data/script.hpp"
#include "data/script/script_std.hpp"

namespace data::script {

void InstallCoreGlobals(Interpreter& vm) {
	// ── Général ─────────────────────────────────────────────────────────────

	vm.RegisterNative("print", 0, -1, [](Interpreter& vm, std::vector<Value>& args) -> Result<Value, ScriptError> {
		String line;
		for (size_t i = 0; i < args.size(); ++i) {
			if (i > 0)
				line.Concat(" ");
			auto text = vm.Stringify(args[i]);
			if (text.IsError())
				return Err(text.Error());
			line.Concat(text.Value());
		}
		vm.Emit(line);
		return Ok(Value::Nil());
	});

	vm.RegisterNative("type", 1, 1, [](Interpreter&, std::vector<Value>& args) -> Result<Value, ScriptError> {
		return Ok(Value::Str(String(args[0].TypeName())));
	});

	// Le seul moyen pour un script de signaler lui-même un échec — utilisé
	// par les scénarios de test de l'éditeur.
	vm.RegisterNative("assert", 1, 2, [](Interpreter&, std::vector<Value>& args) -> Result<Value, ScriptError> {
		if (args[0].IsTruthy())
			return Ok(args[0]);
		String message = args.size() > 1 ? args[1].ToDisplayString() : String("assertion échouée");
		return Err(Interpreter::MakeError(std::move(message)));
	});

	vm.RegisterNative("len", 1, 1, [](Interpreter&, std::vector<Value>& args) -> Result<Value, ScriptError> {
		const Value& v = args[0];
		if (v.IsList() && v.AsList())
			return Ok(Value::Int(int64_t(v.AsList()->Size())));
		if (v.IsMap() && v.AsMap())
			return Ok(Value::Int(int64_t(v.AsMap()->Size())));
		if (v.IsString())
			return Ok(Value::Int(int64_t(v.AsString().GetSize())));
		if (v.IsInstance() && v.AsInstance())
			return Ok(Value::Int(int64_t(v.AsInstance()->fields.Snapshot().size())));
		if (v.IsHost() && v.AsHost()) {
			const HostType& type = *v.AsHost()->type;
			if (type.size)
				return Ok(Value::Int(int64_t(type.size(*v.AsHost()))));
			if (type.items)
				return Ok(Value::Int(int64_t(type.items(*v.AsHost()).size())));
		}
		return Err(Interpreter::MakeError(String::Format(
			"`len` attend une liste, une table, une chaîne, un objet ou un conteneur, trouvé `%s`", v.TypeName())));
	});

	vm.RegisterNative("str", 1, 1, [](Interpreter& vm, std::vector<Value>& args) -> Result<Value, ScriptError> {
		auto text = vm.Stringify(args[0]);
		if (text.IsError())
			return Err(text.Error());
		return Ok(Value::Str(text.Unwrap()));
	});

	// `num` rend `nil` (et non une erreur) sur une chaîne non numérique :
	// c'est ce qui permet `let v = num(saisie) or defaut`.
	vm.RegisterNative("num", 1, 1, [](Interpreter&, std::vector<Value>& args) -> Result<Value, ScriptError> {
		const Value& v = args[0];
		if (v.IsNumber())
			return Ok(v);
		if (v.IsBoolean())
			return Ok(Value::Int(v.AsBoolean() ? 1 : 0));
		if (v.IsString()) {
			Option<Value> parsed = numeric::Parse(v.AsString());
			return Ok(parsed.IsSome() ? std::move(parsed).Unwrap() : Value::Nil());
		}
		return Ok(Value::Nil());
	});

	// Conversions vers les types numériques (remplacent `int`) : un nombre,
	// un booléen ou une chaîne numérique, rendu DANS le type demandé.
	// Entiers : partie entière, puis repliement sur la largeur du type comme
	// un transtypage C (`u8(300)` vaut 44) — d'un flottant vers 64 bits :
	// saturation aux bornes. `f32` : arrondi à la précision simple.
	for (int index = 0; index <= static_cast<int>(NumberType::F64); ++index) {
		const NumberType target = static_cast<NumberType>(index);
		vm.RegisterNative(NumberTypeName(target), 1, 1,
						  [target](Interpreter&, std::vector<Value>& args) -> Result<Value, ScriptError> {
							  const Value& v = args[0];
							  Value source;
							  if (v.IsNumber())
								  source = v;
							  else if (v.IsBoolean())
								  source = Value::Int(v.AsBoolean() ? 1 : 0);
							  else if (v.IsString() && numeric::Parse(v.AsString()).IsSome())
								  source = numeric::Parse(v.AsString()).Unwrap();
							  else
								  return Err(Interpreter::MakeError(
									  String::Format("`%s` : conversion impossible depuis %s", NumberTypeName(target),
													 v.IsString() ? "une chaîne non numérique" : v.TypeName())));
							  auto converted = numeric::ConvertWrapping(source, target);
							  if (converted.IsError())
								  return Err(Interpreter::MakeError(
									  String::Format("`%s` : %s", NumberTypeName(target), converted.Error().CStr())));
							  return Ok(std::move(converted).Unwrap());
						  });
	}

	// Horloge monotone, en secondes depuis la création de l'interpréteur —
	// std::chrono plutôt que sdl3::GetTicksMS pour que data:: reste
	// indépendant de sdl3:: (aucune autre partie du module n'en dépend).
	vm.RegisterNative("clock", 0, 0, [](Interpreter&, std::vector<Value>&) -> Result<Value, ScriptError> {
		static const std::chrono::steady_clock::time_point START = std::chrono::steady_clock::now();
		auto elapsed = std::chrono::steady_clock::now() - START;
		return Ok(Value::Number(std::chrono::duration<double>(elapsed).count()));
	});

	// ── Exécution asynchrone ────────────────────────────────────────────────

	// Met le fil courant en pause. Sur le fil principal, les appels envoyés
	// par les fils `async` continuent d'être traités pendant la pause.
	vm.RegisterNative("sleep", 1, 1, [](Interpreter& vm, std::vector<Value>& args) -> Result<Value, ScriptError> {
		auto ms = detail::ArgNumber(args, 0, "sleep");
		if (ms.IsError())
			return Err(ms.Error());
		const auto until = std::chrono::steady_clock::now() + std::chrono::microseconds(int64_t(ms.Unwrap() * 1000.0));
		while (std::chrono::steady_clock::now() < until) {
			if (vm.IsStopping())
				return Err(Interpreter::MakeError(String("`sleep` interrompu : l'interpréteur s'arrête")));
			if (vm.IsMainThread())
				vm.PumpMainThread();
			const auto left = until - std::chrono::steady_clock::now();
			std::this_thread::sleep_for(
				std::min<std::chrono::steady_clock::duration>(left, std::chrono::milliseconds(2)));
		}
		return Ok(Value::Nil());
	});

	// `future.value(v)` / `future.error(message)` : Futures déjà terminés ;
	// `future.all([f1, f2…])` : un Future de la liste des résultats ;
	// `future.delayed(ms[, fn])` : un Future qui se termine après `ms`
	// millisecondes, avec le résultat de `fn()` s'il est donné.
	vm.RegisterNamespacedNative(String("future"), String("value"), 1, 1,
								[](Interpreter&, std::vector<Value>& args) -> Result<Value, ScriptError> {
									auto future = std::make_shared<FutureObject>();
									future->Complete(Ok(args[0]));
									return Ok(Value::Future(std::move(future)));
								});
	vm.RegisterNamespacedNative(String("future"), String("error"), 1, 1,
								[](Interpreter&, std::vector<Value>& args) -> Result<Value, ScriptError> {
									auto future = std::make_shared<FutureObject>();
									future->Complete(Err(Interpreter::MakeError(args[0].ToDisplayString())));
									return Ok(Value::Future(std::move(future)));
								});
	vm.RegisterNamespacedNative(String("future"), String("all"), 1, 1,
								[](Interpreter& vm, std::vector<Value>& args) -> Result<Value, ScriptError> {
									auto list = detail::ArgList(args, 0, "future.all");
									if (list.IsError())
										return Err(list.Error());
									std::vector<Value> pending = list.Value()->Snapshot();
									return vm.SpawnTask([&vm, pending]() -> Result<Value, ScriptError> {
										auto results = std::make_shared<ListObject>();
										for (const Value& item : pending) {
											auto result = vm.AwaitValue(item);
											if (result.IsError())
												return result;
											results->items.push_back(result.Value());
										}
										return Ok(Value::List(std::move(results)));
									});
								});
	vm.RegisterNamespacedNative(String("future"), String("delayed"), 1, 2,
								[](Interpreter& vm, std::vector<Value>& args) -> Result<Value, ScriptError> {
									auto ms = detail::ArgNumber(args, 0, "future.delayed");
									if (ms.IsError())
										return Err(ms.Error());
									const double delay = ms.Unwrap();
									Value callback = args.size() > 1 ? args[1] : Value::Nil();
									return vm.SpawnTask([&vm, delay, callback]() -> Result<Value, ScriptError> {
										std::vector<Value> pause{Value::Number(delay)};
										if (auto sleep = vm.GetGlobal(String("sleep")); sleep.IsSome()) {
											auto slept = vm.CallValue(sleep.Value(), pause, 0, 0);
											if (slept.IsError())
												return slept;
										}
										if (!callback.IsCallable())
											return Ok(Value::Nil());
										auto result = vm.CallValue(callback, {}, 0, 0);
										if (result.IsError())
											return result;
										return vm.AwaitValue(result.Value());
									});
								});

	// ── Listes ──────────────────────────────────────────────────────────────

	vm.RegisterNative("list", 0, -1, [](Interpreter&, std::vector<Value>& args) -> Result<Value, ScriptError> {
		auto list = std::make_shared<ListObject>();
		list->items = args;
		return Ok(Value::List(std::move(list)));
	});

	vm.RegisterNative("push", 2, -1, [](Interpreter&, std::vector<Value>& args) -> Result<Value, ScriptError> {
		auto list = detail::ArgList(args, 0, "push");
		if (list.IsError())
			return Err(list.Error());
		std::lock_guard<std::mutex> lock(list.Value()->mutex);
		for (size_t i = 1; i < args.size(); ++i)
			list.Value()->items.push_back(args[i]);
		return Ok(args[0]);
	});

	vm.RegisterNative("pop", 1, 1, [](Interpreter&, std::vector<Value>& args) -> Result<Value, ScriptError> {
		auto list = detail::ArgList(args, 0, "pop");
		if (list.IsError())
			return Err(list.Error());
		std::lock_guard<std::mutex> lock(list.Value()->mutex);
		std::vector<Value>& items = list.Value()->items;
		if (items.empty())
			return Ok(Value::Nil());
		Value last = items.back();
		items.pop_back();
		return Ok(last);
	});

	vm.RegisterNative("insert", 3, 3, [](Interpreter&, std::vector<Value>& args) -> Result<Value, ScriptError> {
		auto list = detail::ArgList(args, 0, "insert");
		if (list.IsError())
			return Err(list.Error());
		auto rawIndex = detail::ArgNumber(args, 1, "insert");
		if (rawIndex.IsError())
			return Err(rawIndex.Error());
		std::lock_guard<std::mutex> lock(list.Value()->mutex);
		std::vector<Value>& items = list.Value()->items;
		double raw = rawIndex.Unwrap();
		// Insertion EN FIN autorisée (indice == taille), contrairement à une
		// lecture — d'où le contrôle local plutôt que detail::ToIndex.
		if (raw != std::floor(raw) || raw < 0.0 || raw > double(items.size()))
			return Err(Interpreter::MakeError(String::Format("`insert` : indice %s hors bornes (taille %d)",
															 Value::NumberToString(raw).CStr(), int(items.size()))));
		items.insert(items.begin() + static_cast<ptrdiff_t>(raw), args[2]);
		return Ok(args[0]);
	});

	vm.RegisterNative("remove", 2, 2, [](Interpreter&, std::vector<Value>& args) -> Result<Value, ScriptError> {
		auto list = detail::ArgList(args, 0, "remove");
		if (list.IsError())
			return Err(list.Error());
		auto rawIndex = detail::ArgNumber(args, 1, "remove");
		if (rawIndex.IsError())
			return Err(rawIndex.Error());
		std::lock_guard<std::mutex> lock(list.Value()->mutex);
		std::vector<Value>& items = list.Value()->items;
		auto index = detail::ToIndex(rawIndex.Unwrap(), items.size(), "remove");
		if (index.IsError())
			return Err(index.Error());
		Value removed = items[index.Value()];
		items.erase(items.begin() + static_cast<ptrdiff_t>(index.Value()));
		return Ok(removed);
	});

	vm.RegisterNative("clear", 1, 1, [](Interpreter&, std::vector<Value>& args) -> Result<Value, ScriptError> {
		if (args[0].IsList() && args[0].AsList()) {
			args[0].AsList()->Replace({});
			return Ok(args[0]);
		}
		if (args[0].IsMap() && args[0].AsMap()) {
			std::lock_guard<std::mutex> lock(args[0].AsMap()->mutex);
			args[0].AsMap()->entries.clear();
			return Ok(args[0]);
		}
		return Err(Interpreter::MakeError(
			String::Format("`clear` attend une liste ou une table, trouvé `%s`", args[0].TypeName())));
	});

	vm.RegisterNative("index_of", 2, 2, [](Interpreter&, std::vector<Value>& args) -> Result<Value, ScriptError> {
		auto list = detail::ArgList(args, 0, "index_of");
		if (list.IsError())
			return Err(list.Error());
		const std::vector<Value> items = list.Value()->Snapshot();
		for (size_t i = 0; i < items.size(); ++i)
			if (items[i].Equals(args[1]))
				return Ok(Value::Int(int64_t(i)));
		return Ok(Value::Int(-1));
	});

	vm.RegisterNative("reverse", 1, 1, [](Interpreter&, std::vector<Value>& args) -> Result<Value, ScriptError> {
		auto list = detail::ArgList(args, 0, "reverse");
		if (list.IsError())
			return Err(list.Error());
		std::lock_guard<std::mutex> lock(list.Value()->mutex);
		std::vector<Value>& items = list.Value()->items;
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
	vm.RegisterNative("sort_numbers", 1, 1, [](Interpreter&, std::vector<Value>& args) -> Result<Value, ScriptError> {
		auto list = detail::ArgList(args, 0, "sort_numbers");
		if (list.IsError())
			return Err(list.Error());
		std::lock_guard<std::mutex> lock(list.Value()->mutex);
		std::vector<Value>& items = list.Value()->items;
		for (const Value& item : items)
			if (!item.IsNumber())
				return Err(Interpreter::MakeError(
					String::Format("`sort_numbers` : élément `%s` non numérique", item.TypeName())));
		for (size_t i = 1; i < items.size(); ++i) {
			Value key = items[i];
			size_t j = i;
			while (j > 0 && numeric::Compare(items[j - 1], key) == 1) {
				items[j] = items[j - 1];
				--j;
			}
			items[j] = key;
		}
		return Ok(args[0]);
	});

	vm.RegisterNative("range", 1, 3, [](Interpreter&, std::vector<Value>& args) -> Result<Value, ScriptError> {
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
		// Bornes et pas entiers : une liste d'i64 exacts.
		bool integral = true;
		for (const Value& arg : args)
			integral = integral && arg.IsInteger() && numeric::Fits(numeric::ToWide(arg), NumberType::I64);
		if (integral) {
			const int64_t first = args.size() == 1 ? 0 : args[0].AsInt64();
			const int64_t last = args[args.size() == 1 ? 0 : 1].AsInt64();
			const int64_t by = args.size() > 2 ? args[2].AsInt64() : 1;
			for (numeric::Wide v = first; (by > 0 ? v < last : v > last); v += by) {
				if (list->items.size() >= MAX_RANGE)
					return Err(Interpreter::MakeError(String::Format("`range` : plus de %d éléments", int(MAX_RANGE))));
				list->items.push_back(Value::Int(static_cast<int64_t>(v)));
			}
			return Ok(Value::List(std::move(list)));
		}
		for (double v = start; (step > 0.0 ? v < stop : v > stop); v += step) {
			if (list->items.size() >= MAX_RANGE)
				return Err(Interpreter::MakeError(String::Format("`range` : plus de %d éléments", int(MAX_RANGE))));
			list->items.push_back(Value::Number(v));
		}
		return Ok(Value::List(std::move(list)));
	});

	// ── Tables ──────────────────────────────────────────────────────────────

	vm.RegisterNative("map", 0, 0, [](Interpreter&, std::vector<Value>&) -> Result<Value, ScriptError> {
		return Ok(Value::EmptyMap());
	});

	// Classe d'une instance (`nil` pour toute autre valeur).
	vm.RegisterNative("class_of", 1, 1, [](Interpreter&, std::vector<Value>& args) -> Result<Value, ScriptError> {
		if (args[0].IsInstance() && args[0].AsInstance())
			return Ok(Value::Class(args[0].AsInstance()->klass));
		return Ok(Value::Nil());
	});

	vm.RegisterNative("keys", 1, 1, [](Interpreter&, std::vector<Value>& args) -> Result<Value, ScriptError> {
		// Objet : ses champs, dans l'ordre de leur création.
		if (args[0].IsInstance() && args[0].AsInstance()) {
			auto fields = std::make_shared<ListObject>();
			for (const FieldSlot& field : args[0].AsInstance()->fields.Snapshot())
				fields->items.push_back(Value::Str(field.name));
			return Ok(Value::List(std::move(fields)));
		}
		// Espace de noms : ses membres initialisés (ce qu'on peut lire).
		if (args[0].IsNamespace() && args[0].AsNamespace()) {
			auto members = std::make_shared<ListObject>();
			for (const Binding& binding : args[0].AsNamespace()->scope->Bindings())
				if (binding.initialized)
					members->items.push_back(Value::Str(binding.name));
			return Ok(Value::List(std::move(members)));
		}
		auto map = detail::ArgMap(args, 0, "keys");
		if (map.IsError())
			return Err(map.Error());
		auto list = std::make_shared<ListObject>();
		for (const auto& entry : map.Value()->Snapshot())
			list->items.push_back(Value::Str(entry.first));
		return Ok(Value::List(std::move(list)));
	});

	vm.RegisterNative("values", 1, 1, [](Interpreter&, std::vector<Value>& args) -> Result<Value, ScriptError> {
		auto map = detail::ArgMap(args, 0, "values");
		if (map.IsError())
			return Err(map.Error());
		auto list = std::make_shared<ListObject>();
		for (const auto& entry : map.Value()->Snapshot())
			list->items.push_back(entry.second);
		return Ok(Value::List(std::move(list)));
	});

	vm.RegisterNative("has", 2, 2, [](Interpreter&, std::vector<Value>& args) -> Result<Value, ScriptError> {
		// Objet : un champ OU une méthode de ce nom.
		if (args[0].IsInstance() && args[0].AsInstance()) {
			const String name = args[1].ToDisplayString();
			const std::shared_ptr<InstanceObject>& instance = args[0].AsInstance();
			if (instance->fields.Has(name))
				return Ok(Value::Boolean(true));
			for (std::shared_ptr<ClassObject> cls = instance->klass; cls; cls = cls->superclass)
				if (cls->def->FindMethod(name, false))
					return Ok(Value::Boolean(true));
			return Ok(Value::Boolean(false));
		}
		if (args[0].IsNamespace() && args[0].AsNamespace()) {
			Option<BindingInfo> member = args[0].AsNamespace()->scope->Local(args[1].ToDisplayString());
			return Ok(Value::Boolean(member.IsSome() && member.Value().initialized));
		}
		auto map = detail::ArgMap(args, 0, "has");
		if (map.IsError())
			return Err(map.Error());
		return Ok(Value::Boolean(map.Value()->Get(args[1].ToDisplayString()).IsSome()));
	});

	vm.RegisterNative("erase", 2, 2, [](Interpreter&, std::vector<Value>& args) -> Result<Value, ScriptError> {
		auto map = detail::ArgMap(args, 0, "erase");
		if (map.IsError())
			return Err(map.Error());
		return Ok(Value::Boolean(map.Value()->RemoveKey(args[1].ToDisplayString())));
	});

	// ── Chaînes ─────────────────────────────────────────────────────────────

	vm.RegisterNative("upper", 1, 1, [](Interpreter&, std::vector<Value>& args) -> Result<Value, ScriptError> {
		auto text = detail::ArgString(args, 0, "upper");
		if (text.IsError())
			return Err(text.Error());
		return Ok(Value::Str(text.Value().ToUpper()));
	});

	vm.RegisterNative("lower", 1, 1, [](Interpreter&, std::vector<Value>& args) -> Result<Value, ScriptError> {
		auto text = detail::ArgString(args, 0, "lower");
		if (text.IsError())
			return Err(text.Error());
		return Ok(Value::Str(text.Value().ToLower()));
	});

	vm.RegisterNative("trim", 1, 1, [](Interpreter&, std::vector<Value>& args) -> Result<Value, ScriptError> {
		auto text = detail::ArgString(args, 0, "trim");
		if (text.IsError())
			return Err(text.Error());
		return Ok(Value::Str(text.Value().Trim()));
	});

	vm.RegisterNative("sub", 2, 3, [](Interpreter&, std::vector<Value>& args) -> Result<Value, ScriptError> {
		auto text = detail::ArgString(args, 0, "sub");
		if (text.IsError())
			return Err(text.Error());
		auto start = detail::ArgNumber(args, 1, "sub");
		if (start.IsError())
			return Err(start.Error());
		const String& s = text.Value();
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
	vm.RegisterNative("find", 2, 2, [](Interpreter&, std::vector<Value>& args) -> Result<Value, ScriptError> {
		auto text = detail::ArgString(args, 0, "find");
		if (text.IsError())
			return Err(text.Error());
		auto needle = detail::ArgString(args, 1, "find");
		if (needle.IsError())
			return Err(needle.Error());
		size_t found = text.Value().Find(needle.Value());
		if (found == String::NPOS)
			return Ok(Value::Int(-1));
		return Ok(Value::Int(int64_t(found)));
	});

	vm.RegisterNative("replace", 3, 3, [](Interpreter&, std::vector<Value>& args) -> Result<Value, ScriptError> {
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

	vm.RegisterNative("starts_with", 2, 2, [](Interpreter&, std::vector<Value>& args) -> Result<Value, ScriptError> {
		auto text = detail::ArgString(args, 0, "starts_with");
		if (text.IsError())
			return Err(text.Error());
		auto prefix = detail::ArgString(args, 1, "starts_with");
		if (prefix.IsError())
			return Err(prefix.Error());
		return Ok(Value::Boolean(text.Value().StartsWith(prefix.Value().View())));
	});

	vm.RegisterNative("ends_with", 2, 2, [](Interpreter&, std::vector<Value>& args) -> Result<Value, ScriptError> {
		auto text = detail::ArgString(args, 0, "ends_with");
		if (text.IsError())
			return Err(text.Error());
		auto suffix = detail::ArgString(args, 1, "ends_with");
		if (suffix.IsError())
			return Err(suffix.Error());
		return Ok(Value::Boolean(text.Value().EndsWith(suffix.Value().View())));
	});

	vm.RegisterNative("split", 2, 2, [](Interpreter&, std::vector<Value>& args) -> Result<Value, ScriptError> {
		auto text = detail::ArgString(args, 0, "split");
		if (text.IsError())
			return Err(text.Error());
		auto separator = detail::ArgString(args, 1, "split");
		if (separator.IsError())
			return Err(separator.Error());
		auto list = std::make_shared<ListObject>();
		if (separator.Value().IsEmpty()) {
			const String& s = text.Value();
			for (size_t i = 0; i < s.GetSize(); ++i)
				list->items.push_back(Value::Str(String(s.CStr() + i, 1)));
		} else {
			for (const String& part : text.Value().Split(separator.Value().View()))
				list->items.push_back(Value::Str(part));
		}
		return Ok(Value::List(std::move(list)));
	});

	vm.RegisterNative("join", 2, 2, [](Interpreter& vm, std::vector<Value>& args) -> Result<Value, ScriptError> {
		auto list = detail::ArgList(args, 0, "join");
		if (list.IsError())
			return Err(list.Error());
		auto separator = detail::ArgString(args, 1, "join");
		if (separator.IsError())
			return Err(separator.Error());
		String out;
		const std::vector<Value> items = list.Value()->Snapshot();
		for (size_t i = 0; i < items.size(); ++i) {
			if (i > 0)
				out.Concat(separator.Value());
			auto text = vm.Stringify(items[i]);
			if (text.IsError())
				return Err(text.Error());
			out.Concat(text.Value());
		}
		return Ok(Value::Str(std::move(out)));
	});

	// `format("pos {} / {}", x, total)` — marqueurs positionnels `{}`,
	// délibérément PAS un printf : un `%d` face à une chaîne serait un
	// comportement indéfini, alors qu'ici tout argument est affichable.
	vm.RegisterNative("format", 1, -1, [](Interpreter& vm, std::vector<Value>& args) -> Result<Value, ScriptError> {
		auto pattern = detail::ArgString(args, 0, "format");
		if (pattern.IsError())
			return Err(pattern.Error());
		const String& fmt = pattern.Value();
		String out;
		size_t nextArg = 1;
		for (size_t i = 0; i < fmt.GetSize(); ++i) {
			char c = fmt.CharAt(i);
			if (c == '{' && i + 1 < fmt.GetSize() && fmt.CharAt(i + 1) == '}') {
				if (nextArg < args.size()) {
					auto text = vm.Stringify(args[nextArg]);
					if (text.IsError())
						return Err(text.Error());
					out.Concat(text.Value());
				} else {
					out.Concat("nil");
				}
				++nextArg;
				++i;
				continue;
			}
			out.PushBack(c);
		}
		return Ok(Value::Str(std::move(out)));
	});
}

namespace lib {

ScriptError Fail(String message) {
	return Interpreter::MakeError(std::move(message));
}

// ── TypeBuilder ──────────────────────────────────────────────────────────────

TypeBuilder::TypeBuilder(const char* qualifiedName) {
type->name = String(qualifiedName);
}

TypeBuilder& TypeBuilder::Method(const char* name, int minArity, int maxArity, MethodFn fn) {
type->methods.push_back(HostMethod{String(name), minArity, maxArity, std::move(fn)});
return *this;
}

TypeBuilder& TypeBuilder::Alias(const char* alias, const char* target) {
if (const HostMethod* method = type->FindMethod(String(target)))
	type->methods.push_back(HostMethod{String(alias), method->minArity, method->maxArity, method->fn});
return *this;
}

TypeBuilder& TypeBuilder::Static(const char* name, Value value) {
type->statics.emplace_back(String(name), std::move(value));
return *this;
}

TypeBuilder& TypeBuilder::StaticFn(const char* name, int minArity, int maxArity, NativeFn fn) {
auto native = std::make_shared<NativeObject>();
native->name = String::Format("%s.%s", type->name.CStr(), name);
native->minArity = minArity;
native->maxArity = maxArity;
native->anyThread = true;
native->fn = std::move(fn);
return Static(name, Value::Native(std::move(native)));
}

TypeBuilder& TypeBuilder::Construct(int minArity, int maxArity,
		std::function<Result<Value, ScriptError>(Interpreter&, Args&, const Args&)> fn) {
type->minArity = minArity;
type->maxArity = maxArity;
type->construct = std::move(fn);
return *this;
}

Value MakeFunction(const String& name, int minArity, int maxArity, NativeFn fn) {
	auto native = std::make_shared<NativeObject>();
	native->name = name;
	native->minArity = minArity;
	native->maxArity = maxArity;
	native->anyThread = true;
	native->fn = std::move(fn);
	return Value::Native(std::move(native));
}

Result<double, ScriptError> ArgFloat(const Args& args, size_t i, const char* fn) {
	if (i >= args.size() || !args[i].IsNumber())
		return Err(Fail(String::Format("`%s` : argument %d doit être un nombre, trouvé `%s`", fn, int(i + 1),
									   i < args.size() ? args[i].TypeName() : "rien")));
	return Ok(args[i].AsNumber());
}

Result<int64_t, ScriptError> ArgInt(const Args& args, size_t i, const char* fn) {
	if (i >= args.size() || !args[i].IsNumber())
		return Err(Fail(String::Format("`%s` : argument %d doit être un entier, trouvé `%s`", fn, int(i + 1),
									   i < args.size() ? args[i].TypeName() : "rien")));
	const Value& v = args[i];
	if (v.IsFloat() && (v.AsNumber() != std::floor(v.AsNumber()) || !std::isfinite(v.AsNumber())))
		return Err(Fail(String::Format("`%s` : argument %d doit être un entier, trouvé %s", fn, int(i + 1),
									   v.ToDisplayString().CStr())));
	return Ok(v.AsInt64());
}

Result<String, ScriptError> ArgText(const Args& args, size_t i, const char* fn) {
	if (i >= args.size() || !args[i].IsString())
		return Err(Fail(String::Format("`%s` : argument %d doit être une chaîne, trouvé `%s`", fn, int(i + 1),
									   i < args.size() ? args[i].TypeName() : "rien")));
	return Ok(args[i].AsString());
}

Option<ScriptError> ArgCallable(Interpreter& vm, const Args& args, size_t i, const char* fn) {
	if (i >= args.size() || !vm.IsCallableValue(args[i]))
		return Some(Fail(String::Format("`%s` : argument %d doit être une fonction, trouvé `%s`", fn, int(i + 1),
										i < args.size() ? args[i].TypeName() : "rien")));
	return NONE;
}

Result<size_t, ScriptError> ToIndex(int64_t raw, size_t size, const char* fn, bool allowEnd) {
	int64_t index = raw < 0 ? raw + int64_t(size) : raw;
	const int64_t limit = int64_t(size) + (allowEnd ? 1 : 0);
	if (index < 0 || index >= limit)
		return Err(Fail(
			String::Format("`%s` : indice %lld hors bornes (taille %d)", fn, static_cast<long long>(raw), int(size))));
	return Ok(size_t(index));
}

std::shared_ptr<const HostType> HostTypeOf(Interpreter& vm, const char* ns, const char* name) {
	Option<Value> member = vm.GetNamespaceMember(String(ns), String(name));
	if (member.IsNone() || !member.Value().IsNative() || !member.Value().AsNative())
		return nullptr;
	return member.Value().AsNative()->hostType;
}

Result<Value, ScriptError> Conform(Interpreter& vm, const HostObject& object, size_t i, Value value) {
	if (i >= object.typeArgs.size())
		return Ok(std::move(value));
	return vm.ConformValue(object.typeArgs[i], std::move(value));
}

Result<bool, ScriptError> Less(Interpreter& vm, const Value& comparator, const Value& a, const Value& b) {
	if (comparator.IsNil()) {
		auto order = vm.CompareValues(a, b);
		if (order.IsError())
			return Err(order.Error());
		return Ok(order.Value() < 0);
	}
	auto result = vm.CallValue(comparator, {a, b}, 0, 0);
	if (result.IsError())
		return Err(result.Error());
	if (result.Value().IsNumber())
		return Ok(result.Value().AsNumber() < 0.0);
	return Ok(result.Value().IsTruthy());
}

Option<ScriptError> SortValues(Interpreter& vm, std::vector<Value>& items, const Value& comparator) {
	std::vector<Value> buffer(items.size());
	for (size_t width = 1; width < items.size(); width *= 2) {
		for (size_t low = 0; low < items.size(); low += 2 * width) {
			const size_t mid = std::min(low + width, items.size());
			const size_t high = std::min(low + 2 * width, items.size());
			size_t i = low, j = mid, k = low;
			while (i < mid && j < high) {
				auto less = Less(vm, comparator, items[j], items[i]);
				if (less.IsError())
					return Some(less.Error());
				buffer[k++] = less.Value() ? items[j++] : items[i++];
			}
			while (i < mid)
				buffer[k++] = items[i++];
			while (j < high)
				buffer[k++] = items[j++];
		}
		items.swap(buffer);
	}
	return NONE;
}

String JoinDisplay(const char* prefix, const std::vector<Value>& items, const char* open, const char* close) {
	String out(prefix);
	out.Concat(open);
	for (size_t i = 0; i < items.size(); ++i) {
		if (i > 0)
			out.Concat(", ");
		out.Concat(items[i].IsString() ? String::Format("\"%s\"", items[i].AsString().CStr())
									   : items[i].ToDisplayString());
	}
	out.Concat(close);
	return out;
}

Result<std::vector<Value>, ScriptError> ItemsOf(Interpreter& vm, const Value& source) {
	return vm.Iterate(source);
}

const void* IdentityOf(const Value& v) noexcept {
	switch (v.GetKind()) {
		case Value::Kind::LIST:
			return v.AsList().get();
		case Value::Kind::MAP:
			return v.AsMap().get();
		case Value::Kind::FUNCTION:
			return v.AsFunction().get();
		case Value::Kind::NATIVE:
			return v.AsNative().get();
		case Value::Kind::NAMESPACE:
			return v.AsNamespace().get();
		case Value::Kind::CLASS:
			return v.AsClass().get();
		case Value::Kind::INSTANCE:
			return v.AsInstance().get();
		case Value::Kind::FUTURE:
			return v.AsFuture().get();
		case Value::Kind::HOST:
			return v.AsHost().get();
		case Value::Kind::TYPE:
			return v.AsType().get();
		default:
			return nullptr;
	}
}

int KeyRank(const Value& v) noexcept {
	switch (v.GetKind()) {
		case Value::Kind::NIL:
			return 0;
		case Value::Kind::BOOLEAN:
			return 1;
		case Value::Kind::NUMBER:
			return 2;
		case Value::Kind::STRING:
			return 3;
		default:
			return 4;
	}
}

int KeyCompare(const Value& a, const Value& b) noexcept {
	const int ra = KeyRank(a), rb = KeyRank(b);
	if (ra != rb)
		return ra < rb ? -1 : 1;
	switch (ra) {
		case 1:
			return int(a.AsBoolean()) - int(b.AsBoolean());
		case 2: {
			const int order = numeric::Compare(a, b);
			return order == 2 ? 0 : order;
		}
		case 3: {
			const int order = std::strcmp(a.AsString().CStr(), b.AsString().CStr());
			return order < 0 ? -1 : (order > 0 ? 1 : 0);
		}
		case 4: {
			const void *x = IdentityOf(a), *y = IdentityOf(b);
			return std::less<const void*>()(x, y) ? -1 : (std::less<const void*>()(y, x) ? 1 : 0);
		}
		default:
			return 0;
	}
}

Option<ScriptError> CheckKey(const Value& key, const char* fn) {
	if (key.IsNumber() && std::isnan(key.AsNumber()))
		return Some(Fail(String::Format("`%s` : NaN n'est pas une clé valide", fn)));
	return NONE;
}

Result<Value, ScriptError> EmitItems(Interpreter& vm, const std::vector<Value>& items, const Value& sink) {
	if (!sink.IsHost() && !sink.IsList() && vm.IsCallableValue(sink)) {
		std::vector<Value> results;
		for (const Value& item : items) {
			auto result = vm.CallValue(sink, {item}, 0, 0);
			if (result.IsError())
				return Err(result.Error());
			if (!result.Value().IsNil())
				results.push_back(result.Unwrap());
		}
		return NewVector(vm, std::move(results));
	}
	for (const Value& item : items) {
		auto delivered = vm.FlowInto(sink, item);
		if (delivered.IsError())
			return Err(delivered.Error());
	}
	return Ok(sink);
}

} // namespace lib

namespace lib {

Result<Value, ScriptError> NewVector(Interpreter& vm, std::vector<Value> items) {
	return MakeSequence<std::vector<Value>>(HostTypeOf(vm, "std", "vector"), std::move(items));
}

Result<std::vector<Value>, ScriptError> ConformAll(Interpreter& vm, const HostObject& object, std::vector<Value> values) {
	for (Value& value : values) {
		auto conformed = Conform(vm, object, 0, std::move(value));
		if (conformed.IsError())
			return Err(conformed.Error());
		value = conformed.Unwrap();
	}
	return Ok(std::move(values));
}

Option<ScriptError> HeapPush(Interpreter& vm, std::vector<Value>& heap, Value value, const Value& comparator) {
	heap.push_back(std::move(value));
	size_t i = heap.size() - 1;
	while (i > 0) {
		const size_t parent = (i - 1) / 2;
		auto less = Less(vm, comparator, heap[parent], heap[i]);
		if (less.IsError())
			return Some(less.Error());
		if (!less.Value())
			break;
		std::swap(heap[parent], heap[i]);
		i = parent;
	}
	return NONE;
}

Result<Value, ScriptError> HeapPop(Interpreter& vm, std::vector<Value>& heap, const Value& comparator) {
	if (heap.empty())
		return Ok(Value::Nil());
	Value top = heap.front();
	heap.front() = heap.back();
	heap.pop_back();
	size_t i = 0;
	for (;;) {
		size_t largest = i;
		for (size_t child : {2 * i + 1, 2 * i + 2}) {
			if (child >= heap.size())
				continue;
			auto less = Less(vm, comparator, heap[largest], heap[child]);
			if (less.IsError())
				return Err(less.Error());
			if (less.Value())
				largest = child;
		}
		if (largest == i)
			break;
		std::swap(heap[i], heap[largest]);
		i = largest;
	}
	return Ok(top);
}

void DefineAdapter(TypeBuilder& builder, AdapterObject::Kind kind) {
	using Object = AdapterObject;
	std::weak_ptr<const HostType> weakType = builder.type;
	const bool priority = kind == Object::Kind::PRIORITY;
	builder.Construct(
		0, priority ? 2 : 1,
		[weakType, kind, priority](Interpreter& vm, Args& args, const Args& typeArgs) -> Result<Value, ScriptError> {
			auto object = std::make_shared<Object>();
			object->type = weakType.lock();
			object->typeArgs = typeArgs;
			object->kind = kind;
			Value source;
			for (const Value& arg : args) {
				if (priority && vm.IsCallableValue(arg) && !arg.IsHost())
					object->comparator = arg;
				else
					source = arg;
			}
			if (!source.IsNil()) {
				auto items = ItemsOf(vm, source);
				if (items.IsError())
					return Err(items.Error());
				for (Value& item : items.Value()) {
					auto value = Conform(vm, *object, 0, std::move(item));
					if (value.IsError())
						return value;
					if (priority) {
						std::vector<Value> heap(object->items.begin(), object->items.end());
						if (auto error = HeapPush(vm, heap, value.Unwrap(), object->comparator); error.IsSome())
							return Err(error.Unwrap());
						object->items.assign(heap.begin(), heap.end());
					} else {
						object->items.push_back(value.Unwrap());
					}
				}
			}
			return Ok(Value::Host(std::move(object)));
		});
	auto push = [](Interpreter& vm, const HostRef& self, const Value& raw) -> Result<Value, ScriptError> {
		auto value = Conform(vm, *self, 0, raw);
		if (value.IsError())
			return value;
		Object& o = As<Object>(self);
		if (o.kind != Object::Kind::PRIORITY) {
			std::lock_guard<std::mutex> lock(o.mutex);
			o.items.push_back(value.Unwrap());
			return Ok(Value::Host(self));
		}
		// Le comparateur du script s'exécute HORS du verrou.
		std::vector<Value> heap;
		Value comparator;
		{
			std::lock_guard<std::mutex> lock(o.mutex);
			heap.assign(o.items.begin(), o.items.end());
			comparator = o.comparator;
		}
		if (auto error = HeapPush(vm, heap, value.Unwrap(), comparator); error.IsSome())
			return Err(error.Unwrap());
		std::lock_guard<std::mutex> lock(o.mutex);
		o.items.assign(heap.begin(), heap.end());
		return Ok(Value::Host(self));
	};
	auto pop = [](Interpreter& vm, const HostRef& self) -> Result<Value, ScriptError> {
		Object& o = As<Object>(self);
		if (o.kind != Object::Kind::PRIORITY) {
			std::lock_guard<std::mutex> lock(o.mutex);
			if (o.items.empty())
				return Ok(Value::Nil());
			Value out;
			if (o.kind == Object::Kind::QUEUE) {
				out = o.items.front();
				o.items.pop_front();
			} else {
				out = o.items.back();
				o.items.pop_back();
			}
			return Ok(out);
		}
		std::vector<Value> heap;
		Value comparator;
		{
			std::lock_guard<std::mutex> lock(o.mutex);
			heap.assign(o.items.begin(), o.items.end());
			comparator = o.comparator;
		}
		auto top = HeapPop(vm, heap, comparator);
		if (top.IsError())
			return top;
		std::lock_guard<std::mutex> lock(o.mutex);
		o.items.assign(heap.begin(), heap.end());
		return top;
	};
	builder.Method("push", 1, 1,
				   [push](Interpreter& vm, const HostRef& self, Args& args) { return push(vm, self, args[0]); });
	builder.Method("pop", 0, 0, [pop](Interpreter& vm, const HostRef& self, Args&) { return pop(vm, self); });
	// Élément qui sortirait au prochain `pop` (sans le retirer).
	builder.Method("peek", 0, 0, [](Interpreter&, const HostRef& self, Args&) -> Result<Value, ScriptError> {
		Object& o = As<Object>(self);
		std::lock_guard<std::mutex> lock(o.mutex);
		if (o.items.empty())
			return Ok(Value::Nil());
		return Ok(o.kind == Object::Kind::STACK ? o.items.back() : o.items.front());
	});
	builder.Alias(kind == Object::Kind::QUEUE ? "front" : "top", "peek");
	if (kind == Object::Kind::QUEUE)
		builder.Method("back", 0, 0, [](Interpreter&, const HostRef& self, Args&) -> Result<Value, ScriptError> {
			Object& o = As<Object>(self);
			std::lock_guard<std::mutex> lock(o.mutex);
			return Ok(o.items.empty() ? Value::Nil() : o.items.back());
		});
	builder.Method("size", 0, 0, [](Interpreter&, const HostRef& self, Args&) -> Result<Value, ScriptError> {
		Object& o = As<Object>(self);
		std::lock_guard<std::mutex> lock(o.mutex);
		return Ok(Value::Int(int64_t(o.items.size())));
	});
	builder.Alias("len", "size");
	builder.Method("empty", 0, 0, [](Interpreter&, const HostRef& self, Args&) -> Result<Value, ScriptError> {
		Object& o = As<Object>(self);
		std::lock_guard<std::mutex> lock(o.mutex);
		return Ok(Value::Boolean(o.items.empty()));
	});
	builder.Alias("is_empty", "empty");
	builder.Method("clear", 0, 0, [](Interpreter&, const HostRef& self, Args&) -> Result<Value, ScriptError> {
		Object& o = As<Object>(self);
		std::deque<Value> released;
		std::lock_guard<std::mutex> lock(o.mutex);
		released.swap(o.items);
		return Ok(Value::Host(self));
	});
	HostType& type = *builder.type;
	type.size = [](const HostObject& self) {
		std::lock_guard<std::mutex> lock(As<Object>(self).mutex);
		return As<Object>(self).items.size();
	};
	// Parcours (`for`) : dans l'ordre de sortie, SANS vider.
	type.items = [](const HostObject& self) {
		const Object& o = As<Object>(self);
		std::lock_guard<std::mutex> lock(o.mutex);
		std::vector<Value> out(o.items.begin(), o.items.end());
		if (o.kind == Object::Kind::STACK)
			std::reverse(out.begin(), out.end());
		return out;
	};
	const String name = builder.type->name;
	type.display = [name](const HostObject& self) {
		const Object& o = As<Object>(self);
		std::lock_guard<std::mutex> lock(o.mutex);
		return String::Format("<%s : %d élément(s)>", name.CStr(), int(o.items.size()));
	};
	type.flowIn = [push](Interpreter& vm, const HostRef& self, const Value& value) { return push(vm, self, value); };
	type.flowOut = [pop](Interpreter& vm, const HostRef& self, const Value& sink) -> Result<Value, ScriptError> {
		std::vector<Value> drained;
		for (;;) {
			bool empty = false;
			{
				std::lock_guard<std::mutex> lock(As<Object>(self).mutex);
				empty = As<Object>(self).items.empty();
			}
			if (empty)
				break;
			auto next = pop(vm, self);
			if (next.IsError())
				return next;
			drained.push_back(next.Unwrap());
		}
		return EmitItems(vm, drained, sink);
	};
}

} // namespace lib

namespace lib {

Result<Value, ScriptError> ConformKey(Interpreter& vm, const HostObject& object, Value key, const char* fn) {
	if (auto bad = CheckKey(key, fn); bad.IsSome())
		return Err(bad.Unwrap());
	return Conform(vm, object, 0, std::move(key));
}

Value Pair(const Value& a, const Value& b) {
	return Value::List(std::make_shared<ListObject>(std::vector<Value>{a, b}));
}

} // namespace lib

namespace lib {

// ── StringObject ─────────────────────────────────────────────────────────────

std::string StringObject::Get() const {
std::lock_guard<std::mutex> lock(mutex);
return text;
}

String ToText(const std::string& s) {
	return String(s.data(), s.size());
}

std::string ToStd(const String& s) {
	return std::string(s.CStr(), s.GetSize());
}

Result<std::string, ScriptError> TextOf(Interpreter& vm, const Value& value) {
	if (value.IsString())
		return Ok(ToStd(value.AsString()));
	if (value.IsHost() && value.AsHost() && value.AsHost()->type->name == "std.string")
		return Ok(As<StringObject>(*value.AsHost()).Get());
	auto text = vm.Stringify(value);
	if (text.IsError())
		return Err(text.Error());
	return Ok(ToStd(text.Value()));
}

Result<Value, ScriptError> MakeStringObject(const std::shared_ptr<const HostType>& type, std::string text) {
	if (!type)
		return Err(Fail(String("std.string indisponible")));
	auto object = std::make_shared<StringObject>();
	object->type = type;
	object->text = std::move(text);
	return Ok(Value::Host(std::move(object)));
}

void AppendUtf8(std::string& out, uint32_t cp) {
	if (cp < 0x80) {
		out.push_back(char(cp));
	} else if (cp < 0x800) {
		out.push_back(char(0xC0 | (cp >> 6)));
		out.push_back(char(0x80 | (cp & 0x3F)));
	} else if (cp < 0x10000) {
		out.push_back(char(0xE0 | (cp >> 12)));
		out.push_back(char(0x80 | ((cp >> 6) & 0x3F)));
		out.push_back(char(0x80 | (cp & 0x3F)));
	} else {
		out.push_back(char(0xF0 | (cp >> 18)));
		out.push_back(char(0x80 | ((cp >> 12) & 0x3F)));
		out.push_back(char(0x80 | ((cp >> 6) & 0x3F)));
		out.push_back(char(0x80 | (cp & 0x3F)));
	}
}

std::vector<TextFn> TextFunctions() {
	auto str = [](std::string s) { return Value::Str(ToText(s)); };
	auto trimmed = [](const std::string& s, bool left, bool right) {
		size_t a = 0, b = s.size();
		while (left && a < b && std::isspace(static_cast<unsigned char>(s[a])))
			++a;
		while (right && b > a && std::isspace(static_cast<unsigned char>(s[b - 1])))
			--b;
		return s.substr(a, b - a);
	};
	auto all = [](const std::string& s, int (*test)(int)) {
		if (s.empty())
			return false;
		for (unsigned char c : s)
			if (!test(c))
				return false;
		return true;
	};
	std::vector<TextFn> fns;
	fns.push_back({"upper", 0, 0, [str](Interpreter&, const std::string& s, Args&) -> Result<Value, ScriptError> {
					   std::string out = s;
					   for (char& c : out)
						   c = char(std::toupper(static_cast<unsigned char>(c)));
					   return Ok(str(out));
				   }});
	fns.push_back({"lower", 0, 0, [str](Interpreter&, const std::string& s, Args&) -> Result<Value, ScriptError> {
					   std::string out = s;
					   for (char& c : out)
						   c = char(std::tolower(static_cast<unsigned char>(c)));
					   return Ok(str(out));
				   }});
	fns.push_back(
		{"trim", 0, 0, [str, trimmed](Interpreter&, const std::string& s, Args&) -> Result<Value, ScriptError> {
			 return Ok(str(trimmed(s, true, true)));
		 }});
	fns.push_back(
		{"trim_start", 0, 0, [str, trimmed](Interpreter&, const std::string& s, Args&) -> Result<Value, ScriptError> {
			 return Ok(str(trimmed(s, true, false)));
		 }});
	fns.push_back(
		{"trim_end", 0, 0, [str, trimmed](Interpreter&, const std::string& s, Args&) -> Result<Value, ScriptError> {
			 return Ok(str(trimmed(s, false, true)));
		 }});
	fns.push_back({"split", 0, 1, [](Interpreter& vm, const std::string& s, Args& args) -> Result<Value, ScriptError> {
					   auto list = std::make_shared<ListObject>();
					   if (args.empty()) { // sur les blancs, comme Python
						   std::string word;
						   for (char c : s) {
							   if (std::isspace(static_cast<unsigned char>(c))) {
								   if (!word.empty())
									   list->items.push_back(Value::Str(ToText(word)));
								   word.clear();
							   } else {
								   word.push_back(c);
							   }
						   }
						   if (!word.empty())
							   list->items.push_back(Value::Str(ToText(word)));
						   return Ok(Value::List(std::move(list)));
					   }
					   auto sep = TextOf(vm, args[0]);
					   if (sep.IsError())
						   return Err(sep.Error());
					   const std::string& separator = sep.Value();
					   if (separator.empty()) {
						   for (char c : s)
							   list->items.push_back(Value::Str(String(&c, 1)));
						   return Ok(Value::List(std::move(list)));
					   }
					   size_t start = 0;
					   for (;;) {
						   const size_t found = s.find(separator, start);
						   list->items.push_back(Value::Str(ToText(s.substr(start, found - start))));
						   if (found == std::string::npos)
							   break;
						   start = found + separator.size();
					   }
					   return Ok(Value::List(std::move(list)));
				   }});
	fns.push_back({"find", 1, 2, [](Interpreter& vm, const std::string& s, Args& args) -> Result<Value, ScriptError> {
					   auto needle = TextOf(vm, args[0]);
					   if (needle.IsError())
						   return Err(needle.Error());
					   size_t from = 0;
					   if (args.size() > 1) {
						   auto start = ArgInt(args, 1, "find");
						   if (start.IsError())
							   return Err(start.Error());
						   from = size_t(std::max<int64_t>(0, start.Value()));
					   }
					   const size_t found = s.find(needle.Value(), from);
					   return Ok(Value::Int(found == std::string::npos ? -1 : int64_t(found)));
				   }});
	fns.push_back({"rfind", 1, 1, [](Interpreter& vm, const std::string& s, Args& args) -> Result<Value, ScriptError> {
					   auto needle = TextOf(vm, args[0]);
					   if (needle.IsError())
						   return Err(needle.Error());
					   const size_t found = s.rfind(needle.Value());
					   return Ok(Value::Int(found == std::string::npos ? -1 : int64_t(found)));
				   }});
	fns.push_back(
		{"contains", 1, 1, [](Interpreter& vm, const std::string& s, Args& args) -> Result<Value, ScriptError> {
			 auto needle = TextOf(vm, args[0]);
			 if (needle.IsError())
				 return Err(needle.Error());
			 return Ok(Value::Boolean(s.find(needle.Value()) != std::string::npos));
		 }});
	fns.push_back({"count", 1, 1, [](Interpreter& vm, const std::string& s, Args& args) -> Result<Value, ScriptError> {
					   auto needle = TextOf(vm, args[0]);
					   if (needle.IsError())
						   return Err(needle.Error());
					   if (needle.Value().empty())
						   return Ok(Value::Int(0));
					   int64_t n = 0;
					   for (size_t at = s.find(needle.Value()); at != std::string::npos;
							at = s.find(needle.Value(), at + needle.Value().size()))
						   ++n;
					   return Ok(Value::Int(n));
				   }});
	fns.push_back(
		{"starts_with", 1, 1, [](Interpreter& vm, const std::string& s, Args& args) -> Result<Value, ScriptError> {
			 auto prefix = TextOf(vm, args[0]);
			 if (prefix.IsError())
				 return Err(prefix.Error());
			 return Ok(Value::Boolean(s.starts_with(prefix.Value())));
		 }});
	fns.push_back(
		{"ends_with", 1, 1, [](Interpreter& vm, const std::string& s, Args& args) -> Result<Value, ScriptError> {
			 auto suffix = TextOf(vm, args[0]);
			 if (suffix.IsError())
				 return Err(suffix.Error());
			 return Ok(Value::Boolean(s.ends_with(suffix.Value())));
		 }});
	fns.push_back(
		{"replace", 2, 2, [str](Interpreter& vm, const std::string& s, Args& args) -> Result<Value, ScriptError> {
			 auto from = TextOf(vm, args[0]);
			 auto to = TextOf(vm, args[1]);
			 if (from.IsError())
				 return Err(from.Error());
			 if (to.IsError())
				 return Err(to.Error());
			 if (from.Value().empty())
				 return Ok(str(s));
			 std::string out;
			 size_t start = 0;
			 for (size_t at = s.find(from.Value()); at != std::string::npos; at = s.find(from.Value(), start)) {
				 out.append(s, start, at - start);
				 out.append(to.Value());
				 start = at + from.Value().size();
			 }
			 out.append(s, start, std::string::npos);
			 return Ok(str(out));
		 }});
	fns.push_back({"substr", 1, 2, [str](Interpreter&, const std::string& s, Args& args) -> Result<Value, ScriptError> {
					   auto from = ArgInt(args, 0, "substr");
					   if (from.IsError())
						   return Err(from.Error());
					   int64_t start = from.Value() < 0 ? from.Value() + int64_t(s.size()) : from.Value();
					   start = std::clamp<int64_t>(start, 0, int64_t(s.size()));
					   size_t count = std::string::npos;
					   if (args.size() > 1) {
						   auto n = ArgInt(args, 1, "substr");
						   if (n.IsError())
							   return Err(n.Error());
						   count = size_t(std::max<int64_t>(0, n.Value()));
					   }
					   return Ok(str(s.substr(size_t(start), count)));
				   }});
	fns.push_back({"repeat", 1, 1, [str](Interpreter&, const std::string& s, Args& args) -> Result<Value, ScriptError> {
					   auto n = ArgInt(args, 0, "repeat");
					   if (n.IsError())
						   return Err(n.Error());
					   if (n.Value() < 0 || double(n.Value()) * double(s.size()) > 1e8)
						   return Err(Fail(String("`repeat` : nombre de répétitions invalide")));
					   std::string out;
					   for (int64_t i = 0; i < n.Value(); ++i)
						   out += s;
					   return Ok(str(out));
				   }});
	fns.push_back({"reverse", 0, 0, [str](Interpreter&, const std::string& s, Args&) -> Result<Value, ScriptError> {
					   return Ok(str(std::string(s.rbegin(), s.rend())));
				   }});
	auto pad = [str](bool left) {
		return [str, left](Interpreter& vm, const std::string& s, Args& args) -> Result<Value, ScriptError> {
			auto width = ArgInt(args, 0, left ? "pad_left" : "pad_right");
			if (width.IsError())
				return Err(width.Error());
			std::string fill = " ";
			if (args.size() > 1) {
				auto text = TextOf(vm, args[1]);
				if (text.IsError())
					return Err(text.Error());
				fill = text.Value().empty() ? std::string(" ") : text.Value();
			}
			std::string out = s;
			while (int64_t(out.size()) < width.Value() && width.Value() < 100000000)
				out = left ? fill + out : out + fill;
			return Ok(str(out));
		};
	};
	fns.push_back({"pad_left", 1, 2, pad(true)});
	fns.push_back({"pad_right", 1, 2, pad(false)});
	fns.push_back({"lines", 0, 0, [](Interpreter&, const std::string& s, Args&) -> Result<Value, ScriptError> {
					   auto list = std::make_shared<ListObject>();
					   size_t start = 0;
					   while (start < s.size()) {
						   size_t end = s.find('\n', start);
						   if (end == std::string::npos)
							   end = s.size();
						   std::string line = s.substr(start, end - start);
						   if (!line.empty() && line.back() == '\r')
							   line.pop_back();
						   list->items.push_back(Value::Str(ToText(line)));
						   start = end + 1;
					   }
					   return Ok(Value::List(std::move(list)));
				   }});
	fns.push_back({"bytes", 0, 0, [](Interpreter&, const std::string& s, Args&) -> Result<Value, ScriptError> {
					   auto list = std::make_shared<ListObject>();
					   for (unsigned char c : s)
						   list->items.push_back(Value::UInt(c, NumberType::U8));
					   return Ok(Value::List(std::move(list)));
				   }});
	fns.push_back({"char_code", 0, 1, [](Interpreter&, const std::string& s, Args& args) -> Result<Value, ScriptError> {
					   int64_t i = 0;
					   if (!args.empty()) {
						   auto index = ArgInt(args, 0, "char_code");
						   if (index.IsError())
							   return Err(index.Error());
						   i = index.Value();
					   }
					   if (i < 0 || i >= int64_t(s.size()))
						   return Ok(Value::Nil());
					   return Ok(Value::UInt(static_cast<unsigned char>(s[size_t(i)]), NumberType::U8));
				   }});
	fns.push_back({"is_digit", 0, 0, [all](Interpreter&, const std::string& s, Args&) -> Result<Value, ScriptError> {
					   return Ok(Value::Boolean(all(s, &isdigit)));
				   }});
	fns.push_back({"is_alpha", 0, 0, [all](Interpreter&, const std::string& s, Args&) -> Result<Value, ScriptError> {
					   return Ok(Value::Boolean(all(s, &isalpha)));
				   }});
	fns.push_back({"is_space", 0, 0, [all](Interpreter&, const std::string& s, Args&) -> Result<Value, ScriptError> {
					   return Ok(Value::Boolean(all(s, &isspace)));
				   }});
	fns.push_back({"to_number", 0, 0, [](Interpreter&, const std::string& s, Args&) -> Result<Value, ScriptError> {
					   Option<Value> parsed = numeric::Parse(ToText(s));
					   return Ok(parsed.IsSome() ? parsed.Unwrap() : Value::Nil());
				   }});
	return fns;
}

void DefineString(TypeBuilder& builder) {
	using Object = StringObject;
	std::weak_ptr<const HostType> weakType = builder.type;
	builder.Construct(0, 1, [weakType](Interpreter& vm, Args& args, const Args&) -> Result<Value, ScriptError> {
		std::string text;
		if (!args.empty()) {
			auto t = TextOf(vm, args[0]);
			if (t.IsError())
				return Err(t.Error());
			text = t.Unwrap();
		}
		return MakeStringObject(weakType.lock(), std::move(text));
	});
	// Fonctions de texte : méthodes (qui rendent des chaînes du script) ET
	// fonctions de classe sur une chaîne (`std.string.upper("abc")`).
	for (TextFn& text : TextFunctions()) {
		auto fn = text.fn;
		builder.Method(text.name, text.minArity, text.maxArity, [fn](Interpreter& vm, const HostRef& self, Args& args) {
			return fn(vm, As<Object>(self).Get(), args);
		});
		const String name(text.name);
		builder.StaticFn(text.name, text.minArity + 1, text.maxArity < 0 ? -1 : text.maxArity + 1,
						 [fn, name](Interpreter& vm, Args& args) -> Result<Value, ScriptError> {
							 auto subject = TextOf(vm, args[0]);
							 if (subject.IsError())
								 return Err(subject.Error());
							 Args rest(args.begin() + 1, args.end());
							 return fn(vm, subject.Value(), rest);
						 });
	}
	builder.StaticFn("join", 1, 2, [](Interpreter& vm, Args& args) -> Result<Value, ScriptError> {
		auto items = ItemsOf(vm, args[0]);
		if (items.IsError())
			return Err(items.Error());
		std::string separator;
		if (args.size() > 1) {
			auto sep = TextOf(vm, args[1]);
			if (sep.IsError())
				return Err(sep.Error());
			separator = sep.Unwrap();
		}
		std::string out;
		for (size_t i = 0; i < items.Value().size(); ++i) {
			auto text = TextOf(vm, items.Value()[i]);
			if (text.IsError())
				return Err(text.Error());
			if (i > 0)
				out += separator;
			out += text.Value();
		}
		return Ok(Value::Str(ToText(out)));
	});
	builder.StaticFn("from_char_code", 1, -1, [](Interpreter&, Args& args) -> Result<Value, ScriptError> {
		std::string out;
		for (size_t i = 0; i < args.size(); ++i) {
			auto cp = ArgInt(args, i, "std.string.from_char_code");
			if (cp.IsError())
				return Err(cp.Error());
			if (cp.Value() < 0 || cp.Value() > 0x10FFFF)
				return Err(Fail(String("`std.string.from_char_code` : point de code hors d'Unicode")));
			AppendUtf8(out, uint32_t(cp.Value()));
		}
		return Ok(Value::Str(ToText(out)));
	});

	// Modification en place (renvoient l'objet : `s.append("a").append("b")`).
	auto mutate = [](std::function<Option<ScriptError>(Interpreter&, std::string&, Args&)> change) {
		return [change](Interpreter& vm, const HostRef& self, Args& args) -> Result<Value, ScriptError> {
			Object& o = As<Object>(self);
			std::string text = o.Get(); // modifié hors verrou (TextOf peut rappeler le script)
			if (auto error = change(vm, text, args); error.IsSome())
				return Err(error.Unwrap());
			std::lock_guard<std::mutex> lock(o.mutex);
			o.text.swap(text);
			return Ok(Value::Host(self));
		};
	};
	builder.Method("append", 1, -1, mutate([](Interpreter& vm, std::string& text, Args& args) -> Option<ScriptError> {
					   for (const Value& arg : args) {
						   auto piece = TextOf(vm, arg);
						   if (piece.IsError())
							   return Some(piece.Error());
						   text += piece.Value();
					   }
					   return NONE;
				   }));
	builder.Method("insert", 2, 2, mutate([](Interpreter& vm, std::string& text, Args& args) -> Option<ScriptError> {
					   auto at = ArgInt(args, 0, "std.string.insert");
					   if (at.IsError())
						   return Some(at.Error());
					   auto piece = TextOf(vm, args[1]);
					   if (piece.IsError())
						   return Some(piece.Error());
					   auto index = ToIndex(at.Value(), text.size(), "std.string.insert", true);
					   if (index.IsError())
						   return Some(index.Error());
					   text.insert(index.Value(), piece.Value());
					   return NONE;
				   }));
	builder.Method("erase", 1, 2, mutate([](Interpreter&, std::string& text, Args& args) -> Option<ScriptError> {
					   auto at = ArgInt(args, 0, "std.string.erase");
					   if (at.IsError())
						   return Some(at.Error());
					   auto index = ToIndex(at.Value(), text.size(), "std.string.erase");
					   if (index.IsError())
						   return Some(index.Error());
					   size_t count = 1;
					   if (args.size() > 1) {
						   auto n = ArgInt(args, 1, "std.string.erase");
						   if (n.IsError())
							   return Some(n.Error());
						   count = size_t(std::max<int64_t>(0, n.Value()));
					   }
					   text.erase(index.Value(), count);
					   return NONE;
				   }));
	builder.Method("clear", 0, 0, mutate([](Interpreter&, std::string& text, Args&) -> Option<ScriptError> {
					   text.clear();
					   return NONE;
				   }));
	builder.Method("size", 0, 0, [](Interpreter&, const HostRef& self, Args&) -> Result<Value, ScriptError> {
		return Ok(Value::Int(int64_t(As<Object>(self).Get().size())));
	});
	builder.Alias("length", "size").Alias("len", "size");
	builder.Method("empty", 0, 0, [](Interpreter&, const HostRef& self, Args&) -> Result<Value, ScriptError> {
		return Ok(Value::Boolean(As<Object>(self).Get().empty()));
	});
	builder.Alias("is_empty", "empty");
	builder.Method("str", 0, 0, [](Interpreter&, const HostRef& self, Args&) -> Result<Value, ScriptError> {
		return Ok(Value::Str(ToText(As<Object>(self).Get())));
	});
	builder.Method("copy", 0, 0, [](Interpreter&, const HostRef& self, Args&) -> Result<Value, ScriptError> {
		return MakeStringObject(self->type, As<Object>(self).Get());
	});

	HostType& type = *builder.type;
	type.index = [](Interpreter&, const HostRef& self, const Value& key) -> Result<Value, ScriptError> {
		Args args{key};
		auto raw = ArgInt(args, 0, "std.string[]");
		if (raw.IsError())
			return Err(raw.Error());
		const std::string text = As<Object>(self).Get();
		auto index = ToIndex(raw.Value(), text.size(), "std.string[]");
		if (index.IsError())
			return Err(index.Error());
		return Ok(Value::Str(String(text.data() + index.Value(), 1)));
	};
	type.setIndex = [](Interpreter& vm, const HostRef& self, const Value& key, Value value) -> Option<ScriptError> {
		Args args{key};
		auto raw = ArgInt(args, 0, "std.string[]");
		if (raw.IsError())
			return Some(raw.Error());
		auto piece = TextOf(vm, value);
		if (piece.IsError())
			return Some(piece.Error());
		Object& o = As<Object>(self);
		std::lock_guard<std::mutex> lock(o.mutex);
		auto index = ToIndex(raw.Value(), o.text.size(), "std.string[]");
		if (index.IsError())
			return Some(index.Error());
		o.text.replace(index.Value(), 1, piece.Value());
		return NONE;
	};
	type.items = [](const HostObject& self) {
		std::vector<Value> out;
		for (char c : As<Object>(self).Get())
			out.push_back(Value::Str(String(&c, 1)));
		return out;
	};
	type.size = [](const HostObject& self) { return As<Object>(self).Get().size(); };
	type.display = [](const HostObject& self) { return ToText(As<Object>(self).Get()); };
	type.equals = [](const HostObject& a, const HostObject& b) { return As<Object>(a).Get() == As<Object>(b).Get(); };
	// `+` : un nouveau std.string ; `==` et l'ordre face à une chaîne du script.
	type.binary = [](Interpreter& vm, BinaryOp op, const Value& a,
					 const Value& b) -> Option<Result<Value, ScriptError>> {
		auto left = TextOf(vm, a);
		auto right = TextOf(vm, b);
		if (left.IsError())
			return Some(Result<Value, ScriptError>(Err(left.Error())));
		if (right.IsError())
			return Some(Result<Value, ScriptError>(Err(right.Error())));
		const int order = left.Value().compare(right.Value());
		const std::shared_ptr<const HostType>& type = (a.IsHost() ? a : b).AsHost()->type;
		switch (op) {
			case BinaryOp::ADD:
			case BinaryOp::CONCAT:
				return Some(MakeStringObject(type, left.Value() + right.Value()));
			case BinaryOp::EQUAL:
				return Some(Result<Value, ScriptError>(Ok(Value::Boolean(order == 0))));
			case BinaryOp::NOT_EQUAL:
				return Some(Result<Value, ScriptError>(Ok(Value::Boolean(order != 0))));
			case BinaryOp::LESS:
				return Some(Result<Value, ScriptError>(Ok(Value::Boolean(order < 0))));
			default:
				return NONE;
		}
	};
	type.flowIn = [](Interpreter& vm, const HostRef& self, const Value& value) -> Result<Value, ScriptError> {
		auto piece = TextOf(vm, value);
		if (piece.IsError())
			return Err(piece.Error());
		Object& o = As<Object>(self);
		std::lock_guard<std::mutex> lock(o.mutex);
		o.text += piece.Value();
		return Ok(Value::Host(self));
	};
	type.flowOut = [](Interpreter& vm, const HostRef& self, const Value& sink) -> Result<Value, ScriptError> {
		return vm.FlowInto(sink, Value::Str(ToText(As<Object>(self).Get())));
	};
}

Option<std::string> ReadLine(StreamObject& o) {
	std::lock_guard<std::mutex> lock(o.mutex);
	if (o.readPos >= o.buffer.size())
		return NONE;
	size_t end = o.buffer.find('\n', o.readPos);
	const bool newline = end != std::string::npos;
	if (!newline)
		end = o.buffer.size();
	std::string line = o.buffer.substr(o.readPos, end - o.readPos);
	o.readPos = newline ? end + 1 : end;
	if (!line.empty() && line.back() == '\r')
		line.pop_back();
	return Some(std::move(line));
}

void DefineStream(TypeBuilder& builder) {
	using Object = StreamObject;
	std::weak_ptr<const HostType> weakType = builder.type;
	builder.Construct(0, 1, [weakType](Interpreter& vm, Args& args, const Args&) -> Result<Value, ScriptError> {
		auto object = std::make_shared<Object>();
		object->type = weakType.lock();
		if (!args.empty()) {
			auto text = TextOf(vm, args[0]);
			if (text.IsError())
				return Err(text.Error());
			object->buffer = text.Unwrap();
		}
		return Ok(Value::Host(std::move(object)));
	});
	auto write = [](bool newline) {
		return [newline](Interpreter& vm, const HostRef& self, Args& args) -> Result<Value, ScriptError> {
			std::string out;
			for (size_t i = 0; i < args.size(); ++i) {
				auto text = TextOf(vm, args[i]);
				if (text.IsError())
					return Err(text.Error());
				if (i > 0)
					out.push_back(' ');
				out += text.Value();
			}
			if (newline)
				out.push_back('\n');
			Object& o = As<Object>(self);
			std::lock_guard<std::mutex> lock(o.mutex);
			o.buffer += out;
			return Ok(Value::Host(self));
		};
	};
	builder.Method("write", 0, -1, write(false));
	builder.Method("write_line", 0, -1, write(true));
	builder.Method("read", 0, 1, [](Interpreter&, const HostRef& self, Args& args) -> Result<Value, ScriptError> {
		Object& o = As<Object>(self);
		size_t count = std::string::npos;
		if (!args.empty()) {
			auto n = ArgInt(args, 0, "std.stream.read");
			if (n.IsError())
				return Err(n.Error());
			count = size_t(std::max<int64_t>(0, n.Value()));
		}
		std::lock_guard<std::mutex> lock(o.mutex);
		if (o.readPos >= o.buffer.size())
			return Ok(Value::Nil());
		std::string out = o.buffer.substr(o.readPos, count);
		o.readPos += out.size();
		return Ok(Value::Str(ToText(out)));
	});
	builder.Alias("read_all", "read");
	builder.Method("read_line", 0, 0, [](Interpreter&, const HostRef& self, Args&) -> Result<Value, ScriptError> {
		Option<std::string> line = ReadLine(As<Object>(self));
		return Ok(line.IsSome() ? Value::Str(ToText(line.Value())) : Value::Nil());
	});
	builder.Method("lines", 0, 0, [](Interpreter&, const HostRef& self, Args&) -> Result<Value, ScriptError> {
		auto list = std::make_shared<ListObject>();
		for (Option<std::string> line = ReadLine(As<Object>(self)); line.IsSome(); line = ReadLine(As<Object>(self)))
			list->items.push_back(Value::Str(ToText(line.Value())));
		return Ok(Value::List(std::move(list)));
	});
	builder.Method("str", 0, 0, [](Interpreter&, const HostRef& self, Args&) -> Result<Value, ScriptError> {
		Object& o = As<Object>(self);
		std::lock_guard<std::mutex> lock(o.mutex);
		return Ok(Value::Str(ToText(o.buffer)));
	});
	builder.Method("clear", 0, 0, [](Interpreter&, const HostRef& self, Args&) -> Result<Value, ScriptError> {
		Object& o = As<Object>(self);
		std::lock_guard<std::mutex> lock(o.mutex);
		o.buffer.clear();
		o.readPos = 0;
		return Ok(Value::Host(self));
	});
	builder.Method("eof", 0, 0, [](Interpreter&, const HostRef& self, Args&) -> Result<Value, ScriptError> {
		Object& o = As<Object>(self);
		std::lock_guard<std::mutex> lock(o.mutex);
		return Ok(Value::Boolean(o.readPos >= o.buffer.size()));
	});
	builder.Method("size", 0, 0, [](Interpreter&, const HostRef& self, Args&) -> Result<Value, ScriptError> {
		Object& o = As<Object>(self);
		std::lock_guard<std::mutex> lock(o.mutex);
		return Ok(Value::Int(int64_t(o.buffer.size())));
	});
	builder.Method("tell", 0, 0, [](Interpreter&, const HostRef& self, Args&) -> Result<Value, ScriptError> {
		Object& o = As<Object>(self);
		std::lock_guard<std::mutex> lock(o.mutex);
		return Ok(Value::Int(int64_t(o.readPos)));
	});
	builder.Method("seek", 1, 1, [](Interpreter&, const HostRef& self, Args& args) -> Result<Value, ScriptError> {
		auto pos = ArgInt(args, 0, "std.stream.seek");
		if (pos.IsError())
			return Err(pos.Error());
		Object& o = As<Object>(self);
		std::lock_guard<std::mutex> lock(o.mutex);
		o.readPos = size_t(std::clamp<int64_t>(pos.Value(), 0, int64_t(o.buffer.size())));
		return Ok(Value::Host(self));
	});
	HostType& type = *builder.type;
	type.display = [](const HostObject& self) {
		const Object& o = As<Object>(self);
		std::lock_guard<std::mutex> lock(o.mutex);
		return String::Format("<std.stream : %d octet(s), lu jusqu'à %d>", int(o.buffer.size()), int(o.readPos));
	};
	// `flux <- valeur` écrit son texte ; `flux -> puits` émet les lignes restantes.
	type.flowIn = [](Interpreter& vm, const HostRef& self, const Value& value) -> Result<Value, ScriptError> {
		auto text = TextOf(vm, value);
		if (text.IsError())
			return Err(text.Error());
		Object& o = As<Object>(self);
		std::lock_guard<std::mutex> lock(o.mutex);
		o.buffer += text.Value();
		return Ok(Value::Host(self));
	};
	type.flowOut = [](Interpreter& vm, const HostRef& self, const Value& sink) -> Result<Value, ScriptError> {
		std::vector<Value> lines;
		for (Option<std::string> line = ReadLine(As<Object>(self)); line.IsSome(); line = ReadLine(As<Object>(self)))
			lines.push_back(Value::Str(ToText(line.Value())));
		return EmitItems(vm, lines, sink);
	};
	type.items = [](const HostObject& self) {
		const Object& o = As<Object>(self);
		std::lock_guard<std::mutex> lock(o.mutex);
		std::vector<Value> out;
		size_t start = o.readPos;
		while (start < o.buffer.size()) {
			size_t end = o.buffer.find('\n', start);
			if (end == std::string::npos)
				end = o.buffer.size();
			out.push_back(Value::Str(ToText(o.buffer.substr(start, end - start))));
			start = end + 1;
		}
		return out;
	};
}

} // namespace lib

namespace lib {

// ── PipeObject ───────────────────────────────────────────────────────────────

std::vector<Value> PipeObject::Sinks() const {
std::vector<Value> out = sinks;
for (const auto& weak : backLinks)
	if (auto pipe = weak.lock())
		out.push_back(Value::Host(std::move(pipe)));
return out;
}

bool IsPipe(const Value& value) {
	return value.IsHost() && value.AsHost() && value.AsHost()->type->name == "std.pipe";
}

std::vector<const PipeObject*>& PipePath() {
	thread_local std::vector<const PipeObject*> path;
	return path;
}

Result<bool, ScriptError> Deliver(Interpreter& vm, const HostRef& pipeRef, Value value) {
	PipeObject& pipe = As<PipeObject>(pipeRef);
	std::vector<const PipeObject*>& path = PipePath();
	if (std::find(path.begin(), path.end(), &pipe) != path.end())
		return Ok(false);
	Value transform;
	std::vector<Value> sinks;
	{
		std::lock_guard<std::mutex> lock(pipe.mutex);
		if (pipe.closed)
			return Err(Fail(String("`std.pipe` : envoi dans un pipe fermé")));
		transform = pipe.transform;
		sinks = pipe.Sinks();
	}
	path.push_back(&pipe);
	struct PopGuard { // RAII : le chemin est rétabli sur tous les retours
		std::vector<const PipeObject*>& path;
		~PopGuard() {
			path.pop_back();
		}
	} guard{path};
	if (!transform.IsNil()) {
		auto mapped = vm.CallValue(transform, {value}, 0, 0);
		if (mapped.IsError())
			return Err(mapped.Error());
		if (mapped.Value().IsNil())
			return Ok(true); // écartée
		value = mapped.Unwrap();
	}
	bool forwarded = false;
	for (const Value& sink : sinks) {
		if (IsPipe(sink)) {
			auto delivered = Deliver(vm, sink.AsHost(), value);
			if (delivered.IsError())
				return delivered;
			forwarded = forwarded || delivered.Value();
		} else {
			auto delivered = vm.FlowInto(sink, value);
			if (delivered.IsError())
				return Err(delivered.Error());
			forwarded = true;
		}
	}
	if (!forwarded) {
		{
			std::lock_guard<std::mutex> lock(pipe.mutex);
			pipe.buffer.push_back(std::move(value));
		}
		pipe.ready.notify_all();
	}
	return Ok(true);
}

bool Reaches(const PipeObject& from, const PipeObject* target, int depth) {
	if (&from == target)
		return true;
	if (depth > 64)
		return false;
	std::vector<Value> sinks;
	{
		std::lock_guard<std::mutex> lock(from.mutex);
		sinks = from.sinks;
	}
	for (const Value& sink : sinks)
		if (IsPipe(sink) && Reaches(As<PipeObject>(*sink.AsHost()), target, depth + 1))
			return true;
	return false;
}

Result<Value, ScriptError> Connect(Interpreter& vm, const HostRef& pipeRef, const Value& sink) {
	PipeObject& pipe = As<PipeObject>(pipeRef);
	const bool cycle = IsPipe(sink) && Reaches(As<PipeObject>(*sink.AsHost()), &pipe);
	std::deque<Value> pending;
	{
		std::lock_guard<std::mutex> lock(pipe.mutex);
		for (const Value& existing : pipe.Sinks())
			if (IdentityOf(existing) == IdentityOf(sink) && existing.Equals(sink))
				return Ok(sink);
		if (cycle)
			pipe.backLinks.push_back(sink.AsHost());
		else
			pipe.sinks.push_back(sink);
		pending.swap(pipe.buffer);
	}
	for (Value& value : pending) {
		// Déjà transformée à son entrée : livrée telle quelle en aval.
		if (IsPipe(sink)) {
			std::vector<const PipeObject*>& path = PipePath();
			path.push_back(&pipe);
			auto delivered = Deliver(vm, sink.AsHost(), std::move(value));
			path.pop_back();
			if (delivered.IsError())
				return Err(delivered.Error());
		} else {
			auto delivered = vm.FlowInto(sink, value);
			if (delivered.IsError())
				return delivered;
		}
	}
	return Ok(sink);
}

Result<Value, ScriptError> NewPipe(const std::shared_ptr<const HostType>& type, Value transform) {
	auto pipe = std::make_shared<PipeObject>();
	pipe->type = type;
	pipe->transform = std::move(transform);
	return Ok(Value::Host(std::move(pipe)));
}

Result<Value, ScriptError> Receive(Interpreter& vm, PipeObject& pipe, double timeoutMs) {
	const auto deadline =
		std::chrono::steady_clock::now() + std::chrono::microseconds(int64_t(timeoutMs < 0 ? 0 : timeoutMs * 1000.0));
	const bool main = vm.IsMainThread();
	std::unique_lock<std::mutex> lock(pipe.mutex);
	for (;;) {
		if (!pipe.buffer.empty()) {
			Value value = std::move(pipe.buffer.front());
			pipe.buffer.pop_front();
			return Ok(value);
		}
		if (pipe.closed || vm.IsStopping())
			return Ok(Value::Nil());
		if (timeoutMs >= 0 && std::chrono::steady_clock::now() >= deadline)
			return Ok(Value::Nil());
		if (main) {
			lock.unlock();
			vm.PumpMainThread();
			lock.lock();
			if (!pipe.buffer.empty())
				continue;
		}
		pipe.ready.wait_for(lock, std::chrono::milliseconds(main ? 1 : 20));
	}
}

void DefinePipe(TypeBuilder& builder) {
	using Object = PipeObject;
	std::weak_ptr<const HostType> weakType = builder.type;
	// `std.pipe()` ou `std.pipe(fn)` (pipe transformé).
	builder.Construct(
		0, 1, [weakType](Interpreter& vm, Args& args, const Args& typeArgs) -> Result<Value, ScriptError> {
			if (!args.empty() && !vm.IsCallableValue(args[0]))
				return Err(Fail(String::Format("`std.pipe(fn)` : fonction attendue, trouvé `%s`", args[0].TypeName())));
			auto made = NewPipe(weakType.lock(), args.empty() ? Value::Nil() : args[0]);
			if (made.IsOk())
				made.Value().AsHost()->typeArgs = typeArgs;
			return made;
		});
	builder.Method("send", 1, -1, [](Interpreter& vm, const HostRef& self, Args& args) -> Result<Value, ScriptError> {
		for (const Value& arg : args) {
			auto value = Conform(vm, *self, 0, arg);
			if (value.IsError())
				return value;
			auto delivered = Deliver(vm, self, value.Unwrap());
			if (delivered.IsError())
				return Err(delivered.Error());
		}
		return Ok(Value::Host(self));
	});
	builder.Method("receive", 0, 1, [](Interpreter& vm, const HostRef& self, Args& args) -> Result<Value, ScriptError> {
		double timeout = -1.0;
		if (!args.empty()) {
			auto ms = ArgFloat(args, 0, "std.pipe.receive");
			if (ms.IsError())
				return Err(ms.Error());
			timeout = ms.Value();
		}
		return Receive(vm, As<Object>(self), timeout);
	});
	builder.Method("try_receive", 0, 0, [](Interpreter& vm, const HostRef& self, Args&) -> Result<Value, ScriptError> {
		return Receive(vm, As<Object>(self), 0.0);
	});
	builder.Method("drain", 0, 0, [](Interpreter&, const HostRef& self, Args&) -> Result<Value, ScriptError> {
		Object& o = As<Object>(self);
		std::lock_guard<std::mutex> lock(o.mutex);
		auto list = std::make_shared<ListObject>(std::vector<Value>(o.buffer.begin(), o.buffer.end()));
		o.buffer.clear();
		return Ok(Value::List(std::move(list)));
	});
	builder.Method("size", 0, 0, [](Interpreter&, const HostRef& self, Args&) -> Result<Value, ScriptError> {
		Object& o = As<Object>(self);
		std::lock_guard<std::mutex> lock(o.mutex);
		return Ok(Value::Int(int64_t(o.buffer.size())));
	});
	builder.Alias("len", "size");
	builder.Method("empty", 0, 0, [](Interpreter&, const HostRef& self, Args&) -> Result<Value, ScriptError> {
		Object& o = As<Object>(self);
		std::lock_guard<std::mutex> lock(o.mutex);
		return Ok(Value::Boolean(o.buffer.empty()));
	});
	builder.Method("close", 0, 0, [](Interpreter&, const HostRef& self, Args&) -> Result<Value, ScriptError> {
		Object& o = As<Object>(self);
		{
			std::lock_guard<std::mutex> lock(o.mutex);
			o.closed = true;
		}
		o.ready.notify_all();
		return Ok(Value::Host(self));
	});
	builder.Method("connect", 1, 1, [](Interpreter& vm, const HostRef& self, Args& args) -> Result<Value, ScriptError> {
		return Connect(vm, self, args[0]);
	});
	builder.Method("disconnect", 0, 1, [](Interpreter&, const HostRef& self, Args& args) -> Result<Value, ScriptError> {
		Object& o = As<Object>(self);
		std::vector<Value> released;
		std::lock_guard<std::mutex> lock(o.mutex);
		if (args.empty()) {
			released.swap(o.sinks);
			o.backLinks.clear();
		} else {
			for (size_t i = 0; i < o.backLinks.size();) {
				auto linked = o.backLinks[i].lock();
				if (!linked || linked.get() == IdentityOf(args[0]))
					o.backLinks.erase(o.backLinks.begin() + ptrdiff_t(i));
				else
					++i;
			}
			for (size_t i = 0; i < o.sinks.size();) {
				if (IdentityOf(o.sinks[i]) == IdentityOf(args[0]) && o.sinks[i].Equals(args[0])) {
					released.push_back(o.sinks[i]);
					o.sinks.erase(o.sinks.begin() + ptrdiff_t(i));
				} else {
					++i;
				}
			}
		}
		return Ok(Value::Int(int64_t(released.size())));
	});
	auto derived = [](bool filter) {
		return [filter](Interpreter& vm, const HostRef& self, Args& args) -> Result<Value, ScriptError> {
			if (auto error = ArgCallable(vm, args, 0, filter ? "std.pipe.filter" : "std.pipe.map"); error.IsSome())
				return Err(error.Unwrap());
			Value transform = args[0];
			if (filter) {
				// Garde la valeur si le prédicat est vrai.
				transform = MakeFunction(String("std.pipe.filter"), 1, 1,
										 [predicate = args[0]](Interpreter& vm, Args& a) -> Result<Value, ScriptError> {
											 auto keep = vm.CallValue(predicate, {a[0]}, 0, 0);
											 if (keep.IsError())
												 return keep;
											 return Ok(keep.Value().IsTruthy() ? a[0] : Value::Nil());
										 });
			}
			auto made = NewPipe(self->type, transform);
			if (made.IsError())
				return made;
			return Connect(vm, self, made.Value());
		};
	};
	builder.Method("map", 1, 1, derived(false));
	builder.Method("filter", 1, 1, derived(true));

	HostType& type = *builder.type;
	type.get = [](const HostObject& self, const String& name) -> Option<Value> {
		const Object& o = As<Object>(self);
		std::lock_guard<std::mutex> lock(o.mutex);
		if (name == "closed")
			return Some(Value::Boolean(o.closed));
		if (name == "connected")
			return Some(Value::Int(int64_t(o.Sinks().size())));
		return NONE;
	};
	type.display = [](const HostObject& self) {
		const Object& o = As<Object>(self);
		std::lock_guard<std::mutex> lock(o.mutex);
		return String::Format("<std.pipe : %d en attente, %d puits%s>", int(o.buffer.size()), int(o.Sinks().size()),
							  o.closed ? ", fermé" : "");
	};
	type.items = [](const HostObject& self) {
		const Object& o = As<Object>(self);
		std::lock_guard<std::mutex> lock(o.mutex);
		return std::vector<Value>(o.buffer.begin(), o.buffer.end());
	};
	type.flowIn = [](Interpreter& vm, const HostRef& self, const Value& value) -> Result<Value, ScriptError> {
		auto conformed = Conform(vm, *self, 0, value);
		if (conformed.IsError())
			return conformed;
		auto delivered = Deliver(vm, self, conformed.Unwrap());
		if (delivered.IsError())
			return Err(delivered.Error());
		return Ok(Value::Host(self));
	};
	// `pipe -> fonction` : un nouveau pipe transformé, connecté en aval (la
	// chaîne continue : `p -> f -> q`) ; `pipe -> puits` : connexion.
	type.flowOut = [](Interpreter& vm, const HostRef& self, const Value& sink) -> Result<Value, ScriptError> {
		if (!sink.IsHost() && !sink.IsList() && vm.IsCallableValue(sink)) {
			auto made = NewPipe(self->type, sink);
			if (made.IsError())
				return made;
			return Connect(vm, self, made.Value());
		}
		return Connect(vm, self, sink);
	};
	type.link = [](Interpreter& vm, const HostRef& self, const Value& other) -> Result<Value, ScriptError> {
		if (!IsPipe(other))
			return Err(Fail(String::Format("`<->` relie deux pipes, pas un pipe et `%s`", other.TypeName())));
		auto forward = Connect(vm, self, other);
		if (forward.IsError())
			return forward;
		auto backward = Connect(vm, other.AsHost(), Value::Host(self));
		if (backward.IsError())
			return backward;
		return Ok(Value::Host(self));
	};
}

} // namespace lib

namespace lib {

String PathText(const fs::path& path) {
	const std::string text = path.generic_string();
	return String(text.data(), text.size());
}

Result<fs::path, ScriptError> ArgPath(Interpreter& vm, const Args& args, size_t i, const char* fn) {
	if (i >= args.size())
		return Err(Fail(String::Format("`%s` : chemin attendu", fn)));
	auto text = TextOf(vm, args[i]);
	if (text.IsError())
		return Err(text.Error());
	return Ok(fs::path(text.Unwrap()));
}

ScriptError FsError(const char* fn, const fs::path& path, const std::error_code& ec) {
	return Fail(String::Format("`%s` : %s (%s)", fn, PathText(path).CStr(), ec.message().c_str()));
}

Option<std::ios::openmode> ParseMode(const std::string& mode) {
	std::string m;
	bool binary = false;
	for (char c : mode) {
		if (c == 'b')
			binary = true;
		else
			m.push_back(c);
	}
	std::ios::openmode flags;
	if (m == "r")
		flags = std::ios::in;
	else if (m == "w")
		flags = std::ios::out | std::ios::trunc;
	else if (m == "a")
		flags = std::ios::out | std::ios::app;
	else if (m == "r+" || m == "rw")
		flags = std::ios::in | std::ios::out;
	else if (m == "w+")
		flags = std::ios::in | std::ios::out | std::ios::trunc;
	else if (m == "a+")
		flags = std::ios::in | std::ios::out | std::ios::app;
	else
		return NONE;
	if (binary)
		flags |= std::ios::binary;
	return Some(flags);
}

Result<std::string, ScriptError> ReadWhole(const fs::path& path, const char* fn) {
	std::ifstream in(path, std::ios::binary);
	if (!in)
		return Err(Fail(String::Format("`%s` : impossible d'ouvrir %s", fn, PathText(path).CStr())));
	std::string out((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
	return Ok(std::move(out));
}

Option<ScriptError> WriteWhole(const fs::path& path, const std::string& text, bool append, const char* fn) {
	std::ofstream out(path, std::ios::binary | (append ? std::ios::app : std::ios::trunc));
	if (!out)
		return Some(Fail(String::Format("`%s` : impossible d'écrire %s", fn, PathText(path).CStr())));
	out.write(text.data(), std::streamsize(text.size()));
	if (!out)
		return Some(Fail(String::Format("`%s` : écriture incomplète de %s", fn, PathText(path).CStr())));
	return NONE;
}

void DefineFile(TypeBuilder& builder) {
	using Object = FileObject;
	std::weak_ptr<const HostType> weakType = builder.type;
	builder.Construct(1, 2, [weakType](Interpreter& vm, Args& args, const Args&) -> Result<Value, ScriptError> {
		auto path = ArgPath(vm, args, 0, "std.file");
		if (path.IsError())
			return Err(path.Error());
		std::string mode = "r";
		if (args.size() > 1) {
			auto m = TextOf(vm, args[1]);
			if (m.IsError())
				return Err(m.Error());
			mode = m.Unwrap();
		}
		Option<std::ios::openmode> flags = ParseMode(mode);
		if (flags.IsNone())
			return Err(Fail(String::Format("`std.file` : mode `%s` inconnu (r, w, a, r+, w+, a+, suivis ou non de b)",
										   mode.c_str())));
		auto object = std::make_shared<Object>();
		object->type = weakType.lock();
		object->path = path.Value().string();
		object->mode = mode;
		object->stream.open(path.Value(), flags.Unwrap());
		if (!object->stream.is_open())
			return Err(Fail(String::Format("`std.file` : impossible d'ouvrir %s en mode `%s` (%s)",
										   PathText(path.Value()).CStr(), mode.c_str(), std::strerror(errno))));
		return Ok(Value::Host(std::move(object)));
	});
	auto open = [](Object& o) -> Option<ScriptError> {
		if (!o.stream.is_open())
			return Some(Fail(String::Format("`std.file` : %s est fermé", o.path.c_str())));
		return NONE;
	};
	auto write = [open](bool newline) {
		return [open, newline](Interpreter& vm, const HostRef& self, Args& args) -> Result<Value, ScriptError> {
			std::string out;
			for (size_t i = 0; i < args.size(); ++i) {
				auto text = TextOf(vm, args[i]);
				if (text.IsError())
					return Err(text.Error());
				if (i > 0)
					out.push_back(' ');
				out += text.Value();
			}
			if (newline)
				out.push_back('\n');
			Object& o = As<Object>(self);
			std::lock_guard<std::mutex> lock(o.mutex);
			if (auto error = open(o); error.IsSome())
				return Err(error.Unwrap());
			o.stream.clear();
			o.stream.write(out.data(), std::streamsize(out.size()));
			if (!o.stream)
				return Err(Fail(String::Format("`std.file` : écriture impossible dans %s (mode `%s`)", o.path.c_str(),
											   o.mode.c_str())));
			return Ok(Value::Host(self));
		};
	};
	builder.Method("write", 0, -1, write(false));
	builder.Method("write_line", 0, -1, write(true));
	builder.Method("read", 0, 1, [open](Interpreter&, const HostRef& self, Args& args) -> Result<Value, ScriptError> {
		Object& o = As<Object>(self);
		std::lock_guard<std::mutex> lock(o.mutex);
		if (auto error = open(o); error.IsSome())
			return Err(error.Unwrap());
		std::string out;
		if (args.empty()) {
			out.assign(std::istreambuf_iterator<char>(o.stream), std::istreambuf_iterator<char>());
		} else {
			auto n = ArgInt(args, 0, "std.file.read");
			if (n.IsError())
				return Err(n.Error());
			out.resize(size_t(std::clamp<int64_t>(n.Value(), 0, 100000000)));
			o.stream.read(out.data(), std::streamsize(out.size()));
			out.resize(size_t(o.stream.gcount()));
		}
		if (out.empty() && o.stream.eof())
			return Ok(Value::Nil());
		return Ok(Value::Str(ToText(out)));
	});
	builder.Alias("read_all", "read");
	builder.Method("read_line", 0, 0, [open](Interpreter&, const HostRef& self, Args&) -> Result<Value, ScriptError> {
		Object& o = As<Object>(self);
		std::lock_guard<std::mutex> lock(o.mutex);
		if (auto error = open(o); error.IsSome())
			return Err(error.Unwrap());
		std::string line;
		if (!std::getline(o.stream, line))
			return Ok(Value::Nil());
		if (!line.empty() && line.back() == '\r')
			line.pop_back();
		return Ok(Value::Str(ToText(line)));
	});
	builder.Method("lines", 0, 0, [open](Interpreter&, const HostRef& self, Args&) -> Result<Value, ScriptError> {
		Object& o = As<Object>(self);
		std::lock_guard<std::mutex> lock(o.mutex);
		if (auto error = open(o); error.IsSome())
			return Err(error.Unwrap());
		auto list = std::make_shared<ListObject>();
		for (std::string line; std::getline(o.stream, line);) {
			if (!line.empty() && line.back() == '\r')
				line.pop_back();
			list->items.push_back(Value::Str(ToText(line)));
		}
		return Ok(Value::List(std::move(list)));
	});
	builder.Method("flush", 0, 0, [](Interpreter&, const HostRef& self, Args&) -> Result<Value, ScriptError> {
		Object& o = As<Object>(self);
		std::lock_guard<std::mutex> lock(o.mutex);
		o.stream.flush();
		return Ok(Value::Host(self));
	});
	builder.Method("close", 0, 0, [](Interpreter&, const HostRef& self, Args&) -> Result<Value, ScriptError> {
		Object& o = As<Object>(self);
		std::lock_guard<std::mutex> lock(o.mutex);
		if (o.stream.is_open())
			o.stream.close();
		return Ok(Value::Nil());
	});
	builder.Method("is_open", 0, 0, [](Interpreter&, const HostRef& self, Args&) -> Result<Value, ScriptError> {
		Object& o = As<Object>(self);
		std::lock_guard<std::mutex> lock(o.mutex);
		return Ok(Value::Boolean(o.stream.is_open()));
	});
	builder.Method("eof", 0, 0, [](Interpreter&, const HostRef& self, Args&) -> Result<Value, ScriptError> {
		Object& o = As<Object>(self);
		std::lock_guard<std::mutex> lock(o.mutex);
		return Ok(Value::Boolean(!o.stream.is_open() || o.stream.peek() == std::char_traits<char>::eof()));
	});
	builder.Method("tell", 0, 0, [](Interpreter&, const HostRef& self, Args&) -> Result<Value, ScriptError> {
		Object& o = As<Object>(self);
		std::lock_guard<std::mutex> lock(o.mutex);
		return Ok(Value::Int(int64_t(o.stream.tellg())));
	});
	builder.Method("seek", 1, 1, [](Interpreter&, const HostRef& self, Args& args) -> Result<Value, ScriptError> {
		auto pos = ArgInt(args, 0, "std.file.seek");
		if (pos.IsError())
			return Err(pos.Error());
		Object& o = As<Object>(self);
		std::lock_guard<std::mutex> lock(o.mutex);
		o.stream.clear();
		o.stream.seekg(std::streamoff(pos.Value()));
		o.stream.seekp(std::streamoff(pos.Value()));
		return Ok(Value::Host(self));
	});
	builder.Method("size", 0, 0, [](Interpreter&, const HostRef& self, Args&) -> Result<Value, ScriptError> {
		Object& o = As<Object>(self);
		std::lock_guard<std::mutex> lock(o.mutex);
		o.stream.flush();
		std::error_code ec;
		const auto size = fs::file_size(o.path, ec);
		return Ok(ec ? Value::Nil() : Value::UInt(uint64_t(size)));
	});
	// Fonctions de classe : le fichier entier en une fois.
	builder.StaticFn("read", 1, 1, [](Interpreter& vm, Args& args) -> Result<Value, ScriptError> {
		auto path = ArgPath(vm, args, 0, "std.file.read");
		if (path.IsError())
			return Err(path.Error());
		auto text = ReadWhole(path.Value(), "std.file.read");
		if (text.IsError())
			return Err(text.Error());
		return Ok(Value::Str(ToText(text.Value())));
	});
	auto writeAll = [](bool append) {
		return [append](Interpreter& vm, Args& args) -> Result<Value, ScriptError> {
			const char* fn = append ? "std.file.append" : "std.file.write";
			auto path = ArgPath(vm, args, 0, fn);
			if (path.IsError())
				return Err(path.Error());
			auto text = TextOf(vm, args[1]);
			if (text.IsError())
				return Err(text.Error());
			if (auto error = WriteWhole(path.Value(), text.Value(), append, fn); error.IsSome())
				return Err(error.Unwrap());
			return Ok(Value::Nil());
		};
	};
	builder.StaticFn("write", 2, 2, writeAll(false));
	builder.StaticFn("append", 2, 2, writeAll(true));
	builder.StaticFn("exists", 1, 1, [](Interpreter& vm, Args& args) -> Result<Value, ScriptError> {
		auto path = ArgPath(vm, args, 0, "std.file.exists");
		if (path.IsError())
			return Err(path.Error());
		std::error_code ec;
		return Ok(Value::Boolean(fs::is_regular_file(path.Value(), ec)));
	});
	builder.StaticFn("lines", 1, 1, [](Interpreter& vm, Args& args) -> Result<Value, ScriptError> {
		auto path = ArgPath(vm, args, 0, "std.file.lines");
		if (path.IsError())
			return Err(path.Error());
		auto text = ReadWhole(path.Value(), "std.file.lines");
		if (text.IsError())
			return Err(text.Error());
		Args none;
		for (TextFn& fn : TextFunctions())
			if (std::string_view(fn.name) == "lines")
				return fn.fn(vm, text.Value(), none);
		return Ok(Value::Nil());
	});

	HostType& type = *builder.type;
	type.get = [](const HostObject& self, const String& name) -> Option<Value> {
		const Object& o = As<Object>(self);
		if (name == "path")
			return Some(Value::Str(ToText(o.path)));
		if (name == "mode")
			return Some(Value::Str(ToText(o.mode)));
		return NONE;
	};
	type.display = [](const HostObject& self) {
		const Object& o = As<Object>(self);
		std::lock_guard<std::mutex> lock(o.mutex);
		return String::Format("<std.file %s (%s)%s>", o.path.c_str(), o.mode.c_str(),
							  o.stream.is_open() ? "" : " fermé");
	};
	type.flowIn = [write](Interpreter& vm, const HostRef& self, const Value& value) -> Result<Value, ScriptError> {
		Args args{value};
		return write(false)(vm, self, args);
	};
	type.flowOut = [open](Interpreter& vm, const HostRef& self, const Value& sink) -> Result<Value, ScriptError> {
		std::vector<Value> lines;
		{
			Object& o = As<Object>(self);
			std::lock_guard<std::mutex> lock(o.mutex);
			if (auto error = open(o); error.IsSome())
				return Err(error.Unwrap());
			for (std::string line; std::getline(o.stream, line);) {
				if (!line.empty() && line.back() == '\r')
					line.pop_back();
				lines.push_back(Value::Str(ToText(line)));
			}
		}
		return EmitItems(vm, lines, sink);
	};
}

void InstallFilesystem(Interpreter& vm) {
	const String ns("std.filesystem");
	constexpr auto ANY = Interpreter::NativeThread::ANY;
	auto predicate = [&vm, ns](const char* name, bool (*test)(const fs::path&, std::error_code&)) {
		vm.RegisterNamespacedNative(
			ns, String(name), 1, 1,
			[test, name](Interpreter& vm, Args& args) -> Result<Value, ScriptError> {
				auto path = ArgPath(vm, args, 0, name);
				if (path.IsError())
					return Err(path.Error());
				std::error_code ec;
				return Ok(Value::Boolean(test(path.Value(), ec)));
			},
			ANY);
	};
	predicate("exists", [](const fs::path& p, std::error_code& ec) { return fs::exists(p, ec); });
	predicate("is_file", [](const fs::path& p, std::error_code& ec) { return fs::is_regular_file(p, ec); });
	predicate("is_dir", [](const fs::path& p, std::error_code& ec) { return fs::is_directory(p, ec); });
	predicate("is_empty", [](const fs::path& p, std::error_code& ec) { return fs::is_empty(p, ec); });
	predicate("is_symlink", [](const fs::path& p, std::error_code& ec) { return fs::is_symlink(p, ec); });

	// Chemins : pur calcul, aucune entrée/sortie.
	auto pathPart = [&vm, ns](const char* name, fs::path (*part)(const fs::path&)) {
		vm.RegisterNamespacedNative(
			ns, String(name), 1, 1,
			[part, name](Interpreter& vm, Args& args) -> Result<Value, ScriptError> {
				auto path = ArgPath(vm, args, 0, name);
				if (path.IsError())
					return Err(path.Error());
				return Ok(Value::Str(PathText(part(path.Value()))));
			},
			ANY);
	};
	pathPart("parent", [](const fs::path& p) { return p.parent_path(); });
	pathPart("filename", [](const fs::path& p) { return p.filename(); });
	pathPart("stem", [](const fs::path& p) { return p.stem(); });
	pathPart("extension", [](const fs::path& p) { return p.extension(); });
	pathPart("normalize", [](const fs::path& p) { return p.lexically_normal(); });
	vm.RegisterNamespacedNative(
		ns, String("join"), 1, -1,
		[](Interpreter& vm, Args& args) -> Result<Value, ScriptError> {
			fs::path out;
			for (size_t i = 0; i < args.size(); ++i) {
				auto part = ArgPath(vm, args, i, "std.filesystem.join");
				if (part.IsError())
					return Err(part.Error());
				out /= part.Value();
			}
			return Ok(Value::Str(PathText(out)));
		},
		ANY);
	auto resolve = [&vm, ns](const char* name, bool canonical) {
		vm.RegisterNamespacedNative(
			ns, String(name), 1, 1,
			[name, canonical](Interpreter& vm, Args& args) -> Result<Value, ScriptError> {
				auto path = ArgPath(vm, args, 0, name);
				if (path.IsError())
					return Err(path.Error());
				std::error_code ec;
				fs::path out = canonical ? fs::canonical(path.Value(), ec) : fs::absolute(path.Value(), ec);
				if (ec)
					return Err(FsError(name, path.Value(), ec));
				return Ok(Value::Str(PathText(out)));
			},
			ANY);
	};
	resolve("absolute", false);
	resolve("canonical", true);
	vm.RegisterNamespacedNative(
		ns, String("relative"), 1, 2,
		[](Interpreter& vm, Args& args) -> Result<Value, ScriptError> {
			auto path = ArgPath(vm, args, 0, "std.filesystem.relative");
			if (path.IsError())
				return Err(path.Error());
			std::error_code ec;
			fs::path base = fs::current_path(ec);
			if (args.size() > 1) {
				auto b = ArgPath(vm, args, 1, "std.filesystem.relative");
				if (b.IsError())
					return Err(b.Error());
				base = b.Unwrap();
			}
			fs::path out = fs::relative(path.Value(), base, ec);
			if (ec)
				return Err(FsError("std.filesystem.relative", path.Value(), ec));
			return Ok(Value::Str(PathText(out)));
		},
		ANY);
	vm.RegisterNamespacedNative(
		ns, String("current_path"), 0, 0,
		[](Interpreter&, Args&) -> Result<Value, ScriptError> {
			std::error_code ec;
			fs::path out = fs::current_path(ec);
			if (ec)
				return Err(FsError("std.filesystem.current_path", out, ec));
			return Ok(Value::Str(PathText(out)));
		},
		ANY);
	vm.RegisterNamespacedNative(
		ns, String("temp_dir"), 0, 0,
		[](Interpreter&, Args&) -> Result<Value, ScriptError> {
			std::error_code ec;
			fs::path out = fs::temp_directory_path(ec);
			if (ec)
				return Err(FsError("std.filesystem.temp_dir", out, ec));
			return Ok(Value::Str(PathText(out)));
		},
		ANY);

	// Contenu d'un dossier : noms (triés) ; `walk` : chemins relatifs, récursif.
	auto listing = [&vm, ns](const char* name, bool recursive) {
		vm.RegisterNamespacedNative(
			ns, String(name), 0, 1,
			[name, recursive](Interpreter& vm, Args& args) -> Result<Value, ScriptError> {
				fs::path root(".");
				if (!args.empty()) {
					auto path = ArgPath(vm, args, 0, name);
					if (path.IsError())
						return Err(path.Error());
					root = path.Unwrap();
				}
				std::error_code ec;
				std::vector<std::string> names;
				if (recursive) {
					fs::recursive_directory_iterator it(root, ec), end;
					for (; !ec && it != end; it.increment(ec))
						names.push_back(it->path().lexically_relative(root).generic_string());
				} else {
					fs::directory_iterator it(root, ec), end;
					for (; !ec && it != end; it.increment(ec))
						names.push_back(it->path().filename().generic_string());
				}
				if (ec)
					return Err(FsError(name, root, ec));
				std::sort(names.begin(), names.end());
				auto list = std::make_shared<ListObject>();
				for (const std::string& entry : names)
					list->items.push_back(Value::Str(ToText(entry)));
				return Ok(Value::List(std::move(list)));
			},
			ANY);
	};
	listing("list", false);
	listing("walk", true);

	vm.RegisterNamespacedNative(
		ns, String("mkdir"), 1, 1,
		[](Interpreter& vm, Args& args) -> Result<Value, ScriptError> {
			auto path = ArgPath(vm, args, 0, "std.filesystem.mkdir");
			if (path.IsError())
				return Err(path.Error());
			std::error_code ec;
			const bool created = fs::create_directories(path.Value(), ec);
			if (ec)
				return Err(FsError("std.filesystem.mkdir", path.Value(), ec));
			return Ok(Value::Boolean(created));
		},
		ANY);
	vm.RegisterNamespacedNative(
		ns, String("remove"), 1, 1,
		[](Interpreter& vm, Args& args) -> Result<Value, ScriptError> {
			auto path = ArgPath(vm, args, 0, "std.filesystem.remove");
			if (path.IsError())
				return Err(path.Error());
			std::error_code ec;
			const bool removed = fs::remove(path.Value(), ec);
			if (ec)
				return Err(FsError("std.filesystem.remove", path.Value(), ec));
			return Ok(Value::Boolean(removed));
		},
		ANY);
	vm.RegisterNamespacedNative(
		ns, String("remove_all"), 1, 1,
		[](Interpreter& vm, Args& args) -> Result<Value, ScriptError> {
			auto path = ArgPath(vm, args, 0, "std.filesystem.remove_all");
			if (path.IsError())
				return Err(path.Error());
			std::error_code ec;
			const auto removed = fs::remove_all(path.Value(), ec);
			if (ec)
				return Err(FsError("std.filesystem.remove_all", path.Value(), ec));
			return Ok(Value::UInt(uint64_t(removed)));
		},
		ANY);
	auto twoPaths = [&vm, ns](const char* name, void (*op)(const fs::path&, const fs::path&, std::error_code&)) {
		vm.RegisterNamespacedNative(
			ns, String(name), 2, 2,
			[name, op](Interpreter& vm, Args& args) -> Result<Value, ScriptError> {
				auto from = ArgPath(vm, args, 0, name);
				auto to = ArgPath(vm, args, 1, name);
				if (from.IsError())
					return Err(from.Error());
				if (to.IsError())
					return Err(to.Error());
				std::error_code ec;
				op(from.Value(), to.Value(), ec);
				if (ec)
					return Err(FsError(name, from.Value(), ec));
				return Ok(Value::Nil());
			},
			ANY);
	};
	twoPaths("rename", [](const fs::path& a, const fs::path& b, std::error_code& ec) { fs::rename(a, b, ec); });
	twoPaths("copy", [](const fs::path& a, const fs::path& b, std::error_code& ec) {
		fs::copy(a, b, fs::copy_options::recursive | fs::copy_options::overwrite_existing, ec);
	});
	vm.RegisterNamespacedNative(
		ns, String("size"), 1, 1,
		[](Interpreter& vm, Args& args) -> Result<Value, ScriptError> {
			auto path = ArgPath(vm, args, 0, "std.filesystem.size");
			if (path.IsError())
				return Err(path.Error());
			std::error_code ec;
			const auto size = fs::file_size(path.Value(), ec);
			if (ec)
				return Err(FsError("std.filesystem.size", path.Value(), ec));
			return Ok(Value::UInt(uint64_t(size)));
		},
		ANY);
	// Date de dernière modification : secondes depuis l'époque Unix.
	vm.RegisterNamespacedNative(
		ns, String("last_write_time"), 1, 1,
		[](Interpreter& vm, Args& args) -> Result<Value, ScriptError> {
			auto path = ArgPath(vm, args, 0, "std.filesystem.last_write_time");
			if (path.IsError())
				return Err(path.Error());
			std::error_code ec;
			const auto stamp = fs::last_write_time(path.Value(), ec);
			if (ec)
				return Err(FsError("std.filesystem.last_write_time", path.Value(), ec));
			const auto system = std::chrono::clock_cast<std::chrono::system_clock>(stamp);
			return Ok(Value::Number(std::chrono::duration<double>(system.time_since_epoch()).count()));
		},
		ANY);
}

void InstallOs(Interpreter& vm) {
	const String ns("std.os");
	constexpr auto ANY = Interpreter::NativeThread::ANY;
#if defined(_WIN32)
	const char* platform = "windows";
#elif defined(__APPLE__)
	const char* platform = "macos";
#elif defined(__linux__)
	const char* platform = "linux";
#else
	const char* platform = "unknown";
#endif
#if defined(__x86_64__) || defined(_M_X64)
	const char* arch = "x86_64";
#elif defined(__aarch64__) || defined(_M_ARM64)
	const char* arch = "arm64";
#elif defined(__i386__) || defined(_M_IX86)
	const char* arch = "x86";
#else
	const char* arch = "unknown";
#endif
	vm.RegisterNamespaceConstant(ns, String("platform"), Value::Str(String(platform)));
	vm.RegisterNamespaceConstant(ns, String("arch"), Value::Str(String(arch)));
	vm.RegisterNamespaceConstant(ns, String("path_separator"),
								 Value::Str(String(platform == std::string_view("windows") ? "\\" : "/")));
	vm.RegisterNamespacedNative(
		ns, String("env"), 1, 2,
		[](Interpreter& vm, Args& args) -> Result<Value, ScriptError> {
			auto name = TextOf(vm, args[0]);
			if (name.IsError())
				return Err(name.Error());
			const char* value = std::getenv(name.Value().c_str());
			if (!value)
				return Ok(args.size() > 1 ? args[1] : Value::Nil());
			return Ok(Value::Str(String(value)));
		},
		ANY);
	vm.RegisterNamespacedNative(
		ns, String("set_env"), 2, 2,
		[](Interpreter& vm, Args& args) -> Result<Value, ScriptError> {
			auto name = TextOf(vm, args[0]);
			auto value = TextOf(vm, args[1]);
			if (name.IsError())
				return Err(name.Error());
			if (value.IsError())
				return Err(value.Error());
#if defined(_WIN32)
			const bool ok = _putenv_s(name.Value().c_str(), value.Value().c_str()) == 0;
#else
			const bool ok = ::setenv(name.Value().c_str(), value.Value().c_str(), 1) == 0;
#endif
			return Ok(Value::Boolean(ok));
		},
		Interpreter::NativeThread::MAIN);
	vm.RegisterNamespacedNative(
		ns, String("cpu_count"), 0, 0,
		[](Interpreter&, Args&) -> Result<Value, ScriptError> {
			return Ok(Value::Int(int64_t(std::thread::hardware_concurrency())));
		},
		ANY);
	vm.RegisterNamespacedNative(
		ns, String("pid"), 0, 0,
		[](Interpreter&, Args&) -> Result<Value, ScriptError> {
#if defined(_WIN32)
			return Ok(Value::Int(int64_t(_getpid())));
#else
			return Ok(Value::Int(int64_t(::getpid())));
#endif
		},
		ANY);
	vm.RegisterNamespacedNative(
		ns, String("hostname"), 0, 0,
		[](Interpreter&, Args&) -> Result<Value, ScriptError> {
#if defined(_WIN32)
			const char* name = std::getenv("COMPUTERNAME");
			return Ok(name ? Value::Str(String(name)) : Value::Nil());
#else
			char buffer[256] = {};
			if (::gethostname(buffer, sizeof(buffer) - 1) != 0)
				return Ok(Value::Nil());
			return Ok(Value::Str(String(buffer)));
#endif
		},
		ANY);
	vm.RegisterNamespacedNative(
		ns, String("home"), 0, 0,
		[](Interpreter&, Args&) -> Result<Value, ScriptError> {
			const char* home = std::getenv("HOME");
			if (!home)
				home = std::getenv("USERPROFILE");
			return Ok(home ? Value::Str(String(home)) : Value::Nil());
		},
		ANY);
	vm.RegisterNamespacedNative(
		ns, String("user"), 0, 0,
		[](Interpreter&, Args&) -> Result<Value, ScriptError> {
			const char* user = std::getenv("USER");
			if (!user)
				user = std::getenv("USERNAME");
			return Ok(user ? Value::Str(String(user)) : Value::Nil());
		},
		ANY);
	// Temps : secondes depuis l'époque Unix (horloge murale) ; `clock` :
	// secondes monotones (mesures de durée).
	vm.RegisterNamespacedNative(
		ns, String("time"), 0, 0,
		[](Interpreter&, Args&) -> Result<Value, ScriptError> {
			const auto now = std::chrono::system_clock::now().time_since_epoch();
			return Ok(Value::Number(std::chrono::duration<double>(now).count()));
		},
		ANY);
	vm.RegisterNamespacedNative(
		ns, String("clock"), 0, 0,
		[](Interpreter&, Args&) -> Result<Value, ScriptError> {
			const auto now = std::chrono::steady_clock::now().time_since_epoch();
			return Ok(Value::Number(std::chrono::duration<double>(now).count()));
		},
		ANY);
	if (Option<Value> sleep = vm.GetGlobal(String("sleep")); sleep.IsSome())
		vm.RegisterNamespaceConstant(ns, String("sleep"), sleep.Unwrap());
}

} // namespace lib

namespace lib {

std::tm BreakDown(Millis at, bool utc) {
	const std::time_t seconds = std::chrono::system_clock::to_time_t(std::chrono::floor<std::chrono::seconds>(at));
	std::tm out{};
#if defined(_WIN32)
	if (utc)
		gmtime_s(&out, &seconds);
	else
		localtime_s(&out, &seconds);
#else
	if (utc)
		gmtime_r(&seconds, &out);
	else
		localtime_r(&seconds, &out);
#endif
	return out;
}

Millis Compose(std::tm fields, int64_t millis, bool utc) {
	fields.tm_isdst = -1;
#if defined(_WIN32)
	const std::time_t seconds = utc ? _mkgmtime(&fields) : std::mktime(&fields);
#else
	const std::time_t seconds = utc ? timegm(&fields) : std::mktime(&fields);
#endif
	return Millis(std::chrono::milliseconds(int64_t(seconds) * 1000 + millis));
}

Result<String, ScriptError> FormatTm(const std::tm& fields, const std::string& format) {
	if (format.empty())
		return Ok(String());
	char buffer[512];
	const size_t written = std::strftime(buffer, sizeof(buffer), format.c_str(), &fields);
	if (written == 0)
		return Err(Fail(String::Format("`format` : le motif `%s` produit un texte vide ou trop long", format.c_str())));
	return Ok(String(buffer, written));
}

Option<std::tm> ParseTm(const std::string& text, const std::string& format) {
	std::tm fields{};
	std::istringstream in(text);
	in >> std::get_time(&fields, format.c_str());
	if (in.fail())
		return NONE;
	return Some(fields);
}

int64_t IsoWeekday(int tmWday) {
	return tmWday == 0 ? 7 : tmWday;
}

Value MakeDateTime(const std::shared_ptr<const HostType>& type, Millis at, bool utc) {
	auto object = std::make_shared<DateTimeObject>();
	object->type = type;
	object->at = at;
	object->utc = utc;
	return Value::Host(std::move(object));
}

Value MakeDate(const std::shared_ptr<const HostType>& type, std::chrono::sys_days day) {
	auto object = std::make_shared<DateObject>();
	object->type = type;
	object->day = day;
	return Value::Host(std::move(object));
}

Value MakeTime(const std::shared_ptr<const HostType>& type, int64_t millis) {
	auto object = std::make_shared<TimeObject>();
	object->type = type;
	object->millis = ((millis % MS_PER_DAY) + MS_PER_DAY) % MS_PER_DAY;
	return Value::Host(std::move(object));
}

std::tm DateFields(std::chrono::sys_days day) {
	const std::chrono::year_month_day ymd(day);
	const std::chrono::weekday wd(day);
	const std::chrono::sys_days jan1 = std::chrono::sys_days(ymd.year() / std::chrono::January / 1);
	std::tm fields{};
	fields.tm_year = int(ymd.year()) - 1900;
	fields.tm_mon = int(unsigned(ymd.month())) - 1;
	fields.tm_mday = int(unsigned(ymd.day()));
	fields.tm_wday = int(wd.c_encoding());
	fields.tm_yday = int((day - jan1).count());
	return fields;
}

std::tm TimeFields(int64_t millis) {
	std::tm fields{};
	fields.tm_year = 70;
	fields.tm_mday = 1;
	fields.tm_hour = int(millis / 3600000);
	fields.tm_min = int(millis / 60000 % 60);
	fields.tm_sec = int(millis / 1000 % 60);
	return fields;
}

Result<std::chrono::sys_days, ScriptError> MakeDay(int64_t y, int64_t m, int64_t d, bool clampDay, const char* fn) {
	using namespace std::chrono;
	if (y < -32767 || y > 32767 || m < 1 || m > 12)
		return Err(Fail(String::Format("`%s` : date invalide %lld-%lld-%lld", fn, static_cast<long long>(y),
									   static_cast<long long>(m), static_cast<long long>(d))));
	const year_month_day_last last{year(int(y)), month_day_last(month(unsigned(m)))};
	const int64_t maxDay = int64_t(unsigned(last.day()));
	if (clampDay)
		d = std::clamp<int64_t>(d, 1, maxDay);
	if (d < 1 || d > maxDay)
		return Err(Fail(String::Format("`%s` : date invalide %lld-%02lld-%02lld", fn, static_cast<long long>(y),
									   static_cast<long long>(m), static_cast<long long>(d))));
	return Ok(sys_days(year_month_day(year(int(y)), month(unsigned(m)), day(unsigned(d)))));
}

Result<std::string, ScriptError> FormatArg(Interpreter& vm, const Args& args, size_t i, const char* fallback) {
	if (i >= args.size())
		return Ok(std::string(fallback));
	return TextOf(vm, args[i]);
}

void DefineDateTime(TypeBuilder& builder, TypeBuilder& dateBuilder, TypeBuilder& timeBuilder) {
	using namespace std::chrono;
	std::weak_ptr<const HostType> weakType = builder.type, weakDate = dateBuilder.type, weakTime = timeBuilder.type;
	auto now = [](bool utc, std::weak_ptr<const HostType> type) {
		return [utc, type](Interpreter&, Args&) -> Result<Value, ScriptError> {
			return Ok(MakeDateTime(type.lock(), floor<milliseconds>(system_clock::now()), utc));
		};
	};
	builder.Construct(0, 7, [weakType](Interpreter&, Args& args, const Args&) -> Result<Value, ScriptError> {
		if (args.empty())
			return Ok(MakeDateTime(weakType.lock(), floor<milliseconds>(system_clock::now()), false));
		if (args.size() < 3)
			return Err(Fail(String("`std.datetime(année, mois, jour[, h, min, s, ms])`")));
		int64_t v[7] = {0, 1, 1, 0, 0, 0, 0};
		for (size_t i = 0; i < args.size(); ++i) {
			auto n = ArgInt(args, i, "std.datetime");
			if (n.IsError())
				return Err(n.Error());
			v[i] = n.Value();
		}
		auto day = MakeDay(v[0], v[1], v[2], false, "std.datetime");
		if (day.IsError())
			return Err(day.Error());
		if (v[3] < 0 || v[3] > 23 || v[4] < 0 || v[4] > 59 || v[5] < 0 || v[5] > 60 || v[6] < 0 || v[6] > 999)
			return Err(Fail(String("`std.datetime` : heure invalide")));
		std::tm fields = DateFields(day.Value());
		fields.tm_hour = int(v[3]);
		fields.tm_min = int(v[4]);
		fields.tm_sec = int(v[5]);
		return Ok(MakeDateTime(weakType.lock(), Compose(fields, v[6], false), false));
	});
	builder.StaticFn("now", 0, 0, now(false, weakType));
	builder.StaticFn("utc_now", 0, 0, now(true, weakType));
	builder.StaticFn("from_timestamp", 1, 2, [weakType](Interpreter&, Args& args) -> Result<Value, ScriptError> {
		auto seconds = ArgFloat(args, 0, "std.datetime.from_timestamp");
		if (seconds.IsError())
			return Err(seconds.Error());
		const bool utc = args.size() > 1 && args[1].IsTruthy();
		return Ok(
			MakeDateTime(weakType.lock(), Millis(milliseconds(int64_t(std::llround(seconds.Value() * 1000.0)))), utc));
	});
	builder.StaticFn("parse", 1, 2, [weakType](Interpreter& vm, Args& args) -> Result<Value, ScriptError> {
		auto text = TextOf(vm, args[0]);
		if (text.IsError())
			return Err(text.Error());
		std::vector<std::string> formats = {"%Y-%m-%d %H:%M:%S", "%Y-%m-%dT%H:%M:%S", "%Y-%m-%d %H:%M", "%Y-%m-%d"};
		if (args.size() > 1) {
			auto format = TextOf(vm, args[1]);
			if (format.IsError())
				return Err(format.Error());
			formats = {format.Unwrap()};
		}
		for (const std::string& format : formats)
			if (Option<std::tm> fields = ParseTm(text.Value(), format); fields.IsSome())
				return Ok(MakeDateTime(weakType.lock(), Compose(fields.Unwrap(), 0, false), false));
		return Ok(Value::Nil());
	});
	builder.Method("format", 0, 1, [](Interpreter& vm, const HostRef& self, Args& args) -> Result<Value, ScriptError> {
		auto format = FormatArg(vm, args, 0, "%Y-%m-%d %H:%M:%S");
		if (format.IsError())
			return Err(format.Error());
		const DateTimeObject& o = As<DateTimeObject>(self);
		auto text = FormatTm(BreakDown(o.at, o.utc), format.Value());
		if (text.IsError())
			return Err(text.Error());
		return Ok(Value::Str(text.Unwrap()));
	});
	builder.Method("iso", 0, 0, [](Interpreter&, const HostRef& self, Args&) -> Result<Value, ScriptError> {
		const DateTimeObject& o = As<DateTimeObject>(self);
		auto text = FormatTm(BreakDown(o.at, o.utc), o.utc ? "%Y-%m-%dT%H:%M:%SZ" : "%Y-%m-%dT%H:%M:%S");
		if (text.IsError())
			return Err(text.Error());
		return Ok(Value::Str(text.Unwrap()));
	});
	auto add = [](int64_t unitMs, const char* name) {
		return [unitMs, name](Interpreter&, const HostRef& self, Args& args) -> Result<Value, ScriptError> {
			auto n = ArgFloat(args, 0, name);
			if (n.IsError())
				return Err(n.Error());
			const DateTimeObject& o = As<DateTimeObject>(self);
			return Ok(MakeDateTime(self->type, o.at + milliseconds(int64_t(std::llround(n.Value() * double(unitMs)))),
								   o.utc));
		};
	};
	builder.Method("add_seconds", 1, 1, add(1000, "std.datetime.add_seconds"));
	builder.Method("add_minutes", 1, 1, add(60000, "std.datetime.add_minutes"));
	builder.Method("add_hours", 1, 1, add(3600000, "std.datetime.add_hours"));
	builder.Method("add_days", 1, 1, add(MS_PER_DAY, "std.datetime.add_days"));
	builder.Method("to_utc", 0, 0, [](Interpreter&, const HostRef& self, Args&) -> Result<Value, ScriptError> {
		return Ok(MakeDateTime(self->type, As<DateTimeObject>(self).at, true));
	});
	builder.Method("to_local", 0, 0, [](Interpreter&, const HostRef& self, Args&) -> Result<Value, ScriptError> {
		return Ok(MakeDateTime(self->type, As<DateTimeObject>(self).at, false));
	});
	builder.Method("date", 0, 0, [weakDate](Interpreter&, const HostRef& self, Args&) -> Result<Value, ScriptError> {
		const DateTimeObject& o = As<DateTimeObject>(self);
		const std::tm fields = BreakDown(o.at, o.utc);
		auto day = MakeDay(fields.tm_year + 1900, fields.tm_mon + 1, fields.tm_mday, false, "date");
		if (day.IsError())
			return Err(day.Error());
		return Ok(MakeDate(weakDate.lock(), day.Value()));
	});
	builder.Method("time", 0, 0, [weakTime](Interpreter&, const HostRef& self, Args&) -> Result<Value, ScriptError> {
		const DateTimeObject& o = As<DateTimeObject>(self);
		const std::tm fields = BreakDown(o.at, o.utc);
		const int64_t ms = o.at.time_since_epoch().count() % 1000;
		return Ok(
			MakeTime(weakTime.lock(), (int64_t(fields.tm_hour) * 3600 + fields.tm_min * 60 + fields.tm_sec) * 1000 +
										  (ms + 1000) % 1000));
	});
	HostType& type = *builder.type;
	type.get = [](const HostObject& self, const String& name) -> Option<Value> {
		const DateTimeObject& o = As<DateTimeObject>(self);
		const std::tm f = BreakDown(o.at, o.utc);
		if (name == "year")
			return Some(Value::Int(f.tm_year + 1900));
		if (name == "month")
			return Some(Value::Int(f.tm_mon + 1));
		if (name == "day")
			return Some(Value::Int(f.tm_mday));
		if (name == "hour")
			return Some(Value::Int(f.tm_hour));
		if (name == "minute")
			return Some(Value::Int(f.tm_min));
		if (name == "second")
			return Some(Value::Int(f.tm_sec));
		if (name == "millisecond")
			return Some(Value::Int((o.at.time_since_epoch().count() % 1000 + 1000) % 1000));
		if (name == "weekday")
			return Some(Value::Int(IsoWeekday(f.tm_wday)));
		if (name == "yearday")
			return Some(Value::Int(f.tm_yday + 1));
		if (name == "timestamp")
			return Some(Value::Number(double(o.at.time_since_epoch().count()) / 1000.0));
		if (name == "utc")
			return Some(Value::Boolean(o.utc));
		return NONE;
	};
	type.display = [](const HostObject& self) {
		const DateTimeObject& o = As<DateTimeObject>(self);
		auto text = FormatTm(BreakDown(o.at, o.utc), "%Y-%m-%d %H:%M:%S");
		return text.IsOk() ? text.Unwrap() : String("<std.datetime>");
	};
	type.equals = [](const HostObject& a, const HostObject& b) {
		return As<DateTimeObject>(a).at == As<DateTimeObject>(b).at;
	};
	// `b - a` : secondes (f64) ; `d ± secondes` : un nouveau datetime ; ordre.
	type.binary = [](Interpreter&, BinaryOp op, const Value& a, const Value& b) -> Option<Result<Value, ScriptError>> {
		using R = Result<Value, ScriptError>;
		const bool left = a.IsHost() && dynamic_cast<const DateTimeObject*>(a.AsHost().get());
		const bool right = b.IsHost() && dynamic_cast<const DateTimeObject*>(b.AsHost().get());
		if (left && right) {
			const Millis x = As<DateTimeObject>(*a.AsHost()).at, y = As<DateTimeObject>(*b.AsHost()).at;
			switch (op) {
				case BinaryOp::SUBTRACT:
					return Some(R(Ok(Value::Number(double((x - y).count()) / 1000.0))));
				case BinaryOp::EQUAL:
					return Some(R(Ok(Value::Boolean(x == y))));
				case BinaryOp::NOT_EQUAL:
					return Some(R(Ok(Value::Boolean(x != y))));
				case BinaryOp::LESS:
					return Some(R(Ok(Value::Boolean(x < y))));
				default:
					return NONE;
			}
		}
		if (left && b.IsNumber() && (op == BinaryOp::ADD || op == BinaryOp::SUBTRACT)) {
			const DateTimeObject& o = As<DateTimeObject>(*a.AsHost());
			const auto delta = milliseconds(int64_t(std::llround(b.AsNumber() * 1000.0)));
			return Some(
				R(Ok(MakeDateTime(a.AsHost()->type, op == BinaryOp::ADD ? o.at + delta : o.at - delta, o.utc))));
		}
		if (right && a.IsNumber() && op == BinaryOp::ADD) {
			const DateTimeObject& o = As<DateTimeObject>(*b.AsHost());
			return Some(R(Ok(MakeDateTime(b.AsHost()->type,
										  o.at + milliseconds(int64_t(std::llround(a.AsNumber() * 1000.0))), o.utc))));
		}
		return NONE;
	};

	// ── std.date ──
	dateBuilder.Construct(0, 3, [weakDate](Interpreter&, Args& args, const Args&) -> Result<Value, ScriptError> {
		if (args.empty()) {
			const std::tm f = BreakDown(floor<milliseconds>(system_clock::now()), false);
			auto day = MakeDay(f.tm_year + 1900, f.tm_mon + 1, f.tm_mday, false, "std.date");
			return day.IsError() ? Result<Value, ScriptError>(Err(day.Error()))
								 : Ok(MakeDate(weakDate.lock(), day.Value()));
		}
		if (args.size() != 3)
			return Err(Fail(String("`std.date(année, mois, jour)`")));
		int64_t v[3];
		for (size_t i = 0; i < 3; ++i) {
			auto n = ArgInt(args, i, "std.date");
			if (n.IsError())
				return Err(n.Error());
			v[i] = n.Value();
		}
		auto day = MakeDay(v[0], v[1], v[2], false, "std.date");
		if (day.IsError())
			return Err(day.Error());
		return Ok(MakeDate(weakDate.lock(), day.Value()));
	});
	dateBuilder.StaticFn("today", 0, 0, [weakDate](Interpreter&, Args&) -> Result<Value, ScriptError> {
		const std::tm f = BreakDown(floor<milliseconds>(system_clock::now()), false);
		auto day = MakeDay(f.tm_year + 1900, f.tm_mon + 1, f.tm_mday, false, "std.date.today");
		if (day.IsError())
			return Err(day.Error());
		return Ok(MakeDate(weakDate.lock(), day.Value()));
	});
	dateBuilder.StaticFn("is_leap", 1, 1, [](Interpreter&, Args& args) -> Result<Value, ScriptError> {
		auto y = ArgInt(args, 0, "std.date.is_leap");
		if (y.IsError())
			return Err(y.Error());
		return Ok(Value::Boolean(year(int(y.Value())).is_leap()));
	});
	dateBuilder.StaticFn("days_in_month", 2, 2, [](Interpreter&, Args& args) -> Result<Value, ScriptError> {
		auto y = ArgInt(args, 0, "std.date.days_in_month");
		auto m = ArgInt(args, 1, "std.date.days_in_month");
		if (y.IsError())
			return Err(y.Error());
		if (m.IsError())
			return Err(m.Error());
		if (m.Value() < 1 || m.Value() > 12)
			return Err(Fail(String("`std.date.days_in_month` : mois invalide")));
		const year_month_day_last last{year(int(y.Value())), month_day_last(month(unsigned(m.Value())))};
		return Ok(Value::Int(int64_t(unsigned(last.day()))));
	});
	dateBuilder.StaticFn("parse", 1, 2, [weakDate](Interpreter& vm, Args& args) -> Result<Value, ScriptError> {
		auto text = TextOf(vm, args[0]);
		if (text.IsError())
			return Err(text.Error());
		auto format = FormatArg(vm, args, 1, "%Y-%m-%d");
		if (format.IsError())
			return Err(format.Error());
		Option<std::tm> f = ParseTm(text.Value(), format.Value());
		if (f.IsNone())
			return Ok(Value::Nil());
		auto day = MakeDay(f.Value().tm_year + 1900, f.Value().tm_mon + 1, f.Value().tm_mday, false, "std.date.parse");
		if (day.IsError())
			return Ok(Value::Nil());
		return Ok(MakeDate(weakDate.lock(), day.Value()));
	});
	dateBuilder.Method("add_days", 1, 1,
					   [](Interpreter&, const HostRef& self, Args& args) -> Result<Value, ScriptError> {
						   auto n = ArgInt(args, 0, "std.date.add_days");
						   if (n.IsError())
							   return Err(n.Error());
						   return Ok(MakeDate(self->type, As<DateObject>(self).day + days(n.Value())));
					   });
	auto addMonths = [](bool years) {
		return [years](Interpreter&, const HostRef& self, Args& args) -> Result<Value, ScriptError> {
			auto n = ArgInt(args, 0, years ? "std.date.add_years" : "std.date.add_months");
			if (n.IsError())
				return Err(n.Error());
			const year_month_day ymd(As<DateObject>(self).day);
			int64_t total =
				int64_t(int(ymd.year())) * 12 + int64_t(unsigned(ymd.month())) - 1 + n.Value() * (years ? 12 : 1);
			const int64_t y = total >= 0 ? total / 12 : (total - 11) / 12;
			const int64_t m = total - y * 12 + 1;
			auto day = MakeDay(y, m, int64_t(unsigned(ymd.day())), true, "std.date.add_months");
			if (day.IsError())
				return Err(day.Error());
			return Ok(MakeDate(self->type, day.Value()));
		};
	};
	dateBuilder.Method("add_months", 1, 1, addMonths(false));
	dateBuilder.Method("add_years", 1, 1, addMonths(true));
	dateBuilder.Method("format", 0, 1,
					   [](Interpreter& vm, const HostRef& self, Args& args) -> Result<Value, ScriptError> {
						   auto format = FormatArg(vm, args, 0, "%Y-%m-%d");
						   if (format.IsError())
							   return Err(format.Error());
						   auto text = FormatTm(DateFields(As<DateObject>(self).day), format.Value());
						   if (text.IsError())
							   return Err(text.Error());
						   return Ok(Value::Str(text.Unwrap()));
					   });
	dateBuilder.Method("to_datetime", 0, 3,
					   [weakType](Interpreter&, const HostRef& self, Args& args) -> Result<Value, ScriptError> {
						   std::tm fields = DateFields(As<DateObject>(self).day);
						   int* slots[3] = {&fields.tm_hour, &fields.tm_min, &fields.tm_sec};
						   for (size_t i = 0; i < args.size(); ++i) {
							   auto n = ArgInt(args, i, "std.date.to_datetime");
							   if (n.IsError())
								   return Err(n.Error());
							   *slots[i] = int(n.Value());
						   }
						   return Ok(MakeDateTime(weakType.lock(), Compose(fields, 0, false), false));
					   });
	HostType& dateType = *dateBuilder.type;
	dateType.get = [](const HostObject& self, const String& name) -> Option<Value> {
		const sys_days day = As<DateObject>(self).day;
		const year_month_day ymd(day);
		if (name == "year")
			return Some(Value::Int(int(ymd.year())));
		if (name == "month")
			return Some(Value::Int(unsigned(ymd.month())));
		if (name == "day")
			return Some(Value::Int(unsigned(ymd.day())));
		if (name == "weekday")
			return Some(Value::Int(int64_t(weekday(day).iso_encoding())));
		if (name == "yearday")
			return Some(Value::Int(DateFields(day).tm_yday + 1));
		if (name == "is_leap")
			return Some(Value::Boolean(ymd.year().is_leap()));
		return NONE;
	};
	dateType.display = [](const HostObject& self) {
		auto text = FormatTm(DateFields(As<DateObject>(self).day), "%Y-%m-%d");
		return text.IsOk() ? text.Unwrap() : String("<std.date>");
	};
	dateType.equals = [](const HostObject& a, const HostObject& b) {
		return As<DateObject>(a).day == As<DateObject>(b).day;
	};
	dateType.binary = [](Interpreter&, BinaryOp op, const Value& a,
						 const Value& b) -> Option<Result<Value, ScriptError>> {
		using R = Result<Value, ScriptError>;
		const bool left = a.IsHost() && dynamic_cast<const DateObject*>(a.AsHost().get());
		const bool right = b.IsHost() && dynamic_cast<const DateObject*>(b.AsHost().get());
		if (left && right) {
			const sys_days x = As<DateObject>(*a.AsHost()).day, y = As<DateObject>(*b.AsHost()).day;
			switch (op) {
				case BinaryOp::SUBTRACT:
					return Some(R(Ok(Value::Int(int64_t((x - y).count())))));
				case BinaryOp::EQUAL:
					return Some(R(Ok(Value::Boolean(x == y))));
				case BinaryOp::NOT_EQUAL:
					return Some(R(Ok(Value::Boolean(x != y))));
				case BinaryOp::LESS:
					return Some(R(Ok(Value::Boolean(x < y))));
				default:
					return NONE;
			}
		}
		if (left && b.IsNumber() && (op == BinaryOp::ADD || op == BinaryOp::SUBTRACT)) {
			const auto delta = days(b.AsInt64());
			const sys_days day = As<DateObject>(*a.AsHost()).day;
			return Some(R(Ok(MakeDate(a.AsHost()->type, op == BinaryOp::ADD ? day + delta : day - delta))));
		}
		return NONE;
	};

	// ── std.time ──
	timeBuilder.Construct(0, 4, [weakTime](Interpreter&, Args& args, const Args&) -> Result<Value, ScriptError> {
		if (args.empty()) {
			const Millis now = floor<milliseconds>(system_clock::now());
			const std::tm f = BreakDown(now, false);
			return Ok(MakeTime(weakTime.lock(), (int64_t(f.tm_hour) * 3600 + f.tm_min * 60 + f.tm_sec) * 1000 +
													now.time_since_epoch().count() % 1000));
		}
		int64_t v[4] = {0, 0, 0, 0};
		const int64_t limits[4] = {23, 59, 59, 999};
		for (size_t i = 0; i < args.size(); ++i) {
			auto n = ArgInt(args, i, "std.time");
			if (n.IsError())
				return Err(n.Error());
			if (n.Value() < 0 || n.Value() > limits[i])
				return Err(Fail(String::Format("`std.time` : composante %d hors bornes (%lld)", int(i + 1),
											   static_cast<long long>(n.Value()))));
			v[i] = n.Value();
		}
		return Ok(MakeTime(weakTime.lock(), ((v[0] * 60 + v[1]) * 60 + v[2]) * 1000 + v[3]));
	});
	timeBuilder.StaticFn("now", 0, 0, [weakTime](Interpreter&, Args&) -> Result<Value, ScriptError> {
		const Millis now = floor<milliseconds>(system_clock::now());
		const std::tm f = BreakDown(now, false);
		return Ok(MakeTime(weakTime.lock(), (int64_t(f.tm_hour) * 3600 + f.tm_min * 60 + f.tm_sec) * 1000 +
												now.time_since_epoch().count() % 1000));
	});
	timeBuilder.StaticFn("parse", 1, 1, [weakTime](Interpreter& vm, Args& args) -> Result<Value, ScriptError> {
		auto text = TextOf(vm, args[0]);
		if (text.IsError())
			return Err(text.Error());
		int h = 0, m = 0, s = 0, ms = 0;
		const int read = std::sscanf(text.Value().c_str(), "%d:%d:%d.%d", &h, &m, &s, &ms);
		if (read < 2 || h < 0 || h > 23 || m < 0 || m > 59 || s < 0 || s > 59 || ms < 0 || ms > 999)
			return Ok(Value::Nil());
		return Ok(MakeTime(weakTime.lock(), ((int64_t(h) * 60 + m) * 60 + s) * 1000 + ms));
	});
	timeBuilder.StaticFn("timestamp", 0, 0, [](Interpreter&, Args&) -> Result<Value, ScriptError> {
		return Ok(Value::Number(duration<double>(system_clock::now().time_since_epoch()).count()));
	});
	timeBuilder.StaticFn("clock", 0, 0, [](Interpreter&, Args&) -> Result<Value, ScriptError> {
		return Ok(Value::Number(duration<double>(steady_clock::now().time_since_epoch()).count()));
	});
	timeBuilder.Method(
		"add_seconds", 1, 1, [](Interpreter&, const HostRef& self, Args& args) -> Result<Value, ScriptError> {
			auto n = ArgFloat(args, 0, "std.time.add_seconds");
			if (n.IsError())
				return Err(n.Error());
			return Ok(MakeTime(self->type, As<TimeObject>(self).millis + int64_t(std::llround(n.Value() * 1000.0))));
		});
	timeBuilder.Method("format", 0, 1,
					   [](Interpreter& vm, const HostRef& self, Args& args) -> Result<Value, ScriptError> {
						   auto format = FormatArg(vm, args, 0, "%H:%M:%S");
						   if (format.IsError())
							   return Err(format.Error());
						   auto text = FormatTm(TimeFields(As<TimeObject>(self).millis), format.Value());
						   if (text.IsError())
							   return Err(text.Error());
						   return Ok(Value::Str(text.Unwrap()));
					   });
	HostType& timeType = *timeBuilder.type;
	timeType.get = [](const HostObject& self, const String& name) -> Option<Value> {
		const int64_t ms = As<TimeObject>(self).millis;
		if (name == "hour")
			return Some(Value::Int(ms / 3600000));
		if (name == "minute")
			return Some(Value::Int(ms / 60000 % 60));
		if (name == "second")
			return Some(Value::Int(ms / 1000 % 60));
		if (name == "millisecond")
			return Some(Value::Int(ms % 1000));
		if (name == "total_seconds")
			return Some(Value::Number(double(ms) / 1000.0));
		return NONE;
	};
	timeType.display = [](const HostObject& self) {
		const int64_t ms = As<TimeObject>(self).millis;
		String text = String::Format("%02d:%02d:%02d", int(ms / 3600000), int(ms / 60000 % 60), int(ms / 1000 % 60));
		if (ms % 1000)
			text.Concat(String::Format(".%03d", int(ms % 1000)));
		return text;
	};
	timeType.equals = [](const HostObject& a, const HostObject& b) {
		return As<TimeObject>(a).millis == As<TimeObject>(b).millis;
	};
	timeType.binary = [](Interpreter&, BinaryOp op, const Value& a,
						 const Value& b) -> Option<Result<Value, ScriptError>> {
		using R = Result<Value, ScriptError>;
		const bool left = a.IsHost() && dynamic_cast<const TimeObject*>(a.AsHost().get());
		const bool right = b.IsHost() && dynamic_cast<const TimeObject*>(b.AsHost().get());
		if (left && right) {
			const int64_t x = As<TimeObject>(*a.AsHost()).millis, y = As<TimeObject>(*b.AsHost()).millis;
			switch (op) {
				case BinaryOp::SUBTRACT:
					return Some(R(Ok(Value::Number(double(x - y) / 1000.0))));
				case BinaryOp::EQUAL:
					return Some(R(Ok(Value::Boolean(x == y))));
				case BinaryOp::NOT_EQUAL:
					return Some(R(Ok(Value::Boolean(x != y))));
				case BinaryOp::LESS:
					return Some(R(Ok(Value::Boolean(x < y))));
				default:
					return NONE;
			}
		}
		if (left && b.IsNumber() && (op == BinaryOp::ADD || op == BinaryOp::SUBTRACT)) {
			const int64_t delta = int64_t(std::llround(b.AsNumber() * 1000.0));
			const int64_t ms = As<TimeObject>(*a.AsHost()).millis;
			return Some(R(Ok(MakeTime(a.AsHost()->type, op == BinaryOp::ADD ? ms + delta : ms - delta))));
		}
		return NONE;
	};
}

} // namespace lib

namespace lib {

Value MakeOption(const std::shared_ptr<const HostType>& type, Option<Value> value) {
	auto object = std::make_shared<OptionObject>();
	object->type = type;
	object->some = value.IsSome();
	if (value.IsSome())
		object->value = value.Unwrap();
	return Value::Host(std::move(object));
}

void DefineOption(TypeBuilder& builder) {
	using Object = OptionObject;
	std::weak_ptr<const HostType> weakType = builder.type;
	builder.Construct(0, 1, [weakType](Interpreter&, Args& args, const Args&) -> Result<Value, ScriptError> {
		const bool some = !args.empty() && !args[0].IsNil();
		return Ok(MakeOption(weakType.lock(), some ? Option<Value>(Some(args[0])) : Option<Value>(NONE)));
	});
	builder.StaticFn("some", 1, 1, [weakType](Interpreter&, Args& args) -> Result<Value, ScriptError> {
		return Ok(MakeOption(weakType.lock(), Some(args[0])));
	});
	builder.StaticFn("none", 0, 0, [weakType](Interpreter&, Args&) -> Result<Value, ScriptError> {
		return Ok(MakeOption(weakType.lock(), NONE));
	});
	builder.Method("is_some", 0, 0, [](Interpreter&, const HostRef& self, Args&) -> Result<Value, ScriptError> {
		return Ok(Value::Boolean(As<Object>(self).some));
	});
	builder.Method("is_none", 0, 0, [](Interpreter&, const HostRef& self, Args&) -> Result<Value, ScriptError> {
		return Ok(Value::Boolean(!As<Object>(self).some));
	});
	builder.Method("unwrap", 0, 0, [](Interpreter&, const HostRef& self, Args&) -> Result<Value, ScriptError> {
		if (!As<Object>(self).some)
			return Err(Fail(String("`option.unwrap` sur None")));
		return Ok(As<Object>(self).value);
	});
	builder.Method("expect", 1, 1, [](Interpreter&, const HostRef& self, Args& args) -> Result<Value, ScriptError> {
		if (!As<Object>(self).some)
			return Err(Fail(args[0].ToDisplayString()));
		return Ok(As<Object>(self).value);
	});
	builder.Method("unwrap_or", 1, 1, [](Interpreter&, const HostRef& self, Args& args) -> Result<Value, ScriptError> {
		return Ok(As<Object>(self).some ? As<Object>(self).value : args[0]);
	});
	builder.Method("unwrap_or_else", 1, 1,
				   [](Interpreter& vm, const HostRef& self, Args& args) -> Result<Value, ScriptError> {
					   if (As<Object>(self).some)
						   return Ok(As<Object>(self).value);
					   return vm.CallValue(args[0], {}, 0, 0);
				   });
	builder.Method("map", 1, 1, [](Interpreter& vm, const HostRef& self, Args& args) -> Result<Value, ScriptError> {
		if (!As<Object>(self).some)
			return Ok(Value::Host(self));
		auto mapped = vm.CallValue(args[0], {As<Object>(self).value}, 0, 0);
		if (mapped.IsError())
			return mapped;
		return Ok(MakeOption(self->type, Some(mapped.Unwrap())));
	});
	builder.Method("and_then", 1, 1,
				   [](Interpreter& vm, const HostRef& self, Args& args) -> Result<Value, ScriptError> {
					   if (!As<Object>(self).some)
						   return Ok(Value::Host(self));
					   return vm.CallValue(args[0], {As<Object>(self).value}, 0, 0);
				   });
	builder.Method("or_else", 1, 1, [](Interpreter& vm, const HostRef& self, Args& args) -> Result<Value, ScriptError> {
		if (As<Object>(self).some)
			return Ok(Value::Host(self));
		return vm.CallValue(args[0], {}, 0, 0);
	});
	builder.Method("filter", 1, 1, [](Interpreter& vm, const HostRef& self, Args& args) -> Result<Value, ScriptError> {
		if (!As<Object>(self).some)
			return Ok(Value::Host(self));
		auto keep = vm.CallValue(args[0], {As<Object>(self).value}, 0, 0);
		if (keep.IsError())
			return keep;
		return Ok(keep.Value().IsTruthy() ? Value::Host(self) : MakeOption(self->type, NONE));
	});
	HostType& type = *builder.type;
	type.display = [](const HostObject& self) {
		const Object& o = As<Object>(self);
		return o.some ? String::Format("Some(%s)", o.value.ToDisplayString().CStr()) : String("None");
	};
	type.equals = [](const HostObject& a, const HostObject& b) {
		const Object &x = As<Object>(a), &y = As<Object>(b);
		return x.some == y.some && (!x.some || x.value.Equals(y.value));
	};
	type.items = [](const HostObject& self) {
		const Object& o = As<Object>(self);
		return o.some ? std::vector<Value>{o.value} : std::vector<Value>{};
	};
}

Value MakeResult(const std::shared_ptr<const HostType>& type, bool ok, Value value) {
	auto object = std::make_shared<ResultObject>();
	object->type = type;
	object->ok = ok;
	object->value = std::move(value);
	return Value::Host(std::move(object));
}

void DefineResult(TypeBuilder& builder, std::weak_ptr<const HostType> optionType) {
	using Object = ResultObject;
	std::weak_ptr<const HostType> weakType = builder.type;
	builder.StaticFn("ok", 0, 1, [weakType](Interpreter&, Args& args) -> Result<Value, ScriptError> {
		return Ok(MakeResult(weakType.lock(), true, args.empty() ? Value::Nil() : args[0]));
	});
	builder.StaticFn("err", 1, 1, [weakType](Interpreter&, Args& args) -> Result<Value, ScriptError> {
		return Ok(MakeResult(weakType.lock(), false, args[0]));
	});
	builder.StaticFn("of", 1, -1, [weakType](Interpreter& vm, Args& args) -> Result<Value, ScriptError> {
		Args rest(args.begin() + 1, args.end());
		auto outcome = vm.CallValue(args[0], std::move(rest), 0, 0);
		if (outcome.IsError())
			return Ok(MakeResult(weakType.lock(), false, Value::Str(outcome.Error().message)));
		return Ok(MakeResult(weakType.lock(), true, outcome.Unwrap()));
	});
	builder.Method("is_ok", 0, 0, [](Interpreter&, const HostRef& self, Args&) -> Result<Value, ScriptError> {
		return Ok(Value::Boolean(As<Object>(self).ok));
	});
	builder.Method("is_err", 0, 0, [](Interpreter&, const HostRef& self, Args&) -> Result<Value, ScriptError> {
		return Ok(Value::Boolean(!As<Object>(self).ok));
	});
	builder.Method("unwrap", 0, 0, [](Interpreter&, const HostRef& self, Args&) -> Result<Value, ScriptError> {
		const Object& o = As<Object>(self);
		if (!o.ok)
			return Err(Fail(String::Format("`result.unwrap` sur Err(%s)", o.value.ToDisplayString().CStr())));
		return Ok(o.value);
	});
	builder.Method("expect", 1, 1, [](Interpreter&, const HostRef& self, Args& args) -> Result<Value, ScriptError> {
		const Object& o = As<Object>(self);
		if (!o.ok)
			return Err(
				Fail(String::Format("%s : %s", args[0].ToDisplayString().CStr(), o.value.ToDisplayString().CStr())));
		return Ok(o.value);
	});
	builder.Method("unwrap_err", 0, 0, [](Interpreter&, const HostRef& self, Args&) -> Result<Value, ScriptError> {
		const Object& o = As<Object>(self);
		if (o.ok)
			return Err(Fail(String("`result.unwrap_err` sur Ok")));
		return Ok(o.value);
	});
	builder.Method("unwrap_or", 1, 1, [](Interpreter&, const HostRef& self, Args& args) -> Result<Value, ScriptError> {
		return Ok(As<Object>(self).ok ? As<Object>(self).value : args[0]);
	});
	builder.Method("map", 1, 1, [](Interpreter& vm, const HostRef& self, Args& args) -> Result<Value, ScriptError> {
		if (!As<Object>(self).ok)
			return Ok(Value::Host(self));
		auto mapped = vm.CallValue(args[0], {As<Object>(self).value}, 0, 0);
		if (mapped.IsError())
			return mapped;
		return Ok(MakeResult(self->type, true, mapped.Unwrap()));
	});
	builder.Method("map_err", 1, 1, [](Interpreter& vm, const HostRef& self, Args& args) -> Result<Value, ScriptError> {
		if (As<Object>(self).ok)
			return Ok(Value::Host(self));
		auto mapped = vm.CallValue(args[0], {As<Object>(self).value}, 0, 0);
		if (mapped.IsError())
			return mapped;
		return Ok(MakeResult(self->type, false, mapped.Unwrap()));
	});
	builder.Method("and_then", 1, 1,
				   [](Interpreter& vm, const HostRef& self, Args& args) -> Result<Value, ScriptError> {
					   if (!As<Object>(self).ok)
						   return Ok(Value::Host(self));
					   return vm.CallValue(args[0], {As<Object>(self).value}, 0, 0);
				   });
	// `ok()` / `err()` : la valeur ou l'erreur, en option.
	builder.Method("ok", 0, 0, [optionType](Interpreter&, const HostRef& self, Args&) -> Result<Value, ScriptError> {
		const Object& o = As<Object>(self);
		return Ok(MakeOption(optionType.lock(), o.ok ? Option<Value>(Some(o.value)) : Option<Value>(NONE)));
	});
	builder.Method("err", 0, 0, [optionType](Interpreter&, const HostRef& self, Args&) -> Result<Value, ScriptError> {
		const Object& o = As<Object>(self);
		return Ok(MakeOption(optionType.lock(), !o.ok ? Option<Value>(Some(o.value)) : Option<Value>(NONE)));
	});
	HostType& type = *builder.type;
	type.display = [](const HostObject& self) {
		const Object& o = As<Object>(self);
		return String::Format(o.ok ? "Ok(%s)" : "Err(%s)", o.value.ToDisplayString().CStr());
	};
	type.equals = [](const HostObject& a, const HostObject& b) {
		return As<Object>(a).ok == As<Object>(b).ok && As<Object>(a).value.Equals(As<Object>(b).value);
	};
}

void DefineFuture(Interpreter& vm, TypeBuilder& builder) {
	builder.Construct(1, -1, [](Interpreter& vm, Args& args, const Args&) -> Result<Value, ScriptError> {
		if (auto error = ArgCallable(vm, args, 0, "std.future"); error.IsSome())
			return Err(error.Unwrap());
		Value fn = args[0];
		Args rest(args.begin() + 1, args.end());
		return vm.SpawnTask([&vm, fn, rest]() mutable -> Result<Value, ScriptError> {
			auto result = vm.CallValue(fn, std::move(rest), 0, 0);
			if (result.IsError())
				return result;
			return vm.AwaitValue(result.Value());
		});
	});
	for (const char* name : {"value", "error", "all", "delayed"})
		if (Option<Value> member = vm.GetNamespaceMember(String("future"), String(name)); member.IsSome())
			builder.Static(name, member.Unwrap());
}

Option<ScriptError> LockMutex(Interpreter& vm, MutexObject& m) {
	const bool main = vm.IsMainThread();
	std::unique_lock<std::mutex> lock(m.mutex);
	while (m.locked) {
		if (vm.IsStopping())
			return Some(Fail(String("`std.mutex.lock` interrompu : l'interpréteur s'arrête")));
		if (main) {
			lock.unlock();
			vm.PumpMainThread();
			lock.lock();
			if (!m.locked)
				break;
		}
		m.released.wait_for(lock, std::chrono::milliseconds(main ? 1 : 20));
	}
	m.locked = true;
	return NONE;
}

void UnlockMutex(MutexObject& m) {
	{
		std::lock_guard<std::mutex> lock(m.mutex);
		m.locked = false;
	}
	m.released.notify_one();
}

// ── LockGuardObject ──────────────────────────────────────────────────────────

LockGuardObject::~LockGuardObject() {
if (held && mutex)
	UnlockMutex(static_cast<MutexObject&>(*mutex));
}

void DefineMutex(TypeBuilder& builder, TypeBuilder& guardBuilder) {
	using Object = MutexObject;
	std::weak_ptr<const HostType> weakType = builder.type, weakGuard = guardBuilder.type;
	builder.Construct(0, 0, [weakType](Interpreter&, Args&, const Args&) -> Result<Value, ScriptError> {
		auto object = std::make_shared<Object>();
		object->type = weakType.lock();
		return Ok(Value::Host(std::move(object)));
	});
	builder.Method("lock", 0, 0, [](Interpreter& vm, const HostRef& self, Args&) -> Result<Value, ScriptError> {
		if (auto error = LockMutex(vm, As<Object>(self)); error.IsSome())
			return Err(error.Unwrap());
		return Ok(Value::Nil());
	});
	builder.Method("try_lock", 0, 0, [](Interpreter&, const HostRef& self, Args&) -> Result<Value, ScriptError> {
		Object& m = As<Object>(self);
		std::lock_guard<std::mutex> lock(m.mutex);
		if (m.locked)
			return Ok(Value::Boolean(false));
		m.locked = true;
		return Ok(Value::Boolean(true));
	});
	builder.Method("unlock", 0, 0, [](Interpreter&, const HostRef& self, Args&) -> Result<Value, ScriptError> {
		Object& m = As<Object>(self);
		{
			std::lock_guard<std::mutex> lock(m.mutex);
			if (!m.locked)
				return Err(Fail(String("`std.mutex.unlock` : le verrou n'est pas pris")));
		}
		UnlockMutex(m);
		return Ok(Value::Nil());
	});
	builder.Method("with", 1, 1, [](Interpreter& vm, const HostRef& self, Args& args) -> Result<Value, ScriptError> {
		if (auto error = ArgCallable(vm, args, 0, "std.mutex.with"); error.IsSome())
			return Err(error.Unwrap());
		if (auto error = LockMutex(vm, As<Object>(self)); error.IsSome())
			return Err(error.Unwrap());
		auto result = vm.CallValue(args[0], {}, 0, 0);
		UnlockMutex(As<Object>(self)); // déverrouillé même en cas d'erreur
		return result;
	});
	builder.type->get = [](const HostObject& self, const String& name) -> Option<Value> {
		if (name != "locked")
			return NONE;
		auto& m = const_cast<Object&>(As<Object>(self));
		std::lock_guard<std::mutex> lock(m.mutex);
		return Some(Value::Boolean(m.locked));
	};

	guardBuilder.Construct(1, 1, [weakGuard](Interpreter& vm, Args& args, const Args&) -> Result<Value, ScriptError> {
		if (!args[0].IsHost() || !dynamic_cast<Object*>(args[0].AsHost().get()))
			return Err(
				Fail(String::Format("`std.lock_guard` : un `std.mutex` attendu, trouvé `%s`", args[0].TypeName())));
		if (auto error = LockMutex(vm, As<Object>(args[0].AsHost())); error.IsSome())
			return Err(error.Unwrap());
		auto guard = std::make_shared<LockGuardObject>();
		guard->type = weakGuard.lock();
		guard->mutex = args[0].AsHost();
		guard->held = true;
		return Ok(Value::Host(std::move(guard)));
	});
	// Déverrouillage anticipé (sinon : à la destruction de la garde).
	guardBuilder.Method("release", 0, 0, [](Interpreter&, const HostRef& self, Args&) -> Result<Value, ScriptError> {
		LockGuardObject& guard = As<LockGuardObject>(self);
		if (guard.held) {
			guard.held = false;
			UnlockMutex(static_cast<MutexObject&>(*guard.mutex));
		}
		return Ok(Value::Nil());
	});
}

} // namespace lib

void InstallStdLibrary(Interpreter& vm) {
	InstallCoreGlobals(vm);
	using namespace lib;
	auto declare = [&vm](TypeBuilder& builder, const char* name) {
		vm.RegisterHostType(String("std"), String(name), builder.type);
	};

	TypeBuilder vector("std.vector"), list("std.list"), deque("std.deque");
	DefineSequence<std::vector<Value>>(vector, "std.vector");
	DefineSequence<std::list<Value>>(list, "std.list");
	DefineSequence<std::deque<Value>>(deque, "std.deque");
	declare(vector, "vector");
	declare(list, "list");
	declare(deque, "deque");

	TypeBuilder queue("std.queue"), stack("std.stack"), priority("std.priority_queue");
	DefineAdapter(queue, AdapterObject::Kind::QUEUE);
	DefineAdapter(stack, AdapterObject::Kind::STACK);
	DefineAdapter(priority, AdapterObject::Kind::PRIORITY);
	declare(queue, "queue");
	declare(stack, "stack");
	declare(priority, "priority_queue");

	TypeBuilder map("std.map"), unorderedMap("std.unordered_map"), set("std.set"), unorderedSet("std.unordered_set");
	DefineTable<OrderedMap>(map);
	DefineTable<HashMap>(unorderedMap);
	DefineTable<OrderedSet>(set);
	DefineTable<HashSet>(unorderedSet);
	declare(map, "map");
	declare(unorderedMap, "unordered_map");
	declare(set, "set");
	declare(unorderedSet, "unordered_set");

	TypeBuilder string("std.string"), stream("std.stream"), pipe("std.pipe"), file("std.file");
	DefineString(string);
	DefineStream(stream);
	DefinePipe(pipe);
	DefineFile(file);
	declare(string, "string");
	declare(stream, "stream");
	declare(pipe, "pipe");
	declare(file, "file");

	TypeBuilder datetime("std.datetime"), date("std.date"), time("std.time");
	DefineDateTime(datetime, date, time);
	declare(datetime, "datetime");
	declare(date, "date");
	declare(time, "time");

	TypeBuilder option("std.option"), result("std.result"), future("std.future"), mutex("std.mutex"),
		guard("std.lock_guard");
	DefineOption(option);
	DefineResult(result, option.type);
	DefineFuture(vm, future);
	DefineMutex(mutex, guard);
	declare(option, "option");
	declare(result, "result");
	declare(future, "future");
	declare(mutex, "mutex");
	declare(guard, "lock_guard");

	InstallFilesystem(vm);
	InstallOs(vm);

	// `std.sort(liste[, comparateur])` : tri stable EN PLACE d'une liste.
	vm.RegisterNamespacedNative(
		String("std"), String("sort"), 1, 2,
		[](Interpreter& vm, Args& args) -> Result<Value, ScriptError> {
			auto list = detail::ArgList(args, 0, "std.sort");
			if (list.IsError())
				return Err(list.Error());
			std::vector<Value> items = list.Value()->Snapshot();
			if (auto error = SortValues(vm, items, args.size() > 1 ? args[1] : Value::Nil()); error.IsSome())
				return Err(error.Unwrap());
			list.Value()->Replace(std::move(items));
			return Ok(args[0]);
		},
		Interpreter::NativeThread::ANY);

	// Les fonctions globales du langage, aussi rangées sous `std.` (sans
	// écraser les types ci-dessus : `std.list` est la liste chaînée,
	// `std.map` la table ordonnée).
	vm.ExportGlobalsTo(String("std"));
}

} // namespace data::script
