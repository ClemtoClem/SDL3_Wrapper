// Définitions de sdl3/filesystem.hpp — fichier généré par splitter.py : le code
// vient tel quel de l'en-tête (seules les signatures sont réécrites).

#include "sdl3/filesystem.hpp"

namespace sdl3 {

namespace filesystem {

String BasePath() {
    const char *p = SDL_GetBasePath();
    return String(p ? p : "");
}

Option<String> PrefPath(const String &org, const String &app) {
    char *p = SDL_GetPrefPath(org.c_str(), app.c_str());
    if (!p)
        return NONE;
    String s(p);
    SDL_free(p);
    return Some(s);
}

String CurrentDirectory() {
    char *p = SDL_GetCurrentDirectory();
    String s(p ? p : "");
    if (p)
        SDL_free(p);
    return s;
}

bool EnumerateDirectory(const String &path, std::function<bool(const char *, const char *)> fn) {
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

bool Rename(const String &oldPath, const String &newPath) {
    return SDL_RenamePath(oldPath.c_str(), newPath.c_str());
}

bool CopyFile(const String &oldPath, const String &newPath) {
    return SDL_CopyFile(oldPath.c_str(), newPath.c_str());
}

Option<sdl3::PathInfo> PathInfo(const String &path) {
    SDL_PathInfo info{};
    if (!SDL_GetPathInfo(path.c_str(), &info))
        return NONE;
    return Some(sdl3::PathInfo(info));
}

std::vector<String> Glob(const String &path, const String &pattern, bool caseInsensitive) {
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
