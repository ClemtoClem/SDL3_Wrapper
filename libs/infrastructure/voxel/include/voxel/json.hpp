#pragma once
/**
 * Lecture PERMISSIVE des fichiers de définitions (JSON écrit à la main par
 * les moddeurs) et accès typés aux champs.
 *
 * Avant le décodage JSON strict (dataio::JsonDocument), le texte est
 * normalisé : commentaires (de ligne et de bloc) retirés, clés sans guillemets
 * (`type: "obj"`) entourées de guillemets, virgules finales (`[1, 2,]`,
 * `{"a": 1, }`) supprimées, et chaînes entre apostrophes acceptées.
 *
 * Les accesseurs (Str, Num, Bool…) suivent un CHEMIN pointé
 * (`"physics.surface.slipperiness"`) et rendent la valeur par défaut si
 * le champ manque ou n'a pas le bon type.
 */
#include <vector>

#include <core/core.hpp>
#include <dataio/node.hpp>
#include <math/math.hpp>

namespace voxel::json {

using dataio::NodePtr;

/// Texte permissif → JSON strict (cf. en-tête).
[[nodiscard]] String Normalize(const String &text);
[[nodiscard]] Result<NodePtr, String> Parse(const String &text);
[[nodiscard]] Result<NodePtr, String> ReadFile(const String &path);
[[nodiscard]] String Encode(const NodePtr &node);
[[nodiscard]] Result<bool, String> WriteFile(const String &path, const NodePtr &node);

/// Nœud au bout d'un chemin pointé (nullptr s'il manque).
[[nodiscard]] NodePtr At(const NodePtr &node, const char *path);

[[nodiscard]] String Str(const NodePtr &node, const char *path, const String &fallback = String());
[[nodiscard]] double Num(const NodePtr &node, const char *path, double fallback = 0.0);
[[nodiscard]] int Int(const NodePtr &node, const char *path, int fallback = 0);
[[nodiscard]] bool Bool(const NodePtr &node, const char *path, bool fallback = false);
[[nodiscard]] bool Has(const NodePtr &node, const char *path);
/// {"x":…, "y":…, "z":…} ou [x, y, z].
[[nodiscard]] math::FVector3 Vec3(const NodePtr &node, const char *path, math::FVector3 fallback = {0.f, 0.f, 0.f});
[[nodiscard]] std::vector<String> StrList(const NodePtr &node, const char *path);
/// Tableau de tableaux de nombres ([[…], […]]).
[[nodiscard]] std::vector<std::vector<double>> NumRows(const NodePtr &node, const char *path);

[[nodiscard]] NodePtr MakeStr(const String &v);
[[nodiscard]] NodePtr MakeNum(double v);
[[nodiscard]] NodePtr MakeInt(int64_t v);
[[nodiscard]] NodePtr MakeBool(bool v);
[[nodiscard]] NodePtr MakeVec3(const math::FVector3 &v);

} // namespace voxel::json
