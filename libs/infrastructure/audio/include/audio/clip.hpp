#pragma once
/**
 * audio::AudioClipResource — un son en `resources::Resource` : chargé ET
 * DÉCODÉ en arrière-plan par le ResourceCache (en PCM), identifié par un
 * AssetId stable, partagé par tous ceux qui le jouent. Le format et les
 * échantillons sont un sdl3::WavData.
 *
 * Formats : WAV toujours (sdl3::LoadWav) ; OGG, MP3, FLAC… par les décodeurs
 * de SDL3_mixer, qui doivent avoir été initialisés UNE fois par
 * l'application sur le fil principal (sdl3::MixerContext::Create — MIX_Init
 * n'est pas sûr entre fils) avant que ces sons ne se chargent.
 */
#include <resources/cache.hpp>
#include <sdl3/audio.hpp>

namespace audio {

class AudioClipResource final : public resources::Resource {
public:
	[[nodiscard]] static std::shared_ptr<AudioClipResource> FromFile(String source, String diskPath);

	[[nodiscard]] StringView Path() const noexcept override { return m_source.View(); }
	/// Format et échantillons — significatifs quand IsReady().
	[[nodiscard]] const sdl3::WavData &Data() const noexcept { return m_wav; }
	[[nodiscard]] int Frequency() const noexcept { return m_wav.spec.freq; }
	[[nodiscard]] int Channels() const noexcept { return m_wav.spec.channels; }
	[[nodiscard]] double DurationSeconds() const noexcept;

protected:
	Result<bool, String> DoLoad() override;
	void DoUnload() override;

private:
	AudioClipResource() = default;

	String m_source;
	String m_diskPath;
	sdl3::WavData m_wav{};
};

/// Apprend à `cache` à recréer les sons depuis leur entrée de catalogue.
void RegisterResourceFactories(resources::ResourceCache &cache);

/// Catalogue un fichier WAV (id tiré de son chemin) et lance son chargement.
/// Extensions de fichiers son reconnues (« .wav », « .ogg »…).
[[nodiscard]] bool IsAudioFile(StringView path) noexcept;

resources::AssetRef<AudioClipResource> ImportAudio(resources::ResourceCache &cache, const String &path,
												   resources::LoadPolicy policy = resources::LoadPolicy::ASYNC);

} // namespace audio

template <> struct resources::AssetTypeOf<audio::AudioClipResource> {
	static constexpr AssetType value = AssetType::AUDIO_CLIP;
};
