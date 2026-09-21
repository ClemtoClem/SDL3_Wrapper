#pragma once
#include <SDL3/SDL.h>
#include <vector>

#include "../core/core.hpp"
#include "joystick.hpp"

namespace sdl3 {

// ============================================================================
// Énumération des périphériques haptiques (retour de force)
// ============================================================================

namespace haptic {

[[nodiscard]] inline std::vector<SDL_HapticID> Enumerated() {
    int count = 0;
    SDL_HapticID *ids = SDL_GetHaptics(&count);
    if (!ids)
        return {};
    std::vector<SDL_HapticID> v(ids, ids + count);
    SDL_free(ids);
    return v;
}

[[nodiscard]] inline const char *NameFor(SDL_HapticID id) noexcept { return SDL_GetHapticNameForID(id); }

[[nodiscard]] inline bool MouseHaptic() noexcept { return SDL_IsMouseHaptic(); }
[[nodiscard]] inline bool JoystickHaptic(const Joystick &j) noexcept { return SDL_IsJoystickHaptic(j.Get()); }

} // namespace haptic

// ============================================================================
// Haptic — RAII SDL_Haptic
// ============================================================================

class Haptic : public Wrapper<SDL_Haptic, SDL_CloseHaptic> {
public:
    using Wrapper::Wrapper;

    [[nodiscard]] static Result<Haptic, StringView> Open(SDL_HapticID id) {
        auto *h = SDL_OpenHaptic(id);
        if (!h)
            return Err(GetError());
        return Ok(Haptic(h));
    }

    [[nodiscard]] static Result<Haptic, StringView> OpenFromMouse() {
        auto *h = SDL_OpenHapticFromMouse();
        if (!h)
            return Err(GetError());
        return Ok(Haptic(h));
    }

    [[nodiscard]] static Result<Haptic, StringView> OpenFromJoystick(Joystick &j) {
        auto *h = SDL_OpenHapticFromJoystick(j.Get());
        if (!h)
            return Err(GetError());
        return Ok(Haptic(h));
    }

    [[nodiscard]] SDL_HapticID GetId() const noexcept { return m_handle ? SDL_GetHapticID(m_handle) : 0; }
    [[nodiscard]] const char *Name() const noexcept { return m_handle ? SDL_GetHapticName(m_handle) : ""; }

    [[nodiscard]] int MaxEffects() const noexcept { return m_handle ? SDL_GetMaxHapticEffects(m_handle) : 0; }
    [[nodiscard]] int MaxEffectsPlaying() const noexcept {
        return m_handle ? SDL_GetMaxHapticEffectsPlaying(m_handle) : 0;
    }
    [[nodiscard]] uint32_t Features() const noexcept { return m_handle ? SDL_GetHapticFeatures(m_handle) : 0; }
    [[nodiscard]] int NumAxes() const noexcept { return m_handle ? SDL_GetNumHapticAxes(m_handle) : 0; }

    bool SetGain(int gain) noexcept { return m_handle && SDL_SetHapticGain(m_handle, gain); }
    bool SetAutocenter(int a) noexcept { return m_handle && SDL_SetHapticAutocenter(m_handle, a); }
    bool Pause() noexcept { return m_handle && SDL_PauseHaptic(m_handle); }
    bool Resume() noexcept { return m_handle && SDL_ResumeHaptic(m_handle); }
    bool StopAllEffects() noexcept { return m_handle && SDL_StopHapticEffects(m_handle); }

    // ── API simple : rumble générique (pas besoin de décrire un effet) ───────

    [[nodiscard]] bool RumbleSupported() const noexcept { return m_handle && SDL_HapticRumbleSupported(m_handle); }
    bool InitRumble() noexcept { return m_handle && SDL_InitHapticRumble(m_handle); }
    bool PlayRumble(float strength, uint32_t lengthMs) noexcept {
        return m_handle && SDL_PlayHapticRumble(m_handle, strength, lengthMs);
    }
    bool StopRumble() noexcept { return m_handle && SDL_StopHapticRumble(m_handle); }

    // ── API avancée : effets décrits par SDL_HapticEffect (union native SDL) ─

    [[nodiscard]] bool EffectSupported(const SDL_HapticEffect &effect) const noexcept {
        return m_handle && SDL_HapticEffectSupported(m_handle, &effect);
    }
    [[nodiscard]] Result<SDL_HapticEffectID, StringView> CreateEffect(const SDL_HapticEffect &effect) {
        if (!m_handle)
            return Err(StringView("haptic invalide"));
        auto id = SDL_CreateHapticEffect(m_handle, &effect);
        if (id < 0)
            return Err(GetError());
        return Ok(id);
    }
    bool UpdateEffect(SDL_HapticEffectID id, const SDL_HapticEffect &effect) noexcept {
        return m_handle && SDL_UpdateHapticEffect(m_handle, id, &effect);
    }
    bool RunEffect(SDL_HapticEffectID id, uint32_t iterations = 1) noexcept {
        return m_handle && SDL_RunHapticEffect(m_handle, id, iterations);
    }
    bool StopEffect(SDL_HapticEffectID id) noexcept { return m_handle && SDL_StopHapticEffect(m_handle, id); }
    void DestroyEffect(SDL_HapticEffectID id) noexcept {
        if (m_handle)
            SDL_DestroyHapticEffect(m_handle, id);
    }
    [[nodiscard]] bool EffectRunning(SDL_HapticEffectID id) const noexcept {
        return m_handle && SDL_GetHapticEffectStatus(m_handle, id);
    }
};

} // namespace sdl3
