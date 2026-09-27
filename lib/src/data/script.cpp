#include "data/script.hpp"

namespace data::script {

void InstallDataLibrary(Interpreter &vm) {
	// Calcul pur et fichiers : sûrs sur n'importe quel fil (une fonction
	// `async` peut décoder un gros document sans passer par le fil principal).
	constexpr Interpreter::NativeThread ANY = Interpreter::NativeThread::ANY;

	vm.RegisterNative("parse", 2, 2, [](Interpreter &, std::vector<Value> &args) -> Result<Value, ScriptError> {
		auto format = detail::ArgString(args, 0, "parse");
		if (format.IsError())
			return Err(format.Error());
		auto text = detail::ArgString(args, 1, "parse");
		if (text.IsError())
			return Err(text.Error());

		std::unique_ptr<Document> doc = detail::MakeDocumentFor(format.Value());
		if (!doc)
			return Err(Interpreter::MakeError(String::Format("`parse` : format inconnu `%s`",
															 format.Value().CStr())));
		auto error = doc->DecodeStr(text.Value());
		if (error.IsSome())
			return Err(Interpreter::MakeError(
				String::Format("`parse` (%s) : %s", format.Value().CStr(), error.Unwrap().Format().CStr())));
		return Ok(ValueFromNode(doc->GetRoot()));
	}, ANY);

	vm.RegisterNative("encode", 2, 2, [](Interpreter &, std::vector<Value> &args) -> Result<Value, ScriptError> {
		auto format = detail::ArgString(args, 0, "encode");
		if (format.IsError())
			return Err(format.Error());
		std::unique_ptr<Document> doc = detail::MakeDocumentFor(format.Value());
		if (!doc)
			return Err(Interpreter::MakeError(String::Format("`encode` : format inconnu `%s`",
															 format.Value().CStr())));
		doc->SetRoot(NodeFromValue(args[1]));
		return Ok(Value::Str(doc->EncodeStr()));
	}, ANY);

	vm.RegisterNative("read_file", 1, 1, [](Interpreter &, std::vector<Value> &args) -> Result<Value, ScriptError> {
		auto path = detail::ArgString(args, 0, "read_file");
		if (path.IsError())
			return Err(path.Error());
		auto content = LoadScriptFile(path.Value());
		// Fichier illisible => `nil`, pas une erreur : un script qui teste
		// l'existence d'un projet ne doit pas s'interrompre pour ça.
		if (content.IsError())
			return Ok(Value::Nil());
		return Ok(Value::Str(content.Value()));
	}, ANY);

	vm.RegisterNative("write_file", 2, 2, [](Interpreter &, std::vector<Value> &args) -> Result<Value, ScriptError> {
		auto path = detail::ArgString(args, 0, "write_file");
		if (path.IsError())
			return Err(path.Error());
		auto text = detail::ArgString(args, 1, "write_file");
		if (text.IsError())
			return Err(text.Error());
		return Ok(Value::Boolean(sdl3::WriteFile(path.Value(), text.Value().CStr(), text.Value().GetSize())));
	}, ANY);

	vm.RegisterNative("load", 2, 2, [](Interpreter &vmRef, std::vector<Value> &args) -> Result<Value, ScriptError> {
		auto format = detail::ArgString(args, 0, "load");
		if (format.IsError())
			return Err(format.Error());
		auto path = detail::ArgString(args, 1, "load");
		if (path.IsError())
			return Err(path.Error());
		auto content = LoadScriptFile(path.Value());
		if (content.IsError())
			return Err(Interpreter::MakeError(content.Error()));

		Option<Value> parse = vmRef.GetGlobal(String("parse"));
		if (parse.IsNone())
			return Err(Interpreter::MakeError(String("`load` : `parse` a été retirée de l'environnement")));
		std::vector<Value> parseArgs = {args[0], Value::Str(content.Value())};
		return vmRef.CallValue(parse.Unwrap(), std::move(parseArgs), 0, 0);
	}, ANY);
}

} /* namespace data::script */