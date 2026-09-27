// Définitions de sdl3/dialog.hpp — fichier généré par splitter.py : le code
// vient tel quel de l'en-tête (seules les signatures sont réécrites).

#include "sdl3/dialog.hpp"

namespace sdl3 {

namespace dialog {

namespace detail {

void SDLCALL Trampoline(void *userdata, const char *const *filelist, int filter) {
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

std::vector<SDL_DialogFileFilter> ToSdlFilters(const std::vector<DialogFilter> &filters) {
    std::vector<SDL_DialogFileFilter> out;
    out.reserve(filters.size());
    for (auto &f : filters)
        out.push_back({f.name.c_str(), f.pattern.c_str()});
    return out;
}

void ShowOpenFileImpl(Callback cb, SDL_Window *window, const std::vector<DialogFilter> &filters,
		const String &defaultLocation, bool allowMany) {
    auto sdlFilters = ToSdlFilters(filters);
    auto *ctx = new DialogCtx{std::move(cb)};
    SDL_ShowOpenFileDialog(Trampoline, ctx, window, sdlFilters.empty() ? nullptr : sdlFilters.data(),
                           int(sdlFilters.size()), defaultLocation.IsEmpty() ? nullptr : defaultLocation.c_str(),
                           allowMany);
}

void ShowSaveFileImpl(Callback cb, SDL_Window *window, const std::vector<DialogFilter> &filters,
		const String &defaultLocation) {
    auto sdlFilters = ToSdlFilters(filters);
    auto *ctx = new DialogCtx{std::move(cb)};
    SDL_ShowSaveFileDialog(Trampoline, ctx, window, sdlFilters.empty() ? nullptr : sdlFilters.data(),
                           int(sdlFilters.size()), defaultLocation.IsEmpty() ? nullptr : defaultLocation.c_str());
}

void ShowOpenFolderImpl(Callback cb, SDL_Window *window, const String &defaultLocation, bool allowMany) {
    auto *ctx = new DialogCtx{std::move(cb)};
    SDL_ShowOpenFolderDialog(Trampoline, ctx, window, defaultLocation.IsEmpty() ? nullptr : defaultLocation.c_str(),
                             allowMany);
}

} // namespace detail

void ShowOpenFile(Callback cb, const std::vector<DialogFilter> &filters, const String &defaultLocation, bool allowMany) {
    detail::ShowOpenFileImpl(std::move(cb), nullptr, filters, defaultLocation, allowMany);
}

void ShowSaveFile(Callback cb, const std::vector<DialogFilter> &filters, const String &defaultLocation) {
    detail::ShowSaveFileImpl(std::move(cb), nullptr, filters, defaultLocation);
}

void ShowOpenFolder(Callback cb, const String &defaultLocation, bool allowMany) {
    detail::ShowOpenFolderImpl(std::move(cb), nullptr, defaultLocation, allowMany);
}

void ShowOpenFile(Callback cb, Window &window, const std::vector<DialogFilter> &filters,
		const String &defaultLocation, bool allowMany) {
    detail::ShowOpenFileImpl(std::move(cb), window.Get(), filters, defaultLocation, allowMany);
}

void ShowSaveFile(Callback cb, Window &window, const std::vector<DialogFilter> &filters, const String &defaultLocation) {
    detail::ShowSaveFileImpl(std::move(cb), window.Get(), filters, defaultLocation);
}

void ShowOpenFolder(Callback cb, Window &window, const String &defaultLocation, bool allowMany) {
    detail::ShowOpenFolderImpl(std::move(cb), window.Get(), defaultLocation, allowMany);
}

} // namespace dialog

} // namespace sdl3
