#pragma once
#include <SDL3/SDL.h>
#include <functional>
#include <vector>

#include "../core/core.hpp"
#include "storage.hpp" // PathInfo / PathType (partagés avec l'API Storage sandboxée)
#include <SDL3/SDL.h>

namespace sdl3 {

// ============================================================================
// Folder — dossiers utilisateur standard (Documents, Downloads, ...)
// ============================================================================

enum class Folder {
    HOME = SDL_FOLDER_HOME,
    DESKTOP = SDL_FOLDER_DESKTOP,
    DOCUMENTS = SDL_FOLDER_DOCUMENTS,
    DOWNLOADS = SDL_FOLDER_DOWNLOADS,
    MUSIC = SDL_FOLDER_MUSIC,
    PICTURES = SDL_FOLDER_PICTURES,
    PUBLIC_SHARE = SDL_FOLDER_PUBLICSHARE,
    SAVED_GAMES = SDL_FOLDER_SAVEDGAMES,
    SCREENSHOTS = SDL_FOLDER_SCREENSHOTS,
    TEMPLATES = SDL_FOLDER_TEMPLATES,
    VIDEOS = SDL_FOLDER_VIDEOS,
};

// ============================================================================
// filesystem — accès direct au système de fichiers (sans sandbox, cf.
// `Storage` dans storage.hpp pour l'API sandboxée titre/utilisateur)
// ============================================================================

namespace filesystem {

// Répertoire contenant l'exécutable (se termine par un séparateur de chemin).
[[nodiscard]] inline String BasePath() {
    const char *p = SDL_GetBasePath();
    return String(p ? p : "");
}

// Répertoire d'écriture recommandé pour "org/app" (créé si besoin par l'OS).
[[nodiscard]] inline Option<String> PrefPath(const String &org, const String &app) {
    char *p = SDL_GetPrefPath(org.c_str(), app.c_str());
    if (!p)
        return NONE;
    String s(p);
    SDL_free(p);
    return Some(s);
}

[[nodiscard]] inline const char *UserFolder(Folder f) noexcept { return SDL_GetUserFolder(SDL_Folder(f)); }

[[nodiscard]] inline String CurrentDirectory() {
    char *p = SDL_GetCurrentDirectory();
    String s(p ? p : "");
    if (p)
        SDL_free(p);
    return s;
}

inline bool CreateDirectory(const String &path) { return SDL_CreateDirectory(path.c_str()); }

// Appelle `fn(dirname, filename)` pour chaque entrée du dossier.
// Retourner `false` depuis `fn` arrête l'énumération anticipativement.
inline bool EnumerateDirectory(const String &path, std::function<bool(const char *, const char *)> fn) {
    struct Ctx {
        std::function<bool(const char *, const char *)> fn;
    };
    Ctx ctx{std::move(fn)};
    auto cb = [](void *ud, const char *dirname, const char *fname) -> SDL_EnumerationResult {
        auto *c = static_cast<Ctx *>(ud);
        return c->fn(dirname, fname) ? SDL_ENUM_CONTINUE : SDL_ENUM_SUCCESS;
    };
    return SDL_EnumerateDirectory(path.c_str(), cb, &ctx);
}

inline bool Remove(const String &path) { return SDL_RemovePath(path.c_str()); }

inline bool Rename(const String &oldPath, const String &newPath) {
    return SDL_RenamePath(oldPath.c_str(), newPath.c_str());
}
inline bool CopyFile(const String &oldPath, const String &newPath) {
    return SDL_CopyFile(oldPath.c_str(), newPath.c_str());
}

[[nodiscard]] inline Option<sdl3::PathInfo> PathInfo(const String &path) {
    SDL_PathInfo info{};
    if (!SDL_GetPathInfo(path.c_str(), &info))
        return NONE;
    return Some(sdl3::PathInfo(info));
}

// Motif glob (ex: "*.png").
[[nodiscard]] inline std::vector<String> Glob(const String &path, const String &pattern, bool caseInsensitive = false) {
    int count = 0;
    char **items =
        SDL_GlobDirectory(path.IsEmpty() ? nullptr : path.c_str(), pattern.IsEmpty() ? nullptr : pattern.c_str(),
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

} // namespace filesystem

} // namespace sdl3
