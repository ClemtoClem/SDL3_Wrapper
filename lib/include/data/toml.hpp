#pragma once
/**
 * data::TomlDocument — codec TOML : SOUS-ENSEMBLE volontairement limité
 * (comme pour YAML, le spec TOML complet — dotted keys, inline tables,
 * array-of-tables `[[x]]`, datetimes, chaînes multi-lignes — est hors de
 * portée raisonnable ici).
 *
 * Supporté : tables `[section]` et tables imbriquées `[section.sous]`
 * (créent une hiérarchie d'Object), assignations `clé = valeur`, chaînes
 * citées "..."/'...', bool/int/float auto-typés, tableaux "inline" d'une
 * seule ligne de scalaires `[1, 2, 3]`, commentaires `# ...`.
 *
 * NON supporté : `[[array_of_tables]]`, tables inline `{k = v}`, clés
 * pointées (`a.b = 1`), chaînes multi-lignes/littérales, dates/heures
 * (gardées telles quelles comme chaînes non citées si non numériques).
 *
 * Aucune exception, aucun std::istringstream : découpage en lignes via
 * String::Lines(). Les valeurs invalides sont signalées via `nullptr`
 * (NodePtr vide) et remontées en Option<ParseError> par DecodeImpl.
 */
#include "document.hpp"

namespace data {

class TomlDocument final : public Document {
public:
	TomlDocument() = default;

	[[nodiscard]] String EncodeStr() const override;

protected:
	[[nodiscard]] Option<ParseError> DecodeImpl(const String &content) override;

private:
	// Retire un commentaire `# ...` en fin de ligne, en respectant les guillemets.
	[[nodiscard]] static String StripComment(const String &line);

	[[nodiscard]] static std::vector<String> SplitDotted(const String &path);

	[[nodiscard]] static size_t FindTopLevelEquals(const String &s);

	// Découpe une liste séparée par des virgules en respectant les guillemets
	// et les crochets imbriqués (utilisé pour le contenu d'un tableau inline).
	[[nodiscard]] static std::vector<String> SplitTopLevelCommas(const String &s);

	// Retourne nullptr en cas d'échec (pas d'exception).
	[[nodiscard]] static NodePtr ParseValue(const String &raw);

	[[nodiscard]] static String Unescape(const String &s);

	// ── Encodage ─────────────────────────────────────────────────────────────

	[[nodiscard]] static String Escape(const String &s);

	[[nodiscard]] static String EncodeScalar(const NodePtr &node);

	[[nodiscard]] static String EncodeArrayInline(const NodePtr &arr);

	static void EncodeScalarsOf(String &out, const NodePtr &table);

	static void EncodeTablesOf(String &out, const NodePtr &table, const String &prefix);
};

DATA_REGISTER_FORMAT("toml", TomlDocument, ".toml")

} // namespace data