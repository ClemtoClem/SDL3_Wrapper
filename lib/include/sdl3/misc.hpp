#pragma once
#include <SDL3/SDL.h>
#include <functional>

#include "../core/core.hpp"
#include <SDL3/SDL.h>

namespace sdl3 {

// ============================================================================
// PropertyType
// ============================================================================

enum class PropertyType {
    INVALID = SDL_PROPERTY_TYPE_INVALID,
    POINTER = SDL_PROPERTY_TYPE_POINTER,
    STRING = SDL_PROPERTY_TYPE_STRING,
    NUMBER = SDL_PROPERTY_TYPE_NUMBER,
    FLOAT = SDL_PROPERTY_TYPE_FLOAT,
    BOOLEAN = SDL_PROPERTY_TYPE_BOOLEAN,
};

// ============================================================================
// PropertyProxy — returned by Properties::operator[]
// Allows type-deduced get and set through a single name lookup.
// ============================================================================

class PropertyProxy {
    SDL_PropertiesID id;
    const char *name;

public:
    constexpr PropertyProxy(SDL_PropertiesID id, const char *name) noexcept : id(id), name(name) {}

    // ── Setters ──────────────────────────────────────────────────────────────

    PropertyProxy &operator=(const char *v) {
        SDL_SetStringProperty(id, name, v);
        return *this;
    }
    PropertyProxy &operator=(const String &v) {
        SDL_SetStringProperty(id, name, v.CStr());
        return *this;
    }
    PropertyProxy &operator=(Sint64 v) {
        SDL_SetNumberProperty(id, name, v);
        return *this;
    }
    PropertyProxy &operator=(int v) {
        SDL_SetNumberProperty(id, name, Sint64(v));
        return *this;
    }
    PropertyProxy &operator=(float v) {
        SDL_SetFloatProperty(id, name, v);
        return *this;
    }
    PropertyProxy &operator=(double v) {
        SDL_SetFloatProperty(id, name, float(v));
        return *this;
    }
    PropertyProxy &operator=(bool v) {
        SDL_SetBooleanProperty(id, name, v);
        return *this;
    }
    PropertyProxy &operator=(void *v) {
        SDL_SetPointerProperty(id, name, v);
        return *this;
    }

    /// Remove the property.
    void Clear() { SDL_ClearProperty(id, name); }

    // ── Type query ────────────────────────────────────────────────────────────

    [[nodiscard]] PropertyType Type() const noexcept { return PropertyType(SDL_GetPropertyType(id, name)); }
    [[nodiscard]] bool Exists() const noexcept { return SDL_HasProperty(id, name); }

    // ── Typed getters ─────────────────────────────────────────────────────────

    template <typename T> [[nodiscard]] T Get(T def = T{}) const;

    // Explicit conversion operators for ergonomic use
    [[nodiscard]] explicit operator const char *() const { return SDL_GetStringProperty(id, name, ""); }
    [[nodiscard]] explicit operator String() const { return String(SDL_GetStringProperty(id, name, "")); }
    [[nodiscard]] explicit operator Sint64() const { return SDL_GetNumberProperty(id, name, 0); }
    [[nodiscard]] explicit operator int() const { return int(SDL_GetNumberProperty(id, name, 0)); }
    [[nodiscard]] explicit operator float() const { return SDL_GetFloatProperty(id, name, 0.f); }
    [[nodiscard]] explicit operator bool() const { return SDL_GetBooleanProperty(id, name, false); }
    [[nodiscard]] explicit operator void *() const { return SDL_GetPointerProperty(id, name, nullptr); }
};

// Template specialisations for PropertyProxy::get<T>
template <> inline const char *PropertyProxy::Get(const char *def) const {
    return SDL_GetStringProperty(id, name, def);
}
template <> inline String PropertyProxy::Get(String def) const {
    return String(SDL_GetStringProperty(id, name, def.CStr()));
}
template <> inline Sint64 PropertyProxy::Get(Sint64 def) const { return SDL_GetNumberProperty(id, name, def); }
template <> inline int PropertyProxy::Get(int def) const {
    return int(SDL_GetNumberProperty(id, name, Sint64(def)));
}
template <> inline float PropertyProxy::Get(float def) const { return SDL_GetFloatProperty(id, name, def); }
template <> inline bool PropertyProxy::Get(bool def) const { return SDL_GetBooleanProperty(id, name, def); }
template <> inline void *PropertyProxy::Get(void *def) const { return SDL_GetPointerProperty(id, name, def); }

// ============================================================================
// PropertiesLock — RAII lock for thread-safe multi-property operations
// ============================================================================

class PropertiesLock {
    SDL_PropertiesID id = 0;

public:
    explicit PropertiesLock(SDL_PropertiesID id) : id(id) {
        if (id)
            SDL_LockProperties(id);
    }
    ~PropertiesLock() {
        if (id)
            SDL_UnlockProperties(id);
    }

