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
[[nodiscard]] String BasePath();

// Répertoire d'écriture recommandé pour "org/app" (créé si besoin par l'OS).
[[nodiscard]] Option<String> PrefPath(const String &org, const String &app);

[[nodiscard]] inline const char *UserFolder(Folder f) noexcept { return SDL_GetUserFolder(SDL_Folder(f)); }

[[nodiscard]] String CurrentDirectory();

inline bool CreateDirectory(const String &path) { return SDL_CreateDirectory(path.c_str()); }

// Appelle `fn(dirname, filename)` pour chaque entrée du dossier.
// Retourner `false` depuis `fn` arrête l'énumération anticipativement.
bool EnumerateDirectory(const String &path, std::function<bool(const char *, const char *)> fn);

inline bool Remove(const String &path) { return SDL_RemovePath(path.c_str()); }

bool Rename(const String &oldPath, const String &newPath);
bool CopyFile(const String &oldPath, const String &newPath);

[[nodiscard]] Option<sdl3::PathInfo> PathInfo(const String &path);

// Motif glob (ex: "*.png").
[[nodiscard]] std::vector<String> Glob(const String &path, const String &pattern, bool caseInsensitive = false);

} // namespace filesystem

} // namespace sdl3
