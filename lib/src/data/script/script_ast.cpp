// Définitions de data/script/script_ast.hpp — fichier généré par splitter.py : le code
// vient tel quel de l'en-tête (seules les signatures sont réécrites).

#include "data/script/script_ast.hpp"

namespace data::script {

const char * DeclKindName(DeclKind kind) noexcept {
	return kind == DeclKind::VAR ? "var" : kind == DeclKind::CONST ? "const" : "let";
}

String JoinTypePath(const TypePath &path) {
	String out;
	for (size_t i = 0; i < path.size(); ++i) {
		if (i > 0)
			out.Concat(".");
		out.Concat(path[i]);
	}
	return out;
}

// ── TypeExpr ─────────────────────────────────────────────────────────────────

String TypeExpr::ToString() const {
	String out = JoinTypePath(path);
	if (!args.empty()) {
		out.Concat("<");
		for (size_t i = 0; i < args.size(); ++i) {
			if (i > 0)
				out.Concat(", ");
			out.Concat(args[i] ? args[i]->ToString() : String("?"));
		}
		out.Concat(">");
	}
	if (nullable)
		out.Concat("?");
	return out;
}

// ── ClassDef ─────────────────────────────────────────────────────────────────

const MethodDef * ClassDef::FindMethod(const String &methodName, bool wantStatic) const noexcept {
	for (const MethodDef &method : methods)
		if (method.def && method.def->name == methodName && method.isStatic == wantStatic)
			return &method;
	return nullptr;
}

} // namespace data::script
