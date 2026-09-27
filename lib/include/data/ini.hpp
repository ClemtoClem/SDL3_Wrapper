#pragma once
/**
 * data::IniDocument — codec INI (2 niveaux : sections -> clé=valeur).
 *
 * Les clés situées avant la première ligne `[section]` sont placées
 * directement à la racine (section implicite / globale). Commentaires `;`
 * et `#` supportés. Les valeurs sont auto-typées (bool/int/float/string) via
 * `scalar::ParseNode`, comme pour les autres formats texte de ce module.
 *
 * Aucune exception, aucun std::istringstream : découpage en lignes via
 * String::Lines(), erreurs propagées via Option<ParseError>.
 */
#include "document.hpp"

namespace data {

class IniDocument final : public Document {
public:
	IniDocument() = default;

	[[nodiscard]] String EncodeStr() const override;

protected:
	[[nodiscard]] Option<ParseError> DecodeImpl(const String &content) override;
};

DATA_REGISTER_FORMAT("ini", IniDocument, ".ini", ".cfg")

} // namespace data