#pragma once
#include <SDL3/SDL.h>
#include <span>
#include <vector>

#include "../core/core.hpp"

namespace sdl3 {

// ============================================================================
// AudioFormat
// ============================================================================

enum class AudioFormat : Uint32 {
    UNKNOWN = SDL_AUDIO_UNKNOWN,
    U8      = SDL_AUDIO_U8,
    S8      = SDL_AUDIO_S8,
    S16     = SDL_AUDIO_S16,
    S32     = SDL_AUDIO_S32,
    F32     = SDL_AUDIO_F32,
};

namespace detail {
    constexpr SDL_AudioFormat ToSDL(AudioFormat e) {
        return static_cast<SDL_AudioFormat>(e);
    }
}

// ============================================================================
// AudioSpec
// ============================================================================

struct AudioSpec {
    AudioFormat format = AudioFormat::F32;
    int channels = 2;
    int freq = 44100;

    [[nodiscard]] SDL_AudioSpec ToSDL() const { return {detail::ToSDL(format), channels, freq}; }

    [[nodiscard]] static AudioSpec MonoS16(int hz = 44100) { return {AudioFormat::S16, 1, hz}; }
    [[nodiscard]] static AudioSpec StereoF32(int hz = 44100) { return {AudioFormat::F32, 2, hz}; }
    [[nodiscard]] static AudioSpec StereoS16(int hz = 44100) { return {AudioFormat::S16, 2, hz}; }
};

// ============================================================================
// AudioDevice — enumeration entry (SDL_GetAudioPlaybackDevices/RecordingDevices)
// ============================================================================

struct AudioDevice {
    SDL_AudioDeviceID id = 0;
    String name;
};

/// List physical playback devices (does NOT include SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK).
[[nodiscard]] std::vector<AudioDevice> EnumeratePlaybackDevices();

/// List physical recording devices (does NOT include SDL_AUDIO_DEVICE_DEFAULT_RECORDING).
[[nodiscard]] std::vector<AudioDevice> EnumerateRecordingDevices();

// ============================================================================
// AudioStream — RAII wrapper around SDL_AudioStream
// ============================================================================

class AudioStream : public Wrapper<SDL_AudioStream, SDL_DestroyAudioStream> {
public:
    using Wrapper::Wrapper;

    /// Open a stream connected to the default playback device.
    [[nodiscard]] static Result<AudioStream, StringView> OpenPlayback(const AudioSpec &spec);

    /// Open a stream connected to a specific playback device (from enumeratePlaybackDevices()).
    [[nodiscard]] static Result<AudioStream, StringView> OpenPlayback(const AudioSpec &spec,
                                                                       SDL_AudioDeviceID deviceId);

    /// Open a stream connected to the default recording device.
    [[nodiscard]] static Result<AudioStream, StringView> OpenRecording(const AudioSpec &spec);

    /// Open a stream connected to a specific recording device (from EnumerateRecordingDevices()).
    [[nodiscard]] static Result<AudioStream, StringView> OpenRecording(const AudioSpec &spec,
                                                                        SDL_AudioDeviceID deviceId);

    /// Create a standalone stream for format conversion (not bound to a device).
    [[nodiscard]] static Result<AudioStream, Error> Create(const AudioSpec &src, const AudioSpec &dst);

    // ── Data I/O ─────────────────────────────────────────────────────────────

    bool PutData(const void *data, int len) { return m_handle && SDL_PutAudioStreamData(m_handle, data, len); }
    template <typename T> bool PutData(std::span<const T> data) { return PutData(data.data(), int(data.size_bytes())); }

    int GetData(void *data, int len) { return m_handle ? SDL_GetAudioStreamData(m_handle, data, len) : -1; }
    template <typename T> int GetData(std::span<T> data) { return GetData(data.data(), int(data.size_bytes())); }

    [[nodiscard]] int Available() const { return m_handle ? SDL_GetAudioStreamAvailable(m_handle) : 0; }

    /// Bytes still queued (put but not yet consumed by the device/GetData) — use to
    /// regulate how far ahead a producer fills the stream (e.g. keep a fixed latency).
    [[nodiscard]] int Queued() const { return m_handle ? SDL_GetAudioStreamQueued(m_handle) : 0; }

    bool Flush() { return m_handle && SDL_FlushAudioStream(m_handle); }
    bool Clear() { return m_handle && SDL_ClearAudioStream(m_handle); }

    // ── Playback control ─────────────────────────────────────────────────────

    bool Resume() { return m_handle && SDL_ResumeAudioStreamDevice(m_handle); }
    bool Pause() { return m_handle && SDL_PauseAudioStreamDevice(m_handle); }
    bool SetGain(float g) { return m_handle && SDL_SetAudioStreamGain(m_handle, g); }

    [[nodiscard]] float Gain() const { return m_handle ? SDL_GetAudioStreamGain(m_handle) : 1.f; }

    // ── Binding ──────────────────────────────────────────────────────────────

    bool Bind(SDL_AudioDeviceID devid) { return m_handle && SDL_BindAudioStream(devid, m_handle); }
    void Unbind();
};

// ============================================================================
// WavData — loaded WAV file (non-RAII; owns the sample buffer)
// ============================================================================

struct WavData {
    SDL_AudioSpec spec;
    std::vector<uint8_t> samples;
};

[[nodiscard]] Result<WavData, StringView> LoadWav(const String &path);

} // namespace sdl3
