#pragma once
/**
 * data:: — API unifiée pour parser et générer des scripts de données :
 * JSON · XML · YAML · INI · TOML · CSV · CSS, via un arbre commun (data::Node)
 * indépendant du format et stocké dans un sdl3::Properties (cf. node.hpp).
 *
 * Aucune exception n'est levée par ce module : toute opération pouvant
 * échouer retourne Option<T> ou Result<T,E> (cf. option.hpp / result.hpp).
 * Les chaînes utilisent String / StringView (cf. string.hpp /
 * string_view.hpp) à la place de std::string / std::string_view.
 *
 * Permet par ex. de lire un fichier XML et de l'exporter en JSON :
 * @code{.cpp}
 * auto io = sdl3::IOStream::FromFile("config.xml", "rb").Value();
 * data::XmlDocument xml;
 * auto err = xml.Decode(io);
 * if (err.IsSome()) {
 *     // err->Format() donne un message d'erreur avec numéro de ligne.
 * }
 * data::JsonDocument json;
 * json.SetRoot(xml.GetRoot());
 * String out = json.EncodeStr();
 * @endcode
 *
 * Ou via le registre par extension :
 * @code{.cpp}
 * auto doc = data::DocumentFactory::instance().CreateByFilename("config.yaml");
 * auto err = doc->DecodeStr(text);
 * @endcode
 */
#include "document.hpp"
#include "node.hpp"

#include "css.hpp"
#include "csv.hpp"
#include "ini.hpp"
#include "json.hpp"
#include "toml.hpp"
#include "yaml.hpp"
#include "xml.hpp"
#include "html.hpp"

#undef DATA_REGISTER_FORMAT