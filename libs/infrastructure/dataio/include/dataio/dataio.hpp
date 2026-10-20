#pragma once
/**
 * dataio:: — API unifiée pour parser et générer des scripts de données :
 * JSON · XML · YAML · INI · TOML · CSV · CSS, via un arbre commun (dataio::Node)
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
 * dataio::XmlDocument xml;
 * auto err = xml.Decode(io);
 * if (err.IsSome()) {
 *     // err->Format() donne un message d'erreur avec numéro de ligne.
 * }
 * dataio::JsonDocument json;
 * json.SetRoot(xml.GetRoot());
 * String out = json.EncodeStr();
 * @endcode
 *
 * Ou via le registre par extension :
 * @code{.cpp}
 * auto doc = dataio::DocumentFactory::instance().CreateByFilename("config.yaml");
 * auto err = doc->DecodeStr(text);
 * @endcode
 */
#include "document.hpp"
#include "node.hpp"

#include "documents/css.hpp"
#include "documents/csv.hpp"
#include "documents/gltf.hpp"
#include "documents/ini.hpp"
#include "documents/json.hpp"
#include "documents/toml.hpp"
#include "documents/yaml.hpp"
#include "documents/xml.hpp"
#include "documents/html.hpp"

#undef DOC_REGISTER_FORMAT