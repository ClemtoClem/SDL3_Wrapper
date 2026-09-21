#pragma once
#include <SDL3_mixer/SDL_mixer.h>

#include "../core/core.hpp"
#include "../core/wrapper.hpp"
#include "audio.hpp"
#include <SDL3/SDL.h>

namespace sdl3 {

using AudioDeviceID = SDL_AudioDeviceID;

// Forward declarations
class Mixer;
class MixAudio;
class MixTrack;

// ============================================================================
// MixerContext — RAII MIX_Init / MIX_Quit
// ============================================================================

class MixerContext {
    bool owns = false;

public:
    MixerContext() = default;
    ~MixerContext() {
        if (owns)
            MIX_Quit();
    }

    MixerContext(const MixerContext &) = delete;
    MixerContext &operator=(const MixerContext &) = delete;
    MixerContext(MixerContext &&o) noexcept : owns(o.owns) { o.owns = false; }
    MixerContext &operator=(MixerContext &&o) noexcept {
        if (this != &o) {
            if (owns)
                MIX_Quit();
            owns = o.owns;
            o.owns = false;
        }
        return *this;
    }

    [[nodiscard]] explicit operator bool() const noexcept { return owns; }

    [[nodiscard]] static Result<MixerContext, Error> Create() {
        MixerContext ctx;
        ctx.owns = MIX_Init();
        if (!ctx)
            return Err(GetError());
        return Ok(std::move(ctx));
    }
};

// ============================================================================
// AudioDecoder — RAII MIX_AudioDecoder: streaming decode-to-buffer, any format
// SDL3_mixer supports (WAV/OGG/FLAC/MP3/...). Unlike MixAudio+MixTrack (which
// hand playback straight to a Mixer), this hands back raw PCM the caller pulls
// on demand — the primitive for feeding decoded audio INTO a custom DSP graph
// instead of straight to the speakers.
// ============================================================================

class AudioDecoder : public Wrapper<MIX_AudioDecoder, MIX_DestroyAudioDecoder> {
public:
    using Wrapper::Wrapper;

    /// Open a decoder for a file on disk. `props` may be SDL_PropertiesID{0} for defaults.
    [[nodiscard]] static Result<AudioDecoder, Error> Create(const String &path, SDL_PropertiesID props = 0) {
        auto *d = MIX_CreateAudioDecoder(path.c_str(), props);
        if (!d)
            return Err(GetError());
        return Ok(AudioDecoder(d));
    }

    /// Query the format the decoder will produce (may not match the source file's format).
    [[nodiscard]] bool GetFormat(SDL_AudioSpec &outSpec) const {
        return m_handle && MIX_GetAudioDecoderFormat(m_handle, &outSpec);
    }

    /// Decode up to `buflen` bytes into `buffer`, converted to `spec` if given (nullptr = decoder's native
    /// format). Returns the number of bytes actually written, 0 at end-of-stream, -1 on error.
    [[nodiscard]] int Decode(void *buffer, int buflen, const SDL_AudioSpec *spec = nullptr) {
        return m_handle ? MIX_DecodeAudio(m_handle, buffer, buflen, spec) : -1;
    }
};

// ============================================================================
// MixTrack — RAII MIX_Track (one playback channel)
// ============================================================================

class MixTrack : public Wrapper<MIX_Track, MIX_DestroyTrack> {
public:
    using Wrapper::Wrapper;

    bool SetAudio(MixAudio &audio);

    /// Feed this track from a live SDL_AudioStream (e.g. a synthesized/streamed
    /// PCM source) instead of a decoded MIX_Audio file. The stream is polled by
    /// the mixer as it plays; keep pushing data into it via AudioStream::PutData().
    bool SetAudioStream(AudioStream &stream) { return m_handle && MIX_SetTrackAudioStream(m_handle, stream.Get()); }

    bool SetLoops(int loops) { return m_handle && MIX_SetTrackLoops(m_handle, loops); }

    /// Play the track (loops = 0 → play once, -1 → loop forever).
    bool Play(int loops = 0) {
        if (!m_handle)
            return false;
        MIX_SetTrackLoops(m_handle, loops);
        return MIX_PlayTrack(m_handle, 0);
    }

    /// Starts playback for a track fed from a live/incremental SDL_AudioStream
    /// (set via setAudioStream()) — e.g. synthesized audio pushed in as it's
    /// generated, rather than a complete clip. MIX_PlayTrack's default
    /// (MIX_PROP_PLAY_HALT_WHEN_EXHAUSTED_BOOLEAN = true) would otherwise mark
    /// the track stopped the instant the mixer finds the stream momentarily
    /// empty (almost immediately, since the producer hasn't caught up yet) —
    /// this sets that property to false so the mixer just contributes silence
    /// until more data arrives instead of halting for good. Loop count doesn't
    /// apply to AudioStream inputs, so there's no `loops` parameter here.
    bool PlayStreaming() {
        if (!m_handle)
            return false;
        SDL_PropertiesID props = SDL_CreateProperties();
        SDL_SetBooleanProperty(props, MIX_PROP_PLAY_HALT_WHEN_EXHAUSTED_BOOLEAN, false);
        bool ok = MIX_PlayTrack(m_handle, props);
        SDL_DestroyProperties(props);
        return ok;
    }

    /// Stop the track, optionally with a fade-out over `fadeFrames` sample frames.
    bool Stop(Sint64 fadeFrames = 0) { return m_handle && MIX_StopTrack(m_handle, fadeFrames); }

