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

[[nodiscard]] inline std::filesystem::path ToPath(const String& path) {
	return std::filesystem::path(
		std::u8string(reinterpret_cast<const char8_t*>(path.CStr()), path.GetSize()));
}

[[nodiscard]] inline String FromPath(const std::filesystem::path& path) {
	const std::u8string text = path.u8string();
	return String(reinterpret_cast<const char*>(text.data()), text.size());
}

} // namespace detail

/// Vrai si `path` est lui-même un lien symbolique (sans le suivre).
[[nodiscard]] inline bool IsSymlink(const String& path) {
	std::error_code error;
	return std::filesystem::is_symlink(detail::ToPath(path), error) && !error;
}

/// Cible d'un lien symbolique, telle qu'écrite dans le lien.
[[nodiscard]] inline Option<String> ReadSymlink(const String& path) {
	std::error_code error;
	std::filesystem::path target = std::filesystem::read_symlink(detail::ToPath(path), error);
	if (error)
		return NONE;
	// Séparateurs d'archive : « / » partout.
	String text = detail::FromPath(target);
	return Some(text.Replace('\\', '/'));
}

/// Crée `linkPath` → `target`. `directoryHint` : la cible est un dossier
/// (seul Windows distingue les deux sortes de liens).
[[nodiscard]] inline Result<bool, String>
CreateSymlink(const String& target, const String& linkPath, bool directoryHint = false) {
	std::error_code error;
	const std::filesystem::path link = detail::ToPath(linkPath);
	std::filesystem::remove(link,
							error); // un lien ou fichier existant est remplacé
	error.clear();
	if (directoryHint)
		std::filesystem::create_directory_symlink(detail::ToPath(target), link, error);
	else
		std::filesystem::create_symlink(detail::ToPath(target), link, error);
	if (error)
		return Err(String::Format("lien %s -> %s impossible : %s", linkPath.CStr(), target.CStr(),
								  error.message().c_str()));
	return Ok(true);
}

} // namespace data::archive::platform
