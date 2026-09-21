#pragma once
#include <SDL3/SDL.h>
#include <functional>
#include <vector>

#include "../core/core.hpp"
#include "error.hpp"
#include "render.hpp" // Window
#include <SDL3/SDL.h>

namespace sdl3 {

// ============================================================================
// Dialog — native file / folder selection dialogs (async)
// ============================================================================

struct DialogFilter {
    String name;
    String pattern; // ex: "png;jpg;jpeg" ou "*" pour tout accepter
};

// Résultat transmis au callback : soit une liste de chemins choisis,
// soit une liste vide (annulation), soit une erreur.
struct DialogResult {
    std::vector<String> files;
    Option<String> error = NONE;

    [[nodiscard]] bool Cancelled() const noexcept { return error.IsNone() && files.empty(); }
    [[nodiscard]] bool Ok() const noexcept { return error.IsNone() && !files.empty(); }
    [[nodiscard]] const String &First() const noexcept { return files.front(); }
};

namespace dialog {

using Callback = std::function<void(const DialogResult &, int filterIndex)>;

namespace detail {

struct DialogCtx {
    Callback fn;
};

inline void SDLCALL Trampoline(void *userdata, const char *const *filelist, int filter) {
    auto *ctx = static_cast<DialogCtx *>(userdata);
    DialogResult res;
    if (!filelist) {
        res.error = Some(String(GetError()));
    } else {
        for (auto p = filelist; *p; ++p)
            res.files.push_back(String(*p));
    }
    ctx->fn(res, filter);
    delete ctx;
}

[[nodiscard]] inline std::vector<SDL_DialogFileFilter> ToSdlFilters(const std::vector<DialogFilter> &filters) {
    std::vector<SDL_DialogFileFilter> out;
    out.reserve(filters.size());
    for (auto &f : filters)
        out.push_back({f.name.c_str(), f.pattern.c_str()});
    return out;
}

inline void ShowOpenFileImpl(Callback cb, SDL_Window *window, const std::vector<DialogFilter> &filters,
                             const String &defaultLocation, bool allowMany) {
    auto sdlFilters = ToSdlFilters(filters);
    auto *ctx = new DialogCtx{std::move(cb)};
    SDL_ShowOpenFileDialog(Trampoline, ctx, window, sdlFilters.empty() ? nullptr : sdlFilters.data(),
                           int(sdlFilters.size()), defaultLocation.IsEmpty() ? nullptr : defaultLocation.c_str(),
                           allowMany);
}

inline void ShowSaveFileImpl(Callback cb, SDL_Window *window, const std::vector<DialogFilter> &filters,
                             const String &defaultLocation) {
    auto sdlFilters = ToSdlFilters(filters);
    auto *ctx = new DialogCtx{std::move(cb)};
    SDL_ShowSaveFileDialog(Trampoline, ctx, window, sdlFilters.empty() ? nullptr : sdlFilters.data(),
                           int(sdlFilters.size()), defaultLocation.IsEmpty() ? nullptr : defaultLocation.c_str());
}

inline void ShowOpenFolderImpl(Callback cb, SDL_Window *window, const String &defaultLocation, bool allowMany) {
    auto *ctx = new DialogCtx{std::move(cb)};
    SDL_ShowOpenFolderDialog(Trampoline, ctx, window, defaultLocation.IsEmpty() ? nullptr : defaultLocation.c_str(),
                             allowMany);
}

} // namespace detail

// --- Sans fenêtre parente ---

inline void ShowOpenFile(Callback cb, const std::vector<DialogFilter> &filters = {}, const String &defaultLocation = "",
                         bool allowMany = false) {
    detail::ShowOpenFileImpl(std::move(cb), nullptr, filters, defaultLocation, allowMany);
}

inline void ShowSaveFile(Callback cb, const std::vector<DialogFilter> &filters = {},
                         const String &defaultLocation = "") {
    detail::ShowSaveFileImpl(std::move(cb), nullptr, filters, defaultLocation);
}

inline void ShowOpenFolder(Callback cb, const String &defaultLocation = "", bool allowMany = false) {
    detail::ShowOpenFolderImpl(std::move(cb), nullptr, defaultLocation, allowMany);
}

// --- Modal pour une fenêtre donnée ---

inline void ShowOpenFile(Callback cb, Window &window, const std::vector<DialogFilter> &filters = {},
                         const String &defaultLocation = "", bool allowMany = false) {
    detail::ShowOpenFileImpl(std::move(cb), window.Get(), filters, defaultLocation, allowMany);
}

inline void ShowSaveFile(Callback cb, Window &window, const std::vector<DialogFilter> &filters = {},
                         const String &defaultLocation = "") {
    detail::ShowSaveFileImpl(std::move(cb), window.Get(), filters, defaultLocation);
}

inline void ShowOpenFolder(Callback cb, Window &window, const String &defaultLocation = "", bool allowMany = false) {
    detail::ShowOpenFolderImpl(std::move(cb), window.Get(), defaultLocation, allowMany);
}

} // namespace dialog
} // namespace sdl3
