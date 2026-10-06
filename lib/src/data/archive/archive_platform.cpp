// Définitions de data/archive/archive_platform.hpp
#include "data/archive/archive_platform.hpp"

namespace data::archive::platform {

namespace detail {

std::filesystem::path ToPath(const String& path) {
	return std::filesystem::path(
		std::u8string(reinterpret_cast<const char8_t*>(path.CStr()), path.GetSize()));
}

String FromPath(const std::filesystem::path& path) {
	const std::u8string text = path.u8string();
	return String(reinterpret_cast<const char*>(text.data()), text.size());
}

} // namespace detail

bool IsSymlink(const String& path) {
	std::error_code error;
	return std::filesystem::is_symlink(detail::ToPath(path), error) && !error;
}

Option<String> ReadSymlink(const String& path) {
	std::error_code error;
	std::filesystem::path target = std::filesystem::read_symlink(detail::ToPath(path), error);
	if (error)
		return NONE;
	// Séparateurs d'archive : « / » partout.
	String text = detail::FromPath(target);
	return Some(text.Replace('\\', '/'));
}

Result<bool, String> CreateSymlink(const String& target, const String& linkPath, bool directoryHint) {
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
