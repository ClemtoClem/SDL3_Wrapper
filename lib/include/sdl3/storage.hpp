#pragma once
#include <SDL3/SDL.h>
#include <functional>
#include <vector>

#include "../core/core.hpp"
#include "error.hpp"

namespace sdl3 {

// ============================================================================
// PathInfo / PathType
// ============================================================================

enum class PathType: Uint8 {
	NONE 		= SDL_PATHTYPE_NONE,
	FILE 		= SDL_PATHTYPE_FILE,
	DIRECTORY 	= SDL_PATHTYPE_DIRECTORY,
	OTHER 		= SDL_PATHTYPE_OTHER,
};
namespace detail {
	constexpr SDL_PathType ToSDL(PathType e) {
		return static_cast<SDL_PathType>(e);
	}
}

struct PathInfo {
	PathType type = PathType::NONE;
	uint64_t size = 0;
	Sint64 createTime = 0;
	Sint64 modifyTime = 0;
	Sint64 accessTime = 0;

	PathInfo() = default;
	explicit PathInfo(const SDL_PathInfo &info) noexcept
		: type(PathType(info.type)), size(info.size), createTime(info.create_time), modifyTime(info.modify_time),
		  accessTime(info.access_time) {}
};

// ============================================================================
// Storage — RAII SDL_Storage (accès fichiers abstrait : titre, user, disque)
// ============================================================================

class Storage : public Wrapper<SDL_Storage, SDL_CloseStorage> {
public:
	using Wrapper::Wrapper;

	// Stockage en lecture seule des assets embarqués avec l'application.
	[[nodiscard]] static Result<Storage, StringView> OpenTitle(const String &overridePath = "",
															   SDL_PropertiesID props = 0);

	// Stockage persistant (sauvegardes, config) propre à l'utilisateur, isolé par org/app.
	[[nodiscard]] static Result<Storage, StringView> OpenUser(const String &org, const String &app,
															  SDL_PropertiesID props = 0);

	// Accès direct à un chemin du disque local (pas de sandboxing).
	[[nodiscard]] static Result<Storage, StringView> OpenFile(const String &path);

	// Le stockage peut être asynchrone (ex: montage réseau) : à sonder avant usage.
	[[nodiscard]] bool Ready() const noexcept { return m_handle && SDL_StorageReady(m_handle); }

	[[nodiscard]] Result<uint64_t, StringView> FileSize(const String &path) const;

	[[nodiscard]] Result<std::vector<uint8_t>, StringView> ReadFile(const String &path) const;

	[[nodiscard]] bool WriteFile(const String &path, const void *data, size_t len);
	[[nodiscard]] bool WriteFile(const String &path, const std::vector<uint8_t> &data);

	[[nodiscard]] bool CreateDirectory(const String &path);

	// Appelle `fn(dirname, filename)` pour chaque entrée du dossier.
	// Retourner `false` depuis `fn` arrête l'énumération anticipativement.
	[[nodiscard]] bool EnumerateDirectory(const String &path,
										  std::function<bool(const char *, const char *)> fn) const;

	bool Remove(const String &path) { return m_handle && SDL_RemoveStoragePath(m_handle, path.c_str()); }

	bool Rename(const String &oldPath, const String &newPath);
	bool Copy(const String &oldPath, const String &newPath);

	[[nodiscard]] Option<sdl3::PathInfo> PathInfo(const String &path) const;

	[[nodiscard]] uint64_t SpaceRemaining() const noexcept;

	// Motif glob (ex: "*.png"). `path` peut être vide pour la racine du storage.
	[[nodiscard]] std::vector<String> Glob(const String &path, const String &pattern,
										   bool caseInsensitive = false) const;
};

} // namespace sdl3
