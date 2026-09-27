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
#include "toml.hpp"
#include "xml.hpp"
#include "html.hpp"
#include "yaml.hpp"
#include "script/script_ast.hpp"
#include "script/script_interpreter.hpp"
#include "script/script_lexer.hpp"
#include "script/script_parser.hpp"
#include "script/script_value.hpp"

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
	if (key == "html" || key == "htm")
		return std::make_unique<HtmlDocument>();
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
void InstallDataLibrary(Interpreter &vm);

} // namespace data::script
