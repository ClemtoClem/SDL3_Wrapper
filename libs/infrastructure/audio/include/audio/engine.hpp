#pragma once
/**
 * audio::AudioEngine — le son des jeux et des éditeurs, sur SDL3_mixer.
 *
 * Les sons sont des RESSOURCES (AudioClipResource) du ResourceCache du
 * propriétaire : chargés et décodés en arrière-plan, partagés par toutes les voix
 * qui les jouent. Une VOIX est une lecture en cours d'un son (une piste du
 * mixeur) ; elle est désignée par un identifiant, jamais par un pointeur, et
 * peut être demandée AVANT que son son ne soit prêt : elle démarre dès qu'il
 * l'est (Update).
 *
 * Voix spatialisées : leur position MONDE est convertie à chaque Update dans
 * le repère de l'auditeur (la caméra : x à droite, y en haut, −z devant —
 * celui de SDL3_mixer), qui en déduit panoramique et atténuation.
 *
 * Sans périphérique audio (machine sans son, tests), le moteur tourne sur un
 * mixeur HORS LIGNE : tout fonctionne (voix, durées, fin de lecture) sans
 * rien produire — un projet se joue pareil avec ou sans haut-parleurs.
 */
#include <audio/clip.hpp>
#include <math/math.hpp>
#include <resources/cache.hpp>
#include <sdl3/mixer.hpp>

#include <memory>
#include <unordered_map>
#include <vector>

namespace audio {

struct AudioPlayOptions {
	float volume = 1.f; ///< 0 … 2
	bool loop = false;
	float pitch = 1.f;		 ///< rapport de fréquence (0,5 : une octave plus bas)
	bool spatial = false;	 ///< positionné dans le monde (sinon « dans la tête »)
	math::FVector3 position; ///< position MONDE d'une voix spatialisée
};

class AudioEngine {
public:
	using VoiceId = uint32_t;
	static constexpr VoiceId NO_VOICE = 0;

	explicit AudioEngine(resources::ResourceCache& cache);
	~AudioEngine();

	AudioEngine(const AudioEngine&) = delete;
	AudioEngine& operator=(const AudioEngine&) = delete;

	/// Initialise SDL3_mixer (fil principal) et ouvre le périphérique par
	/// défaut — à défaut (ou si `allowDevice` est faux), un mixeur hors
	/// ligne. Idempotent.
	void Start(bool allowDevice = true);
	void Shutdown();

	[[nodiscard]] bool IsStarted() const noexcept { return m_mixer != nullptr; }
	[[nodiscard]] bool HasDevice() const noexcept { return m_hasDevice; }
	/// « périphérique par défaut », « hors ligne (…) », « inactif ».
	[[nodiscard]] const String& Status() const noexcept { return m_status; }

	// ── Lecture ──────────────────────────────────────────────────────────────

	/// Joue un son (démarre dès qu'il est chargé). NO_VOICE si le moteur
	/// n'est pas démarré ou si la référence est invalide.
	VoiceId Play(resources::AssetRef<AudioClipResource> clip,
				 const AudioPlayOptions& options = {});
	/// Joue un fichier (catalogué et chargé en arrière-plan au besoin).
	VoiceId Play(const String& path, const AudioPlayOptions& options = {});

	bool Stop(VoiceId voice);
	void StopAll();
	/// Vrai tant que la voix existe : en attente de son son ou en lecture.
	[[nodiscard]] bool IsPlaying(VoiceId voice) const;
	bool SetVolume(VoiceId voice, float volume);
	bool SetPitch(VoiceId voice, float pitch);
	/// Nouvelle position MONDE d'une voix spatialisée (appliquée à l'Update).
	bool SetPosition(VoiceId voice, const math::FVector3& position);

	void SetMasterVolume(float volume);
	[[nodiscard]] float MasterVolume() const noexcept { return m_master; }

	/// L'auditeur : MATRICE DE VUE de la caméra courante (monde → repère de
	/// l'auditeur : x à droite, y en haut, −z devant).
	void SetListener(const math::FMatrix4& view) { m_listenerView = view; }

	/// À chaque image : démarre les voix dont le son vient d'être prêt,
	/// oublie celles qui sont finies, replace les voix spatialisées.
	void Update();

	[[nodiscard]] size_t VoiceCount() const noexcept { return m_voices.size(); }
	/// Messages d'erreur accumulés depuis le dernier appel (le Runtime les
	/// verse dans son journal).
	[[nodiscard]] std::vector<String> TakeErrors();

private:
	struct Voice {
		VoiceId id = NO_VOICE;
		resources::AssetRef<AudioClipResource> clip;
		AudioPlayOptions options;
		std::unique_ptr<sdl3::MixTrack> track; ///< nul : son pas encore prêt
	};

	[[nodiscard]] Voice* FindVoice(VoiceId voice);
	[[nodiscard]] const Voice* FindVoice(VoiceId voice) const;
	/// Le son décodé, prêt pour le mixeur (créé une fois par son) ; nullptr
	/// tant que la ressource n'est pas prête.
	[[nodiscard]] sdl3::MixAudio* AudioFor(resources::AssetRef<AudioClipResource> clip,
										   bool& failed);
	bool StartVoice(Voice& voice);
	void ApplyPosition(Voice& voice);

	resources::ResourceCache& m_cache;
	sdl3::MixerContext m_context;
	std::unique_ptr<sdl3::Mixer> m_mixer;
	bool m_hasDevice = false;
	String m_status = String("inactif");
	float m_master = 1.f;
	math::FMatrix4 m_listenerView = math::FMatrix4::Identity();
	VoiceId m_next = 1;
	std::vector<Voice> m_voices;
	std::unordered_map<resources::AssetId, std::unique_ptr<sdl3::MixAudio>> m_decoded;
	std::vector<String> m_errors;
};

} // namespace audio
