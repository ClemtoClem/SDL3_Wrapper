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

[[nodiscard]] std::vector<SDL_HapticID> Enumerated();

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

    [[nodiscard]] static Result<Haptic, StringView> Open(SDL_HapticID id);

    [[nodiscard]] static Result<Haptic, StringView> OpenFromMouse();

    [[nodiscard]] static Result<Haptic, StringView> OpenFromJoystick(Joystick &j);

    [[nodiscard]] SDL_HapticID GetId() const noexcept { return m_handle ? SDL_GetHapticID(m_handle) : 0; }
    [[nodiscard]] const char *Name() const noexcept { return m_handle ? SDL_GetHapticName(m_handle) : ""; }

    [[nodiscard]] int MaxEffects() const noexcept { return m_handle ? SDL_GetMaxHapticEffects(m_handle) : 0; }
    [[nodiscard]] int MaxEffectsPlaying() const noexcept;
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
    bool PlayRumble(float strength, uint32_t lengthMs) noexcept;
    bool StopRumble() noexcept { return m_handle && SDL_StopHapticRumble(m_handle); }

    // ── API avancée : effets décrits par SDL_HapticEffect (union native SDL) ─

    [[nodiscard]] bool EffectSupported(const SDL_HapticEffect &effect) const noexcept;
    [[nodiscard]] Result<SDL_HapticEffectID, StringView> CreateEffect(const SDL_HapticEffect &effect);
    bool UpdateEffect(SDL_HapticEffectID id, const SDL_HapticEffect &effect) noexcept;
    bool RunEffect(SDL_HapticEffectID id, uint32_t iterations = 1) noexcept;
    bool StopEffect(SDL_HapticEffectID id) noexcept { return m_handle && SDL_StopHapticEffect(m_handle, id); }
    void DestroyEffect(SDL_HapticEffectID id) noexcept;
    [[nodiscard]] bool EffectRunning(SDL_HapticEffectID id) const noexcept;
};

} // namespace sdl3
