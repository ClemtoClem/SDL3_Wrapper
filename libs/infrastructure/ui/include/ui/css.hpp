#pragma once
/**
 * ui::css — feuilles de style CSS : analyse, sélecteurs, ressources.
 *
 * Sous-ensemble pris en charge :
 *
 *  - sélecteurs : type (`button`), universel (`*`), `#id`, `.classe`,
 *    `this` (le widget qui a importé la feuille — utile quand on ne connaît
 *    pas son id), `root` / `:root`, pseudo-classes d'état (`:hover`,
 *    `:active`, `:focus`, `:focus-visible`, `:checked`, `:disabled`,
 *    `:selected`, `:open`, `:drag-over`, `:dragging`), pseudo-classes
 *    structurelles (`:first-child`, `:last-child`, `:nth-child(odd|even|N)`),
 *    combinateurs descendant (espace) et enfant (`>`), listes (`a, b`) ;
 *  - déclarations : toutes les propriétés de style.hpp, leurs raccourcis,
 *    les propriétés personnalisées `--nom` et `var(--nom, défaut)` ;
 *  - règles-@ : `@import "f.css";` / `@import url(f.css);` (relatif à la
 *    feuille, cycles ignorés), `@font-face { font-family; src: url(); font-
 *    weight; font-style }` (police chargée à la demande par le contexte),
 *    les autres (`@media`…) sont ignorées sans erreur.
 *
 * Une règle dont le sélecteur le plus à droite porte un `#id` ou `this` est
 * PRIVÉE : elle ne s'applique qu'au widget visé et n'est jamais héritée par
 * ses enfants. Les autres (type, classe) sont PARTAGÉES : leurs propriétés
 * héritables passent aux descendants (cf. style.hpp pour la cascade).
 *
 * La syntaxe (règles, règles-@, blocs, commentaires) est lue par
 * `dataio::CssDocument` ; ce fichier n'ajoute que les sélecteurs, la
 * conversion des déclarations en valeurs typées, les imports et les polices.
 *
 * Aucune exception : une règle invalide est ignorée et signalée dans
 * `Warnings()`, seule une feuille illisible fait échouer `Load`.
 */
#include <memory>
#include <vector>

#include "style.hpp"

namespace ui {

// ============================================================================
// Sélecteurs
// ============================================================================

enum class Combinator : uint8_t { Descendant, Child };

enum class NthChild : uint8_t { None, First, Last, Odd, Even, Index };

struct CompoundSelector {
	String type;		 ///< vide = n'importe quel type
	bool isThis = false; ///< `this` : propriétaire de la feuille
	bool isRoot = false; ///< `root` / `:root`
	String id;
	std::vector<String> classes;
	State states = State::None;
	NthChild nth = NthChild::None;
	int nthIndex = 0; ///< NthChild::Index : rang 1..N

	[[nodiscard]] bool IsPrivate() const noexcept { return isThis || !id.IsEmpty(); }
};

struct Selector {
	std::vector<CompoundSelector> parts;  ///< de gauche à droite
	std::vector<Combinator> combinators;  ///< entre parts[i] et parts[i+1]
	uint32_t specificity = 0;			  ///< ids*10000 + (classes+états)*100 + types

	[[nodiscard]] const CompoundSelector &Subject() const noexcept { return parts.back(); }
	[[nodiscard]] bool IsPrivate() const noexcept { return !parts.empty() && parts.back().IsPrivate(); }
};

[[nodiscard]] Result<Selector, String> ParseSelector(StringView text);
/// Liste séparée par des virgules ; les sélecteurs invalides sont ignorés.
[[nodiscard]] std::vector<Selector> ParseSelectorList(StringView text);

/// Ce qu'un sélecteur sait d'un élément (indépendant de l'ECS).
struct ElementData {
	StringView type;
	StringView id;
	const std::vector<String> *classes = nullptr;
	State state = State::None;
	int index = 0;		  ///< rang parmi les frères visibles (0..)
	int siblingCount = 1; ///< nombre de frères visibles (lui compris)
	bool isRoot = false;
	bool isScopeOwner = false; ///< widget qui a importé la feuille évaluée
};

[[nodiscard]] bool MatchCompound(const CompoundSelector &c, const ElementData &e) noexcept;

// ============================================================================
// Feuille de style
// ============================================================================

struct StyleRule {
	std::vector<Selector> selectors;
	StyleProps props;
	size_t order = 0; ///< rang de la règle dans la feuille (imports compris)
};

/// Police déclarée par `@font-face` (chemin résolu par rapport à la feuille).
struct FontFace {
	String family;
	String source;
	int weight = 400;
	FontStyle style = FontStyle::Normal;
};

class StyleSheet {
public:
	/// Charge un fichier (et ses @import).
	[[nodiscard]] static Result<std::shared_ptr<StyleSheet>, String> Load(const String &path);
	/// Analyse un texte CSS ; `baseDir` résout les `url()`/`@import` relatifs.
	[[nodiscard]] static std::shared_ptr<StyleSheet> Parse(StringView css, const String &baseDir = String(),
														   const String &name = String());

	[[nodiscard]] const std::vector<StyleRule> &Rules() const noexcept { return m_rules; }
	[[nodiscard]] const std::vector<FontFace> &FontFaces() const noexcept { return m_fonts; }
	[[nodiscard]] const std::vector<String> &Warnings() const noexcept { return m_warnings; }
	[[nodiscard]] const String &Name() const noexcept { return m_name; }
	/// Change à chaque modification (règles ajoutées) — invalide les caches.
	[[nodiscard]] uint64_t Revision() const noexcept { return m_revision; }

	/// Ajoute une règle construite en C++ (feuilles du module, scripts).
	void AddRule(std::vector<Selector> selectors, StyleProps props);
	/// Ajoute le contenu d'un autre texte CSS à la fin de la feuille.
	void Append(StringView css, const String &baseDir = String());

private:
	struct ParseState;
	void ParseInto(StringView css, const String &baseDir, ParseState &state);

	std::vector<StyleRule> m_rules;
	std::vector<FontFace> m_fonts;
	std::vector<String> m_warnings;
	String m_name;
	uint64_t m_revision = 0;
};

using StyleSheetPtr = std::shared_ptr<const StyleSheet>;

/// Dossier d'un chemin de fichier ("a/b/c.css" -> "a/b").
[[nodiscard]] String DirectoryOf(const String &path);

} // namespace ui
