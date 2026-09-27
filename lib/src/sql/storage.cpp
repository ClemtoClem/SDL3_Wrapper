// Définitions de sql/storage.hpp — fichier généré par splitter.py : le code
// vient tel quel de l'en-tête (seules les signatures sont réécrites).

#include "sql/storage.hpp"

namespace sql {

bool SaveProjectJson(const String &path, const data::NodePtr &root) {
	data::JsonDocument doc;
	doc.SetRoot(root);
	String text = doc.EncodeStr();
	return sdl3::WriteFile(path, text.CStr(), text.GetSize());
}

Result<data::NodePtr, String> LoadProjectJson(const String &path) {
	auto bytes = sdl3::ReadFile(path);
	if (!bytes)
		return Err(String(bytes.Error()));

	String content(reinterpret_cast<const char *>(bytes.Value().data()), bytes.Value().size());

	data::JsonDocument doc;
	auto err = doc.DecodeStr(content);
	if (err.IsSome())
		return Err(err->Format());

	return Ok(doc.GetRoot());
}

} // namespace sql
