// Définitions de sdl3/misc.hpp — fichier généré par splitter.py : le code
// vient tel quel de l'en-tête (seules les signatures sont réécrites).

#include "sdl3/misc.hpp"

namespace sdl3 {

// ── PropertiesLock ───────────────────────────────────────────────────────────

PropertiesLock::PropertiesLock(SDL_PropertiesID id) : id(id) {
    if (id)
        SDL_LockProperties(id);
}

PropertiesLock::~PropertiesLock() {
    if (id)
        SDL_UnlockProperties(id);
}

// ── PropertiesBase ───────────────────────────────────────────────────────────

bool PropertiesBase::SetPointerWithCleanup(const char *name, void *val, SDL_CleanupPropertyCallback cleanup,
		void *userdata) {
    return SDL_SetPointerPropertyWithCleanup(id, name, val, cleanup, userdata);
}

bool PropertiesBase::SetPointerWithCleanup(const char *name, void *val, std::function<void(void *)> cleanup) {
    auto *fn = new std::function<void(void *)>(std::move(cleanup));
    auto cb = [](void *ud, void *v) {
        auto *f = static_cast<std::function<void(void *)> *>(ud);
        (*f)(v);
        delete f;
    };
    return SDL_SetPointerPropertyWithCleanup(id, name, val, cb, fn);
}

const char * PropertiesBase::GetString(const char *name, const char *def) const {
    return SDL_GetStringProperty(id, name, def);
}

Sint64 PropertiesBase::GetNumber(const char *name, Sint64 def) const {
    return SDL_GetNumberProperty(id, name, def);
}

float PropertiesBase::GetFloat(const char *name, float def) const {
    return SDL_GetFloatProperty(id, name, def);
}

void * PropertiesBase::GetPointer(const char *name, void *def) const {
    return SDL_GetPointerProperty(id, name, def);
}

sdl3::PropertyType PropertiesBase::PropertyType(const char *name) const noexcept {
    return sdl3::PropertyType(SDL_GetPropertyType(id, name));
}

void PropertiesBase::Enumerate(std::function<void(const char *name)> fn) const {
    auto cb = [](void *ud, SDL_PropertiesID, const char *name) {
        (*static_cast<std::function<void(const char *)> *>(ud))(name);
    };
    SDL_EnumerateProperties(id, cb, &fn);
}

uint64_t PropertiesBase::Count() const {
    uint64_t n = 0;
    Enumerate([&n](const char *) { ++n; });
    return n;
}

// ── Properties ───────────────────────────────────────────────────────────────

Properties::~Properties() {
    if (id)
        SDL_DestroyProperties(id);
}

Properties Properties::Clone() const {
    Properties dst = Properties::Create();
    SDL_CopyProperties(id, dst.id);
    return dst;
}

bool ShowMessage(MsgBoxKind kind, const String &title, const String &msg, SDL_Window *parent) {
    return SDL_ShowSimpleMessageBox(uint32_t(kind), title.CStr(), msg.CStr(), parent);
}

bool ShowInfo(const String &title, const String &msg, SDL_Window *parent) {
    return ShowMessage(MsgBoxKind::INFO, title, msg, parent);
}

bool ShowWarning(const String &title, const String &msg, SDL_Window *parent) {
    return ShowMessage(MsgBoxKind::WARNING, title, msg, parent);
}

bool ShowError(const String &title, const String &msg, SDL_Window *parent) {
    return ShowMessage(MsgBoxKind::ERROR, title, msg, parent);
}

namespace clipboard {

String GetText() {
    char *t = SDL_GetClipboardText();
    String s(t ? t : "");
    SDL_free(t);
    return s;
}

} // namespace clipboard

} // namespace sdl3
