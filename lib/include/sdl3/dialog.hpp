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

void SDLCALL Trampoline(void *userdata, const char *const *filelist, int filter);

[[nodiscard]] std::vector<SDL_DialogFileFilter> ToSdlFilters(const std::vector<DialogFilter> &filters);

void ShowOpenFileImpl(Callback cb, SDL_Window *window, const std::vector<DialogFilter> &filters,
                             const String &defaultLocation, bool allowMany);

void ShowSaveFileImpl(Callback cb, SDL_Window *window, const std::vector<DialogFilter> &filters,
                             const String &defaultLocation);

void ShowOpenFolderImpl(Callback cb, SDL_Window *window, const String &defaultLocation, bool allowMany);

} // namespace detail

// --- Sans fenêtre parente ---

void ShowOpenFile(Callback cb, const std::vector<DialogFilter> &filters = {}, const String &defaultLocation = "",
                         bool allowMany = false);

void ShowSaveFile(Callback cb, const std::vector<DialogFilter> &filters = {},
                         const String &defaultLocation = "");

void ShowOpenFolder(Callback cb, const String &defaultLocation = "", bool allowMany = false);

// --- Modal pour une fenêtre donnée ---

void ShowOpenFile(Callback cb, Window &window, const std::vector<DialogFilter> &filters = {},
                         const String &defaultLocation = "", bool allowMany = false);

void ShowSaveFile(Callback cb, Window &window, const std::vector<DialogFilter> &filters = {},
                         const String &defaultLocation = "");

void ShowOpenFolder(Callback cb, Window &window, const String &defaultLocation = "", bool allowMany = false);

} // namespace dialog
} // namespace sdl3
