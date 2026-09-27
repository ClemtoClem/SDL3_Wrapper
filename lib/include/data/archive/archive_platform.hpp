#pragma once
/**
 * data::archive — opérations de système de fichiers absentes de SDL3.
 *
 * SDL3 ne sait ni créer ni lire un lien symbolique (`SDL_GetPathInfo` les
 * suit) ; ces quelques fonctions passent par `std::filesystem`, toujours dans
 * ses surcharges à `std::error_code` : aucune exception n'est levée (cf.
 * memory/feedback_no_exceptions.md). Les chemins sont convertis en
 * `std::u8string` pour que l'UTF-8 survive aussi sous Windows.
 *
 * Sous Windows, créer un lien exige le mode développeur ou des droits
 * d'administrateur : l'échec est alors signalé, pas masqué.
 */
#include "../../core/core.hpp"

#include <filesystem>
#include <string>
#include <system_error>

namespace data::archive::platform {

namespace detail {

[[nodiscard]] std::filesystem::path ToPath(const String& path);

[[nodiscard]] String FromPath(const std::filesystem::path& path);

} // namespace detail

/// Vrai si `path` est lui-même un lien symbolique (sans le suivre).
[[nodiscard]] bool IsSymlink(const String& path);

/// Cible d'un lien symbolique, telle qu'écrite dans le lien.
[[nodiscard]] Option<String> ReadSymlink(const String& path);

/// Crée `linkPath` → `target`. `directoryHint` : la cible est un dossier
/// (seul Windows distingue les deux sortes de liens).
[[nodiscard]] Result<bool, String>
CreateSymlink(const String& target, const String& linkPath, bool directoryHint = false);

} // namespace data::archive::platform