    PropertiesLock(const PropertiesLock &) = delete;
    PropertiesLock &operator=(const PropertiesLock &) = delete;
    PropertiesLock(PropertiesLock &&o) noexcept : id(o.id) { o.id = 0; }
};

// ============================================================================
// PropertiesBase — accesseurs partagés entre Properties (possédant) et
// PropertiesRef (emprunté), inspiré de SDL3pp_properties.h (talesm/SDL3pp).
// ============================================================================

class PropertiesRef;

class PropertiesBase {
protected:
    SDL_PropertiesID id = 0;

public:
    constexpr PropertiesBase() noexcept = default;
    explicit constexpr PropertiesBase(SDL_PropertiesID id) noexcept : id(id) {}

    [[nodiscard]] SDL_PropertiesID GetId() const noexcept { return id; }
    [[nodiscard]] explicit operator bool() const noexcept { return id != 0; }

    // ── Operator[] — ergonomic typed access ──────────────────────────────────

    [[nodiscard]] PropertyProxy operator[](const char *name) const noexcept { return {id, name}; }
    [[nodiscard]] PropertyProxy operator[](const String &name) const noexcept { return {id, name.CStr()}; }

    // ── Explicit setters ─────────────────────────────────────────────────────

    bool SetString(const char *name, const char *val) { return SDL_SetStringProperty(id, name, val); }
    bool SetNumber(const char *name, Sint64 val) { return SDL_SetNumberProperty(id, name, val); }
    bool SetFloat(const char *name, float val) { return SDL_SetFloatProperty(id, name, val); }
    bool SetBool(const char *name, bool val) { return SDL_SetBooleanProperty(id, name, val); }
    bool SetPointer(const char *name, void *val) { return SDL_SetPointerProperty(id, name, val); }

    bool SetPointerWithCleanup(const char *name, void *val, SDL_CleanupPropertyCallback cleanup,
                               void *userdata = nullptr) {
        return SDL_SetPointerPropertyWithCleanup(id, name, val, cleanup, userdata);
    }

    /// Cleanup via std::function — captures et gère la durée de vie.
    bool SetPointerWithCleanup(const char *name, void *val, std::function<void(void *)> cleanup) {
        auto *fn = new std::function<void(void *)>(std::move(cleanup));
        auto cb = [](void *ud, void *v) {
            auto *f = static_cast<std::function<void(void *)> *>(ud);
            (*f)(v);
            delete f;
        };
        return SDL_SetPointerPropertyWithCleanup(id, name, val, cb, fn);
    }

    // ── Explicit getters ─────────────────────────────────────────────────────

    [[nodiscard]] const char *GetString(const char *name, const char *def = "") const {
        return SDL_GetStringProperty(id, name, def);
    }
    [[nodiscard]] Sint64 GetNumber(const char *name, Sint64 def = 0) const {
        return SDL_GetNumberProperty(id, name, def);
    }
    [[nodiscard]] float GetFloat(const char *name, float def = 0.f) const {
        return SDL_GetFloatProperty(id, name, def);
    }
    [[nodiscard]] bool GetBool(const char *name, bool def = false) const {
        return SDL_GetBooleanProperty(id, name, def);
    }
    [[nodiscard]] void *GetPointer(const char *name, void *def = nullptr) const {
        return SDL_GetPointerProperty(id, name, def);
    }

    template <typename T> [[nodiscard]] T *GetTypedPointer(const char *name) const noexcept {
        return static_cast<T *>(SDL_GetPointerProperty(id, name, nullptr));
    }

    // ── Inspection ────────────────────────────────────────────────────────────

    [[nodiscard]] bool HasProperty(const char *name) const noexcept { return SDL_HasProperty(id, name); }
    [[nodiscard]] sdl3::PropertyType PropertyType(const char *name) const noexcept {
        return sdl3::PropertyType(SDL_GetPropertyType(id, name));
    }
    bool ClearProperty(const char *name) { return SDL_ClearProperty(id, name); }

    // ── Enumeration ──────────────────────────────────────────────────────────

    /// Call `fn(name)` for every property. Thread-safe if you hold the lock.
    void Enumerate(std::function<void(const char *name)> fn) const {
        auto cb = [](void *ud, SDL_PropertiesID, const char *name) {
            (*static_cast<std::function<void(const char *)> *>(ud))(name);
        };
        SDL_EnumerateProperties(id, cb, &fn);
    }

    /// Nombre de propriétés (implémenté via enumerate() — pas O(1)).
    [[nodiscard]] uint64_t Count() const {
        uint64_t n = 0;
        Enumerate([&n](const char *) { ++n; });
        return n;
    }

    // ── Thread safety ─────────────────────────────────────────────────────────

    /// Acquire un verrou scopé ; relâché automatiquement à la destruction du retour.
    [[nodiscard]] PropertiesLock Lock() const { return PropertiesLock(id); }

