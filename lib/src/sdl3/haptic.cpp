// Définitions de sdl3/haptic.hpp
// L'en-tête n'est pas autonome : il compte sur ce qu'inclut son module.
#include "sdl3/sdl3.hpp"
#include "sdl3/haptic.hpp"

namespace sdl3 {

namespace haptic {

std::vector<SDL_HapticID> Enumerated() {
    int count = 0;
    SDL_HapticID *ids = SDL_GetHaptics(&count);
    if (!ids)
        return {};
    std::vector<SDL_HapticID> v(ids, ids + count);
    SDL_free(ids);
    return v;
}

} // namespace haptic

// ── Haptic ───────────────────────────────────────────────────────────────────

Result<Haptic, StringView> Haptic::Open(SDL_HapticID id) {
    auto *h = SDL_OpenHaptic(id);
    if (!h)
        return Err(GetError());
    return Ok(Haptic(h));
}

Result<Haptic, StringView> Haptic::OpenFromMouse() {
    auto *h = SDL_OpenHapticFromMouse();
    if (!h)
        return Err(GetError());
    return Ok(Haptic(h));
}

Result<Haptic, StringView> Haptic::OpenFromJoystick(Joystick &j) {
    auto *h = SDL_OpenHapticFromJoystick(j.Get());
    if (!h)
        return Err(GetError());
    return Ok(Haptic(h));
}

int Haptic::MaxEffectsPlaying() const noexcept {
    return m_handle ? SDL_GetMaxHapticEffectsPlaying(m_handle) : 0;
}

bool Haptic::PlayRumble(float strength, uint32_t lengthMs) noexcept {
    return m_handle && SDL_PlayHapticRumble(m_handle, strength, lengthMs);
}

bool Haptic::EffectSupported(const SDL_HapticEffect &effect) const noexcept {
    return m_handle && SDL_HapticEffectSupported(m_handle, &effect);
}

Result<SDL_HapticEffectID, StringView> Haptic::CreateEffect(const SDL_HapticEffect &effect) {
    if (!m_handle)
        return Err(StringView("haptic invalide"));
    auto id = SDL_CreateHapticEffect(m_handle, &effect);
    if (id < 0)
        return Err(GetError());
    return Ok(id);
}

bool Haptic::UpdateEffect(SDL_HapticEffectID id, const SDL_HapticEffect &effect) noexcept {
    return m_handle && SDL_UpdateHapticEffect(m_handle, id, &effect);
}

bool Haptic::RunEffect(SDL_HapticEffectID id, uint32_t iterations) noexcept {
    return m_handle && SDL_RunHapticEffect(m_handle, id, iterations);
}

void Haptic::DestroyEffect(SDL_HapticEffectID id) noexcept {
    if (m_handle)
        SDL_DestroyHapticEffect(m_handle, id);
}

bool Haptic::EffectRunning(SDL_HapticEffectID id) const noexcept {
    return m_handle && SDL_GetHapticEffectStatus(m_handle, id);
}

} // namespace sdl3
