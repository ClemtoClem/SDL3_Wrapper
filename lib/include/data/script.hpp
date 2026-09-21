#pragma once
/**
 * data::script — langage de script interprété embarqué (« Sled »), point
 * d'entrée unique du sous-module.
 *
 * Ce fichier assemble lexeur → parseur → interpréteur (voir chaque en-tête
 * pour les décisions de conception) et ajoute la seule partie qui dépend des
 * CODECS de `data::` : `InstallDataLibrary()`, qui expose aux scripts
 * l'aller-retour texte ⇄ structure pour les sept formats déjà supportés par
 * le module (JSON, YAML, TOML, XML, INI, CSV, CSS), via le pont
 * `Value` ⇄ `data::Node` de script_value.hpp.
 *
 * C'est cette couche qui répond à « utiliser le module data pour les formats
 * JSON (glTF) des textures » : un script lit un `.gltf` avec
 * `parse("json", texte)` et navigue dedans comme dans une table native.
 *
 * @code{.cpp}
 * data::script::Interpreter vm;
 * data::script::InstallDataLibrary(vm);
 * vm.RegisterNative("spawn", 3, 3, [&](auto&, auto& args) -> Result<Value, ScriptError> {
 *     world.Spawn(args[0].AsFloat(), args[1].AsFloat(), args[2].AsFloat());
 *     return Ok(Value::Nil());
 * });
 * auto result = vm.Run("for i in range(0, 5) { spawn(i * 2, 0, 0) }");
 * if (result.IsError())
 *     std::cerr << result.Error().Format().CStr() << "\n";
 * @endcode
 */
#include "csv.hpp"
#include "document.hpp"
#include "ini.hpp"
#include "json.hpp"
#include "script/script_ast.hpp"
#include "script/script_interpreter.hpp"
#include "script/script_lexer.hpp"
#include "script/script_parser.hpp"
#include "script/script_value.hpp"
#include "toml.hpp"
#include "xml.hpp"
#include "yaml.hpp"

#include "../sdl3/iostream.hpp"

namespace data::script {

// ============================================================================
// Chargement de fichiers
// ============================================================================

/// Lit un fichier de script en mémoire. Err porte déjà un message lisible
/// (l'erreur SDL d'origine) — l'appelant n'a rien à reformuler.
[[nodiscard]] inline Result<String, String> LoadScriptFile(const String &path) {
	auto bytes = sdl3::ReadFile(path);
	if (!bytes)
		return Err(String::Format("%s : %s", path.CStr(), String(bytes.Error()).CStr()));
	return Ok(String(reinterpret_cast<const char *>(bytes.Value().data()), bytes.Value().size()));
}

/// Charge PUIS exécute un fichier. Les deux échecs possibles (I/O et
/// script) sont distingués par la position de l'erreur : une `ScriptError`
/// d'I/O a `line == 0` (rien n'a été compilé), ce qui évite d'inventer un
/// second type d'erreur juste pour cette différence.
[[nodiscard]] inline Result<Value, ScriptError> RunScriptFile(Interpreter &vm, const String &path) {
	auto source = LoadScriptFile(path);
	if (source.IsError())
		return Err(ScriptError(source.Error(), 0, 0));
	return vm.Run(source.Value().View());
}

// ============================================================================
// InstallDataLibrary — les codecs data:: vus depuis un script
// ============================================================================

namespace detail {

/// Instancie le codec correspondant à un nom de format. `nullptr` pour un
/// nom inconnu — l'appelant en fait une erreur de script positionnée.
[[nodiscard]] inline std::unique_ptr<Document> MakeDocumentFor(const String &format) {
	String key = format.ToLower();
	if (key == "json")
		return std::make_unique<JsonDocument>();
	if (key == "yaml" || key == "yml")
		return std::make_unique<YamlDocument>();
	if (key == "toml")
		return std::make_unique<TomlDocument>();
	if (key == "xml")
		return std::make_unique<XmlDocument>();
	if (key == "ini")
		return std::make_unique<IniDocument>();
	if (key == "csv")
		return std::make_unique<CsvDocument>();
	return nullptr;
}

} // namespace detail

/// Ajoute à `vm` les fonctions de (dé)sérialisation adossées aux codecs
/// `data::` :
///
///   - `parse(format, texte)`  → table/liste/scalaire, ou erreur positionnée
///   - `encode(format, valeur)` → texte
///   - `read_file(chemin)`      → texte, ou `nil` si illisible
///   - `write_file(chemin, texte)` → booléen
///   - `load(format, chemin)`   → raccourci `parse(format, read_file(...))`
///
/// Séparé de la bibliothèque standard (installée par le constructeur de
/// `Interpreter`) pour que le cœur du langage n'entraîne AUCUNE dépendance
/// vers les codecs ni vers `sdl3::` : un hôte qui veut un interpréteur
/// purement calculatoire n'appelle simplement pas cette fonction.
inline void InstallDataLibrary(Interpreter &vm) {
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
	});

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
	});

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
	});

	vm.RegisterNative("write_file", 2, 2, [](Interpreter &, std::vector<Value> &args) -> Result<Value, ScriptError> {
		auto path = detail::ArgString(args, 0, "write_file");
		if (path.IsError())
			return Err(path.Error());
		auto text = detail::ArgString(args, 1, "write_file");
		if (text.IsError())
			return Err(text.Error());
		return Ok(Value::Boolean(sdl3::WriteFile(path.Value(), text.Value().CStr(), text.Value().GetSize())));
	});

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
	});
}

} // namespace data::script
