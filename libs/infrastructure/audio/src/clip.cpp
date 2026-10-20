// Définitions de audio/clip.hpp
#include "audio/clip.hpp"

#include <sdl3/mixer.hpp>

namespace audio {

std::shared_ptr<AudioClipResource> AudioClipResource::FromFile(String source, String diskPath) {
	std::shared_ptr<AudioClipResource> clip(new AudioClipResource());
	clip->m_source = std::move(source);
	clip->m_diskPath = std::move(diskPath);
	return clip;
}

double AudioClipResource::DurationSeconds() const noexcept {
	return m_wav.DurationSeconds();
}

Result<bool, String> AudioClipResource::DoLoad() {
	if (m_diskPath.ToLower().EndsWith(".wav")) {
		auto wav = sdl3::LoadWav(m_diskPath);
		if (wav.IsError())
			return Err(String::Format("%s : %s", m_source.CStr(), String(wav.Error()).CStr()));
		m_wav = std::move(wav).Unwrap();
		return Ok(true);
	}
	// Autres formats : décodés entièrement en PCM par SDL3_mixer.
	auto decoded = sdl3::AudioDecoder::DecodeFile(m_diskPath);
	if (decoded.IsError())
		return Err(String::Format("%s : décodage impossible (%s) — SDL3_mixer est-il initialisé ?", m_source.CStr(),
								  String(decoded.Error()).CStr()));
	m_wav = std::move(decoded).Unwrap();
	return Ok(true);
}

bool IsAudioFile(StringView path) noexcept {
	const String lower = String(path).ToLower();
	for (const char *extension : {".wav", ".ogg", ".mp3", ".flac", ".opus", ".aiff", ".voc"})
		if (lower.EndsWith(extension))
			return true;
	return false;
}

void AudioClipResource::DoUnload() {
	m_wav.samples.clear();
	m_wav.samples.shrink_to_fit();
}

void RegisterResourceFactories(resources::ResourceCache &cache) {
	cache.RegisterFactory(resources::AssetType::AUDIO_CLIP,
						  [](const resources::AssetRecord &record,
							 const resources::ResourceCache &owner) -> std::shared_ptr<resources::Resource> {
							  if (record.path.IsEmpty())
								  return nullptr;
							  return AudioClipResource::FromFile(record.path, owner.Resolve(record.path.View()));
						  });
}

resources::AssetRef<AudioClipResource> ImportAudio(resources::ResourceCache &cache, const String &path,
												   resources::LoadPolicy policy) {
	const resources::AssetId id = resources::AssetId::FromPath(path.View());
	if (!cache.IsResident(id)) {
		resources::AssetRecord record;
		record.id = id;
		record.type = resources::AssetType::AUDIO_CLIP;
		const size_t slash = path.Rfind('/');
		record.name = slash == String::NPOS ? path : path.Substr(slash + 1);
		record.path = path;
		(void)cache.Add(AudioClipResource::FromFile(path, cache.Resolve(path.View())), std::move(record), policy);
	} else if (policy == resources::LoadPolicy::SYNC) {
		(void)cache.LoadNow(id);
	}
	return resources::AssetRef<AudioClipResource>{id};
}

} // namespace audio