    bool Pause() { return m_handle && MIX_PauseTrack(m_handle); }
    bool Resume() { return m_handle && MIX_ResumeTrack(m_handle); }

    bool SetGain(float gain) { return m_handle && MIX_SetTrackGain(m_handle, gain); }
    [[nodiscard]] float Gain() const { return m_handle ? MIX_GetTrackGain(m_handle) : 1.f; }

    bool SetFrequencyRatio(float ratio) { return m_handle && MIX_SetTrackFrequencyRatio(m_handle, ratio); }
    [[nodiscard]] float FrequencyRatio() const { return m_handle ? MIX_GetTrackFrequencyRatio(m_handle) : 1.f; }

    [[nodiscard]] bool IsPlaying() const { return m_handle && MIX_TrackPlaying(m_handle); }
    [[nodiscard]] bool IsPaused() const { return m_handle && MIX_TrackPaused(m_handle); }

    [[nodiscard]] Sint64 Remaining() const { return m_handle ? MIX_GetTrackRemaining(m_handle) : 0; }
    [[nodiscard]] Sint64 GetPosition() const { return m_handle ? MIX_GetTrackPlaybackPosition(m_handle) : 0; }

    bool Tag(const String &t) { return m_handle && MIX_TagTrack(m_handle, t.c_str()); }
    void Untag(const String &t) {
        if (m_handle)
            MIX_UntagTrack(m_handle, t.c_str());
    }
};

// ============================================================================
// MixAudio — RAII MIX_Audio (loaded audio data, shared between mixers)
// ============================================================================

class MixAudio : public Wrapper<MIX_Audio, MIX_DestroyAudio> {
public:
    using Wrapper::Wrapper;

    /// Load an audio file from disk. `predecode = true` decodes immediately.
    [[nodiscard]] static Result<MixAudio, StringView> Load(Mixer &mixer, const String &path, bool predecode = false);

    [[nodiscard]] Sint64 Duration() const { return m_handle ? MIX_GetAudioDuration(m_handle) : 0; }
};

// ============================================================================
// Mixer — RAII MIX_Mixer
// ============================================================================

class Mixer : public Wrapper<MIX_Mixer, MIX_DestroyMixer> {
public:
    using Wrapper::Wrapper;

    /// Create a mixer that feeds the given audio device (most common usage).
    [[nodiscard]] static Result<Mixer, StringView>
    CreateDevice(AudioDeviceID devid = SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, const AudioSpec &spec = AudioSpec{}) {
        SDL_AudioSpec sdlSpec = spec.ToSDL();
        auto *m = MIX_CreateMixerDevice(devid, &sdlSpec);
        if (!m)
            return Err(GetError());
        return Ok(Mixer(m));
    }

    /// Create an offline mixer that renders to a memory buffer via generate().
    [[nodiscard]] static Result<Mixer, Error> Create(const AudioSpec &spec) {
        SDL_AudioSpec sdlSpec = spec.ToSDL();
        auto *m = MIX_CreateMixer(&sdlSpec);
        if (!m)
            return Err(GetError());
        return Ok(Mixer(m));
    }

    // ── Tracks ───────────────────────────────────────────────────────────────

    [[nodiscard]] Result<MixTrack, StringView> CreateTrack() {
        auto *t = MIX_CreateTrack(m_handle);
        if (!t)
            return Err(GetError());
        return Ok(MixTrack(t));
    }

    // ── Audio loading ────────────────────────────────────────────────────────

    [[nodiscard]] Result<MixAudio, StringView> LoadAudio(const String &path, bool predecode = false) {
        auto *a = MIX_LoadAudio(m_handle, path.c_str(), predecode);
        if (!a)
            return Err(GetError());
        return Ok(MixAudio(a));
    }

    /// Fire-and-forget playback of an audio object (no track management needed).
    bool PlayAudio(MixAudio &audio) { return m_handle && MIX_PlayAudio(m_handle, audio.Get()); }

    // ── Global controls ──────────────────────────────────────────────────────

    bool SetGain(float gain) { return m_handle && MIX_SetMixerGain(m_handle, gain); }
    [[nodiscard]] float Gain() const { return m_handle ? MIX_GetMixerGain(m_handle) : 1.f; }

    bool SetFrequencyRatio(float ratio) { return m_handle && MIX_SetMixerFrequencyRatio(m_handle, ratio); }
    [[nodiscard]] float FrequencyRatio() const { return m_handle ? MIX_GetMixerFrequencyRatio(m_handle) : 1.f; }

    bool StopAll(Sint64 fadeOutMs = 0) { return m_handle && MIX_StopAllTracks(m_handle, fadeOutMs); }
    bool PauseAll() { return m_handle && MIX_PauseAllTracks(m_handle); }
    bool ResumeAll() { return m_handle && MIX_ResumeAllTracks(m_handle); }
};

// ============================================================================
// MixAudio deferred impl (needs Mixer to be complete)
// ============================================================================

inline Result<MixAudio, StringView> MixAudio::Load(Mixer &mixer, const String &path, bool predecode) {
    auto *a = MIX_LoadAudio(mixer.Get(), path.c_str(), predecode);
    if (!a)
        return Err(GetError());
    return Ok(MixAudio(a));
}

// ============================================================================
// MixTrack deferred impl (needs MixAudio to be complete)
// ============================================================================

inline bool MixTrack::SetAudio(MixAudio &audio) { return m_handle && MIX_SetTrackAudio(m_handle, audio.Get()); }

} // namespace sdl3
