#pragma once
/**
 * data::CssDocument — codec CSS (règles de style + règles-@ usuelles :
 * @media/@supports/@document/@layer/@keyframes imbriquant des règles,
 * @font-face/@page/@property/@counter-style/@viewport en bloc de
 * déclarations, @import/@charset/@namespace en simple instruction).
 *
 * La racine est un Array (et non un Object) car CSS autorise plusieurs
 * règles avec le même sélecteur ou la même règle-@ — un Object les aurait
 * fusionnées/écrasées comme pour INI/TOML. Chaque élément de la racine est
 * un Object représentant une règle :
 *   - règle de style   : { "selector": "...", "declarations": {prop: val, ...} }
 *   - règle-@ bloc      : { "at": "@media", "params": "...", "body": [règle, ...] }
 *                     ou : { "at": "@font-face", "declarations": {...} }
 *   - règle-@ instruction (sans bloc) : { "at": "@import", "params": "..." }
 *
 * Les valeurs de déclaration sont auto-typées via `scalar::ParseNode` comme
 * pour les autres formats texte de ce module ; propriétés/sélecteurs
 * dupliqués au sein d'un même bloc de déclarations suivent la même règle
 * que INI/TOML (le dernier écrase le précédent). Les commentaires de bloc
 * sont supprimés au décodage et ne sont pas préservés.
 *
 * Aucune exception : le décodage repose sur data::Reader et propage ses
 * erreurs via Result<NodePtr,ParseError>, converti en Option<ParseError>
 * par DecodeImpl.
 */
#include "document.hpp"

namespace data {

class CssDocument final : public Document {
public:
	CssDocument() = default;

	[[nodiscard]] String EncodeStr() const override;

protected:
	[[nodiscard]] Option<ParseError> DecodeImpl(const String &content) override;

private:
	// ── Encodage ─────────────────────────────────────────────────────────────

	static void WriteIndent(String &out, int indent);

	static void EncodeDeclarations(String &out, const NodePtr &decls, int indent);

	static void EncodeRule(String &out, const NodePtr &rule, int indent);

	// ── Décodage : utilitaires bas niveau ───────────────────────────────────

	// Réduit toute suite d'espaces/retours à la ligne à un unique espace —
	// utilisé pour les sélecteurs et paramètres de règle-@ afin de produire
	// un texte normalisé sur une seule ligne au ré-encodage.
	[[nodiscard]] static String CollapseWs(const String &s);

	[[nodiscard]] static String ReadName(Reader &r);

	// Supprime les commentaires /* ... */ du texte source, en respectant les
	// chaînes citées (un "/*" à l'intérieur d'une chaîne n'est pas un commentaire).
	[[nodiscard]] static String StripComments(const String &content);

	struct ScanResult {
		String text;
		char terminator = '\0';
	};

	// Lit jusqu'à rencontrer l'un des caractères de `terminators` (non
	// consommé), en respectant les chaînes citées et la profondeur des
	// parenthèses — nécessaire pour ne pas couper sur un ';' ou ':' présent
	// dans une valeur comme `url(data:image/png;base64,...)`.
	[[nodiscard]] static Result<ScanResult, ParseError> ReadUntilAny(Reader &r, const String &terminators);

	// Règles-@ dont le bloc contient des déclarations directes plutôt que des
	// règles imbriquées (les autres, ex. @media/@supports/@keyframes, sont
	// traitées par défaut comme une liste de règles imbriquées).
	[[nodiscard]] static bool IsDeclarationAtRule(const String &name);

	// ── Décodage : grammaire ─────────────────────────────────────────────────

	[[nodiscard]] static Result<NodePtr, ParseError> ParseDeclarations(Reader &r);

	[[nodiscard]] static Result<NodePtr, ParseError> ParseStyleRule(Reader &r);

	[[nodiscard]] static Result<NodePtr, ParseError> ParseAtRule(Reader &r);

	[[nodiscard]] static Result<NodePtr, ParseError> ParseRuleList(Reader &r);
};

DATA_REGISTER_FORMAT("css", CssDocument, ".css")

} // namespace data