    // ── Copy ─────────────────────────────────────────────────────────────────

    /// Copie toutes les propriétés vers `dst`.
    bool CopyTo(const PropertiesBase &dst) const { return SDL_CopyProperties(id, dst.id); }
};

// ============================================================================
// PropertiesRef — vue non-possédante sur un groupe de propriétés existant
// (ex: sdl3::globalProperties(), ou pour passer un Properties sans transférer
// la propriété à une fonction qui n'a pas besoin de le détruire).
// ============================================================================

class PropertiesRef : public PropertiesBase {
public:
    using PropertiesBase::PropertiesBase;
    constexpr PropertiesRef(const PropertiesBase &other) noexcept : PropertiesBase(other.GetId()) {}
};

// ============================================================================
// Properties — RAII SDL_PropertiesID
// ============================================================================

class Properties : public PropertiesBase {
public:
    using PropertiesBase::PropertiesBase;

    ~Properties() {
        if (id)
            SDL_DestroyProperties(id);
    }

    Properties(const Properties &) = delete;
    Properties &operator=(const Properties &) = delete;
    Properties(Properties &&o) noexcept : PropertiesBase(o.id) { o.id = 0; }
    Properties &operator=(Properties &&o) noexcept {
        if (this != &o) {
            if (id)
                SDL_DestroyProperties(id);
            id = o.id;
            o.id = 0;
        }
        return *this;
    }

    // ── Factory ──────────────────────────────────────────────────────────────

    [[nodiscard]] static Properties Create() { return Properties(SDL_CreateProperties()); }

    /// Vue non-possédante sur ce groupe de propriétés.
    [[nodiscard]] PropertiesRef Ref() const noexcept { return PropertiesRef(id); }

    /// Retourne un nouveau Properties contenant une copie de toutes les entrées
    /// (les propriétés "pointer" avec cleanup ne sont PAS copiées — cf. doc SDL).
    [[nodiscard]] Properties Clone() const {
        Properties dst = Properties::Create();
        SDL_CopyProperties(id, dst.id);
        return dst;
    }
};

// ============================================================================
// Global properties (process-lifetime, no init required)
// ============================================================================

/// Retourne le groupe de propriétés global (vue non-possédante, valide pour
/// toute la durée de vie du processus).
[[nodiscard]] inline PropertiesRef GlobalProperties() noexcept { return PropertiesRef(SDL_GetGlobalProperties()); }

#ifdef SDL_PROP_NAME_STRING
/// Nom générique conventionnel pour associer un libellé humain à un objet
/// (ex: nom d'une Texture, d'un AudioStream...). SDL ne définit cette
/// propriété sur aucun objet par défaut ; c'est une convention à disposition
/// des apps/libs.
inline constexpr const char *PROP_NAME_STRING = SDL_PROP_NAME_STRING;
#endif

// ============================================================================
// MessageBox helpers
// ============================================================================

enum class MsgBoxKind : uint32_t {
    INFO = SDL_MESSAGEBOX_INFORMATION,
    WARNING = SDL_MESSAGEBOX_WARNING,
    ERROR = SDL_MESSAGEBOX_ERROR,
};

inline bool ShowMessage(MsgBoxKind kind, const String &title, const String &msg, SDL_Window *parent = nullptr) {
    return SDL_ShowSimpleMessageBox(uint32_t(kind), title.CStr(), msg.CStr(), parent);
}
inline bool ShowInfo(const String &title, const String &msg, SDL_Window *parent = nullptr) {
    return ShowMessage(MsgBoxKind::INFO, title, msg, parent);
}
inline bool ShowWarning(const String &title, const String &msg, SDL_Window *parent = nullptr) {
    return ShowMessage(MsgBoxKind::WARNING, title, msg, parent);
}
inline bool ShowError(const String &title, const String &msg, SDL_Window *parent = nullptr) {
    return ShowMessage(MsgBoxKind::ERROR, title, msg, parent);
}

// ============================================================================
// Clipboard
// ============================================================================

namespace clipboard {

[[nodiscard]] inline String GetText() {
    char *t = SDL_GetClipboardText();
    String s(t ? t : "");
    SDL_free(t);
    return s;
}
inline bool SetText(const String &text) { return SDL_SetClipboardText(text.CStr()); }
[[nodiscard]] inline bool HasText() noexcept { return SDL_HasClipboardText(); }

} // namespace clipboard

// ============================================================================
// System info
// ============================================================================

namespace system {

[[nodiscard]] inline int CpuCount() noexcept { return SDL_GetNumLogicalCPUCores(); }
[[nodiscard]] inline int SystemRam() noexcept { return SDL_GetSystemRAM(); }
[[nodiscard]] inline const char *Platform() noexcept { return SDL_GetPlatform(); }

} // namespace system

inline bool OpenUrl(const String &url) { return SDL_OpenURL(url.CStr()); }

} // namespace sdl3
