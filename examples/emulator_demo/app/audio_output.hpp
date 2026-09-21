#pragma once
/**
 * AudioOutput — pompe les échantillons produits par le cœur (SPU NDS/GBA ou
 * APU GBC) vers une piste SDL3_mixer, depuis un fil dédié.
 *
 * Prend une fonction `getSamples(count)` plutôt qu'un cœur : `Spu::getSamples`
 * et `GbcApu::getSamples` ont exactement la même forme (bloquent jusqu'à ce
 * qu'un bloc soit prêt, rendent un tampon alloué par `new[]`).
 *
 * C'est aussi CE fil qui cadence l'émulation quand le limiteur est actif :
 * `swapBuffers()` du cœur attend que le bloc précédent ait été consommé.
 */
#include <atomic>
#include <functional>
#include <thread>

#include "sdl3/sdl3.hpp"

#include "report.hpp"

namespace emulator_demo::app {

class AudioOutput {
public:
	static constexpr int SAMPLE_RATE = 32768; ///< cadence de sortie du SPU/APU émulé
	static constexpr int SAMPLE_COUNT = 1024;
	/// File maximale avant d'attendre le mixeur : deux blocs, ~62 ms de son.
	static constexpr int MAX_QUEUED_BYTES = 2 * SAMPLE_COUNT * int(sizeof(uint32_t));

	AudioOutput(std::function<uint32_t *(int)> getSamples, sdl3::Mixer &mixer, ThreadTracker *threads)
		: m_getSamples(std::move(getSamples)), m_threads(threads) {
		auto spec = sdl3::AudioSpec::StereoS16(SAMPLE_RATE);
		auto stream = sdl3::AudioStream::Create(spec, spec);
		if (stream.IsOk())
			m_stream = std::move(stream).Unwrap();
		else
			SDL_LogError(SDL_LOG_CATEGORY_AUDIO, "AudioOutput : flux audio impossible : %s", stream.Error().CStr());

		auto track = mixer.CreateTrack();
		if (track.IsOk())
			m_track = std::move(track).Unwrap();
		else
			SDL_LogError(SDL_LOG_CATEGORY_AUDIO, "AudioOutput : piste impossible : %s", track.Error().CStr());

		if (!m_track.SetAudioStream(m_stream))
			SDL_LogError(SDL_LOG_CATEGORY_AUDIO, "AudioOutput : SetAudioStream : %s", sdl3::GetError().CStr());
		// PlayStreaming() et non Play() : l'entrée est un flux alimenté au fil
		// de l'eau, que Play() arrêterait avant la première donnée.
		if (!m_track.PlayStreaming())
			SDL_LogError(SDL_LOG_CATEGORY_AUDIO, "AudioOutput : PlayStreaming : %s", sdl3::GetError().CStr());

		m_pumpThread = std::jthread([this](std::stop_token stop) { PumpLoop(stop); });
	}

	AudioOutput(const AudioOutput &) = delete;
	AudioOutput &operator=(const AudioOutput &) = delete;

	/// Coupe l'alimentation sans détruire flux, piste ni fil (pause, save state).
	void SetPaused(bool paused) { m_paused.store(paused, std::memory_order_relaxed); }
	[[nodiscard]] bool IsPaused() const { return m_paused.load(std::memory_order_relaxed); }
	[[nodiscard]] long ChunksSent() const { return m_chunks.load(std::memory_order_relaxed); }

private:
	void PumpLoop(std::stop_token stop) {
		sdl3::ForeignThreadScope sdlThread; // fil std::jthread qui appelle SDL : TLS à libérer
		ThreadTracker::Scope tracked(m_threads);
		while (!stop.stop_requested()) {
			if (m_paused.load(std::memory_order_relaxed)) {
				std::this_thread::sleep_for(std::chrono::milliseconds(1));
				continue;
			}
			// C'est CETTE attente qui cadence l'émulation : tant que le mixeur
			// n'a pas consommé ce qui est déjà en file, on ne réclame pas de
			// nouveau bloc, donc le SPU reste bloqué dans swapBuffers().
			// `PutData` ne bloque jamais : sans ce test, la file grossissait
			// indéfiniment (latence audio croissante) et le jeu tournait plus
			// vite que la console (134 % mesurés sur un jeu GBA).
			if (m_stream.Queued() > MAX_QUEUED_BYTES) {
				std::this_thread::sleep_for(std::chrono::milliseconds(1));
				continue;
			}
			uint32_t *samples = m_getSamples(SAMPLE_COUNT);
			if (!m_stream.PutData(samples, SAMPLE_COUNT * int(sizeof(uint32_t))))
				SDL_LogError(SDL_LOG_CATEGORY_AUDIO, "AudioOutput : PutData : %s", sdl3::GetError().CStr());
			delete[] samples;
			m_chunks.fetch_add(1, std::memory_order_relaxed);
		}
	}

	std::function<uint32_t *(int)> m_getSamples;
	ThreadTracker *m_threads;
	sdl3::AudioStream m_stream;
	sdl3::MixTrack m_track;
	std::atomic<bool> m_paused{false};
	std::atomic<long> m_chunks{0};
	// Déclaré en DERNIER : arrêté et joint AVANT la destruction du flux.
	std::jthread m_pumpThread;
};

} // namespace emulator_demo::app
