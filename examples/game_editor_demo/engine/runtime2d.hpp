#pragma once
/**
 * game_editor — l'API 2D des scripts (inclus à la fin de runtime.hpp).
 *
 *   node2d.*  les nœuds 2D de la scène, par leur NOM : position, rotation,
 *             échelle, couleur, taille, texte, image, profondeur, création,
 *             sélection au point, recouvrement de deux nœuds ;
 *   canvas.*  le calque 2D : résolution de référence, souris en coordonnées
 *             2D, boutons, caméra 2D courante, fond ;
 *   draw2d.*  dessin IMMÉDIAT : ce qu'un script dessine pendant une image
 *             s'affiche à cette image seulement (HUD, mini-carte, jauges,
 *             trajectoires). Coordonnées de l'écran de référence par défaut
 *             (`space: "world"` pour suivre la caméra 2D).
 *
 * Comme le reste de l'API : un nom inconnu rend `nil`/`false`, un argument
 * mal typé est une erreur de script ; les écritures vont au document (le
 * mode Jeu restaure tout à l'arrêt).
 */
#include "runtime.hpp"

namespace game_editor {

namespace script2d {

using data::script::Interpreter;
using data::script::ScriptError;
using data::script::Value;

[[nodiscard]] Value Vec2ToValue(math::FVector2 v);

/// `[x, y]` d'une valeur de script.
[[nodiscard]] Option<math::FVector2> ToVec2(const Value *value);

/// Couleur de script ("#rrggbb[aa]", [r, g, b(, a)]) en octets.
[[nodiscard]] Result<sdl3::Color, ScriptError> ToColor(const Value &value, const char *fn);

/// Liste de points `[[x, y], …]`.
[[nodiscard]] Result<std::vector<math::FVector2>, ScriptError> ToPoints(const Value &value, const char *fn);

[[nodiscard]] Result<float, ScriptError> Number(const std::vector<Value> &args, size_t index, const char *fn);

/// Options facultatives d'un appel (`{space: "world", z: 3}`), table vide
/// si l'argument manque.
[[nodiscard]] Result<std::shared_ptr<data::script::MapObject>, ScriptError>
Options(const std::vector<Value> &args, size_t index, const char *fn);

/// Applique les options d'apparence communes à un élément 2D.
[[nodiscard]] Option<ScriptError> ApplyStyle(CanvasItemDesc &item, const data::script::MapObject &opts,
													const char *fn);

} // namespace script2d



} // namespace game_editor
