// Définitions de sdl3/mixer.hpp
// L'en-tête n'est pas autonome : il compte sur ce qu'inclut son module.
#include <sdl3/sdl3.hpp>
#include "sdl3/mixer.hpp"

namespace sdl3 {

// ── MixerContext ─────────────────────────────────────────────────────────────

MixerContext::~MixerContext() {
    if (owns)
        MIX_Quit();
}

Result<MixerContext, Error> MixerContext::Create() {
    MixerContext ctx;
    ctx.owns = MIX_Init();
    if (!ctx)
        return Err(GetError());
    return Ok(std::move(ctx));
}

// ── AudioDecoder ─────────────────────────────────────────────────────────────

Result<AudioDecoder, Error> AudioDecoder::Create(const String &path, SDL_PropertiesID props) {
    auto *d = MIX_CreateAudioDecoder(path.c_str(), props);
    if (!d)
        return Err(GetError());
    return Ok(AudioDecoder(d));
}

bool AudioDecoder::GetFormat(SDL_AudioSpec &outSpec) const {
    return m_handle && MIX_GetAudioDecoderFormat(m_handle, &outSpec);
}

int AudioDecoder::Decode(void *buffer, int buflen, const SDL_AudioSpec *spec) {
    return m_handle ? MIX_DecodeAudio(m_handle, buffer, buflen, spec) : -1;
}

// ── MixTrack ─────────────────────────────────────────────────────────────────

bool MixTrack::Play(int loops) {
    if (!m_handle)
        return false;
    MIX_SetTrackLoops(m_handle, loops);
    return MIX_PlayTrack(m_handle, 0);
}

bool MixTrack::PlayStreaming() {
    if (!m_handle)
        return false;
    SDL_PropertiesID props = SDL_CreateProperties();
    SDL_SetBooleanProperty(props, MIX_PROP_PLAY_HALT_WHEN_EXHAUSTED_BOOLEAN, false);
    bool ok = MIX_PlayTrack(m_handle, props);
    SDL_DestroyProperties(props);
    return ok;
}

void MixTrack::Untag(const String &t) {
    if (m_handle)
        MIX_UntagTrack(m_handle, t.c_str());
}

// ── Mixer ────────────────────────────────────────────────────────────────────

Result<Mixer, StringView> Mixer::CreateDevice(AudioDeviceID devid, const AudioSpec &spec) {
    SDL_AudioSpec sdlSpec = spec.ToSDL();
    auto *m = MIX_CreateMixerDevice(devid, &sdlSpec);
    if (!m)
        return Err(GetError());
    return Ok(Mixer(m));
}

Result<Mixer, Error> Mixer::Create(const AudioSpec &spec) {
    SDL_AudioSpec sdlSpec = spec.ToSDL();
    auto *m = MIX_CreateMixer(&sdlSpec);
    if (!m)
        return Err(GetError());
    return Ok(Mixer(m));
}

Result<MixTrack, StringView> Mixer::CreateTrack() {
    auto *t = MIX_CreateTrack(m_handle);
    if (!t)
        return Err(GetError());
    return Ok(MixTrack(t));
}

Result<MixAudio, StringView> Mixer::LoadAudio(const String &path, bool predecode) {
    auto *a = MIX_LoadAudio(m_handle, path.c_str(), predecode);
    if (!a)
        return Err(GetError());
    return Ok(MixAudio(a));
}

Result<MixAudio, StringView> MixAudio::Load(Mixer &mixer, const String &path, bool predecode) {
    auto *a = MIX_LoadAudio(mixer.Get(), path.c_str(), predecode);
    if (!a)
        return Err(GetError());
    return Ok(MixAudio(a));
}

Result<MixAudio, StringView> MixAudio::FromPcm(Mixer &mixer, const void *data, size_t size, const SDL_AudioSpec &spec) {
    auto *a = MIX_LoadRawAudio(mixer.Get(), data, size, &spec);
    if (!a)
        return Err(GetError());
    return Ok(MixAudio(a));
}

Result<WavData, StringView> AudioDecoder::DecodeFile(const String &path) {
    auto decoder = Create(path);
    if (decoder.IsError())
        return Err(GetError());
    WavData out{};
    if (!decoder.Value().GetFormat(out.spec))
        return Err(GetError());
    uint8_t buffer[16384];
    for (;;) {
        const int got = decoder.Value().Decode(buffer, int(sizeof(buffer)), &out.spec);
        if (got < 0)
            return Err(GetError());
        if (got == 0)
            break;
        out.samples.insert(out.samples.end(), buffer, buffer + got);
    }
    return Ok(std::move(out));
}

bool MixTrack::Set3DPosition(float x, float y, float z) {
    const MIX_Point3D point{x, y, z};
    return m_handle && MIX_SetTrack3DPosition(m_handle, &point);
}

bool MixTrack::Clear3DPosition() {
    return m_handle && MIX_SetTrack3DPosition(m_handle, nullptr);
}

} // namespace sdl3
