#pragma once
/**
 * data::CsvDocument — codec CSV (RFC 4180 : champs entre guillemets,
 * guillemet doublé pour échapper, retours à la ligne autorisés dans un
 * champ cité).
 *
 * Contrainte (partagée avec SDL3pp_dataScripts.h) : le CSV n'a qu'un seul
 * niveau de hiérarchie — la racine est un Object dont chaque valeur est un
 * Array (une colonne nommée). La première ligne du fichier est l'en-tête
 * (noms de colonnes), les suivantes sont les données, une cellule par
 * colonne et par ligne.
 *
 * Aucune exception : les erreurs de syntaxe (champ cité non terminé) sont
 * remontées via Option<ParseError>.
 */
#include "document.hpp"

namespace data {

class CsvDocument final : public Document {
public:
	CsvDocument() = default;

	[[nodiscard]] String EncodeStr() const override;

protected:
	[[nodiscard]] Option<ParseError> DecodeImpl(const String &content) override;

private:
	static void WriteField(String &out, const String &field);

	// Découpe le contenu en lignes de cellules, en respectant les guillemets
	// (une cellule citée peut contenir des virgules et des retours à la ligne).
	[[nodiscard]] static Option<ParseError> ParseRows(const String &content,
													   std::vector<std::vector<String>> &rows);
};

DATA_REGISTER_FORMAT("csv", CsvDocument, ".csv")

} // namespace data