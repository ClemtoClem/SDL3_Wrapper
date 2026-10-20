// Définitions de audio/engine.hpp
#include <audio/engine.hpp>

namespace audio {

AudioEngine::AudioEngine(resources::ResourceCache& cache) : m_cache(cache) {}

AudioEngine::~AudioEngine() {
	Shutdown();
}

void AudioEngine::Start(bool allowDevice) {
	if (m_mixer)
		return;
	if (!m_context) {
		auto context = sdl3::MixerContext::Create();
		if (context.IsError()) {
			m_status = String("inactif (SDL3_mixer indisponible)");
			m_errors.push_back(String::Format("Audio : SDL3_mixer indisponible (%s)",
											  String(context.Error()).CStr()));
			return;
		}
		m_context = std::move(context).Unwrap();
	}
	if (allowDevice) {
		auto device = sdl3::Mixer::CreateDevice();
		if (device.IsOk()) {
			m_mixer = std::make_unique<sdl3::Mixer>(std::move(device).Unwrap());
			m_hasDevice = true;
			m_status = String("périphérique par défaut");
			return;
		}
		m_status =
			String::Format("hors ligne (aucun périphérique : %s)", String(device.Error()).CStr());
	} else {
		m_status = String("hors ligne");
	}
	auto offline = sdl3::Mixer::Create(sdl3::AudioSpec::StereoF32(48000));
	if (offline.IsError()) {
		m_status = String("inactif");
		m_errors.push_back(
			String::Format("Audio : mixeur impossible (%s)", String(offline.Error()).CStr()));
		return;
	}
	m_mixer = std::make_unique<sdl3::Mixer>(std::move(offline).Unwrap());
	m_hasDevice = false;
}

void AudioEngine::Shutdown() {
	// Les pistes et les sons décodés avant le mixeur, le mixeur avant MIX_Quit.
	m_voices.clear();
	m_decoded.clear();
	m_mixer.reset();
	m_hasDevice = false;
	m_status = String("inactif");
}

AudioEngine::Voice* AudioEngine::FindVoice(VoiceId voice) {
	for (Voice& v : m_voices)
		if (v.id == voice)
			return &v;
	return nullptr;
}

const AudioEngine::Voice* AudioEngine::FindVoice(VoiceId voice) const {
	for (const Voice& v : m_voices)
		if (v.id == voice)
			return &v;
	return nullptr;
}

AudioEngine::VoiceId AudioEngine::Play(resources::AssetRef<AudioClipResource> clip,
									   const AudioPlayOptions& options) {
	if (!m_mixer || !clip.IsValid())
		return NO_VOICE;
	Voice voice;
	voice.id = m_next++;
	voice.clip = clip;
	voice.options = options;
	voice.options.volume = sdl3::Clamp(options.volume, 0.f, 2.f);
	m_voices.push_back(std::move(voice));
	(void)StartVoice(m_voices.back()); // tout de suite si le son est déjà prêt
	return m_voices.back().id;
}

AudioEngine::VoiceId AudioEngine::Play(const String& path, const AudioPlayOptions& options) {
	if (!m_mixer || path.IsEmpty())
		return NO_VOICE;
	return Play(audio::ImportAudio(m_cache, path), options);
}

sdl3::MixAudio* AudioEngine::AudioFor(resources::AssetRef<AudioClipResource> clip,
									  bool& failed) {
	failed = false;
	if (auto it = m_decoded.find(clip.id); it != m_decoded.end())
		return it->second.get();
	std::shared_ptr<AudioClipResource> resource = m_cache.Get(clip);
	if (!resource || resource->HasFailed()) {
		failed = true;
		m_errors.push_back(
			resource ? String::Format("Audio : %s", resource->LastError().CStr())
					 : String::Format("Audio : son inconnu (%s)", clip.ToString().CStr()));
		return nullptr;
	}
	if (!resource->IsReady())
		return nullptr; // en chargement : la voix attend
	const sdl3::WavData& data = resource->Data();
	auto audio =
		sdl3::MixAudio::FromPcm(*m_mixer, data.samples.data(), data.samples.size(), data.spec);
	if (audio.IsError()) {
		failed = true;
		m_errors.push_back(String::Format("Audio : %s (%s)", resource->DisplayName().CStr(),
										  String(audio.Error()).CStr()));
		return nullptr;
	}
	auto& slot = m_decoded[clip.id];
	slot = std::make_unique<sdl3::MixAudio>(std::move(audio).Unwrap());
	return slot.get();
}

bool AudioEngine::StartVoice(Voice& voice) {
	bool failed = false;
	sdl3::MixAudio* audio = AudioFor(voice.clip, failed);
	if (!audio)
		return false;
	auto track = m_mixer->CreateTrack();
	if (track.IsError()) {
		m_errors.push_back(
			String::Format("Audio : piste impossible (%s)", String(track.Error()).CStr()));
		return false;
	}
	voice.track = std::make_unique<sdl3::MixTrack>(std::move(track).Unwrap());
	(void)voice.track->SetAudio(*audio);
	(void)voice.track->SetGain(voice.options.volume * m_master);
	(void)voice.track->SetFrequencyRatio(sdl3::Clamp(voice.options.pitch, 0.1f, 10.f));
	ApplyPosition(voice);
	(void)voice.track->Play(voice.options.loop ? -1 : 0);
	return true;
}

void AudioEngine::ApplyPosition(Voice& voice) {
	if (!voice.track)
		return;
	if (!voice.options.spatial) {
		(void)voice.track->Clear3DPosition();
		return;
	}
	// Repère de l'auditeur = repère de vue de la caméra (x droite, y haut,
	// −z devant), exactement celui qu'attend SDL3_mixer.
	const math::FVector3 local = m_listenerView.TransformPoint(voice.options.position);
	(void)voice.track->Set3DPosition(local.x, local.y, local.z);
}

bool AudioEngine::Stop(VoiceId voice) {
	for (auto it = m_voices.begin(); it != m_voices.end(); ++it)
		if (it->id == voice) {
			if (it->track)
				(void)it->track->Stop();
			m_voices.erase(it);
			return true;
		}
	return false;
}

void AudioEngine::StopAll() {
	for (Voice& voice : m_voices)
		if (voice.track)
			(void)voice.track->Stop();
	m_voices.clear();
}

bool AudioEngine::IsPlaying(VoiceId voice) const {
	return FindVoice(voice) != nullptr;
}

bool AudioEngine::SetVolume(VoiceId id, float volume) {
	Voice* voice = FindVoice(id);
	if (!voice)
		return false;
	voice->options.volume = sdl3::Clamp(volume, 0.f, 2.f);
	if (voice->track)
		(void)voice->track->SetGain(voice->options.volume * m_master);
	return true;
}

bool AudioEngine::SetPitch(VoiceId id, float pitch) {
	Voice* voice = FindVoice(id);
	if (!voice)
		return false;
	voice->options.pitch = sdl3::Clamp(pitch, 0.1f, 10.f);
	if (voice->track)
		(void)voice->track->SetFrequencyRatio(voice->options.pitch);
	return true;
}

bool AudioEngine::SetPosition(VoiceId id, const math::FVector3& position) {
	Voice* voice = FindVoice(id);
	if (!voice)
		return false;
	voice->options.position = position;
	voice->options.spatial = true;
	return true;
}

void AudioEngine::SetMasterVolume(float volume) {
	m_master = sdl3::Clamp(volume, 0.f, 2.f);
	for (Voice& voice : m_voices)
		if (voice.track)
			(void)voice.track->SetGain(voice.options.volume * m_master);
}

void AudioEngine::Update() {
	if (!m_mixer)
		return;
	for (auto it = m_voices.begin(); it != m_voices.end();) {
		Voice& voice = *it;
		if (!voice.track) {
			bool failed = false;
			if (!AudioFor(voice.clip, failed) && failed) {
				it = m_voices.erase(it); // son introuvable : la voix s'éteint
				continue;
			}
			if (!StartVoice(voice) && failed) {
				it = m_voices.erase(it);
				continue;
			}
		} else if (!voice.track->IsPlaying() && !voice.track->IsPaused()) {
			it = m_voices.erase(it); // fin de lecture
			continue;
		}
		ApplyPosition(voice);
		++it;
	}
}

std::vector<String> AudioEngine::TakeErrors() {
	std::vector<String> out;
	out.swap(m_errors);
	return out;
}

} // namespace audio
