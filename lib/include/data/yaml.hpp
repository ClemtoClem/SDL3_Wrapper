#pragma once
/**
 * data::YamlDocument — codec YAML : SOUS-ENSEMBLE "block style" volontairement
 * limité (YAML complet est un des formats les plus complexes qui existent —
 * réimplémenter tout le spec 1.2 est hors de portée raisonnable ici).
 *
 * Supporté : mappings et séquences en style bloc (indentation), imbrication
 * arbitraire, scalaires auto-typés (bool/int/float/string), chaînes citées
 * '...'/"...", commentaires `# ...`, `-` seul suivi d'un bloc imbriqué pour
 * les éléments de séquence composites (objet/tableau).
 *
 * NON supporté (limitations documentées, pas des bugs) : style flow inline
 * (`[a, b]`, `{k: v}`), ancres/alias (`&`/`*`), tags (`!!str`), scalaires
 * bloc littéral/plié (`|`, `>`), clés complexes, multi-documents (`---`
 * séparateurs simplement ignorés, traités comme un seul document), la forme
 * compacte `- key: value` (le premier champ d'un mapping directement sur la
 * ligne du tiret) — cette dernière EST cependant supportée en lecture, mais
 * l'encodeur produit systématiquement la forme développée (`-` seul puis
 * mapping indenté dessous) pour rester simple et non ambigu.
 *
 * Aucune exception, aucun std::istringstream : le fichier est découpé en
 * lignes via String::Lines().
 */
#include <utility>

#include "document.hpp"

namespace data {

class YamlDocument final : public Document {
public:
	YamlDocument() = default;

	[[nodiscard]] String EncodeStr() const override;

protected:
	[[nodiscard]] Option<ParseError> DecodeImpl(const String &content) override;

private:
	// ── Prétraitement : une ligne logique = (indentation, contenu, est-un-item-de-séquence) ──

	struct PLine {
		int indent;
		String text;
		bool isListItem;
		int lineNo;
	};

	[[nodiscard]] static std::vector<PLine> Preprocess(const String &content);

	// Sépare "key: value" au premier ':' top-level (hors guillemets) suivi
	// d'un espace ou de la fin de ligne. Retourne NONE si la ligne n'a pas
	// cette forme (ex: un item de séquence scalaire brut).
	[[nodiscard]] static Option<std::pair<String, String>> SplitKeyValue(const String &text);

	/// Retire un commentaire de fin de ligne en respectant les guillemets :
	/// le `#` de `"a # b"` fait partie de la valeur, pas d'un commentaire.
	[[nodiscard]] static String StripTrailingComment(const String &text);

	[[nodiscard]] static NodePtr ParseScalar(const String &raw);

	// Consomme les champs d'un mapping (éventuellement débuté par un premier
	// champ déjà extrait de la ligne du tiret, cf. forme compacte "- key: value").
	static void ContinueMapping(const std::vector<PLine> &lines, size_t &idx, NodePtr obj, int fieldIndent);

	[[nodiscard]] static NodePtr ParseBlock(const std::vector<PLine> &lines, size_t &idx, int indent);

	// ── Encodage ─────────────────────────────────────────────────────────────

	[[nodiscard]] static String IndentStr(int n) { return String(size_t(n) * 2, ' '); }

	[[nodiscard]] static bool NeedsQuoting(const String &s);

	[[nodiscard]] static String EncodeScalar(const NodePtr &node);

	static void EncodeMapping(String &out, const NodePtr &obj, int indent);

	static void EncodeSequence(String &out, const NodePtr &arr, int indent);
};

DATA_REGISTER_FORMAT("yaml", YamlDocument, ".yaml", ".yml")

} // namespace data