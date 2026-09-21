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
															   SDL_PropertiesID props = 0) {
		auto *s = SDL_OpenTitleStorage(overridePath.IsEmpty() ? nullptr : overridePath.c_str(), props);
		if (!s)
			return Err(GetError());
		return Ok(Storage(s));
	}

	// Stockage persistant (sauvegardes, config) propre à l'utilisateur, isolé par org/app.
	[[nodiscard]] static Result<Storage, StringView> OpenUser(const String &org, const String &app,
															  SDL_PropertiesID props = 0) {
		auto *s = SDL_OpenUserStorage(org.c_str(), app.c_str(), props);
		if (!s)
			return Err(GetError());
		return Ok(Storage(s));
	}

	// Accès direct à un chemin du disque local (pas de sandboxing).
	[[nodiscard]] static Result<Storage, StringView> OpenFile(const String &path) {
		auto *s = SDL_OpenFileStorage(path.c_str());
		if (!s)
			return Err(GetError());
		return Ok(Storage(s));
	}

	// Le stockage peut être asynchrone (ex: montage réseau) : à sonder avant usage.
	[[nodiscard]] bool Ready() const noexcept { return m_handle && SDL_StorageReady(m_handle); }

	[[nodiscard]] Result<uint64_t, StringView> FileSize(const String &path) const {
		uint64_t len = 0;
		if (!m_handle || !SDL_GetStorageFileSize(m_handle, path.c_str(), &len))
			return Err(GetError());
		return Ok(len);
	}

	[[nodiscard]] Result<std::vector<uint8_t>, StringView> ReadFile(const String &path) const {
		auto sizeRes = FileSize(path);
		if (sizeRes.IsError())
			return Err(sizeRes.unwrap_error());

		std::vector<uint8_t> buf(size_t(sizeRes.Unwrap()));
		if (!buf.empty() && !SDL_ReadStorageFile(m_handle, path.c_str(), buf.data(), uint64_t(buf.size())))
			return Err(GetError());
		return Ok(std::move(buf));
	}

	[[nodiscard]] bool WriteFile(const String &path, const void *data, size_t len) {
		return m_handle && SDL_WriteStorageFile(m_handle, path.c_str(), data, uint64_t(len));
	}
	[[nodiscard]] bool WriteFile(const String &path, const std::vector<uint8_t> &data) {
		return WriteFile(path, data.data(), data.size());
	}

	[[nodiscard]] bool CreateDirectory(const String &path) {
		return m_handle && SDL_CreateStorageDirectory(m_handle, path.c_str());
	}

	// Appelle `fn(dirname, filename)` pour chaque entrée du dossier.
	// Retourner `false` depuis `fn` arrête l'énumération anticipativement.
	[[nodiscard]] bool EnumerateDirectory(const String &path,
										  std::function<bool(const char *, const char *)> fn) const {
		if (!m_handle)
			return false;
		struct Ctx {
			std::function<bool(const char *, const char *)> fn;
		};
		Ctx ctx{std::move(fn)};
		auto cb = [](void *ud, const char *dirname, const char *fname) -> SDL_EnumerationResult {
			auto *c = static_cast<Ctx *>(ud);
			return c->fn(dirname, fname) ? SDL_ENUM_CONTINUE : SDL_ENUM_SUCCESS;
		};
		return SDL_EnumerateStorageDirectory(m_handle, path.c_str(), cb, &ctx);
	}

	bool Remove(const String &path) { return m_handle && SDL_RemoveStoragePath(m_handle, path.c_str()); }

	bool Rename(const String &oldPath, const String &newPath) {
		return m_handle && SDL_RenameStoragePath(m_handle, oldPath.c_str(), newPath.c_str());
	}
	bool Copy(const String &oldPath, const String &newPath) {
		return m_handle && SDL_CopyStorageFile(m_handle, oldPath.c_str(), newPath.c_str());
	}

	[[nodiscard]] Option<sdl3::PathInfo> PathInfo(const String &path) const {
		SDL_PathInfo info{};
		if (!m_handle || !SDL_GetStoragePathInfo(m_handle, path.c_str(), &info))
			return NONE;
		return Some(sdl3::PathInfo(info));
	}

	[[nodiscard]] uint64_t SpaceRemaining() const noexcept {
		return m_handle ? SDL_GetStorageSpaceRemaining(m_handle) : 0;
	}

	// Motif glob (ex: "*.png"). `path` peut être vide pour la racine du storage.
	[[nodiscard]] std::vector<String> Glob(const String &path, const String &pattern,
										   bool caseInsensitive = false) const {
		if (!m_handle)
			return {};
		int count = 0;
		char **items = SDL_GlobStorageDirectory(m_handle, path.IsEmpty() ? nullptr : path.c_str(),
												pattern.IsEmpty() ? nullptr : pattern.c_str(),
												caseInsensitive ? SDL_GLOB_CASEINSENSITIVE : 0, &count);
		if (!items)
			return {};
		std::vector<String> out;
		out.reserve(size_t(count));
		for (int i = 0; i < count; ++i)
			out.emplace_back(items[i]);
		SDL_free(items);
		return out;
	}
};

} // namespace sdl3
